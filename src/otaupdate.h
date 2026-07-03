#pragma once

#include <Arduino.h>
#include <Update.h>
#include "filesystem.h"
#include "terminal.h"

// Flashes new firmware from a .esp_update file (a plain firmware.bin,
// just renamed - no zip/archive parsing involved). Streams the file in
// small chunks rather than loading it into RAM, since firmware images
// can be several hundred KB on a chip with 320KB total.
class OtaUpdater {
public:
  // Looks for a *.esp_update file on /sd first, then / - flashes the
  // first one found. Returns false (with a message already printed) if
  // none was found or the flash failed.
  bool runAuto(FileSystem& fs, Terminal& term) {
    String path = findUpdateFile(fs, "/sd");
    if (path.length() == 0) path = findUpdateFile(fs, "/");

    if (path.length() == 0) {
      term.println("No .esp_update file found on /sd or /");
      return false;
    }

    return flashFrom(fs, term, path);
  }

  bool runPath(FileSystem& fs, Terminal& term, const String& path) {
    if (!fs.exists(path)) {
      term.println("File not found: " + path);
      return false;
    }
    return flashFrom(fs, term, path);
  }

private:
  String findUpdateFile(FileSystem& fs, const String& dir) {
    if (!fs.exists(dir) || !fs.isDir(dir)) return "";

    for (const auto& name : fs.listDir(dir)) {
      String lower = name;
      lower.toLowerCase();
      if (lower.endsWith(".esp_update")) {
        String full = dir;
        if (!full.endsWith("/")) full += "/";
        return full + name;
      }
    }
    return "";
  }

  // ESP32 firmware images always start with this magic byte
  static const uint8_t ESP_IMAGE_MAGIC = 0xE9;

  // Real ESP32 app images are always well over this size; anything
  // smaller is almost certainly a truncated download or the wrong file
  static const size_t MIN_FIRMWARE_SIZE = 64 * 1024;

  bool flashFrom(FileSystem& fs, Terminal& term, const String& path) {
    File file = fs.openRaw(path);
    if (!file) {
      term.println("Failed to open " + path);
      return false;
    }

    size_t size = file.size();
    if (size == 0) {
      term.println("Update file is empty: " + path);
      file.close();
      return false;
    }

    if (size < MIN_FIRMWARE_SIZE) {
      term.println("Refusing to flash " + path + ": file is too small to be");
      term.println("real firmware (" + String(size) + " bytes) - wrong file?");
      file.close();
      return false;
    }

    uint8_t magic = 0;
    if (file.read(&magic, 1) != 1 || magic != ESP_IMAGE_MAGIC) {
      term.println("Refusing to flash " + path + ": missing ESP32 firmware");
      term.println("header (0xE9 magic byte) - not a valid firmware image");
      file.close();
      return false;
    }
    file.seek(0);

    term.println("Flashing " + path + " (" + String(size) + " bytes)...");

    if (!Update.begin(size)) {
      term.println("Not enough OTA space: " + String(Update.errorString()));
      file.close();
      return false;
    }

    uint8_t buf[512];
    size_t written = 0;
    unsigned long lastPrint = 0;

    while (written < size) {
      int chunk = file.read(buf, sizeof(buf));
      if (chunk <= 0) break;

      if (Update.write(buf, (size_t)chunk) != (size_t)chunk) {
        term.println("Write failed: " + String(Update.errorString()));
        file.close();
        Update.abort();
        return false;
      }

      written += chunk;

      if (millis() - lastPrint > 1000) {
        lastPrint = millis();
        term.println(String((written * 100) / size) + "%");
      }
    }

    file.close();

    if (written != size) {
      term.println("Update file read incomplete (" + String(written) + "/" + String(size) + " bytes)");
      Update.abort();
      return false;
    }

    if (!Update.end(true) || !Update.isFinished()) {
      term.println("Update failed: " + String(Update.errorString()));
      return false;
    }

    term.println("Update successful. Rebooting...");
    delay(1000);
    ESP.restart();
    return true;  // unreachable
  }
};
