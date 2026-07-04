#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <vector>
#include <string>
#include <cstring>

class FileSystem {
public:
  FileSystem() : currentPath("/"), sdMounted(false) {}

  bool init() {
    if (!LittleFS.begin(true)) {
      Serial.println("LittleFS Mount Failed");
      return false;
    }

    // SD card is optional - system still boots without one inserted
    sdMounted = SD_MMC.begin();
    if (sdMounted) {
      Serial.println("SD card mounted at /sd");
    } else {
      Serial.println("SD card not found (skipping)");
    }

    return true;
  }

  bool sdAvailable() const {
    return sdMounted;
  }

  // Opens a raw streaming file handle (routed to LittleFS or SD the same
  // way every other method is). Unlike readFile()/writeFile(), this
  // doesn't load the whole file into a String - needed for anything too
  // large to fit comfortably in RAM, like a firmware update image.
  File openRaw(const String& path, const char* mode = FILE_READ) {
    return backend(path).open(stripSd(path), mode);
  }

  bool exists(const String& path) {
    if (isSdRoot(path)) return sdMounted;
    return backend(path).exists(stripSd(path).c_str());
  }

  bool exists(const char* path) {
    return exists(String(path));
  }

  bool isDir(const String& path) {
    if (isSdRoot(path)) return sdMounted;

    File f = backend(path).open(stripSd(path));
    if (!f) return false;
    bool isDirectory = f.isDirectory();
    f.close();
    return isDirectory;
  }

  bool isDir(const char* path) {
    return isDir(String(path));
  }

  String readFile(const String& path) {
    if (!exists(path)) return "";

    File f = backend(path).open(stripSd(path), "r");
    if (!f) return "";

    String content = "";
    while (f.available()) {
      content += (char)f.read();
    }
    f.close();
    return content;
  }

  String readFile(const char* path) {
    return readFile(String(path));
  }

  bool writeFile(const String& path, const String& content) {
    if (isSdPath(path) && !sdMounted) return false;

    File f = backend(path).open(stripSd(path), "w");
    if (!f) return false;

    f.print(content);
    f.close();
    return true;
  }

  bool writeFile(const char* path, const String& content) {
    return writeFile(String(path), content);
  }

  bool deleteFile(const String& path) {
    if (isSdPath(path) && !sdMounted) return false;
    return backend(path).remove(stripSd(path));
  }

  bool deleteFile(const char* path) {
    return deleteFile(String(path));
  }

  bool createDir(const String& path) {
    if (isSdPath(path) && !sdMounted) return false;
    return backend(path).mkdir(stripSd(path));
  }

  bool removeDir(const String& path) {
    if (isSdPath(path) && !sdMounted) return false;
    return backend(path).rmdir(stripSd(path));
  }

  bool removeDir(const char* path) {
    return removeDir(String(path));
  }

  bool createDir(const char* path) {
    return createDir(String(path));
  }

  std::vector<String> listDir(const String& path) {
    std::vector<String> result;

    // Root shows LittleFS contents plus a synthetic "sd" mount point
    if (path == "/") {
      File root = LittleFS.open("/");
      if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
          result.push_back(file.name());
          file = root.openNextFile();
        }
        root.close();
      }
      if (sdMounted) {
        result.push_back("sd");
      }
      return result;
    }

    if (isSdRoot(path) && !sdMounted) {
      return result;
    }

    File root = backend(path).open(stripSd(path));
    if (!root || !root.isDirectory()) {
      return result;
    }

    File file = root.openNextFile();
    while (file) {
      result.push_back(file.name());
      file = root.openNextFile();
    }

    root.close();
    return result;
  }

  std::vector<String> listDir(const char* path) {
    return listDir(String(path));
  }

  String getCurrentPath() const {
    return currentPath;
  }

  void setCurrentPath(const String& path) {
    if (exists(path) && isDir(path)) {
      currentPath = path;
      if (!currentPath.endsWith("/")) {
        currentPath += "/";
      }
    }
  }

  String resolvePath(const String& relativePath) {
    if (relativePath == "/") return "/";
    if (relativePath.startsWith("/")) return relativePath;

    String full = currentPath;
    if (!full.endsWith("/")) full += "/";
    full += relativePath;

    return full;
  }

private:
  String currentPath;
  bool sdMounted;

  bool isSdPath(const String& path) const {
    return path == "/sd" || path.startsWith("/sd/");
  }

  bool isSdRoot(const String& path) const {
    return path == "/sd" || path == "/sd/";
  }

  // Routes to the SD or internal filesystem based on the /sd prefix
  fs::FS& backend(const String& path) {
    if (isSdPath(path)) return SD_MMC;
    return LittleFS;
  }

  // Strips the /sd mount prefix so the backend sees its own root-relative
  // path, and normalizes away any trailing slash (except bare "/"). The
  // shell's current-directory string always carries a trailing slash by
  // design (see Shell::setCurrentPath), but SD_MMC's VFS layer doesn't
  // reliably open a directory for listing with one - open("/etc/") could
  // fail to enumerate even though exists()/isDir() tolerate it fine,
  // which is exactly why 'cd'/'mkdir' worked but a bare 'ls' didn't.
  String stripSd(const String& path) {
    String result = path;
    if (isSdPath(path)) {
      result = path.substring(3);
      if (result.length() == 0) result = "/";
    }
    if (result.length() > 1 && result.endsWith("/")) {
      result.remove(result.length() - 1);
    }
    return result;
  }
};
