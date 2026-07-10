#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <functional>
#include "terminal.h"
#include "input.h"
#include "filesystem.h"
#include "archive.h"

// WiFi file server + browser terminal, reachable from a phone or computer.
// Started on demand by the `webserver` command (wrapped by /system/web.sh
// so it's runnable as just "web"). Blocks the shell while running - press
// any key at the console to stop and return to the prompt.
//
// The command-executor callback is type-erased (std::function) rather
// than a direct dependency on the Commands class, so this header doesn't
// need to include commands.h (which includes this file) - avoids a
// circular include.
//
// Storage is accessed through FileSystem (fs), not SD_MMC directly -
// previously this went straight to SD_MMC everywhere, which meant a board
// with no local SD (e.g. a multi-chip-build S3 acting as MAIN, which per
// the architecture has no SD of its own - only WROOM-32E does) couldn't
// run the web server at all. FileSystem already transparently routes
// between LittleFS and local SD via the "/sd" path prefix, so going
// through it means this server now works on LittleFS alone. A future
// remote-SD relay to WROOM-32E (once that link actually exists in
// hardware/code) would plug in as another FileSystem backend behind that
// same routing, not as a change to this file - see the note in
// filesystem.h's backend() method.
//
// Page templates (the file manager and terminal UI) live as real files
// under /www on whichever storage FileSystem resolves to (SD if mounted,
// LittleFS otherwise), not as C++ string literals in firmware - keeps
// them editable without reflashing, and keeps flash usage down as more
// pages get added over time. Defaults are written on first use if missing.
class WebFileServer {
public:
  using CommandExecutor = std::function<String(const String&)>;
  using CwdGetter = std::function<String()>;

  // Starts its own WiFi access point (works anywhere, no router needed)
  void run(FileSystem& fs, Terminal& term, const String& ssid, const String& password,
           CommandExecutor executeCmd, CwdGetter getCwd, const String& nixfetchOutput,
           const String& hostname) {
    ensureDefaultPages(fs);

    WebServer server(80);
    setupRoutes(fs, term, server, executeCmd, getCwd, nixfetchOutput);

    WiFi.mode(WIFI_AP);
    WiFi.softAPsetHostname(hostname.c_str());
    bool apOk = (password.length() >= 8)
      ? WiFi.softAP(ssid.c_str(), password.c_str())
      : WiFi.softAP(ssid.c_str());

    if (!apOk) {
      term.println("Failed to start WiFi access point");
      return;
    }

    server.begin();

    term.println("Web server running (files + terminal)");
    term.println("WiFi: " + ssid + (password.length() >= 8 ? " / " + password : " (open)"));
    term.println("Browse to: http://" + WiFi.softAPIP().toString());
    term.println("Press any key to stop.");

    serveLoop(server);

    server.close();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    term.println("Web server stopped.");
  }

  // Joins an existing WPA2 network instead of hosting its own AP, so the
  // server is reachable on the same network as your phone/computer.
  void runSTA(FileSystem& fs, Terminal& term, const String& ssid, const String& password,
              CommandExecutor executeCmd, CwdGetter getCwd, const String& nixfetchOutput,
              const String& hostname) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostname.c_str());
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
      return;
    }

    ensureDefaultPages(fs);

    WebServer server(80);
    setupRoutes(fs, term, server, executeCmd, getCwd, nixfetchOutput);
    server.begin();

    term.println("Web server running (files + terminal)");
    term.println("Joined WiFi: " + ssid);
    term.println("Browse to: http://" + WiFi.localIP().toString());
    term.println("Press any key to stop.");

    serveLoop(server);

    server.close();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    term.println("Web server stopped.");
  }

private:
  static void serveLoop(WebServer& server) {
    while (true) {
      server.handleClient();
      if (input.available()) {
        input.read();
        break;
      }
      delay(2);
    }
  }

  static String urlDecode(String in) {
    in.replace("+", " ");
    int index = 0;
    while ((index = in.indexOf('%', index)) != -1) {
      if (index + 2 < (int)in.length()) {
        String hex = in.substring(index + 1, index + 3);
        char c = (char)strtol(hex.c_str(), nullptr, 16);
        in.replace("%" + hex, String(c));
      }
      index++;
    }
    return in;
  }

  static String sanitizeFilename(String fname) {
    fname = urlDecode(fname);
    if (!fname.startsWith("/")) fname = "/" + fname;
    return fname;
  }

  static String htmlEscape(const String& in) {
    String out = in;
    out.replace("&", "&amp;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    return out;
  }

  static String jsonEscape(const String& in) {
    String out = "";
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
      char c = in[i];
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if ((uint8_t)c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
          } else {
            out += c;
          }
      }
    }
    return out;
  }

  // Reads a page template from storage (SD if mounted, LittleFS
  // otherwise - see FileSystem), falling back to a built-in default if
  // the file isn't there for some reason.
  static String loadPage(FileSystem& fs, const char* path, const String& fallback) {
    if (fs.exists(path)) {
      String content = fs.readFile(path);
      if (content.length() > 0) return content;
    }
    return fallback;
  }

  static void writeIfMissing(FileSystem& fs, const char* path, const String& content) {
    if (fs.exists(path)) return;
    if (!fs.exists("/www")) fs.createDir("/www");
    fs.writeFile(path, content);
  }

  static void ensureDefaultPages(FileSystem& fs) {
    writeIfMissing(fs, "/www/index.html", defaultIndexPage());
    writeIfMissing(fs, "/www/shell.html", defaultShellPage());
  }

  // Directories get a "(folder, zipped on download)" label and route
  // through the same zip-on-the-fly logic as the /download handler.
  static String listFilesHTML(FileSystem& fs) {
    String label = fs.sdAvailable() ? "SD Card" : "Storage";
    String html = "<h2>Files on " + label + "</h2><ul>";

    std::vector<String> names = fs.listDir("/");
    if (names.empty()) return html + "<li>(empty)</li></ul>";

    for (const String& fname : names) {
      String fullPath = "/" + fname;
      bool isDir = fs.isDir(fullPath);
      String sizeLabel;
      if (isDir) {
        sizeLabel = " (folder, zipped on download) ";
      } else {
        // .size() on the raw handle, not readFile() - a multi-MB firmware
        // image shouldn't get fully loaded into RAM just to print its size.
        File f = fs.openRaw(fullPath);
        size_t bytes = f ? f.size() : 0;
        if (f) f.close();
        sizeLabel = " (" + String(bytes) + " bytes) ";
      }
      html += "<li>" + fname + sizeLabel;
      html += "<a href='/download?file=" + fname + "'>Download</a> ";
      html += "<a href='/delete?file=" + fname + "' onclick=\"return confirm('Delete " + fname + "?');\">Delete</a>";
      html += "</li>";
    }
    html += "</ul>";
    return html;
  }

  static String defaultIndexPage() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>ESP-Nix SD File Server</title><style>";
    html += "body{background:#121212;color:#e0e0e0;font-family:sans-serif;max-width:600px;margin:2em auto;padding:0 1em}";
    html += "h1,h2{color:#fff} a{color:#80cbc4;text-decoration:none} a:hover{text-decoration:underline}";
    html += "ul{list-style:none;padding-left:0} li{margin-bottom:8px}";
    html += "input[type=submit]{background:#333;color:#fff;border:none;padding:6px 12px;cursor:pointer}";
    html += "input[type=submit]:hover{background:#555}";
    html += "pre{background:#000;color:#0f0;padding:1em;border-radius:4px;overflow-x:auto;font-size:13px}";
    html += "</style></head><body>";
    html += "<h1>ESP-Nix SD File Server</h1>";
    html += "<p><a href='/shell'>&#9654; Open Terminal</a></p>";
    html += "<pre>{{NIXFETCH}}</pre>";
    html += "{{FILES}}";
    html += "<h2>Upload File</h2>";
    html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
    html += "<input type='file' name='upload'><br><br>";
    html += "<input type='submit' value='Upload'></form>";
    html += "</body></html>";
    return html;
  }

  static String defaultShellPage() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>ESP-Nix Terminal</title><style>";
    html += "body{background:#121212;color:#e0e0e0;font-family:sans-serif;max-width:700px;margin:1em auto;padding:0 1em}";
    html += "h1{color:#fff} a{color:#80cbc4;text-decoration:none} a:hover{text-decoration:underline}";
    html += "#out{background:#000;color:#0f0;padding:1em;height:60vh;overflow-y:auto;";
    html += "white-space:pre-wrap;word-break:break-all;border-radius:4px;font-family:monospace;font-size:14px}";
    html += "#cmdline{width:100%;box-sizing:border-box;background:#1e1e1e;color:#fff;border:1px solid #444;";
    html += "padding:10px;font-family:monospace;font-size:14px;margin-top:8px;border-radius:4px}";
    html += "</style></head><body>";
    html += "<h1>ESP-Nix Terminal</h1>";
    html += "<p><a href='/'>&larr; File manager</a></p>";
    html += "<div id='out'></div>";
    html += "<input id='cmdline' autocapitalize='off' autocomplete='off' autofocus placeholder='type a command and press Enter'>";
    html += "<script>";
    html += "const out=document.getElementById('out');const cmdline=document.getElementById('cmdline');";
    html += "let cwd='/';";
    html += "function prompt(){return 'nix:'+cwd+'$ ';}";
    html += "function print(t){out.textContent+=t+'\\n';out.scrollTop=out.scrollHeight;}";
    html += "async function refreshCwd(){";
    html += "try{const res=await fetch('/pwd');cwd=await res.text();}catch(e){}";
    html += "}";
    html += "cmdline.addEventListener('keydown',async(e)=>{";
    html += "if(e.key!=='Enter')return;";
    html += "const cmd=cmdline.value;cmdline.value='';";
    html += "print(prompt()+cmd);";
    html += "if(!cmd.trim())return;";
    html += "try{";
    html += "const res=await fetch('/run',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'cmd='+encodeURIComponent(cmd)});";
    html += "const data=await res.json();";
    html += "cwd=data.cwd;";
    html += "if(data.output.length)print(data.output);";
    html += "}catch(err){print('[error] '+err);}";
    html += "});";
    html += "refreshCwd();";
    html += "print('Connected - same shell as Serial/PS2 (pipes, vars, redirection all work).');";
    html += "print('Note: full-screen commands like edit still need Serial/PS2 (no live keystrokes here).');";
    html += "</script></body></html>";
    return html;
  }

  static void setupRoutes(FileSystem& fs, Terminal& term, WebServer& server, CommandExecutor& executeCmd,
                           CwdGetter& getCwd, const String& nixfetchOutput) {
    server.on("/", HTTP_GET, [&server, &fs, &nixfetchOutput]() {
      String page = loadPage(fs, "/www/index.html", defaultIndexPage());
      page.replace("{{FILES}}", listFilesHTML(fs));
      page.replace("{{NIXFETCH}}", htmlEscape(nixfetchOutput));
      server.send(200, "text/html", page);
    });

    server.on("/shell", HTTP_GET, [&server, &fs]() {
      server.send(200, "text/html", loadPage(fs, "/www/shell.html", defaultShellPage()));
    });

    server.on("/pwd", HTTP_GET, [&server, &getCwd]() {
      server.send(200, "text/plain", getCwd());
    });

    server.on("/run", HTTP_POST, [&server, &executeCmd, &getCwd]() {
      if (!server.hasArg("cmd")) { server.send(400, "text/plain", "Missing cmd parameter"); return; }
      String output = executeCmd(server.arg("cmd"));
      if (output.endsWith("\n")) output.remove(output.length() - 1);
      String cwd = getCwd();
      String json = "{\"cwd\":\"" + jsonEscape(cwd) + "\",\"output\":\"" + jsonEscape(output) + "\"}";
      server.send(200, "application/json", json);
    });

    server.on("/download", HTTP_GET, [&server, &fs, &term]() {
      if (!server.hasArg("file")) { server.send(400, "text/plain", "Missing file parameter"); return; }
      String filename = sanitizeFilename(server.arg("file"));

      // Directories get zipped on the fly, then streamed and cleaned up -
      // there's no way to "download a folder" directly over HTTP.
      if (fs.exists(filename) && fs.isDir(filename)) {
        String tmpZip = "/_download_tmp.zip";
        Archiver archiver;
        if (!archiver.compress(fs, term, filename, tmpZip)) {
          server.send(500, "text/plain", "Failed to zip: " + filename);
          return;
        }

        File zipFile = fs.openRaw(tmpZip, FILE_READ);
        if (!zipFile) { server.send(500, "text/plain", "Failed to open zip"); return; }

        int lastSlash = filename.lastIndexOf('/');
        String shortName = filename.substring(lastSlash + 1);
        server.sendHeader("Content-Disposition", "attachment; filename=\"" + shortName + ".zip\"");
        server.streamFile(zipFile, "application/zip");
        zipFile.close();
        fs.deleteFile(tmpZip);
        return;
      }

      File file = fs.openRaw(filename, FILE_READ);
      if (!file) { server.send(404, "text/plain", "File not found: " + filename); return; }

      int lastSlash = filename.lastIndexOf('/');
      String shortName = filename.substring(lastSlash + 1);
      server.sendHeader("Content-Disposition", "attachment; filename=\"" + shortName + "\"");
      server.streamFile(file, "application/octet-stream");
      file.close();
    });

    server.on("/delete", HTTP_GET, [&server, &fs]() {
      if (!server.hasArg("file")) { server.send(400, "text/plain", "Missing file parameter"); return; }
      String filename = sanitizeFilename(server.arg("file"));
      if (fs.exists(filename)) fs.deleteFile(filename);
      server.sendHeader("Location", "/");
      server.send(303);
    });

    static File uploadFile;
    static String uploadPath;
    static size_t uploadWritten;
    server.on(
      "/upload", HTTP_POST,
      // Runs after the upload handler below has already fully processed
      // the multipart body, so uploadWritten/upload.totalSize are final
      // here - this is where an incomplete transfer (e.g. WiFi dropout
      // mid-upload of a large firmware image) gets caught and reported,
      // instead of silently leaving a truncated file for a later
      // `update` to fail on with a much more confusing error.
      [&server, &fs]() {
        HTTPUpload& upload = server.upload();
        // Defensive close - normally already closed by UPLOAD_FILE_END/
        // UPLOAD_FILE_ABORTED below, but closing an already-closed File
        // is a harmless no-op, whereas deleteFile() on a still-open FD
        // fails outright (LittleFS: "Has open FD") - this is what a
        // dropped connection that never reaches either status would hit.
        if (uploadFile) uploadFile.close();

        if (uploadWritten != upload.totalSize) {
          fs.deleteFile(uploadPath);
          server.send(500, "text/plain",
            "Upload incomplete: got " + String(uploadWritten) + " of " +
            String(upload.totalSize) + " bytes (connection dropped?) - " +
            "deleted the partial file, please retry.");
          return;
        }
        server.send(200);
      },
      [&server, &fs]() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          uploadPath = sanitizeFilename(upload.filename);
          uploadFile = fs.openRaw(uploadPath, FILE_WRITE);
          uploadWritten = 0;
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (uploadFile) {
            size_t n = uploadFile.write(upload.buf, upload.currentSize);
            uploadWritten += n;
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (uploadFile) uploadFile.close();
          server.sendHeader("Location", "/");
          server.send(303);
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          // Previously unhandled - meant a dropped connection left the
          // file open, which is exactly what caused the LittleFS
          // "Has open FD" unlink failure this fix addresses.
          if (uploadFile) uploadFile.close();
        }
      });
  }
};
