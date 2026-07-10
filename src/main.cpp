#include <Arduino.h>
// LCD only wired up on the classic-ESP32 (WROOM-32E) build - the S3
// board this shared main.cpp also targets has no LCD attached, and
// GPIO22 (the LCD's I2C SCL pin below) isn't even a valid pin on the S3.
// CONFIG_IDF_TARGET_ESP32S3 is defined automatically by the toolchain
// when compiling for the S3, so this stays correct for both build
// targets without needing a separate main.cpp per chip.
#ifndef CONFIG_IDF_TARGET_ESP32S3
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#endif

#include "filesystem.h"
#include "terminal.h"
#include "variables.h"
#include "parser.h"
#include "shell.h"
#include "ps2keyboard.h"
#include "input.h"

// PS/2 keyboard pins - free GPIOs that don't conflict with the LCD (21/22)
// or the SD card's SD_MMC pins (2, 4, 12, 13, 14, 15) - on the classic
// ESP32/WROOM-32E specifically. Also gated out on the S3 build: GPIO26 is
// reserved for the S3's own SPI flash, not a general-purpose pin there.
#ifndef CONFIG_IDF_TARGET_ESP32S3
#define PS2_CLOCK_PIN 26
#define PS2_DATA_PIN 25
#endif

// Overrides the Arduino core's weak default (8192) - the shell's command
// dispatch nests deeply (pipeline -> command -> library calls), and some
// commands construct sizable objects on the stack (e.g. ESP32_FTPClient's
// internal 1.5KB buffer). ftp get crashed with a stack canary overflow at
// the default size.
size_t getArduinoLoopTaskStackSize(void) {
  return 16384;
}

// Global objects
#ifndef CONFIG_IDF_TARGET_ESP32S3
LiquidCrystal_I2C lcd(0x27, 16, 2);
#endif
FileSystem fsys;
#ifndef CONFIG_IDF_TARGET_ESP32S3
Terminal term(&lcd);
#else
Terminal term(nullptr);
#endif
Variables vars;
Shell shell(fsys, term, vars);
#ifndef CONFIG_IDF_TARGET_ESP32S3
PS2Keyboard ps2kb;
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\nESP-Nix Boot Sequence Starting...\n");

  // LCD only initialized on the classic-ESP32 build - see the #ifndef
  // above. Wire.begin(21, 22) is the LCD's I2C pins (WROOM-32E-specific;
  // GPIO22 isn't valid on the S3 at all).
#ifndef CONFIG_IDF_TARGET_ESP32S3
  Wire.begin(21, 22);
#endif
  term.init();
  term.println("ESP-Nix: Initializing");

  delay(500);

  // Initialize LittleFS
  if (!fsys.init()) {
    term.println("ERROR: LittleFS failed");
    while (1) delay(1000);
  }

  term.println("LittleFS: OK");
  delay(500);

  term.println(fsys.sdAvailable() ? "SD Card: OK" : "SD Card: none");
  delay(500);

  // PS/2 keyboard only initialized on the classic-ESP32 build - see the
  // #ifndef above (GPIO26 is reserved for the S3's own SPI flash there).
  // Serial input still works fine without this on the S3.
#ifndef CONFIG_IDF_TARGET_ESP32S3
  ps2kb.begin(PS2_CLOCK_PIN, PS2_DATA_PIN);
  input.attachPS2(&ps2kb);
  term.println("PS/2 Keyboard: Ready");
#endif
  delay(500);

  // Create some demo files if they don't exist
  if (!fsys.exists("/readme.txt")) {
    fsys.writeFile("/readme.txt", "Welcome to ESP-Nix!\nA declarative OS for ESP32.\n");
  }

  if (!fsys.exists("/system")) {
    fsys.createDir("/system");
  }

  if (!fsys.exists("/etc")) {
    fsys.createDir("/etc");
  }

  if (!fsys.exists("/boot")) {
    fsys.createDir("/boot");
  }

  if (!fsys.exists("/etc/settings")) {
    fsys.createDir("/etc/settings");
  }

  if (!fsys.exists("/data")) {
    fsys.createDir("/data");
  }

  // Migrate config from its old location (/etc/esp-nix.conf) for devices
  // upgrading via OTA, so saved WiFi credentials etc. aren't lost.
  if (fsys.exists("/etc/esp-nix.conf") && !fsys.exists("/etc/settings/esp-nix.conf")) {
    fsys.writeFile("/etc/settings/esp-nix.conf", fsys.readFile("/etc/esp-nix.conf"));
    fsys.deleteFile("/etc/esp-nix.conf");
  }

  if (!fsys.exists("/etc/settings/esp-nix.conf")) {
    fsys.writeFile("/etc/settings/esp-nix.conf",
      "# ESP-Nix declarative boot configuration\n"
      "# Runs automatically at every boot, and on demand via 'nixos-rebuild'.\n"
      "# Lines are the same commands you'd type interactively: set\n"
      "# variables with VAR=value, create directories with mkdir, or run\n"
      "# any other shell command.\n"
      "\n"
      "GREETING=Welcome to ESP-Nix\n"
      "mkdir /data\n"
      "\n"
      "# WiFi access point used by the 'web' file server command.\n"
      "# WEB_PASS needs 8+ characters, or leave it empty for an open network.\n"
      "WEB_SSID=ESP-Nix\n"
      "WEB_PASS=esp32nix\n"
      "\n"
      "# Timezone offset from UTC in seconds, used by NTP time sync (ntp\n"
      "# command, and automatically on 'web -join'). Example: -18000 for EST.\n"
      "TZ_OFFSET=0\n"
      "\n"
      "# date command formatting.\n"
      "# DATE_FORMAT: us (7/3/2027) or iso (2027/7/3)\n"
      "# TIME_FORMAT: 24 (15:04:05) or 12 (3:04:05 PM)\n"
      "DATE_FORMAT=us\n"
      "TIME_FORMAT=24\n"
      "\n"
      "# Directories searched (in order) for a matching <name>.sh when a\n"
      "# typed command isn't built in - same idea as a real Unix PATH.\n"
      "# Colon-separated, e.g. /system:/data/scripts\n"
      "PATH=/system\n"
      "\n"
      "# Shown in the shell prompt (user@hostname:/$) and applied as the\n"
      "# actual WiFi hostname on next connect. Change with 'hostname -s NAME'.\n"
      "HOSTNAME=esp-nix\n");
  }

  if (!fsys.exists("/etc/settings/timezones.txt")) {
    fsys.writeFile("/etc/settings/timezones.txt",
      "# ESP-Nix timezone table, used by 'settz <name>'\n"
      "# Add your own zones by editing this file - no reflash needed.\n"
      "# name,standard_offset_seconds,dst_offset_seconds\n"
      "pacific,-28800,-25200\n"
      "mountain,-25200,-21600\n"
      "central,-21600,-18000\n"
      "eastern,-18000,-14400\n"
      "alaska,-32400,-28800\n"
      "hawaii,-36000,-36000\n"
      "utc,0,0\n"
      "gmt,0,0\n");
  }

  if (!fsys.exists("/etc/settings/logo.txt")) {
    fsys.writeFile("/etc/settings/logo.txt",
      "   .--.        \n"
      "  |o_o |       \n"
      "  |:_/ |       \n"
      " //   \\ \\      \n"
      "(|     | )     \n"
      "/'\\_   _/`\\    \n"
      "\\___)=(___/    \n"
      "               \n");
  }

  // System-managed wrapper script, not user data - always kept in sync
  // with the current firmware so args typed after "web" (e.g. -join)
  // reach the real webserver command via $@.
  fsys.writeFile("/system/web.sh", "webserver $@\n");

  // Initialize system variables (may be overridden by /etc/settings/esp-nix.conf below)
  vars.set("USER", "root");
  vars.set("HOME", "/");
  vars.set("SHELL", "/bin/nix");
  // Chip family, not the exact model - same "S3" substring check nixfetch
  // already uses. Read by OtaUpdater to only ever flash matching-chip
  // update files (see otaupdate.h) - not meant to be overridden by
  // esp-nix.conf like the vars above, since it reflects real hardware.
  vars.set("DEVICE_TYPE", String(ESP.getChipModel()).indexOf("S3") >= 0 ? "s3" : "classic");

  // Link variables to commands
  shell.setVariables(&vars);

  // Apply declarative boot config (sets vars, creates dirs, runs commands)
  shell.applyBootConfig();

  // Sync the clock over WiFi, if a network was already saved (see 'web -join')
  shell.syncBootTime();

  // Run any startup scripts in /boot, in order (like /etc/init.d)
  shell.runBootScripts();

  // Run any .elf/.o driver programs in /sd/drivers, in order - no mkali
  // alias needed for these, unlike /system or /boot
  shell.runDriverPrograms();

  // Restore command history from /data/history.txt, if any exists
  shell.loadHistory();

  delay(1000);
  term.clear();

  // Initialize and run shell
  shell.init();
}

void loop() {
  shell.run();
}
