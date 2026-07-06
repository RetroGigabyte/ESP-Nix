#pragma once

#include <Arduino.h>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include "filesystem.h"
#include "terminal.h"
#include "input.h"
#include "variables.h"
#include "webfileserver.h"
#include "otaupdate.h"
#include "archive.h"
#include "retron.h"
#include <ESP32Ping.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "ftpclient/ESP32_FTPClient.h"
#include "esp_heap_caps.h"
#include "elfloader.h"
#include "hostsymbols.h"

class Commands {
private:
  FileSystem& fs;
  Terminal& term;
  Variables* vars;
  String* captureBuffer = nullptr;
  String* stdinBuffer = nullptr;
  std::vector<String> lastWifiScan;
  std::function<bool(const String&)> fullExecutor;

public:
  Commands(FileSystem& f, Terminal& t) : fs(f), term(t), vars(nullptr) {}

  void setVariables(Variables* v) {
    vars = v;
  }

  // Set by Shell at construction: routes a line through full shell
  // processing (variable assignment, $VAR expansion, pipes, redirection,
  // && || ;) instead of Commands' own bare single-command dispatch.
  // Lets things Commands starts itself (the browser terminal) behave
  // exactly like the interactive prompt.
  void setFullExecutor(std::function<bool(const String&)> exec) {
    fullExecutor = exec;
  }

  String getCurrentPath() {
    return fs.getCurrentPath();
  }

  // When set, command output is captured into the buffer instead of
  // going to the terminal (used for > and >> redirection, and pipes).
  void setCaptureBuffer(String* buf) {
    captureBuffer = buf;
  }

  // When set, commands that would normally require a file argument
  // (cat, grep, head, tail) fall back to this content (used for pipes
  // and < redirection).
  void setStdinBuffer(String* buf) {
    stdinBuffer = buf;
  }

  static const std::vector<String>& commandNames() {
    static const std::vector<String> names = {
      "help", "ls", "pwd", "cd", "cat", "echo", "touch", "rm", "cp", "mv",
      "grep", "head", "tail", "mkdir", "clear", "edit", "env", "uname",
      "whoami", "date", "df", "free", "nixos-rebuild", "webserver", "update",
      "find", "wc", "du", "reboot", "ntp", "extract", "compress", "test", "settz", "nixfetch", "loop",
      "wifi", "ip", "ping", "curl", "wget", "ftp", "mkali", "rmali", "ls-ali", "runelf", "runmod", "retron", "sleep", "hostname", "backup", "exit"
    };
    return names;
  }

  // Runs /etc/settings/esp-nix.conf if it exists (boot-time declarative config:
  // sets vars, creates dirs, runs commands - same syntax as a shell script).
  bool applyConfig() {
    const char* configPath = "/etc/settings/esp-nix.conf";
    if (!fs.exists(configPath)) return false;
    return runScriptFile(configPath, false);
  }

  bool execute(const String& input) {
    // Check for script execution (./ syntax)
    if (input.startsWith("./")) {
      return executeScript(input);
    }

    std::vector<String> args = parseCommand(input);
    if (args.empty()) return true;

    String cmd = args[0];
    cmd.toLowerCase();

    return dispatch(cmd, args);
  }

  // Executes a command given a pre-split name and args (used by the
  // pipeline executor, which already has parsed tokens from Parser).
  bool executeParsed(const String& cmdName, const std::vector<String>& cmdArgs) {
    std::vector<String> args;
    args.push_back(cmdName);
    for (const auto& a : cmdArgs) args.push_back(a);

    String cmd = cmdName;
    cmd.toLowerCase();

    return dispatch(cmd, args);
  }

private:
  bool dispatch(const String& cmd, const std::vector<String>& args) {
    if (cmd == "help") return cmdHelp(args);
    if (cmd == "ls") return cmdLs(args);
    if (cmd == "pwd") return cmdPwd(args);
    if (cmd == "cd") return cmdCd(args);
    if (cmd == "cat") return cmdCat(args);
    if (cmd == "echo") return cmdEcho(args);
    if (cmd == "touch") return cmdTouch(args);
    if (cmd == "rm") return cmdRm(args);
    if (cmd == "mkdir") return cmdMkdir(args);
    if (cmd == "clear") return cmdClear(args);
    if (cmd == "edit") return cmdEdit(args);
    if (cmd == "env") return cmdEnv(args);
    if (cmd == "uname") return cmdUname(args);
    if (cmd == "whoami") return cmdWhoami(args);
    if (cmd == "date") return cmdDate(args);
    if (cmd == "df") return cmdDf(args);
    if (cmd == "free") return cmdFree(args);
    if (cmd == "cp") return cmdCp(args);
    if (cmd == "mv") return cmdMv(args);
    if (cmd == "grep") return cmdGrep(args);
    if (cmd == "head") return cmdHead(args);
    if (cmd == "tail") return cmdTail(args);
    if (cmd == "nixos-rebuild") return cmdRebuild(args);
    if (cmd == "webserver") return cmdWebServer(args);
    if (cmd == "update") return cmdUpdate(args);
    if (cmd == "find") return cmdFind(args);
    if (cmd == "wc") return cmdWc(args);
    if (cmd == "du") return cmdDu(args);
    if (cmd == "reboot") return cmdReboot(args);
    if (cmd == "ntp") return cmdNtp(args);
    if (cmd == "extract") return cmdExtract(args);
    if (cmd == "compress") return cmdCompress(args);
    if (cmd == "test" || cmd == "[") return cmdTest(args);
    if (cmd == "settz") return cmdSetTz(args);
    if (cmd == "nixfetch") return cmdNixfetch(args);
    if (cmd == "loop") return cmdLoop(args);
    if (cmd == "wifi") return cmdWifi(args);
    if (cmd == "ip") return cmdIp(args);
    if (cmd == "ping") return cmdPing(args);
    if (cmd == "curl") return cmdCurl(args);
    if (cmd == "wget") return cmdWget(args);
    if (cmd == "ftp") return cmdFtp(args);
    if (cmd == "mkali") return cmdMkali(args);
    if (cmd == "rmali") return cmdRmali(args);
    if (cmd == "ls-ali") return cmdLsAli(args);
    if (cmd == "runelf") return cmdRunelf(args);
    if (cmd == "runmod") return cmdRunmod(args);
    if (cmd == "retron") return cmdRetron(args);
    if (cmd == "sleep") return cmdSleep(args);
    if (cmd == "hostname") return cmdHostname(args);
    if (cmd == "backup") return cmdBackup(args);
    if (cmd == "exit") return cmdExit(args);

    return tryRunSystemScript(cmd, args);
  }

  // Any .sh file in /system can be run from anywhere just by its name,
  // no ./ prefix and no .sh extension needed - e.g. /system/backup.sh
  // becomes runnable as just "backup". Anything typed after the name is
  // forwarded to the script wherever it writes "$@".
  // Checks each ':'-separated directory in PATH (defaults to just
  // /system if unset) in order, running the first "<cmd>.sh" match -
  // same lookup order convention as a real Unix PATH.
  bool tryRunSystemScript(const String& cmd, const std::vector<String>& args) {
    String pathVar = (vars && vars->exists("PATH")) ? vars->get("PATH") : "/system";

    int start = 0;
    while (start <= (int)pathVar.length()) {
      int colon = pathVar.indexOf(':', start);
      String dir = (colon >= 0) ? pathVar.substring(start, colon) : pathVar.substring(start);
      dir.trim();

      if (dir.length() > 0) {
        if (!dir.endsWith("/")) dir += "/";
        String scriptPath = dir + cmd + ".sh";

        if (fs.exists(scriptPath)) {
          String extraArgs = "";
          for (size_t i = 1; i < args.size(); i++) {
            if (i > 1) extraArgs += " ";
            extraArgs += args[i];
          }
          return runScriptFile(scriptPath, false, extraArgs);
        }
      }

      if (colon < 0) break;
      start = colon + 1;
    }

    term.println("Unknown command: " + cmd);
    return true;
  }

  bool cmdWebServer(const std::vector<String>& args) {
    bool joinMode = false;
    bool listMode = false;
    int joinIndex = -1;
    String joinPass = "";

    for (size_t i = 1; i < args.size(); i++) {
      String a = args[i];
      if (a == "-join") joinMode = true;
      else if (a == "-list") listMode = true;
      else if (a.startsWith("-pass=")) joinPass = a.substring(6);
      else if (isNumeric(a)) joinIndex = a.toInt();
    }

    if (joinMode && listMode) {
      return wifiScanAndList();
    }

    if (joinMode && joinIndex > 0) {
      return wifiJoin(joinIndex, joinPass);
    }

    if (joinMode) {
      term.println("Usage: web -join -list");
      term.println("       web -join <number> -pass=PASSWORD");
      return true;
    }

    if (!fs.sdAvailable()) {
      term.println("No SD card mounted - insert one and reboot first.");
      return true;
    }

    WebFileServer webServer;

    // Routes through the same full shell processing as the Serial/PS2
    // prompt (variable assignment, $VAR expansion, pipes, redirection,
    // && || ;), capturing output for the browser terminal. Falls back to
    // bare command dispatch if somehow no full executor was wired up.
    // Note: full-screen commands (edit) and anything reading further
    // keystrokes don't work here - there's no live keystroke channel
    // over HTTP, only one request/response per command.
    auto executor = [this](const String& cmd) -> String {
      String captured = "";
      setCaptureBuffer(&captured);
      if (fullExecutor) fullExecutor(cmd); else execute(cmd);
      setCaptureBuffer(nullptr);
      return captured;
    };

    auto cwdGetter = [this]() -> String {
      return getCurrentPath();
    };

    // Snapshot of nixfetch at the moment 'web' starts, shown on the file
    // manager's index page - not live-refreshed per request, just a
    // one-time capture like running it yourself right before opening web.
    String nixfetchOutput = executor("nixfetch");

    String hostname = (vars && vars->exists("HOSTNAME")) ? vars->get("HOSTNAME") : "esp-nix";

    // Prefer a previously-joined WPA2 network (see 'web -join') so the
    // server lands on the same network as your computer; fall back to
    // hosting its own access point otherwise.
    String staSsid = (vars && vars->exists("WIFI_SSID")) ? vars->get("WIFI_SSID") : "";
    if (staSsid.length() > 0) {
      String staPass = (vars && vars->exists("WIFI_PASS")) ? vars->get("WIFI_PASS") : "";
      webServer.runSTA(fs, term, staSsid, staPass, executor, cwdGetter, nixfetchOutput, hostname);
      return true;
    }

    String ssid = (vars && vars->exists("WEB_SSID")) ? vars->get("WEB_SSID") : "ESP-Nix";
    String password = (vars && vars->exists("WEB_PASS")) ? vars->get("WEB_PASS") : "";
    webServer.run(fs, term, ssid, password, executor, cwdGetter, nixfetchOutput, hostname);
    return true;
  }

  // Applies the configured HOSTNAME to the WiFi stack - must be called
  // after WiFi.mode() but before begin()/softAP() to take effect.
  void applyWifiHostname() {
    String hostname = (vars && vars->exists("HOSTNAME")) ? vars->get("HOSTNAME") : "esp-nix";
    WiFi.setHostname(hostname.c_str());
  }

  bool wifiScanAndList() {
    term.println("Scanning for WiFi networks...");
    WiFi.mode(WIFI_STA);
    int count = WiFi.scanNetworks();

    lastWifiScan.clear();

    if (count <= 0) {
      term.println("No networks found.");
      return true;
    }

    for (int i = 0; i < count; i++) {
      String ssid = WiFi.SSID(i);
      lastWifiScan.push_back(ssid);

      bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      char line[64];
      snprintf(line, sizeof(line), "%2d) %-24s %4ddBm %s",
               i + 1, ssid.c_str(), WiFi.RSSI(i), open ? "open" : "secured");
      out(line);
    }

    WiFi.scanDelete();
    term.println("Join with: web -join <number> -pass=PASSWORD");
    return true;
  }

  bool wifiJoin(int index, const String& password) {
    if (index < 1 || index > (int)lastWifiScan.size()) {
      term.println("Invalid network number - run 'web -join -list' first.");
      return true;
    }

    String ssid = lastWifiScan[index - 1];

    term.println("Joining " + ssid + " ...");
    WiFi.mode(WIFI_STA);
    applyWifiHostname();
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
      term.println("Failed to join " + ssid);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return true;
    }

    term.println("Joined " + ssid + " - IP: " + WiFi.localIP().toString());

    // Free time sync while we're already connected
    doNtpSync();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    if (vars) {
      vars->set("WIFI_SSID", ssid);
      vars->set("WIFI_PASS", password);
    }
    persistWifiCredentials(ssid, password);

    term.println("Saved - future 'web' runs will join this network automatically.");
    return true;
  }

  // 'wifi connect'/'disconnect'/'status' - unlike web/web -join/ntp (which
  // connect, do one thing, and explicitly power the radio off again),
  // this holds the connection in the background so wifi/ip/ping actually
  // have something to report between commands.
  bool cmdWifi(const std::vector<String>& args) {
    if (args.size() < 2 || args[1] == "status") {
      return wifiStatus();
    }
    if (args[1] == "connect") {
      return wifiConnectPersistent(args);
    }
    if (args[1] == "disconnect") {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      term.println("WiFi disconnected.");
      return true;
    }
    if (args[1] == "toggle") {
      if (WiFi.status() == WL_CONNECTED) {
        term.println("WiFi was on - disconnecting...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        term.println("WiFi disconnected.");
      } else {
        term.println("WiFi was off - connecting...");
        wifiConnectPersistent(args);
      }
      return true;
    }
    term.println("Usage: wifi status | wifi connect [ssid] [password] | wifi disconnect | wifi toggle");
    return true;
  }

  bool wifiStatus() {
    if (WiFi.status() != WL_CONNECTED) {
      term.println("WiFi is off (run 'wifi connect' first)");
      return true;
    }
    out("SSID: " + WiFi.SSID());
    out("IP: " + WiFi.localIP().toString());
    out("RSSI: " + String(WiFi.RSSI()) + "dBm");
    out("MAC: " + WiFi.macAddress());
    return true;
  }

  bool wifiConnectPersistent(const std::vector<String>& args) {
    String ssid, password;

    if (args.size() >= 3) {
      ssid = args[2];
      password = args.size() >= 4 ? args[3] : "";
    } else if (vars && vars->exists("WIFI_SSID")) {
      ssid = vars->get("WIFI_SSID");
      password = vars->exists("WIFI_PASS") ? vars->get("WIFI_PASS") : "";
    } else {
      term.println("No saved network - use 'wifi connect <ssid> <password>' or run 'web -join' first.");
      return true;
    }

    WiFi.mode(WIFI_STA);
    applyWifiHostname();
    WiFi.begin(ssid.c_str(), password.c_str());

    term.println("Joining " + ssid + " ...");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
      term.println("Failed to join WiFi network: " + ssid);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return true;
    }

    term.println("Joined " + ssid + " - IP: " + WiFi.localIP().toString());
    term.println("WiFi stays connected in the background - use 'wifi disconnect' to stop.");
    return true;
  }

  bool cmdIp(const std::vector<String>& args) {
    if (WiFi.status() != WL_CONNECTED) {
      term.println("WiFi is off (run 'wifi connect' first)");
      return true;
    }
    out(WiFi.localIP().toString());
    return true;
  }

  bool cmdPing(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: ping <host>");
      return true;
    }
    if (WiFi.status() != WL_CONNECTED) {
      term.println("WiFi is off (run 'wifi connect' first)");
      return true;
    }

    String host = args[1];
    term.println("Pinging " + host + " ...");

    bool ok = Ping.ping(host.c_str(), 4);
    if (ok) {
      out("Reply from " + host + ": avg " + String(Ping.averageTime(), 1) + "ms");
    } else {
      out("No reply from " + host);
    }
    return true;
  }

  // curl [-X METHOD] [-d data] <url> - basic HTTP client. HTTPS uses
  // WiFiClientSecure::setInsecure() (no certificate validation) since
  // there's no trust store on this device - fine for hobby use, not for
  // anything security-sensitive.
  bool cmdCurl(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: curl [-X METHOD] [-d data] <url>");
      return true;
    }
    if (WiFi.status() != WL_CONNECTED) {
      term.println("WiFi is off (run 'wifi connect' first)");
      return true;
    }

    String method = "GET";
    String data = "";
    String url = "";

    for (size_t i = 1; i < args.size(); i++) {
      if (args[i] == "-X" && i + 1 < args.size()) {
        method = args[++i];
        method.toUpperCase();
      } else if (args[i] == "-d" && i + 1 < args.size()) {
        data = args[++i];
        if (method == "GET") method = "POST";
      } else {
        url = args[i];
      }
    }

    if (url.length() == 0) {
      term.println("Usage: curl [-X METHOD] [-d data] <url>");
      return true;
    }

    HTTPClient http;
    WiFiClientSecure secureClient;

    bool began;
    if (url.startsWith("https://")) {
      secureClient.setInsecure();
      began = http.begin(secureClient, url);
    } else {
      began = http.begin(url);
    }

    if (!began) {
      term.println("curl: could not parse URL: " + url);
      return true;
    }

    int statusCode;
    if (method == "POST") {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      statusCode = http.POST(data);
    } else if (method == "PUT") {
      statusCode = http.PUT(data);
    } else if (method == "DELETE") {
      statusCode = http.sendRequest("DELETE");
    } else {
      statusCode = http.GET();
    }

    if (statusCode > 0) {
      out(http.getString());
    } else {
      term.println("curl: request failed: " + http.errorToString(statusCode));
    }

    http.end();
    return true;
  }

  // wget <url> [-O output] - downloads a URL straight to a file, using
  // the same HTTPClient/WiFiClientSecure plumbing as curl. Streams the
  // response in chunks rather than buffering the whole body in RAM.
  bool cmdWget(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: wget <url> [-O output]");
      return true;
    }
    if (WiFi.status() != WL_CONNECTED) {
      term.println("WiFi is off (run 'wifi connect' first)");
      return true;
    }

    String url = "";
    String outputArg = "";

    for (size_t i = 1; i < args.size(); i++) {
      if (args[i] == "-O" && i + 1 < args.size()) {
        outputArg = args[++i];
      } else {
        url = args[i];
      }
    }

    if (url.length() == 0) {
      term.println("Usage: wget <url> [-O output]");
      return true;
    }

    String outputPath;
    if (outputArg.length() > 0) {
      outputPath = fs.resolvePath(outputArg);
    } else {
      String base = url;
      int q = base.indexOf('?');
      if (q >= 0) base = base.substring(0, q);
      int slash = base.lastIndexOf('/');
      base = (slash >= 0 && slash + 1 < (int)base.length()) ? base.substring(slash + 1) : "download";
      outputPath = fs.resolvePath(base);
    }

    HTTPClient http;
    WiFiClientSecure secureClient;

    bool began;
    if (url.startsWith("https://")) {
      secureClient.setInsecure();
      began = http.begin(secureClient, url);
    } else {
      began = http.begin(url);
    }

    if (!began) {
      term.println("wget: could not parse URL: " + url);
      return true;
    }

    int statusCode = http.GET();
    if (statusCode <= 0) {
      term.println("wget: request failed: " + http.errorToString(statusCode));
      http.end();
      return true;
    }
    if (statusCode != 200) {
      term.println("wget: server returned HTTP " + String(statusCode));
      http.end();
      return true;
    }

    File outFile = fs.openRaw(outputPath, "w");
    if (!outFile) {
      term.println("wget: could not create " + outputPath);
      http.end();
      return true;
    }

    WiFiClient* stream = http.getStreamPtr();
    int total = http.getSize();
    int written = 0;
    uint8_t buf[512];

    while (http.connected() && (total < 0 || written < total)) {
      size_t avail = stream->available();
      if (avail == 0) {
        if (!stream->connected()) break;
        delay(5);
        continue;
      }
      size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
      int n = stream->readBytes(buf, toRead);
      if (n <= 0) break;
      outFile.write(buf, n);
      written += n;
    }

    outFile.close();
    http.end();

    term.println("Saved " + String(written) + " bytes to " + outputPath);
    return true;
  }

  // Parses ftp://[user[:pass]@]host[:port]/path into its parts. Returns
  // false if the URL isn't a valid ftp:// URL. Missing user/pass default
  // to "anonymous"/"" per FTP convention.
  bool parseFtpUrl(const String& url, String& host, uint16_t& port, String& user, String& pass, String& path) {
    if (!url.startsWith("ftp://")) return false;
    String rest = url.substring(6);

    int slash = rest.indexOf('/');
    String authority = (slash >= 0) ? rest.substring(0, slash) : rest;
    path = (slash >= 0) ? rest.substring(slash) : "/";
    if (path.length() == 0) path = "/";

    user = "anonymous";
    pass = "";
    int at = authority.lastIndexOf('@');
    if (at >= 0) {
      String cred = authority.substring(0, at);
      authority = authority.substring(at + 1);
      int colon = cred.indexOf(':');
      if (colon >= 0) {
        user = cred.substring(0, colon);
        pass = cred.substring(colon + 1);
      } else {
        user = cred;
      }
    }

    port = 21;
    int pcolon = authority.indexOf(':');
    if (pcolon >= 0) {
      port = authority.substring(pcolon + 1).toInt();
      host = authority.substring(0, pcolon);
    } else {
      host = authority;
    }

    return host.length() > 0;
  }

  // ftp get ftp://[user:pass@]host/remote/path [localfile]
  // ftp put <localfile> ftp://[user:pass@]host/remote/path
  // ftp ls ftp://[user:pass@]host/dir
  bool cmdFtp(const std::vector<String>& args) {
    if (args.size() < 3) {
      term.println("Usage: ftp get <ftp://url> [localfile]");
      term.println("       ftp put <localfile> <ftp://url>");
      term.println("       ftp ls <ftp://url>");
      return true;
    }
    if (WiFi.status() != WL_CONNECTED) {
      term.println("WiFi is off (run 'wifi connect' first)");
      return true;
    }

    String sub = args[1];
    String host, user, pass, path;
    uint16_t port;

    if (sub == "get") {
      if (!parseFtpUrl(args[2], host, port, user, pass, path)) {
        term.println("ftp: invalid URL: " + args[2]);
        return true;
      }
      String localPath;
      if (args.size() >= 4) {
        localPath = fs.resolvePath(args[3]);
      } else {
        int slash = path.lastIndexOf('/');
        String base = (slash >= 0 && slash + 1 < (int)path.length()) ? path.substring(slash + 1) : "download";
        localPath = fs.resolvePath(base);
      }

      char hostBuf[128], userBuf[64], passBuf[64];
      host.toCharArray(hostBuf, sizeof(hostBuf));
      user.toCharArray(userBuf, sizeof(userBuf));
      pass.toCharArray(passBuf, sizeof(passBuf));

      ESP32_FTPClient ftp(hostBuf, port, userBuf, passBuf, 10000, 0);
      ftp.OpenConnection();
      if (!ftp.isConnected()) {
        term.println("ftp: could not connect/login to " + host);
        return true;
      }

      File outFile = fs.openRaw(localPath, "w");
      if (!outFile) {
        term.println("ftp: could not create " + localPath);
        ftp.CloseConnection();
        return true;
      }

      ftp.InitFile("Type I");
      char pathBuf[256];
      path.toCharArray(pathBuf, sizeof(pathBuf));
      size_t written = ftp.DownloadFileToFile(pathBuf, outFile);
      outFile.close();
      ftp.CloseConnection();

      if (written == 0) {
        term.println("ftp: download failed or file not found: " + path);
        fs.deleteFile(localPath);
      } else {
        term.println("Saved " + String(written) + " bytes to " + localPath);
      }
      return true;

    } else if (sub == "put") {
      if (args.size() < 4) {
        term.println("Usage: ftp put <localfile> <ftp://url>");
        return true;
      }
      String localPath = fs.resolvePath(args[2]);
      if (!parseFtpUrl(args[3], host, port, user, pass, path)) {
        term.println("ftp: invalid URL: " + args[3]);
        return true;
      }

      File inFile = fs.openRaw(localPath, "r");
      if (!inFile) {
        term.println("ftp: no such file: " + localPath);
        return true;
      }

      char hostBuf[128], userBuf[64], passBuf[64];
      host.toCharArray(hostBuf, sizeof(hostBuf));
      user.toCharArray(userBuf, sizeof(userBuf));
      pass.toCharArray(passBuf, sizeof(passBuf));

      ESP32_FTPClient ftp(hostBuf, port, userBuf, passBuf, 10000, 0);
      ftp.OpenConnection();
      if (!ftp.isConnected()) {
        term.println("ftp: could not connect/login to " + host);
        inFile.close();
        return true;
      }

      ftp.InitFile("Type I");
      char pathBuf[256];
      path.toCharArray(pathBuf, sizeof(pathBuf));
      ftp.NewFile(pathBuf);

      size_t total = 0;
      uint8_t buf[512];
      while (inFile.available()) {
        size_t n = inFile.read(buf, sizeof(buf));
        if (n == 0) break;
        ftp.WriteData(buf, n);
        total += n;
      }
      inFile.close();
      ftp.CloseFile();
      ftp.CloseConnection();

      term.println("Uploaded " + String(total) + " bytes to " + path);
      return true;

    } else if (sub == "ls") {
      if (!parseFtpUrl(args[2], host, port, user, pass, path)) {
        term.println("ftp: invalid URL: " + args[2]);
        return true;
      }

      char hostBuf[128], userBuf[64], passBuf[64];
      host.toCharArray(hostBuf, sizeof(hostBuf));
      user.toCharArray(userBuf, sizeof(userBuf));
      pass.toCharArray(passBuf, sizeof(passBuf));

      ESP32_FTPClient ftp(hostBuf, port, userBuf, passBuf, 10000, 0);
      ftp.OpenConnection();
      if (!ftp.isConnected()) {
        term.println("ftp: could not connect/login to " + host);
        return true;
      }

      ftp.InitFile("Type A");
      char pathBuf[256];
      path.toCharArray(pathBuf, sizeof(pathBuf));
      String listing[128];
      ftp.ContentListWithListCommand(pathBuf, listing);
      ftp.CloseConnection();

      for (int i = 0; i < 128 && listing[i].length() > 0; i++) {
        term.println(listing[i]);
      }
      return true;

    } else {
      term.println("ftp: unknown subcommand: " + sub);
      return true;
    }
  }

  // mkali <source> <name> [-boot] - aliases a program (anywhere on the
  // filesystem, including /sd) so it's runnable by <name> from anywhere,
  // the same way /system/*.sh scripts already are. Works by dropping a
  // tiny wrapper script into /system, whose one line picks the right way
  // to run <source> based on its extension (.sh runs directly, .retro
  // goes through the retron interpreter, .elf goes through runelf).
  // With -boot, the same wrapper
  // is also dropped into /boot so it runs automatically at every startup.
  bool cmdMkali(const std::vector<String>& args) {
    if (args.size() < 3) {
      term.println("Usage: mkali <source> <name> [-boot]");
      return true;
    }

    String source = fs.resolvePath(args[1]);
    String name = args[2];
    bool atBoot = false;
    for (size_t i = 3; i < args.size(); i++) {
      if (args[i] == "-boot") atBoot = true;
    }

    if (!fs.exists(source)) {
      term.println("mkali: no such file: " + source);
      return true;
    }

    String wrapper;
    if (source.endsWith(".sh")) {
      wrapper = "./" + source + " $@\n";
    } else if (source.endsWith(".retro")) {
      wrapper = "retron " + source + " $@\n";
    } else if (source.endsWith(".elf")) {
      wrapper = "runelf " + source + " $@\n";
    } else {
      term.println("mkali: don't know how to run this file type: " + source);
      return true;
    }

    if (!fs.exists("/system")) fs.createDir("/system");
    fs.writeFile("/system/" + name + ".sh", wrapper);
    term.println("Aliased " + source + " as '" + name + "'");

    if (atBoot) {
      if (!fs.exists("/boot")) fs.createDir("/boot");
      fs.writeFile("/boot/" + name + ".sh", wrapper);
      term.println("Also set to run at boot");
    }

    return true;
  }

  // rmali <name> - removes an alias created by mkali, from /system (and
  // /boot too, if it was set to run there).
  bool cmdRmali(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: rmali <name>");
      return true;
    }

    String name = args[1];
    bool removedAny = false;

    if (fs.exists("/system/" + name + ".sh")) {
      fs.deleteFile("/system/" + name + ".sh");
      term.println("Removed alias '" + name + "' from /system");
      removedAny = true;
    }

    if (fs.exists("/boot/" + name + ".sh")) {
      fs.deleteFile("/boot/" + name + ".sh");
      term.println("Removed '" + name + "' from /boot");
      removedAny = true;
    }

    if (!removedAny) {
      term.println("rmali: no alias named '" + name + "'");
    }

    return true;
  }

  // ls-ali - lists every /system/*.sh command (mkali-created or not - the
  // wrapper format is indistinguishable from any other hand-written
  // one-liner script, so all of them show up here), along with what it
  // actually runs and whether it's also set to run at boot.
  bool cmdLsAli(const std::vector<String>& args) {
    if (!fs.exists("/system") || !fs.isDir("/system")) {
      term.println("No aliases (/system doesn't exist)");
      return true;
    }

    std::vector<String> names = fs.listDir("/system");
    std::sort(names.begin(), names.end());

    bool any = false;
    for (const auto& fname : names) {
      if (!fname.endsWith(".sh")) continue;
      any = true;

      String name = fname.substring(0, fname.length() - 3);
      String content = fs.readFile("/system/" + fname);
      content.trim();

      bool atBoot = fs.exists("/boot/" + fname);
      term.println(name + " -> " + content + (atBoot ? "  [boot]" : ""));
    }

    if (!any) {
      term.println("No aliases");
    }

    return true;
  }

  // runelf <path> [a] [b] - stage-1 native-code loader. NOT a real ELF
  // loader yet: it loads a flat, position-independent blob of raw Xtensa
  // machine code (produced on a PC by compiling a single self-contained
  // function with -mtext-section-literals -mlongcalls and then extracting
  // just its .text section with objcopy - see goals.md/README for the
  // exact recipe), copies it into executable RAM, and calls it. No
  // relocations, no symbol resolution against the firmware, no calls to
  // other functions or globals are supported - the loaded code must be
  // entirely self-contained. This is deliberately the smallest possible
  // slice of "run compiled code from SD" to prove the loading mechanism
  // (executable RAM allocation via heap_caps_malloc, copying, calling
  // through a function pointer) before building a real relocator/symbol
  // table on top of it.
  //
  // Signature depends on how many args are given: with both <a> and <b>,
  // calls int(int,int); with neither, calls int() - this second form is
  // what makes running an .elf at boot (via "mkali ... -boot") actually
  // useful, since a boot-time alias is invoked with no arguments at all.
  bool cmdRunelf(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: runelf <path> [a] [b]");
      term.println("With a and b: calls the loaded code as int(int,int).");
      term.println("With neither: calls it as int().");
      return true;
    }

    String path = fs.resolvePath(args[1]);
    bool hasArgs = args.size() >= 4;
    int a = hasArgs ? args[2].toInt() : 0;
    int b = hasArgs ? args[3].toInt() : 0;

    if (!fs.exists(path)) {
      term.println("runelf: no such file: " + path);
      return true;
    }

    File inFile = fs.openRaw(path, "r");
    if (!inFile) {
      term.println("runelf: could not open " + path);
      return true;
    }
    size_t len = inFile.size();
    if (len == 0) {
      term.println("runelf: empty file: " + path);
      inFile.close();
      return true;
    }

    // IRAM (MALLOC_CAP_EXEC memory) only supports 32-bit-aligned word
    // accesses on the ESP32 - a byte-wise read/memcpy into it faults with
    // a LoadStoreError. So the file is read into a normal byte-addressable
    // buffer first, then copied into the exec region one 32-bit word at a
    // time (padding the tail up to a word boundary with zero bytes).
    size_t wordLen = (len + 3) & ~((size_t)3);

    uint8_t* stagingBuf = (uint8_t*)calloc(1, wordLen);
    if (!stagingBuf) {
      term.println("runelf: could not allocate staging buffer");
      inFile.close();
      return true;
    }
    inFile.read(stagingBuf, len);
    inFile.close();

    void* execMem = heap_caps_malloc(wordLen, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
    if (!execMem) {
      term.println("runelf: could not allocate " + String(wordLen) + " bytes of executable RAM");
      free(stagingBuf);
      return true;
    }

    const uint32_t* src = (const uint32_t*)stagingBuf;
    uint32_t* dst = (uint32_t*)execMem;
    for (size_t i = 0; i < wordLen / 4; i++) {
      dst[i] = src[i];
    }
    free(stagingBuf);

    int result;
    if (hasArgs) {
      typedef int (*EntryFn2)(int, int);
      EntryFn2 entry = (EntryFn2)execMem;
      result = entry(a, b);
    } else {
      typedef int (*EntryFn0)();
      EntryFn0 entry = (EntryFn0)execMem;
      result = entry();
    }

    term.println("Result: " + String(result));
    heap_caps_free(execMem);
    return true;
  }

  // Parses "123" or "0x7f"-style tokens into a uint32_t, for runmod's
  // argument list (also useful for passing addresses around manually).
  uint32_t parseU32(const String& token) {
    if (token.startsWith("0x") || token.startsWith("0X")) {
      return (uint32_t)strtoul(token.c_str() + 2, nullptr, 16);
    }
    return (uint32_t)strtoul(token.c_str(), nullptr, 10);
  }

  // runmod <file.o> [file2.o ...] [--] <function> [arg1] [arg2] ... -
  // stage-3 native-code loader/linker. Unlike runelf (which needs a bare
  // .text dump with zero relocations), this loads one or more genuine
  // .o files - with .text/.rodata/.data/.bss, not just .text - resolves
  // calls to firmware-exported functions (see kRunmodSymbols above) and
  // to each other if more than one file is given, and calls a named
  // function by symbol name with up to 6 register-sized (int or
  // pointer-as-integer) arguments. A bare "--" is only needed to
  // disambiguate when the function name itself could be mistaken for a
  // filename; without one, the last file-shaped-looking argument before
  // a name that isn't a real file is treated as the function name.
  bool cmdRunmod(const std::vector<String>& args) {
    if (args.size() < 3) {
      term.println("Usage: runmod <file.o> [file2.o ...] [--] <function> [arg1] [arg2] ...");
      term.println("Up to 6 arguments, each a decimal or 0x-prefixed hex integer.");
      return true;
    }

    std::vector<String> files;
    String funcName;
    std::vector<String> callArgs;

    size_t i = 1;
    for (; i < args.size(); i++) {
      if (args[i] == "--") {
        i++;
        break;
      }
      String resolved = fs.resolvePath(args[i]);
      if (fs.exists(resolved)) {
        files.push_back(resolved);
      } else {
        break;  // first non-existent-file argument is the function name
      }
    }

    if (files.empty()) {
      term.println("runmod: no valid .o file given");
      return true;
    }
    if (i >= args.size()) {
      term.println("runmod: no function name given");
      return true;
    }

    funcName = args[i];
    for (i = i + 1; i < args.size(); i++) callArgs.push_back(args[i]);

    if (callArgs.size() > 6) {
      term.println("runmod: at most 6 arguments are supported");
      return true;
    }

    ElfModule mod(fs, term);
    if (!mod.load(files, kRunmodSymbols)) {
      return true;  // load() already printed a specific error
    }

    if (!mod.hasFunction(funcName)) {
      term.println("runmod: no such function: " + funcName);
      return true;
    }

    std::vector<uint32_t> parsedArgs;
    for (const auto& a : callArgs) parsedArgs.push_back(parseU32(a));

    uint32_t result;
    if (!mod.call(funcName, parsedArgs, result)) {
      term.println("runmod: call failed");
      return true;
    }

    term.println("Result: " + String(result));
    return true;
  }

  // Runs a .retro script (RetroGigabyte/Retron language), ported from
  // that project's reference interpreter. DRAW errors clearly rather
  // than silently no-op'ing, since composite video output isn't wired up
  // on this ESP-Nix build yet - see goals.md.
  bool cmdRetron(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: retron <file.retro>");
      return true;
    }

    String path = fs.resolvePath(args[1]);
    RetronInterpreter interpreter(fs, term);
    if (!interpreter.loadScript(path)) {
      return true;
    }

    interpreter.execute();
    return true;
  }

  // Syncs the system clock over NTP. Uses an existing WiFi connection if
  // there is one; otherwise briefly connects using the saved WIFI_SSID
  // (from 'web -join') and disconnects again afterward. TZ_OFFSET (in
  // seconds, e.g. -18000 for EST) is read from /etc/settings/esp-nix.conf if set.
  bool doNtpSync() {
    bool wasConnected = (WiFi.status() == WL_CONNECTED);

    if (!wasConnected) {
      String ssid = (vars && vars->exists("WIFI_SSID")) ? vars->get("WIFI_SSID") : "";
      if (ssid.length() == 0) {
        term.println("No saved WiFi network - run 'web -join' first.");
        return false;
      }

      String pass = (vars && vars->exists("WIFI_PASS")) ? vars->get("WIFI_PASS") : "";
      term.println("Connecting to " + ssid + " for time sync...");
      WiFi.mode(WIFI_STA);
      applyWifiHostname();
      WiFi.begin(ssid.c_str(), pass.c_str());

      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
      }

      if (WiFi.status() != WL_CONNECTED) {
        term.println("Failed to connect to WiFi.");
        WiFi.mode(WIFI_OFF);
        return false;
      }
    }

    long tzOffset = (vars && vars->exists("TZ_OFFSET")) ? vars->get("TZ_OFFSET").toInt() : 0;
    configTime(tzOffset, 0, "pool.ntp.org", "time.nist.gov");

    term.println("Syncing time...");
    time_t now = 0;
    unsigned long start = millis();
    while (now < 100000 && millis() - start < 10000) {
      delay(200);
      now = time(nullptr);
    }

    if (!wasConnected) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }

    if (now < 100000) {
      term.println("Time sync failed.");
      return false;
    }

    struct tm* timeinfo = localtime(&now);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    term.println("Time synced: " + String(buf));
    return true;
  }

  bool cmdNtp(const std::vector<String>& args) {
    doNtpSync();
    return true;
  }

public:
  // Syncs time at boot, but only if a WiFi network is already saved -
  // otherwise this would add a connect-timeout delay to every boot for
  // setups that don't use WiFi at all. Called once from main.cpp.
  bool syncTimeAtBoot() {
    if (!vars || !vars->exists("WIFI_SSID")) return false;
    return doNtpSync();
  }

  // Runs every *.sh script in /boot, in alphabetical order, at startup -
  // the ESP-Nix equivalent of /etc/init.d. Runs verbosely so any boot
  // script output is visible on the console during startup.
  void runBootScripts() {
    const char* bootDir = "/boot";
    if (!fs.exists(bootDir) || !fs.isDir(bootDir)) return;

    std::vector<String> scripts;
    for (const auto& name : fs.listDir(bootDir)) {
      if (name.endsWith(".sh")) scripts.push_back(name);
    }
    std::sort(scripts.begin(), scripts.end());

    for (const auto& name : scripts) {
      runScriptFile(String(bootDir) + "/" + name, true);
    }
  }

  // Runs every .elf/.o "driver" program in /sd/drivers, in alphabetical
  // order, at startup - no mkali alias needed, unlike /system or /boot
  // scripts. A .elf is called the same zero-argument way runelf already
  // supports for boot use (see runelf's own -boot support in mkali). A
  // .o is loaded via runmod and expected to export a function literally
  // named "main" as its entry point, since there's no other way to know
  // which function in an arbitrary object file should run automatically -
  // one missing from a given .o is reported and skipped, not fatal to
  // the rest of boot.
  void runDriverPrograms() {
    const char* driversDir = "/sd/drivers";
    if (!fs.sdAvailable() || !fs.exists(driversDir) || !fs.isDir(driversDir)) return;

    std::vector<String> names = fs.listDir(driversDir);
    std::sort(names.begin(), names.end());

    for (const auto& name : names) {
      String path = String(driversDir) + "/" + name;

      if (name.endsWith(".elf")) {
        term.println("[driver] " + name);
        cmdRunelf({"runelf", path});
      } else if (name.endsWith(".o")) {
        term.println("[driver] " + name);
        ElfModule mod(fs, term);
        if (!mod.load({path}, kRunmodSymbols)) continue;
        if (!mod.hasFunction("main")) {
          term.println("driver: " + name + " has no main() entry point - skipping");
          continue;
        }
        uint32_t result;
        std::vector<uint32_t> noArgs;
        mod.call("main", noArgs, result);
      }
    }
  }

private:

  bool cmdExtract(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: extract [archive] [destdir]");
      term.println("Supports: .zip, .tar.gz, .tgz, .gz, .tar");
      return true;
    }

    String archivePath = fs.resolvePath(args[1]);
    String destDir = args.size() >= 3 ? fs.resolvePath(args[2]) : "";

    Archiver archiver;
    archiver.extract(fs, term, archivePath, destDir);
    return true;
  }

  bool cmdCompress(const std::vector<String>& args) {
    if (args.size() < 3) {
      term.println("Usage: compress [source] [archive]");
      term.println("Output type is chosen by archive's extension:");
      term.println("  .zip     - file or directory");
      term.println("  .tar.gz/.tgz/.tar - directory only");
      term.println("  .gz      - single file only");
      return true;
    }

    String sourcePath = fs.resolvePath(args[1]);
    String archivePath = fs.resolvePath(args[2]);

    Archiver archiver;
    archiver.compress(fs, term, sourcePath, archivePath);
    return true;
  }

  // Writes WIFI_SSID/WIFI_PASS into /etc/settings/esp-nix.conf, replacing any
  // existing entries, so the joined network is remembered across reboots.
  void persistWifiCredentials(const String& ssid, const String& password) {
    String content = fs.readFile("/etc/settings/esp-nix.conf");
    std::vector<String> lines = splitLines(content);

    String result = "";
    for (const auto& line : lines) {
      if (line.startsWith("WIFI_SSID=") || line.startsWith("WIFI_PASS=")) continue;
      result += line + "\n";
    }
    result += "WIFI_SSID=" + ssid + "\n";
    result += "WIFI_PASS=" + password + "\n";

    if (result.endsWith("\n")) {
      result.remove(result.length() - 1);
    }

    fs.writeFile("/etc/settings/esp-nix.conf", result);
  }

  // Sets key=value in a config file, replacing an existing line for that
  // key if present, otherwise appending it.
  void setConfigValue(const String& path, const String& key, const String& value) {
    std::vector<String> lines = splitLines(fs.readFile(path));

    String result = "";
    bool replaced = false;
    for (const auto& line : lines) {
      if (line.startsWith(key + "=")) {
        result += key + "=" + value + "\n";
        replaced = true;
      } else {
        result += line + "\n";
      }
    }
    if (!replaced) {
      result += key + "=" + value + "\n";
    }
    if (result.endsWith("\n")) result.remove(result.length() - 1);

    fs.writeFile(path, result);
  }

  // Day-of-month of the nth weekday-0(Sunday) occurrence in a given
  // month/year - used to compute US DST transition dates.
  int nthSundayOfMonth(int year, int month, int n) {
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    t.tm_hour = 12;
    time_t tt = mktime(&t);
    struct tm* filled = gmtime(&tt);
    int firstSunday = (7 - filled->tm_wday) % 7 + 1;
    return firstSunday + (n - 1) * 7;
  }

  // US DST rule: starts 2nd Sunday of March, ends 1st Sunday of November.
  // Whole-day granularity (ignores the 2am transition time), close enough
  // for a hobby OS clock.
  bool isUsDstActive(time_t utcNow) {
    struct tm t = *gmtime(&utcNow);
    int year = t.tm_year + 1900;
    int month = t.tm_mon + 1;
    int day = t.tm_mday;

    if (month < 3 || month > 11) return false;
    if (month > 3 && month < 11) return true;

    if (month == 3) return day >= nthSundayOfMonth(year, 3, 2);
    return day < nthSundayOfMonth(year, 11, 1);  // month == 11
  }

  // Looks up "name,standardOffsetSeconds,dstOffsetSeconds" in the
  // editable timezone table (auto-created on first boot, extendable by
  // just editing the file - no reflash needed to add a zone).
  bool lookupTimezone(const String& name, long& stdOffset, long& dstOffsetVal) {
    for (auto line : splitLines(fs.readFile("/etc/settings/timezones.txt"))) {
      line.trim();
      if (line.length() == 0 || line.startsWith("#")) continue;

      int c1 = line.indexOf(',');
      int c2 = line.indexOf(',', c1 + 1);
      if (c1 < 0 || c2 < 0) continue;

      String entryName = line.substring(0, c1);
      entryName.trim();
      entryName.toLowerCase();

      if (entryName == name) {
        stdOffset = line.substring(c1 + 1, c2).toInt();
        dstOffsetVal = line.substring(c2 + 1).toInt();
        return true;
      }
    }
    return false;
  }

  bool cmdSetTz(const std::vector<String>& args) {
    if (args.size() < 2 || args[1] == "-list") {
      for (auto line : splitLines(fs.readFile("/etc/settings/timezones.txt"))) {
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;
        out(line);
      }
      if (args.size() < 2) term.println("Usage: settz <name>   (see list above)");
      return true;
    }

    String name = args[1];
    name.toLowerCase();

    long stdOffset, dstOffsetVal;
    if (!lookupTimezone(name, stdOffset, dstOffsetVal)) {
      term.println("Unknown timezone: " + args[1] + " (try 'settz -list')");
      return true;
    }

    bool dst = isUsDstActive(time(nullptr));
    long offset = dst ? dstOffsetVal : stdOffset;

    if (vars) vars->set("TZ_OFFSET", String(offset));
    setConfigValue("/etc/settings/esp-nix.conf", "TZ_OFFSET", String(offset));

    term.println("Timezone set to " + args[1] + " (" + (dst ? "DST" : "standard") +
                  "): TZ_OFFSET=" + String(offset));
    return true;
  }

  bool cmdUpdate(const std::vector<String>& args) {
    OtaUpdater updater;

    if (args.size() >= 2) {
      String path = fs.resolvePath(args[1]);
      updater.runPath(fs, term, path);
    } else {
      updater.runAuto(fs, term);
    }

    return true;
  }

  bool cmdRebuild(const std::vector<String>& args) {
    const char* configPath = "/etc/settings/esp-nix.conf";
    if (!fs.exists(configPath)) {
      term.println("No config found at " + String(configPath));
      return true;
    }

    term.println("Applying " + String(configPath) + " ...");
    runScriptFile(configPath, true);
    term.println("Rebuild complete.");
    return true;
  }
  // Writes a line of command output, respecting redirection capture
  void out(const String& s) {
    if (captureBuffer) {
      *captureBuffer += s + "\n";
    } else {
      term.println(s);
    }
  }

  // Writes raw text (no forced trailing newline), respecting redirection capture
  void outRaw(const String& s) {
    if (captureBuffer) {
      *captureBuffer += s;
    } else {
      term.print(s);
    }
  }

  // Reads one line from Serial, correctly handling \r\n as a single line
  // ending. Supports left/right arrows to move the cursor and mid-line
  // insert/backspace, not just appending at the end.
  String readLine() {
    int navKey = 0;
    return readLineEditable("", navKey);
  }

  // Like readLine(), but starts with existing content (so the caller can
  // "reopen" a previously-entered line for editing) and reports whether
  // Up or Down was pressed to end input, via navKey: 1 = up, -1 = down,
  // 0 = Enter. Up/Down commit whatever's currently typed, same as Enter,
  // but let the caller navigate to a different line instead of advancing
  // to a new one.
  String readLineEditable(const String& seed, int& navKey) {
    String line = seed;
    size_t cursor = seed.length();
    navKey = 0;

    if (seed.length() > 0) {
      Serial.print(seed);
    }

    while (true) {
      if (input.available()) {
        char c = input.read();

        if (c == '\r') {
          // Peek ahead briefly for a paired \n
          delay(5);
          if (input.available() && input.peek() == '\n') {
            input.read();
          }
          Serial.println();
          break;
        } else if (c == '\n') {
          Serial.println();
          break;
        } else if (c == 27) {  // ESC - arrow keys
          delay(5);
          if (input.available() && input.read() == '[') {
            delay(5);
            if (input.available()) {
              char code = input.read();
              if (code == 'D' && cursor > 0) {  // left
                cursor--;
                Serial.write(8);
              } else if (code == 'C' && cursor < line.length()) {  // right
                Serial.write(line[cursor]);
                cursor++;
              } else if (code == 'A') {  // up - commit line, navigate back
                navKey = 1;
                return line;  // no newline: caller redraws this row in place
              } else if (code == 'B') {  // down - commit line, navigate forward
                navKey = -1;
                return line;  // no newline: caller redraws this row in place
              }
            }
          }
        } else if (c == 8 || c == 127) {  // Backspace
          if (cursor > 0) {
            line.remove(cursor - 1, 1);
            cursor--;
            Serial.write(8);
            String tail = line.substring(cursor);
            Serial.print(tail);
            Serial.print(" ");
            for (size_t i = 0; i < tail.length() + 1; i++) Serial.write(8);
          } else {
            // Already empty with nothing left to erase - signal the
            // caller to delete this line immediately rather than
            // requiring Enter on a blank line.
            navKey = 2;
            return line;  // line is "" here
          }
        } else if (c >= 32 && c < 127) {
          if (line.length() < 255) {
            line = line.substring(0, cursor) + c + line.substring(cursor);
            cursor++;
            String tail = line.substring(cursor - 1);
            Serial.print(tail);
            for (size_t i = 0; i < tail.length() - 1; i++) Serial.write(8);
          }
        }
      }
      delay(10);
    }
    return line;
  }

  std::vector<String> parseCommand(const String& input) {
    std::vector<String> args;
    String current = "";

    for (char c : input) {
      if (c == ' ' || c == '\t') {
        if (current.length() > 0) {
          args.push_back(current);
          current = "";
        }
      } else {
        current += c;
      }
    }

    if (current.length() > 0) {
      args.push_back(current);
    }

    return args;
  }

  bool cmdHelp(const std::vector<String>& args) {
    out("ESP-Nix 1.3 - Available commands:");
    out("  help        - Show this help");
    out("  ls [-l] [path] - List directory (-l for permissions/size/date)");
    out("  pwd         - Print working directory");
    out("  cd [path]   - Change directory");
    out("  cat [file]  - Display file contents");
    out("  echo [text] - Print text");
    out("  touch [f]   - Create empty file");
    out("  rm [f]      - Remove file");
    out("  cp [s] [d]  - Copy file");
    out("  mv [s] [d]  - Move/rename file");
    out("  grep [p] [f]- Search file for pattern");
    out("  head [f] [n]- Show first n lines (default 10)");
    out("  tail [f] [n]- Show last n lines (default 10)");
    out("  mkdir [d]   - Create directory");
    out("  clear       - Clear screen");
    out("  edit [f]    - Edit file (:!q save, :d<n> delete, :i<n> insert)");
    out("                arrows move cursor/lines, Up/Down reopen a line");
    out("  ./[script]  - Execute shell script");
    out("  uname [opt] - System information");
    out("  whoami      - Current user");
    out("  date        - Current date/time (DATE_FORMAT/TIME_FORMAT in settings)");
    out("  df          - Disk free space");
    out("  free        - Free memory");
    out("  exit        - Exit shell");
    out("  cmd > file  - Redirect output (overwrite)");
    out("  cmd >> file - Redirect output (append)");
    out("  nixos-rebuild - Re-apply /etc/settings/esp-nix.conf");
    out("  webserver   - Start WiFi file server for the SD card (or 'web')");
    out("  web -join -list          - Scan and list nearby WiFi networks");
    out("  web -join <n> -pass=PW   - Join network #n from the last scan");
    out("  update [file] - Flash firmware from a .esp_update file");
    out("                  (auto-finds one on /sd or / if no path given)");
    out("  rm -r / cp -r / mv -r [dir] - Recursive directory ops");
    out("  find [path] [pattern] - Search for files/dirs by name");
    out("  wc [file]   - Count lines/words/bytes");
    out("  du [path]   - Show total size of a file or directory");
    out("  reboot      - Restart the system");
    out("  ntp         - Sync clock over WiFi (also happens on 'web -join')");
    out("  extract [archive] [destdir] - Unzip/untar (.zip .tar.gz .tgz .gz .tar)");
    out("  compress [source] [archive] - Zip/tar/gzip a file or directory");
    out("  test/[ EXPR ] - -e/-f/-d exists/file/dir, =/!=, -eq/-ne/-lt/-gt");
    out("  settz <name>  - Set TZ_OFFSET by timezone name (settz -list)");
    out("  nixfetch      - System summary with a logo (edit /etc/settings/logo.txt)");
    out("  loop <count|inf> [-i secs] <cmd...> - Repeat a command (any key stops it)");
    out("  wifi status|connect|disconnect|toggle - Persistent WiFi connection (stays up between commands)");
    out("  ip            - Show current IP address");
    out("  ping <host>   - Ping a host (requires 'wifi connect' first)");
    out("  curl [-X METHOD] [-d data] <url> - Basic HTTP client");
    out("  wget <url> [-O output] - Download a URL straight to a file");
    out("  ftp get <ftp://url> [file] | ftp put <file> <ftp://url> | ftp ls <ftp://url>");
    out("  mkali <source> <name> [-boot] - Alias a .sh/.retro/.elf (anywhere, incl. /sd) to run as <name>");
    out("  rmali <name> - Remove an alias created by mkali");
    out("  ls-ali - List aliases (what each runs, and whether it's set to run at boot)");
    out("  /sd/drivers/*.elf,*.o - Run automatically at boot, no alias needed (.o needs a main())");
    out("  runelf <path> [a] [b] - Run a self-contained compiled Xtensa function (stage 1, see README)");
    out("  runmod <file.o> [file2.o ...] [--] <fn> [args...] - Load/link .o(s), call a function (stage 3, see README)");
    out("  retron <file.retro> - Run a Retron language script (variables/loops/if/functions/CALL)");
    out("  sleep <seconds> - Pause (any key interrupts)");
    out("  hostname [-v] | hostname -s <name> - Show/set hostname (prompt + WiFi)");
    out("  backup -m|-l|-r# - Back up/list/restore internal storage to/from SD");
    out("  cat /proc/{version,uptime,meminfo,cpuinfo} - Virtual system info");
    out("  /system/*.sh files run anywhere by name (no ./ or .sh)");
    return true;
  }

  bool cmdLs(const std::vector<String>& args) {
    bool longFormat = false;
    String path = fs.getCurrentPath();

    for (size_t i = 1; i < args.size(); i++) {
      if (args[i] == "-l") longFormat = true;
      else path = fs.resolvePath(args[i]);
    }

    auto files = fs.listDir(path);
    if (files.empty()) {
      out("(empty)");
      return true;
    }

    for (const auto& f : files) {
      if (!longFormat) {
        out(f);
        continue;
      }

      String full = path;
      if (!full.endsWith("/")) full += "/";
      full += f;

      bool isDir = fs.isDir(full);
      String perms = isDir ? "drwxr-xr-x" : "-rw-r--r--";

      uint32_t size = 0;
      time_t mtime = 0;
      File entry = fs.openRaw(full);
      if (entry) {
        size = entry.size();
        mtime = entry.getLastWrite();
        entry.close();
      }

      char dateBuf[20];
      if (mtime > 100000) {
        struct tm* tmv = localtime(&mtime);
        strftime(dateBuf, sizeof(dateBuf), "%b %d %H:%M", tmv);
      } else {
        strcpy(dateBuf, "-");
      }

      char line[100];
      snprintf(line, sizeof(line), "%s %8u %-11s %s", perms.c_str(), (unsigned)size, dateBuf, f.c_str());
      out(line);
    }
    return true;
  }

  bool cmdPwd(const std::vector<String>& args) {
    out(fs.getCurrentPath());
    return true;
  }

  bool cmdCd(const std::vector<String>& args) {
    if (args.size() < 2) {
      fs.setCurrentPath("/");
      return true;
    }

    String target = args[1];

    // Expand a leading ~ to $HOME (falls back to / if HOME isn't set)
    if (target == "~" || target.startsWith("~/")) {
      String home = (vars && vars->exists("HOME")) ? vars->get("HOME") : "/";
      if (!home.endsWith("/")) home += "/";
      target = home + target.substring(target == "~" ? 1 : 2);
    }

    String path = fs.resolvePath(target);

    if (!fs.exists(path) || !fs.isDir(path)) {
      term.println("No such directory: " + target);
      return true;
    }

    fs.setCurrentPath(path);
    return true;
  }

  // A handful of virtual /proc files, generated on the fly rather than
  // read from storage - matches the Linux convention of exposing system
  // info as readable pseudo-files rather than only via commands.
  bool getProcContent(const String& path, String& content) {
    if (path == "/proc/version") {
      content = "ESP-Nix version 1.3 (FreeRTOS) Xtensa\n";
      return true;
    }
    if (path == "/proc/uptime") {
      content = String(millis() / 1000.0, 2) + "\n";
      return true;
    }
    if (path == "/proc/meminfo") {
      uint32_t total = ESP.getHeapSize();
      uint32_t freeMem = ESP.getFreeHeap();
      content = "MemTotal:       " + String(total / 1024) + " kB\n";
      content += "MemFree:        " + String(freeMem / 1024) + " kB\n";
      content += "MemUsed:        " + String((total - freeMem) / 1024) + " kB\n";
      return true;
    }
    if (path == "/proc/cpuinfo") {
      content = "model name\t: ESP32 (Xtensa LX6)\n";
      content += "cpu cores\t: 2\n";
      content += "cpu MHz\t\t: " + String(ESP.getCpuFreqMHz()) + "\n";
      return true;
    }
    return false;
  }

  bool cmdCat(const std::vector<String>& args) {
    String content;

    if (args.size() >= 2) {
      String path = fs.resolvePath(args[1]);
      if (!getProcContent(path, content)) {
        content = fs.readFile(path);
        if (content.length() == 0) {
          term.println("File not found or empty");
          return true;
        }
      }
    } else if (stdinBuffer) {
      content = *stdinBuffer;
    } else {
      term.println("Usage: cat [file]");
      return true;
    }

    outRaw(content);
    if (!content.endsWith("\n")) outRaw("\n");
    return true;
  }

  bool cmdEcho(const std::vector<String>& args) {
    String output = "";
    for (size_t i = 1; i < args.size(); i++) {
      if (i > 1) output += " ";
      output += args[i];
    }
    out(output);
    return true;
  }

  bool cmdTouch(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: touch [file]");
      return true;
    }

    String path = fs.resolvePath(args[1]);
    if (fs.writeFile(path, "")) {
      return true;
    }

    term.println("Error creating file");
    return true;
  }

  bool cmdRm(const std::vector<String>& args) {
    bool recursive = false;
    String target = "";

    for (size_t i = 1; i < args.size(); i++) {
      if (args[i] == "-r" || args[i] == "-rf") recursive = true;
      else target = args[i];
    }

    if (target.length() == 0) {
      term.println("Usage: rm [-r] [file|dir]");
      return true;
    }

    String path = fs.resolvePath(target);

    if (!fs.exists(path)) {
      term.println("Not found: " + path);
      return true;
    }

    if (fs.isDir(path)) {
      if (!recursive) {
        term.println("Is a directory (use rm -r): " + path);
        return true;
      }
      deleteRecursive(path);
      return true;
    }

    if (fs.deleteFile(path)) {
      return true;
    }

    term.println("Error deleting file");
    return true;
  }

  void deleteRecursive(const String& path) {
    if (fs.isDir(path)) {
      for (const auto& name : fs.listDir(path)) {
        String child = path;
        if (!child.endsWith("/")) child += "/";
        child += name;
        deleteRecursive(child);
      }
      fs.removeDir(path);
    } else {
      fs.deleteFile(path);
    }
  }

  void copyRecursive(const String& src, const String& dest) {
    if (fs.isDir(src)) {
      if (!fs.exists(dest)) fs.createDir(dest);
      for (const auto& name : fs.listDir(src)) {
        String s = src;
        if (!s.endsWith("/")) s += "/";
        s += name;
        String d = dest;
        if (!d.endsWith("/")) d += "/";
        d += name;
        copyRecursive(s, d);
      }
    } else {
      fs.writeFile(dest, fs.readFile(src));
    }
  }

  bool cmdMkdir(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: mkdir [dir]");
      return true;
    }

    String path = fs.resolvePath(args[1]);
    term.println("Creating: " + path);

    if (fs.createDir(path)) {
      term.println("Created directory: " + path);
      return true;
    }

    term.println("Error creating directory: " + path);
    return true;
  }

  bool cmdClear(const std::vector<String>& args) {
    term.clear();
    return true;
  }

  bool cmdUname(const std::vector<String>& args) {
    out("ESP-Nix 1.3");
    out("System: ESP32 WROOM32E");
    out("Arch: Xtensa");
    out("Kernel: FreeRTOS");
    out("Flash: 4MB | RAM: 520KB SRAM (~300KB usable after reserved regions)");
    return true;
  }

  String formatUptime(unsigned long totalSeconds) {
    unsigned long days = totalSeconds / 86400;
    unsigned long hours = (totalSeconds % 86400) / 3600;
    unsigned long minutes = (totalSeconds % 3600) / 60;

    String result = "";
    if (days > 0) result += String(days) + "d ";
    if (days > 0 || hours > 0) result += String(hours) + "h ";
    result += String(minutes) + "m";
    return result;
  }

  // Reads /etc/settings/logo.txt as the nixfetch logo, so it's yours to
  // customize (edit it, or 'cp' in something else) without touching
  // firmware. Falls back to a small built-in default if missing/empty.
  std::vector<String> loadLogo() {
    std::vector<String> logo;
    String content = fs.readFile("/etc/settings/logo.txt");

    for (auto line : splitLines(content)) {
      logo.push_back(line);
    }

    if (!logo.empty()) return logo;

    return {
      "    ______     ",
      "   /|_||_|\\    ",
      "  |  ESP-NIX | ",
      "  |__________| ",
      "  |_|  |  |_|  ",
      "    |  |  |    ",
      "   -+  +  +-   ",
      "               "
    };
  }

  // neofetch-style system summary: a small logo next to live stats.
  bool cmdNixfetch(const std::vector<String>& args) {
    std::vector<String> logo = loadLogo();

    std::vector<String> info;
    info.push_back("root@esp-nix");
    info.push_back("------------");
    info.push_back("OS: ESP-Nix 1.3");
    info.push_back("Host: ESP32 WROOM32E");
    info.push_back("Kernel: FreeRTOS");
    info.push_back("Uptime: " + formatUptime(millis() / 1000));
    info.push_back("Shell: /bin/nix");
    info.push_back("CPU: Xtensa LX6 @ " + String(ESP.getCpuFreqMHz()) + "MHz (2 cores)");
    info.push_back("Memory: " + String((ESP.getHeapSize() - ESP.getFreeHeap()) / 1024) +
                    "KB / " + String(ESP.getHeapSize() / 1024) + "KB");
    info.push_back("Disk (/): " + String(LittleFS.usedBytes() / 1024) +
                    "KB / " + String(LittleFS.totalBytes() / 1024) + "KB");

    if (fs.sdAvailable()) {
      info.push_back("Disk (/sd): " + String((uint32_t)(SD_MMC.usedBytes() / (1024ULL * 1024ULL))) +
                      "MB / " + String((uint32_t)(SD_MMC.totalBytes() / (1024ULL * 1024ULL))) + "MB");
    }

    if (WiFi.status() == WL_CONNECTED) {
      info.push_back("IP: " + WiFi.localIP().toString());
    }

    size_t totalLines = logo.size() > info.size() ? logo.size() : info.size();
    for (size_t i = 0; i < totalLines; i++) {
      String left = i < logo.size() ? logo[i] : String("               ");
      String right = i < info.size() ? info[i] : "";
      out(left + "  " + right);
    }
    return true;
  }

  bool cmdWhoami(const std::vector<String>& args) {
    out((vars && vars->exists("USER")) ? vars->get("USER") : "root");
    return true;
  }

  // hostname (or -v): show current hostname. hostname -s NAME: set it
  // (updates the live prompt immediately, and persists to config so it
  // also becomes the WiFi hostname on next connect).
  bool cmdHostname(const std::vector<String>& args) {
    String current = (vars && vars->exists("HOSTNAME")) ? vars->get("HOSTNAME") : "esp-nix";

    if (args.size() < 2 || args[1] == "-v") {
      out(current);
      return true;
    }

    if (args[1] == "-s" && args.size() >= 3) {
      String newName = args[2];
      if (vars) vars->set("HOSTNAME", newName);
      setConfigValue("/etc/settings/esp-nix.conf", "HOSTNAME", newName);
      term.println("Hostname set to " + newName + " (takes effect on next WiFi connect)");
      return true;
    }

    term.println("Usage: hostname [-v] | hostname -s <name>");
    return true;
  }

  // Applies TZ_OFFSET manually to the raw UTC epoch, then formats using
  // gmtime() on the shifted value (rather than relying on localtime()'s
  // own offset, which is only updated when configTime() runs during an
  // NTP sync) - this way editing TZ_OFFSET takes effect on the very next
  // 'date' call without needing to re-sync.
  bool cmdDate(const std::vector<String>& args) {
    long tzOffset = (vars && vars->exists("TZ_OFFSET")) ? vars->get("TZ_OFFSET").toInt() : 0;
    time_t localEpoch = time(nullptr) + tzOffset;
    struct tm* t = gmtime(&localEpoch);

    String dateFormat = (vars && vars->exists("DATE_FORMAT")) ? vars->get("DATE_FORMAT") : "us";
    String timeFormat = (vars && vars->exists("TIME_FORMAT")) ? vars->get("TIME_FORMAT") : "24";

    int year = t->tm_year + 1900;
    int month = t->tm_mon + 1;
    int day = t->tm_mday;

    String dateStr = (dateFormat == "iso")
      ? String(year) + "/" + String(month) + "/" + String(day)
      : String(month) + "/" + String(day) + "/" + String(year);

    char timeBuf[16];
    if (timeFormat == "12") {
      int hour12 = t->tm_hour % 12;
      if (hour12 == 0) hour12 = 12;
      snprintf(timeBuf, sizeof(timeBuf), "%d:%02d:%02d %s",
               hour12, t->tm_min, t->tm_sec, t->tm_hour < 12 ? "AM" : "PM");
    } else {
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    }

    out(dateStr + " " + String(timeBuf));
    return true;
  }

  bool cmdDf(const std::vector<String>& args) {
    // Get LittleFS info
    uint32_t totalBytes = LittleFS.totalBytes();
    uint32_t usedBytes = LittleFS.usedBytes();

    out("Filesystem  Size   Used Available Use%");
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "LittleFS    %uKB  %uKB  %uKB      %d%%",
             totalBytes / 1024,
             usedBytes / 1024,
             (totalBytes - usedBytes) / 1024,
             (usedBytes * 100) / totalBytes);
    out(buffer);

    if (fs.sdAvailable()) {
      uint64_t sdTotal = SD_MMC.totalBytes();
      uint64_t sdUsed = SD_MMC.usedBytes();
      char sdBuffer[50];
      snprintf(sdBuffer, sizeof(sdBuffer), "SD          %lluMB  %lluMB  %lluMB      %llu%%",
               sdTotal / (1024ULL * 1024ULL),
               sdUsed / (1024ULL * 1024ULL),
               (sdTotal - sdUsed) / (1024ULL * 1024ULL),
               sdTotal > 0 ? (sdUsed * 100ULL) / sdTotal : 0);
      out(sdBuffer);
    } else {
      out("SD          (not mounted)");
    }

    return true;
  }

  bool cmdFree(const std::vector<String>& args) {
    char buffer[80];
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    uint32_t usedHeap = totalHeap - freeHeap;

    snprintf(buffer, sizeof(buffer), "Total: %u KB | Used: %u KB | Free: %u KB",
             totalHeap / 1024, usedHeap / 1024, freeHeap / 1024);
    out(buffer);
    return true;
  }

  // Pulls out a -r/-rf flag and returns the remaining positional args
  std::vector<String> extractRecursiveFlag(const std::vector<String>& args, bool& recursive) {
    recursive = false;
    std::vector<String> positional;
    for (size_t i = 1; i < args.size(); i++) {
      if (args[i] == "-r" || args[i] == "-rf") recursive = true;
      else positional.push_back(args[i]);
    }
    return positional;
  }

  // If destPath is an existing directory, the actual target is the
  // source's basename inside it - e.g. "cp file.txt /sd" means
  // "/sd/file.txt", not literally overwriting "/sd" itself.
  // destIsDir is passed in rather than re-checked here on every call:
  // when the destination was just auto-created by cmdCp/cmdMv (for a
  // multi-match glob), re-querying fs.exists()/isDir() immediately
  // afterward isn't reliable on SD_MMC - the same class of VFS quirk
  // fixed for 'ls' in stripSd() (see v0.9.1.1's mkdir+ls bug fix).
  String resolveDestPath(const String& srcPath, const String& destPath, bool destIsDir) {
    if (destIsDir) {
      String base = srcPath;
      int slash = base.lastIndexOf('/');
      if (slash >= 0) base = base.substring(slash + 1);

      String d = destPath;
      if (!d.endsWith("/")) d += "/";
      return d + base;
    }
    return destPath;
  }

  // Expands a source argument that may contain a glob ("cool.*", "*.txt")
  // into a list of resolved absolute paths matching entries in that
  // directory. Arguments without a '*' are returned as-is (resolved),
  // unchanged, so this is safe to call unconditionally.
  std::vector<String> expandGlobSources(const String& arg) {
    std::vector<String> results;

    if (arg.indexOf('*') < 0) {
      results.push_back(fs.resolvePath(arg));
      return results;
    }

    String dir = fs.getCurrentPath();
    String namePattern = arg;
    int slash = arg.lastIndexOf('/');
    if (slash >= 0) {
      dir = fs.resolvePath(arg.substring(0, slash + 1));
      namePattern = arg.substring(slash + 1);
    }

    for (const auto& name : fs.listDir(dir)) {
      if (matchesPattern(name, namePattern)) {
        String full = dir;
        if (!full.endsWith("/")) full += "/";
        full += name;
        results.push_back(full);
      }
    }

    return results;
  }

  bool cmdCp(const std::vector<String>& args) {
    bool recursive;
    std::vector<String> positional = extractRecursiveFlag(args, recursive);

    if (positional.size() < 2) {
      term.println("Usage: cp [-r] [src] [dest]");
      return true;
    }

    std::vector<String> srcPaths = expandGlobSources(positional[0]);
    String destArg = fs.resolvePath(positional[1]);

    if (srcPaths.empty()) {
      term.println("No files match: " + positional[0]);
      return true;
    }

    // Multiple matches can't all land on one literal destination path -
    // auto-create it as a directory so each file gets its own name inside
    // it, instead of silently overwriting each other one by one.
    bool destIsDir = fs.exists(destArg) && fs.isDir(destArg);
    if (srcPaths.size() > 1 && !destIsDir) {
      destIsDir = fs.createDir(destArg);
    }

    for (const auto& srcPath : srcPaths) {
      copyOneFile(srcPath, destArg, destIsDir, recursive);
    }
    return true;
  }

  void copyOneFile(const String& srcPath, const String& destArg, bool destIsDir, bool recursive) {
    if (!fs.exists(srcPath)) {
      term.println("Source not found: " + srcPath);
      return;
    }

    String destPath = resolveDestPath(srcPath, destArg, destIsDir);

    if (fs.isDir(srcPath)) {
      if (!recursive) {
        term.println("Is a directory (use cp -r): " + srcPath);
        return;
      }
      copyRecursive(srcPath, destPath);
      return;
    }

    String content = fs.readFile(srcPath);
    if (!fs.writeFile(destPath, content)) {
      term.println("Error copying file: " + srcPath);
    }
  }

  bool cmdMv(const std::vector<String>& args) {
    bool recursive;
    std::vector<String> positional = extractRecursiveFlag(args, recursive);

    if (positional.size() < 2) {
      term.println("Usage: mv [-r] [src] [dest]");
      return true;
    }

    std::vector<String> srcPaths = expandGlobSources(positional[0]);
    String destArg = fs.resolvePath(positional[1]);

    if (srcPaths.empty()) {
      term.println("No files match: " + positional[0]);
      return true;
    }

    // Multiple matches can't all land on one literal destination path -
    // auto-create it as a directory so each file gets its own name inside
    // it, instead of silently overwriting each other one by one.
    bool destIsDir = fs.exists(destArg) && fs.isDir(destArg);
    if (srcPaths.size() > 1 && !destIsDir) {
      destIsDir = fs.createDir(destArg);
    }

    for (const auto& srcPath : srcPaths) {
      moveOneFile(srcPath, destArg, destIsDir, recursive);
    }
    return true;
  }

  void moveOneFile(const String& srcPath, const String& destArg, bool destIsDir, bool recursive) {
    if (!fs.exists(srcPath)) {
      term.println("Source not found: " + srcPath);
      return;
    }

    String destPath = resolveDestPath(srcPath, destArg, destIsDir);

    if (fs.isDir(srcPath)) {
      if (!recursive) {
        term.println("Is a directory (use mv -r): " + srcPath);
        return;
      }
      copyRecursive(srcPath, destPath);
      deleteRecursive(srcPath);
      return;
    }

    String content = fs.readFile(srcPath);
    if (!fs.writeFile(destPath, content)) {
      term.println("Error moving file: " + srcPath);
      return;
    }

    // Verify the copy actually landed before removing the original -
    // writeFile() can report success even when nothing usable was written
    // (e.g. the destination turned out to be a directory handle).
    if (fs.readFile(destPath) != content) {
      term.println("Error moving file: destination did not match source, keeping original: " + srcPath);
      return;
    }

    fs.deleteFile(srcPath);
  }

  bool cmdGrep(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: grep [pattern] [file]");
      return true;
    }

    String pattern = args[1];
    String content;

    if (args.size() >= 3) {
      String path = fs.resolvePath(args[2]);
      content = fs.readFile(path);
      if (content.length() == 0) {
        term.println("File not found or empty");
        return true;
      }
    } else if (stdinBuffer) {
      content = *stdinBuffer;
    } else {
      term.println("Usage: grep [pattern] [file]");
      return true;
    }

    std::vector<String> lines = splitLines(content);
    for (const auto& line : lines) {
      if (line.indexOf(pattern) >= 0) {
        out(line);
      }
    }
    return true;
  }

  // Resolves (content, lineCount) for head/tail, supporting either a
  // filename argument or piped stdin with an optional line count.
  bool resolveHeadTailInput(const std::vector<String>& args, String& content, int& count) {
    count = 10;

    if (args.size() >= 2) {
      bool argIsNumber = isNumeric(args[1]);
      if (stdinBuffer && argIsNumber) {
        content = *stdinBuffer;
        count = args[1].toInt();
        return true;
      }

      String path = fs.resolvePath(args[1]);
      content = fs.readFile(path);
      if (content.length() == 0) {
        term.println("File not found or empty");
        return false;
      }
      if (args.size() > 2) count = args[2].toInt();
      return true;
    }

    if (stdinBuffer) {
      content = *stdinBuffer;
      return true;
    }

    term.println("Usage: head/tail [file] [lines]");
    return false;
  }

  bool isNumeric(const String& s) {
    if (s.length() == 0) return false;
    for (char c : s) {
      if (!isDigit(c)) return false;
    }
    return true;
  }

  bool cmdHead(const std::vector<String>& args) {
    String content;
    int count;
    if (!resolveHeadTailInput(args, content, count)) return true;

    std::vector<String> lines = splitLines(content);
    for (int i = 0; i < (int)lines.size() && i < count; i++) {
      out(lines[i]);
    }
    return true;
  }

  bool cmdTail(const std::vector<String>& args) {
    String content;
    int count;
    if (!resolveHeadTailInput(args, content, count)) return true;

    std::vector<String> lines = splitLines(content);
    int start = (int)lines.size() - count;
    if (start < 0) start = 0;

    for (int i = start; i < (int)lines.size(); i++) {
      out(lines[i]);
    }
    return true;
  }

  std::vector<String> splitLines(const String& content) {
    std::vector<String> lines;
    String line = "";
    for (int i = 0; i < (int)content.length(); i++) {
      char c = content[i];
      if (c == '\n' || c == '\r') {
        lines.push_back(line);
        line = "";
        if (c == '\r' && i + 1 < (int)content.length() && content[i + 1] == '\n') {
          i++;
        }
      } else {
        line += c;
      }
    }
    if (line.length() > 0) {
      lines.push_back(line);
    }
    return lines;
  }

  // A small full-screen editor in the style of nano: the whole buffer is
  // visible at once, arrow keys move the cursor freely through it, Enter
  // splits a line, Backspace at the start of a line merges it with the
  // previous one. Ctrl+O saves, Ctrl+X saves and exits.
  //   :!q       save and quit
  //   :d<n>     delete line n
  //   :i<n>     insert a new line before line n (prompts for its content)
  //   Up/Down   move to the previous/next line to edit it in place
  bool cmdEdit(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: edit [file]");
      return true;
    }

    String filepath = fs.resolvePath(args[1]);
    String fileContent = fs.readFile(filepath);

    std::vector<String> lines;
    for (auto line : splitLines(fileContent)) {
      line.trim();
      if (line.length() > 0) lines.push_back(line);
    }

    term.println("--- Edit Mode ---");
    term.println("Commands: :!q save+quit  :d<n> delete  :i<n> insert  Up/Down move between lines");
    printEditorLines(lines);

    // idx == lines.size() means "new line at the end" (append mode);
    // idx < lines.size() means editing that existing line in place.
    int idx = (int)lines.size();

    while (true) {
      String seed = (idx < (int)lines.size()) ? lines[idx] : "";
      String prompt = String(idx + 1) + ": ";
      Serial.print(prompt);

      int navKey = 0;
      String userLine = readLineEditable(seed, navKey);

      if (navKey != 0) {
        // Up/Down/backspace-delete don't end with a newline - erase this
        // row in place (carriage return, overwrite with spaces, carriage
        // return again) instead of scrolling a fresh line per keypress.
        size_t oldLen = prompt.length() + userLine.length();
        Serial.print('\r');
        for (size_t i = 0; i < oldLen; i++) Serial.print(' ');
        Serial.print('\r');
      }

      if (navKey == 2) {
        // Backspace pressed with nothing left to erase on this line -
        // delete it immediately (if it existed) and move to the
        // previous line, without needing Enter on a blank line first.
        if (idx < (int)lines.size()) {
          lines.erase(lines.begin() + idx);
          term.println("Deleted line " + String(idx + 1));
          printEditorLines(lines);
        }
        if (idx > 0) idx--;
        continue;
      }

      if (userLine == ":!q") {
        break;
      }

      if (userLine.startsWith(":d")) {
        String numStr = userLine.substring(2);
        if (isNumeric(numStr)) {
          int n = numStr.toInt();
          if (n >= 1 && n <= (int)lines.size()) {
            lines.erase(lines.begin() + (n - 1));
            term.println("Deleted line " + String(n));
            printEditorLines(lines);
            if (idx > (int)lines.size()) idx = (int)lines.size();
          } else {
            term.println("No such line: " + String(n));
          }
        } else {
          term.println("Usage: :d<line number>");
        }
        continue;
      }

      if (userLine.startsWith(":i")) {
        String numStr = userLine.substring(2);
        if (isNumeric(numStr)) {
          int n = numStr.toInt();
          if (n >= 1 && n <= (int)lines.size() + 1) {
            Serial.print("New line " + String(n) + ": ");
            String newContent = readLine();
            newContent.trim();
            if (newContent.length() > 0) {
              lines.insert(lines.begin() + (n - 1), newContent);
              term.println("Inserted at line " + String(n));
              printEditorLines(lines);
              idx = (int)lines.size();
            }
          } else {
            term.println("No such line: " + String(n));
          }
        } else {
          term.println("Usage: :i<line number>");
        }
        continue;
      }

      userLine.trim();
      bool deleted = false;

      if (idx < (int)lines.size()) {
        // Editing an existing line: blank clears it, otherwise update in place
        if (userLine.length() == 0) {
          lines.erase(lines.begin() + idx);
          deleted = true;
        } else {
          lines[idx] = userLine;
        }
      } else if (userLine.length() > 0) {
        lines.push_back(userLine);
      }

      if (userLine.length() > 0) {
        if (userLine.length() > 16) {
          term.setInput("..." + userLine.substring(userLine.length() - 13));
        } else {
          term.setInput(userLine);
        }
      }

      if (navKey == 1) {  // Up
        if (idx > 0) idx--;
      } else if (!deleted && idx < (int)lines.size()) {
        // Down, or Enter: advance - unless a delete already shifted the
        // next line into idx's old position, in which case idx is
        // already pointing at the right place.
        idx++;
      }
    }

    String savedContent = "";
    for (size_t i = 0; i < lines.size(); i++) {
      savedContent += lines[i];
      if (i < lines.size() - 1) savedContent += "\n";
    }

    fs.writeFile(filepath, savedContent);
    term.println("Saved.");

    return true;
  }

  void printEditorLines(const std::vector<String>& lines) {
    for (size_t i = 0; i < lines.size(); i++) {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(lines[i]);
    }
  }

  bool executeScript(const String& scriptPath) {
    // Extract filename without ./
    String filepath = scriptPath.substring(2);  // Remove "./"
    filepath = fs.resolvePath(filepath);
    return runScriptFile(filepath, true);
  }

  // Runs each line of a script file as a command. Blank lines and lines
  // starting with # are skipped. Lines that look like "VAR=value" or
  // "export VAR=value" set a variable instead of being executed as a
  // command - the same assignment syntax the interactive shell supports.
  // "$@" in any line is replaced with extraArgs, so a one-line wrapper
  // script (e.g. /system/web.sh containing "webserver $@") can forward
  // whatever was typed after its own name. When verbose, prints a banner
  // and echoes each line as it runs (used for explicit ./script
  // invocations); silent mode is used for the boot config and for
  // /system/*.sh commands, which should feel like plain commands rather
  // than visibly "running a script".
  bool runScriptFile(const String& filepath, bool verbose, const String& extraArgs = "") {
    String content = fs.readFile(filepath);

    if (content.isEmpty()) {
      if (verbose) term.println("Error: Script not found or empty: " + filepath);
      return true;
    }

    if (verbose) term.println("--- Executing: " + filepath + " ---");

    String currentLine = "";
    int lineNum = 0;

    for (char c : content) {
      if (c == '\n') {
        lineNum++;
        if (!runScriptLine(currentLine, lineNum, verbose, extraArgs)) return false;
        currentLine = "";
      } else if (c != '\r') {
        currentLine += c;
      }
    }

    if (currentLine.length() > 0) {
      lineNum++;
      if (!runScriptLine(currentLine, lineNum, verbose, extraArgs)) return false;
    }

    if (verbose) term.println("--- Script complete ---");
    return true;
  }

  // Returns false if the script should stop (exit was called)
  bool runScriptLine(String currentLine, int lineNum, bool verbose, const String& extraArgs) {
    currentLine.trim();

    if (currentLine.length() == 0 || currentLine.startsWith("#")) {
      return true;
    }

    if (currentLine.indexOf("$@") >= 0) {
      currentLine.replace("$@", extraArgs);
      currentLine.trim();
    }

    if (vars && applyVariableAssignment(*vars, currentLine)) {
      if (verbose) term.println("[" + String(lineNum) + "] " + currentLine);
      return true;
    }

    if (verbose) term.println("[" + String(lineNum) + "] " + currentLine);

    if (!execute(currentLine)) {
      if (verbose) term.println("Script terminated by user");
      return false;
    }

    return true;
  }

  bool cmdEnv(const std::vector<String>& args) {
    if (!vars) {
      term.println("Variables not initialized");
      return true;
    }

    if (args.size() > 1) {
      // env NAME=value
      String name = args[1];
      if (name.indexOf('=') > 0) {
        int eqPos = name.indexOf('=');
        String varName = name.substring(0, eqPos);
        String varValue = name.substring(eqPos + 1);
        vars->set(varName, varValue);
        term.println("Set: " + varName + "=" + varValue);
        return true;
      }
    }

    // Show all variables
    out("Environment Variables:");
    vars->printAll();
    return true;
  }

  bool cmdExit(const std::vector<String>& args) {
    term.println("Bye!");
    return false;
  }

  // loop <count|inf> [-i seconds] <command...> - repeats a command since
  // the script engine has no real loop construct (no if/for, just a flat
  // command sequence). Any keypress stops it early, even mid-wait.
  // sleep <seconds> - pauses, interruptible by any keypress (same pattern
  // as loop's between-iteration wait).
  // Copies a file byte-for-byte via openRaw(), safe for binary content
  // (zip archives) unlike readFile()/writeFile()'s String-based path.
  bool binaryCopy(const String& srcPath, const String& destPath) {
    File src = fs.openRaw(srcPath);
    if (!src) return false;
    File dest = fs.openRaw(destPath, "w");
    if (!dest) { src.close(); return false; }

    uint8_t buf[512];
    size_t n;
    while ((n = src.read(buf, sizeof(buf))) > 0) {
      dest.write(buf, n);
    }
    src.close();
    dest.close();
    return true;
  }

  // Backup filenames sort correctly by name already (YYYYMMDD-HHMMSS),
  // so a plain alphabetical sort doubles as chronological order.
  std::vector<String> listBackups() {
    std::vector<String> result;
    for (const auto& name : fs.listDir("/sd/backups")) {
      if (name.endsWith(".esp_bak")) result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  // backup -m|-l|-r# - backs up internal LittleFS ("user space" - config,
  // boot/system scripts, history, everything not part of the compiled
  // firmware itself) to /sd/backups as hostname-YYYYMMDD-HHMMSS.esp_bak.
  // Internally these are just zip files; the extension is renamed after
  // compression (and back before extraction) since Archiver dispatches
  // format by the archive path's extension, not its actual content.
  bool cmdBackup(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: backup -m (make) | backup -l (list) | backup -r <#> (restore)");
      return true;
    }

    if (!fs.sdAvailable()) {
      term.println("No SD card mounted - insert one and reboot first.");
      return true;
    }

    if (!fs.exists("/sd/backups")) fs.createDir("/sd/backups");

    if (args[1] == "-m") {
      String hostname = (vars && vars->exists("HOSTNAME")) ? vars->get("HOSTNAME") : "esp-nix";
      long tzOffset = (vars && vars->exists("TZ_OFFSET")) ? vars->get("TZ_OFFSET").toInt() : 0;
      time_t localEpoch = time(nullptr) + tzOffset;
      struct tm* t = gmtime(&localEpoch);

      char stamp[20];
      snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
               t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

      String finalPath = "/sd/backups/" + hostname + "-" + String(stamp) + ".esp_bak";
      String tempZip = "/sd/backups/.tmp_backup.zip";

      Archiver archiver;
      if (!archiver.compress(fs, term, "/", tempZip)) {
        term.println("Backup failed.");
        return true;
      }

      if (!binaryCopy(tempZip, finalPath)) {
        term.println("Backup failed: could not finalize " + finalPath);
        fs.deleteFile(tempZip);
        return true;
      }
      fs.deleteFile(tempZip);

      term.println("Backup created: " + finalPath);
      return true;
    }

    if (args[1] == "-l") {
      std::vector<String> backups = listBackups();
      if (backups.empty()) {
        term.println("No backups found in /sd/backups.");
        return true;
      }
      for (size_t i = 0; i < backups.size(); i++) {
        out(String(i + 1) + ") " + backups[i]);
      }
      return true;
    }

    if (args[1] == "-r") {
      if (args.size() < 3) {
        term.println("Usage: backup -r <#>  (see 'backup -l' for numbers)");
        return true;
      }

      std::vector<String> backups = listBackups();
      int index = args[2].toInt();
      if (index < 1 || index > (int)backups.size()) {
        term.println("Invalid backup number - run 'backup -l' first.");
        return true;
      }

      String backupPath = "/sd/backups/" + backups[index - 1];
      String tempZip = "/sd/backups/.tmp_restore.zip";

      if (!binaryCopy(backupPath, tempZip)) {
        term.println("Restore failed: could not read " + backupPath);
        return true;
      }

      Archiver archiver;
      bool ok = archiver.extract(fs, term, tempZip, "/");
      fs.deleteFile(tempZip);

      if (!ok) {
        term.println("Restore failed.");
        return true;
      }

      term.println("Restored " + backups[index - 1] + " - run 'nixos-rebuild' or reboot to reload config into the running shell.");
      return true;
    }

    term.println("Usage: backup -m (make) | backup -l (list) | backup -r <#> (restore)");
    return true;
  }

  bool cmdSleep(const std::vector<String>& args) {
    if (args.size() < 2) {
      term.println("Usage: sleep <seconds>");
      return true;
    }

    unsigned long waitMs = (unsigned long)args[1].toFloat() * 1000;
    unsigned long start = millis();
    while (millis() - start < waitMs) {
      if (input.available()) {
        input.read();
        term.println("Sleep interrupted.");
        return true;
      }
      delay(10);
    }
    return true;
  }

  bool cmdLoop(const std::vector<String>& args) {
    if (args.size() < 3) {
      term.println("Usage: loop <count|inf> [-i seconds] <command...>");
      return true;
    }

    bool infinite = (args[1] == "inf");
    long count = infinite ? 0 : args[1].toInt();

    size_t cmdStart = 2;
    unsigned long intervalMs = 0;
    if (args.size() > 3 && args[2] == "-i") {
      intervalMs = (unsigned long)args[3].toInt() * 1000;
      cmdStart = 4;
    }

    if (cmdStart >= args.size()) {
      term.println("Usage: loop <count|inf> [-i seconds] <command...>");
      return true;
    }

    String cmdLine = "";
    for (size_t i = cmdStart; i < args.size(); i++) {
      if (i > cmdStart) cmdLine += " ";
      cmdLine += args[i];
    }

    // Always wait a little between iterations, both to keep this
    // interruptible and to avoid spamming the terminal instantly - use
    // the -i interval if given, otherwise 1 second by default.
    unsigned long waitMs = intervalMs > 0 ? intervalMs : 1000;

    long i = 0;
    while (infinite || i < count) {
      if (fullExecutor) fullExecutor(cmdLine); else execute(cmdLine);
      i++;

      unsigned long start = millis();
      while (millis() - start < waitMs) {
        if (input.available()) {
          input.read();
          term.println("Loop stopped.");
          return true;
        }
        delay(10);
      }
    }
    return true;
  }

  // POSIX-style test/[ builtin, mainly useful with && / || for basic
  // scripting: test -f foo && echo "found" / [ -d bar ] || mkdir bar
  bool cmdTest(const std::vector<String>& args) {
    size_t start = 1;
    size_t end = args.size();

    if (args[0] == "[") {
      if (end < 2 || args[end - 1] != "]") {
        term.println("test: missing ]");
        return false;
      }
      end -= 1;
    }

    std::vector<String> t(args.begin() + start, args.begin() + end);

    if (t.empty()) return false;

    if (t.size() == 1) {
      return t[0].length() > 0;
    }

    if (t.size() == 2) {
      String op = t[0];
      String path = fs.resolvePath(t[1]);
      if (op == "-e") return fs.exists(path);
      if (op == "-f") return fs.exists(path) && !fs.isDir(path);
      if (op == "-d") return fs.exists(path) && fs.isDir(path);
      if (op == "-z") return t[1].length() == 0;
      if (op == "-n") return t[1].length() > 0;
      term.println("test: unsupported operator: " + op);
      return false;
    }

    if (t.size() == 3) {
      String lhs = t[0], op = t[1], rhs = t[2];
      if (op == "=") return lhs == rhs;
      if (op == "!=") return lhs != rhs;

      long lv = lhs.toInt(), rv = rhs.toInt();
      if (op == "-eq") return lv == rv;
      if (op == "-ne") return lv != rv;
      if (op == "-lt") return lv < rv;
      if (op == "-le") return lv <= rv;
      if (op == "-gt") return lv > rv;
      if (op == "-ge") return lv >= rv;
      term.println("test: unsupported operator: " + op);
      return false;
    }

    term.println("test: unsupported expression");
    return false;
  }

  // find [path] [pattern]  -- but a lone argument is a pattern (e.g.
  // "find *.txt") unless it happens to resolve to an existing directory,
  // in which case it's treated as the path to search (e.g. "find /system").
  bool cmdFind(const std::vector<String>& args) {
    String path = fs.getCurrentPath();
    String pattern = "";

    if (args.size() == 2) {
      String maybePath = fs.resolvePath(args[1]);
      if (fs.exists(maybePath) && fs.isDir(maybePath)) {
        path = maybePath;
      } else {
        pattern = args[1];
      }
    } else if (args.size() >= 3) {
      path = fs.resolvePath(args[1]);
      pattern = args[2];
    }

    if (!fs.exists(path) || !fs.isDir(path)) {
      term.println("Not a directory: " + path);
      return true;
    }

    findRecursive(path, pattern);
    return true;
  }

  // Supports plain substring matching plus basic glob forms: "*.txt",
  // "foo*", "*foo*", and "*" (match everything).
  bool matchesPattern(const String& name, const String& pattern) {
    if (pattern.length() == 0 || pattern == "*") return true;

    bool leadingStar = pattern.startsWith("*");
    bool trailingStar = pattern.endsWith("*");

    if (leadingStar && trailingStar && pattern.length() >= 2) {
      String inner = pattern.substring(1, pattern.length() - 1);
      return inner.length() == 0 || name.indexOf(inner) >= 0;
    }
    if (leadingStar) {
      return name.endsWith(pattern.substring(1));
    }
    if (trailingStar) {
      return name.startsWith(pattern.substring(0, pattern.length() - 1));
    }
    return name.indexOf(pattern) >= 0;
  }

  void findRecursive(const String& dir, const String& pattern) {
    for (const auto& name : fs.listDir(dir)) {
      String full = dir;
      if (!full.endsWith("/")) full += "/";
      full += name;

      if (matchesPattern(name, pattern)) {
        out(full);
      }

      if (fs.isDir(full)) findRecursive(full, pattern);
    }
  }

  bool cmdWc(const std::vector<String>& args) {
    String content;

    if (args.size() >= 2) {
      String path = fs.resolvePath(args[1]);
      if (!fs.exists(path)) {
        term.println("File not found: " + path);
        return true;
      }
      content = fs.readFile(path);
    } else if (stdinBuffer) {
      content = *stdinBuffer;
    } else {
      term.println("Usage: wc [file]");
      return true;
    }

    int lines = 0, words = 0, bytes = content.length();
    bool inWord = false;

    for (int i = 0; i < (int)content.length(); i++) {
      char c = content[i];
      if (c == '\n') lines++;
      if (isspace((unsigned char)c)) {
        inWord = false;
      } else if (!inWord) {
        words++;
        inWord = true;
      }
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "%7d %7d %7d", lines, words, bytes);
    out(buf);
    return true;
  }

  bool cmdDu(const std::vector<String>& args) {
    String path = args.size() >= 2 ? fs.resolvePath(args[1]) : fs.getCurrentPath();

    if (!fs.exists(path)) {
      term.println("Not found: " + path);
      return true;
    }

    uint32_t total = duRecursive(path);
    out(formatSize(total) + "\t" + path);
    return true;
  }

  uint32_t duRecursive(const String& path) {
    if (!fs.isDir(path)) {
      File f = fs.openRaw(path);
      uint32_t size = f ? f.size() : 0;
      if (f) f.close();
      return size;
    }

    uint32_t total = 0;
    for (const auto& name : fs.listDir(path)) {
      String full = path;
      if (!full.endsWith("/")) full += "/";
      full += name;
      total += duRecursive(full);
    }
    return total;
  }

  String formatSize(uint32_t bytes) {
    if (bytes < 1024) return String(bytes) + "B";
    if (bytes < 1024UL * 1024UL) return String(bytes / 1024) + "K";
    return String(bytes / (1024UL * 1024UL)) + "M";
  }

  bool cmdReboot(const std::vector<String>& args) {
    term.println("Rebooting...");
    delay(500);
    ESP.restart();
    return true;
  }
};
