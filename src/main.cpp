#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include "filesystem.h"
#include "terminal.h"
#include "variables.h"
#include "parser.h"
#include "shell.h"
#include "ps2keyboard.h"
#include "input.h"

// PS/2 keyboard pins - free GPIOs that don't conflict with the LCD (21/22)
// or the SD card's SD_MMC pins (2, 4, 12, 13, 14, 15)
#define PS2_CLOCK_PIN 26
#define PS2_DATA_PIN 25

// Global objects
LiquidCrystal_I2C lcd(0x27, 16, 2);
FileSystem fsys;
Terminal term(&lcd);
Variables vars;
Shell shell(fsys, term, vars);
PS2Keyboard ps2kb;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\nESP-Nix Boot Sequence Starting...\n");

  // Initialize LCD
  Wire.begin(21, 22);
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

  // PS/2 keyboard is optional - Serial input still works either way
  ps2kb.begin(PS2_CLOCK_PIN, PS2_DATA_PIN);
  input.attachPS2(&ps2kb);
  term.println("PS/2 Keyboard: Ready");
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
      "TIME_FORMAT=24\n");
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

  // Link variables to commands
  shell.setVariables(&vars);

  // Apply declarative boot config (sets vars, creates dirs, runs commands)
  shell.applyBootConfig();

  // Sync the clock over WiFi, if a network was already saved (see 'web -join')
  shell.syncBootTime();

  // Run any startup scripts in /boot, in order (like /etc/init.d)
  shell.runBootScripts();

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
