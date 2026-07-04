# ESP-Nix v0.7.3

A declarative, Unix-like shell operating system for the ESP32 — built from scratch on FreeRTOS, running entirely off an I2C LCD and a serial console (with optional SD card, PS/2 keyboard, and WiFi).

> **AI Disclaimer:** This project was developed with the assistance of Claude AI. I believe AI-written code should be open source to benefit everyone and maintain transparency.

## Shell
- Full command set: `ls` (with `-l`), `cd`, `cat`, `cp`/`mv` (recursive, glob-aware, directory-destination-aware), `rm -r`, `grep`, `head`, `tail`, `find`, `wc`, `du`, `mkdir`, `touch`, `echo`, and more
- Pipes, `&&`/`||`/`;` operators, `>`/`>>`/`<` redirection, `$VAR` expansion, `VAR=value` assignment
- Command history (arrow keys) and tab completion, persisted to `/data/history.txt` across reboots (30-entry cap)
- `test`/`[` builtin for basic conditionals
- A handful of `/proc` virtual files (`meminfo`, `uptime`, `cpuinfo`, `version`)

## Filesystem & Storage
- LittleFS internal storage, with an SD card (SD_MMC) optionally mounted at `/sd`
- Real `.zip`, `.tar.gz`/`.tgz`, `.gz`, and `.tar` archive support — `extract`/`compress`

## Declarative & Scriptable
- `/etc/settings/esp-nix.conf` — boot-time config (variables, WiFi, timezone, date/time format), re-appliable with `nixos-rebuild`
- `/boot/*.sh` — startup scripts, run in order at every boot
- `/system/*.sh` — any script becomes a first-class command, runnable by name from anywhere

## Connectivity
- `web` — WiFi file server (drag-and-drop upload/download) **and** a browser-based terminal, reachable from any phone or computer, running through the exact same shell processing as Serial/PS2
- `web -join`/`-list` — scan and join real WPA2 networks
- `ntp`/`settz` — WiFi time sync with named-timezone lookup and automatic US DST handling
- `wifi connect`/`disconnect`/`status`, `ip`, `ping` — a persistent WiFi connection independent of `web`'s connect-then-disconnect lifecycle, so the shell can check status, IP, and host reachability at will
- `curl [-X METHOD] [-d data] <url>` — basic HTTP/HTTPS client (HTTPS via `WiFiClientSecure::setInsecure()`, no certificate validation)
- OTA updates via `update`, with pre-flight validation (size and firmware-header checks) before touching flash

**Flash usage:** HTTPS support (`WiFiClientSecure`'s TLS stack) added ~140KB to the firmware image — this build sits at 94.7% of the 1.3MB OTA partition (~70KB headroom left). Worth checking before adding anything else substantial.

**Tested:** `wifi connect` → `curl`/`ping` → `wifi disconnect` cycle confirmed via `free` — heap climbs while connected (WiFi + TLS runtime buffers) and drops back down after `wifi disconnect`, with no growth across repeated cycles.

## Editor & Extras
- Full line editor (`edit`) with mid-line cursor movement, line insert/delete, and Up/Down navigation between lines
- `nixfetch` — a neofetch-style system summary with a customizable ASCII logo
- `loop <count|inf> [-i seconds] <command...>` — repeats a command, since the script engine has no real loop construct; any keypress stops it early

## Hardware
- ESP32 (required)
- I2C LCD1602 and SD card (via SD_MMC) — both recommended, not required. The shell works entirely over Serial/browser terminal without the LCD, and without SD the system just runs off internal LittleFS — you'd just lose `web`, `extract`/`compress` of large archives, and general extra storage.
- PS/2 keyboard — optional, auto-detected at boot

**Recommended kit:** the [SunFounder ESP32 Ultimate Starter Kit (with camera extension board and battery)](https://www.sunfounder.com/products/sunfounder-esp32-ultimate-starter-kit-with-esp32-camera-extension-board-battery) covers the ESP32 board, LCD, and SD adapter this project was built and tested against, in one bundle.

## Credits
- The `web` file server started from [GPT_ESP32-File_network](https://github.com/RetroGigabyte/GPT_ESP32-File_network), extended with the browser terminal, WiFi network joining, and SD-hosted page templates in this project.
