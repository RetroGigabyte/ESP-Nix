# ESP-Nix

![ESP-Nix logo](ESP_NIX.png)

A minimal declarative operating system for ESP32 with a Unix-like shell interface.
> **AI Disclaimer:** This project was developed with the assistance of Claude AI. I believe AI-written code should be open source to benefit Everyone and maintain transparency.

## Features

- **Shell interpreter** with common Linux commands
- **LittleFS file system** for persistent storage (1MB)  
- **Serial terminal** access (115200 baud)
- **LCD display** (16x2) for status output
- **Essential commands**: ls, cd, pwd, cat, echo, rm, cp, mv, grep, head, tail, mkdir, touch, clear, uname, whoami, date, df, free, find, wc, du, reboot
- **Recursive file ops**: `rm -r`, `cp -r`, `mv -r` for directories
- **Pipes and redirection**: `cmd1 | cmd2`, `>`, `>>`, and `<`
- **Command history**: recall previous commands with the up/down arrow keys
- **Tab completion**: completes command names and filenames
- **SD card support**: an inserted SD card is mounted at `/sd` alongside the internal LittleFS root
- **PS/2 keyboard support**: type on a real keyboard alongside (or instead of) the Serial monitor
- **Declarative boot config**: `/etc/settings/esp-nix.conf` runs automatically at every boot and can be re-applied on demand with `nixos-rebuild`
- **Boot scripts + auto NTP**: `.sh` files in `/boot` run in order at every startup, after an automatic clock sync (if WiFi is already saved)
- **Configurable date/time**: `DATE_FORMAT` (`us`/`iso`) and `TIME_FORMAT` (`24`/`12`) in `/etc/settings/esp-nix.conf` control how `date` displays
- **System commands**: any `.sh` file in `/system` is runnable from anywhere by name alone (no `./` prefix, no `.sh` extension)
- **WiFi file server + browser terminal**: `web` serves a file manager and a live shell you can type into from a phone or computer — no app needed
- **OTA firmware updates**: `update` flashes a `.esp_update` firmware file from the SD card or LittleFS — push it over `web`, then flash without a USB cable
- **NTP time sync**: `ntp` (or automatically on `web -join`) sets the system clock over WiFi, so `date` is actually correct
- **Cursor-aware editor**: arrow keys move within a line for mid-line edits; `:d<n>`/`:i<n>` delete or insert by line number
- **Archive support**: `extract`/`compress` handle real `.zip` plus `.tar.gz`/`.tgz`/`.gz`/`.tar`
- **`nixfetch`**: a neofetch-style system summary — logo plus live stats (uptime, memory, disk, CPU) side by side, logo customizable via `/etc/settings/logo.txt`
- **`loop`**: repeats a command a fixed number of times or indefinitely, since the script engine has no real loop construct
- **`wifi`/`ip`/`ping`**: a persistent WiFi connection (`wifi connect`) that stays up in the background, plus status/IP lookup and host reachability checks
- **`curl`**: basic HTTP/HTTPS client for scripts and quick API checks

## Hardware

- **ESP32 WROOM32E** (required, no external RAM needed)
- **I2C LCD1602** (16x2 character display) and **SD card module** (SD_MMC/SDIO) — both recommended, not required. The shell works entirely over Serial or the browser terminal without the LCD, and without SD the system just runs off internal LittleFS — you'd lose `web`, `extract`/`compress` of large archives, and general extra storage.
- **PS/2 keyboard** (optional — works alongside Serial/USB input)
- Serial connection via USB

**Recommended kit:** the [SunFounder ESP32 Ultimate Starter Kit (with camera extension board and battery)](https://www.sunfounder.com/products/sunfounder-esp32-ultimate-starter-kit-with-esp32-camera-extension-board-battery) covers the ESP32 board, LCD, and SD adapter this project was built and tested against, in one bundle.

### Hardware Connections

| LCD Pin | ESP32 Pin |
|---------|-----------|
| GND     | GND       |
| VCC     | 5V        |
| SDA     | GPIO 21   |
| SCL     | GPIO 22   |

| SD Pin | ESP32 Pin |
|--------|-----------|
| GND    | GND       |
| VCC    | 5V        |
| CLK    | GPIO 14   |
| CMD    | GPIO 15   |
| D0     | GPIO 2    |
| D1     | GPIO 4    |
| D2     | GPIO 12   |
| D3     | GPIO 13   |

Uses the ESP32's built-in SDIO peripheral (`SD_MMC`), not SPI — no CS pin, fixed GPIOs.

**Important:** GPIO 2 is one of the ESP32's boot-strapping pins. If your SD module pulls it high (common for SD_MMC), flashing new firmware over USB will fail with a "Failed to communicate with the flash chip" error. Disconnect the SD card's D0 wire before running `pio run -t upload`, then reconnect it and reset the board afterward.

| PS/2 Pin | ESP32 Pin |
|----------|-----------|
| GND      | GND       |
| VCC      | 5V        |
| Clock    | GPIO 26   |
| Data     | GPIO 25   |

## Building & Running

```bash
pio run -t upload
pio device monitor
```

## Usage

After booting, you'll see the shell prompt:

```
nix:/$ help
ESP-Nix 0.7.2 - Available commands:
  help        - Show this help
  ls [path]   - List directory
  pwd         - Print working directory
  cd [path]   - Change directory
  cat [file]  - Display file contents
  echo [text] - Print text
  touch [f]   - Create empty file
  rm [f]      - Remove file
  cp [s] [d]  - Copy file
  mv [s] [d]  - Move/rename file
  grep [p] [f]- Search file for pattern
  head [f] [n]- Show first n lines (default 10)
  tail [f] [n]- Show last n lines (default 10)
  mkdir [d]   - Create directory
  clear       - Clear screen
  edit [f]    - Edit file (:!q to save)
  ./[script]  - Execute shell script
  uname [opt] - System information
  whoami      - Current user
  date        - Current date/time
  df          - Disk free space
  free        - Free memory
  exit        - Exit shell
  cmd > file  - Redirect output (overwrite)
  cmd >> file - Redirect output (append)
```

### Example Commands

```bash
nix:/$ uname
ESP-Nix 0.7.2
System: ESP32 WROOM32E
Arch: Xtensa
Kernel: FreeRTOS
Flash: 4MB | RAM: 320KB SRAM

nix:/$ ls
readme.txt
system

nix:/$ pwd
/

nix:/$ cd system
nix:/system$ mkdir data
nix:/system$ touch data/log.txt
nix:/system$ cat ../readme.txt
Welcome to ESP-Nix!
A declarative OS for ESP32.

nix:/system$ df
Filesystem  Size   Used Available Use%
SPIFFS      1MB   100KB   900KB    10%

nix:/system$ free
Total: 320 KB | Used: 150 KB | Free: 170 KB

nix:/$ cp readme.txt backup.txt
nix:/$ mv backup.txt archive.txt
nix:/$ grep Welcome readme.txt
Welcome to ESP-Nix!

nix:/$ head readme.txt 1
Welcome to ESP-Nix!

nix:/$ tail readme.txt 1
A declarative OS for ESP32.

nix:/$ uname > sysinfo.txt
nix:/$ echo "more info" >> sysinfo.txt
nix:/$ cat sysinfo.txt
ESP-Nix 0.7.2
System: ESP32 WROOM32E
Arch: Xtensa
Kernel: FreeRTOS
Flash: 4MB | RAM: 320KB SRAM
more info

nix:/$ cat readme.txt | grep Welcome
Welcome to ESP-Nix!

nix:/$ grep Welcome < readme.txt
Welcome to ESP-Nix!

nix:/$ cat readme.txt | grep declarative > matches.txt
```

### Recursive File Operations

```bash
nix:/$ cp -r /system /data/system-backup
nix:/$ mv -r /data/system-backup /data/system-backup-2
nix:/$ rm -r /data/system-backup-2
```

`rm`, `cp`, and `mv` refuse to touch a directory unless you pass `-r` — same as real Unix tools warning you before deleting a whole tree.

`cp` and `mv` also accept a directory as the destination, dropping the file inside it under its original name — e.g. `mv notes.txt sd` moves it to `/sd/notes.txt`, not onto `/sd` itself.

Both also accept a glob in the source: `mv cool.* sd` moves every file starting with `cool.` into `/sd`. Supported patterns are `*.txt`, `cool.*`, `*cool*`, and `*` — same matching `find` uses.

### find, wc, du

```bash
nix:/$ find / .sh
/system/hello.sh
/system/web.sh

nix:/$ wc readme.txt
      2      10     52

nix:/$ du /system
   234B	/system
```

`find [path] [pattern]` walks the tree recursively and prints any file or directory whose name contains `pattern` (all entries if omitted). `wc` reports lines/words/bytes, and also reads stdin when piped. `du` totals a file or directory's size recursively.

### SD Card

If an SD card is inserted at boot, it's mounted at `/sd` and works with every existing command — `ls`, `cat`, `cp`, `edit`, redirection, all of it:

```bash
nix:/$ ls
readme.txt
system
sd

nix:/$ cd sd
nix:/sd$ touch notes.txt
nix:/sd$ echo "hello from SD" > notes.txt
nix:/sd$ cat notes.txt
hello from SD

nix:/sd$ cp notes.txt /backup.txt
nix:/sd$ cd /
nix:/$ cat backup.txt
hello from SD

nix:/$ df
Filesystem  Size   Used Available Use%
LittleFS    1024KB  4KB  1020KB      0%
SD          7580MB  12MB  7568MB      0%
```

If no card is inserted, `/sd` simply doesn't appear in `ls /` and boot proceeds normally — the SD card is entirely optional.

### Editor: Line Editing

`edit` supports three kinds of fixes without retyping everything:

- **Mid-line**: while typing a line, the left/right arrows move the cursor back into what you've typed so far, so a typo doesn't require erasing to the end and starting over.
- **Up/Down**: jump to an already-entered line to edit it in place — Up moves to the previous line, Down moves forward. Whatever's currently typed is committed before moving, same as pressing Enter. Clearing a line's content entirely (leave it blank, then move off it) deletes that line.
- **Backspace on an empty line**: pressing Backspace with nothing left to erase deletes that line immediately and jumps to the previous one — no need to press Enter on a blank line first.
- **By line number**: `:d<n>` deletes line n, `:i<n>` inserts a new line before line n:

```bash
nix:/$ edit notes.txt
--- Edit Mode ---
Commands: :!q save+quit  :d<n> delete line n  :i<n> insert before n
1: hello world
2: goodbye

3: :d2
Deleted line 2
1: hello world
3: :i1
New line 1: a fresh start
Inserted at line 1
1: a fresh start
2: hello world
3: :!q
Saved.
```

### PS/2 Keyboard

If a PS/2 keyboard is connected, you can type commands directly on it — the shell reads from Serial and the keyboard simultaneously, so either works at any time (handy for testing over USB while still supporting a standalone keyboard). Supports letters, digits, punctuation, space, enter, backspace, tab, and arrow keys (which work with command history just like Serial input does). It's entirely optional — the shell works fine over Serial alone if nothing is connected.

### Declarative Boot Config

`/etc/settings/esp-nix.conf` is created automatically on first boot and runs every time the system starts — it's the "Nix" in ESP-Nix. It uses the same syntax as a shell script: `VAR=value` or `export VAR=value` sets an environment variable, and any other line runs as a normal command (`mkdir`, `touch`, `echo`, etc).

```bash
# /etc/settings/esp-nix.conf
GREETING=Welcome to ESP-Nix
mkdir /data
mkdir /system/scripts

# WiFi access point used by the 'web' file server command.
# WEB_PASS needs 8+ characters, or leave it empty for an open network.
WEB_SSID=ESP-Nix
WEB_PASS=esp32nix

# Timezone offset from UTC in seconds, used by NTP time sync.
# Example: -18000 for EST.
TZ_OFFSET=0

# date command formatting.
# DATE_FORMAT: us (7/3/2027) or iso (2027/7/3)
# TIME_FORMAT: 24 (15:04:05) or 12 (3:04:05 PM)
DATE_FORMAT=us
TIME_FORMAT=24
```

All settings, not just WiFi/timezone, live under `/etc/settings/` now — a dedicated home for configuration separate from `/etc` in general, so it's easy to find and back up as a unit (`compress /etc/settings settings-backup.zip`). Devices upgrading from an older firmware version have their existing `/etc/esp-nix.conf` migrated automatically on first boot after the update — nothing to redo by hand.

```bash
nix:/$ date
7/3/2027 15:04:05

nix:/$ edit /etc/settings/esp-nix.conf
# change DATE_FORMAT=iso and TIME_FORMAT=12
nix:/$ nixos-rebuild
nix:/$ date
2027/7/3 3:04:05 PM
```

Changing `TZ_OFFSET` takes effect on the very next `date` call too, without needing to re-sync — `date` applies the offset directly to the clock's raw UTC value each time it's called, rather than relying on the offset baked in during the last `ntp` sync.

**Setting `TZ_OFFSET` by name instead of computing seconds by hand:**

```bash
nix:/$ settz -list
pacific,-28800,-25200
mountain,-25200,-21600
central,-21600,-18000
eastern,-18000,-14400
alaska,-32400,-28800
hawaii,-36000,-36000
utc,0,0
gmt,0,0

nix:/$ settz pacific
Timezone set to pacific (DST): TZ_OFFSET=-25200
```

`settz <name>` looks up the zone in `/etc/settings/timezones.txt`, works out whether US daylight saving is currently in effect (2nd Sunday of March through 1st Sunday of November), picks the matching offset, and writes it straight into `/etc/settings/esp-nix.conf` — no manual `nixos-rebuild` needed, it also updates the running session immediately.

The lookup table is a plain editable file, not compiled into firmware — add your own zone by appending a `name,standard_offset,dst_offset` line (or an existing zone with `dst_offset` equal to `standard_offset` if it doesn't observe DST, like `hawaii`/`utc`/`gmt` above). The DST math itself does need real conditional logic, though, which is why it's a proper command rather than a `/boot` or `/system` script — the script engine only runs a flat sequence of commands, with no `if`/`case` branching.

Edit it like any other file, then re-apply it without rebooting:

```bash
nix:/$ edit /etc/settings/esp-nix.conf
nix:/$ nixos-rebuild
Applying /etc/settings/esp-nix.conf ...
--- Executing: /etc/settings/esp-nix.conf ---
[6] GREETING=Welcome to ESP-Nix
[7] mkdir /data
[8] mkdir /system/scripts
--- Script complete ---
Rebuild complete.
```

### System Commands

Any `.sh` file placed in `/system` becomes runnable from anywhere on the system, just by typing its name — no `./` prefix, no `.sh` extension, and it works from any current directory:

```bash
nix:/$ edit /system/hello.sh
1: echo Hello from a system command!
2: :!q
Saved.

nix:/$ cd /data
nix:/data$ hello
Hello from a system command!
```

If a name doesn't match a built-in command, the shell checks `/system/<name>.sh` before giving up with "Unknown command".

### Boot Scripts (`/boot`)

Any `.sh` file placed in `/boot` runs automatically at every startup, in alphabetical order — the ESP-Nix equivalent of `/etc/init.d`. Runs after `/etc/settings/esp-nix.conf` and the boot-time NTP sync, so scripts can rely on variables/directories from the config and a correct clock already being in place.

```bash
nix:/$ edit /boot/01-greet.sh
1: echo Booted at $(date)
2: :!q
Saved.

nix:/$ reboot
...
[1] echo Booted at $(date)
Booted at 2026-07-03 15:32:10
```

Number-prefix filenames (`01-`, `02-`, ...) if execution order matters, same convention as `/etc/init.d` or `cron.d` — alphabetical sort means `01-` always runs before `02-`.

### Automatic NTP Sync at Boot

If a WiFi network is already saved (`web -join` sets `WIFI_SSID`/`WIFI_PASS`), the clock syncs automatically on every boot, before `/boot` scripts run — no need to run `ntp` manually after each restart. If no network is saved yet, this step is skipped entirely rather than stalling boot on a connection attempt that has nowhere to connect to.

### nixfetch

A neofetch-style system summary — logo on the left, live stats on the right:

```
nix:/$ nixfetch
   .--.          root@esp-nix
  |o_o |         ------------
  |:_/ |         OS: ESP-Nix 0.7.2
 //   \ \        Host: ESP32 WROOM32E
(|     | )       Kernel: FreeRTOS
/'\_   _/`\      Uptime: 2m
\___)=(___/      Shell: /bin/nix
                 CPU: Xtensa LX6 @ 240MHz (2 cores)
                 Memory: 73KB / 302KB
                 Disk (/): 48KB / 1408KB
                 Disk (/sd): 1MB / 29709MB
```

The logo isn't compiled into firmware — it's read live from `/etc/settings/logo.txt` (auto-created with a small default penguin on first boot), so replacing it is just `edit /etc/settings/logo.txt`, no rebuild required.

### loop

Repeats a command, since the script engine has no real loop construct (no `if`/`for` — just a flat sequence of commands):

```bash
nix:/$ loop 3 echo hi
hi
hi
hi

nix:/$ loop inf -i 5 date
7/3/2027 3:04:05 PM
7/3/2027 3:04:10 PM
7/3/2027 3:04:15 PM
Loop stopped.
```

`loop <count|inf> [-i seconds] <command...>` — run a fixed number of times, or `inf` until stopped. `-i seconds` sets the delay between runs (default 1 second). Any keypress stops it immediately, even mid-wait.

### wifi, ip, ping

Every other WiFi operation (`web`, `web -join`, `ntp`) connects, does one thing, and explicitly disconnects and powers the radio off again — there's normally no window where WiFi is up and the shell is free to run something else. `wifi connect` is different: it joins a network and leaves it connected in the background, so `ip`/`ping`/`wifi status` have something to report.

```bash
nix:/$ wifi connect
Joining MyHomeNetwork ...
Joined MyHomeNetwork - IP: 192.168.1.42
WiFi stays connected in the background - use 'wifi disconnect' to stop.

nix:/$ ip
192.168.1.42

nix:/$ wifi status
SSID: MyHomeNetwork
IP: 192.168.1.42
RSSI: -52dBm
MAC: 24:6F:28:AA:BB:CC

nix:/$ ping 8.8.8.8
Pinging 8.8.8.8 ...
Reply from 8.8.8.8: avg 23.4ms

nix:/$ wifi disconnect
WiFi disconnected.
```

`wifi connect` with no arguments reuses the saved network from `web -join` (`WIFI_SSID`/`WIFI_PASS`); `wifi connect <ssid> [password]` joins a specific network directly without saving it. Starting `web` or `web -join` while a persistent connection is active will reconfigure the radio for that operation instead — last WiFi command wins.

### curl

A basic HTTP client, using the ESP32 Arduino core's built-in `HTTPClient`/`WiFiClientSecure` (no extra library needed):

```bash
nix:/$ wifi connect
nix:/$ curl http://api.example.com/status
{"status":"ok"}

nix:/$ curl -X POST -d "name=test" https://api.example.com/echo
{"received":{"name":"test"}}
```

`curl [-X METHOD] [-d data] <url>` — defaults to `GET`; `-d` implies `POST` if no `-X` is given, matching real `curl`'s behavior. Needs `wifi connect` run first (or an active `web`/`ntp` session).

**HTTPS doesn't validate certificates** (`WiFiClientSecure::setInsecure()`) — there's no certificate trust store on this device, so any HTTPS server is accepted without verifying its identity. Fine for hobby use and APIs you already trust; don't rely on it for anything security-sensitive. This also costs real flash: pulling in the TLS stack for HTTPS support added roughly 140KB to the firmware image (see version history if you need to check current headroom).

### WiFi File Server + Browser Terminal

Requires an SD card. Start it with `webserver` or the shorter `web` (a `/system/web.sh` wrapper is created automatically):

```bash
nix:/$ web
Web server running (files + terminal)
WiFi: ESP-Nix / esp32nix
Browse to: http://192.168.4.1
Press any key to stop.
```

The ESP32 starts its own WiFi access point (SSID/password from `WEB_SSID`/`WEB_PASS` in `/etc/settings/esp-nix.conf`, edit and run `nixos-rebuild` to change them). Connect your phone or computer to that network, then open the printed address in a browser to drag-and-drop upload files, download, or delete them — straight to/from `/sd`.

**A real terminal, from any browser.** Click "Open Terminal" (or go to `/shell`) for a page where you can type shell commands and see their output — works from a phone, no app needed. It runs through the exact same processing as the Serial/PS2 prompt (`$VAR` expansion, `VAR=value` assignment, pipes, `&&`/`||`/`;`, `>`/`>>` redirection all work), and the prompt shown (`nix:/sd$`) tracks the current directory just like the console does, updating after `cd`.

The one real difference: each command is a single request/response (type it, press Enter, see the output appended below) rather than a live streaming session — there's no continuous keystroke channel over plain HTTP. That means anything that needs to read further keystrokes after you press Enter won't work here: the full-screen `edit` command, or a `web -join -list` prompt waiting for a network number. Use Serial or PS2 for those; everything else behaves identically.

**Pages live on the SD card, not in firmware.** `/www/index.html` and `/www/shell.html` are created automatically the first time `web` runs, and read fresh on every request — edit them directly (`edit /sd/www/shell.html`) to reskin either page without recompiling, and future pages/programs can be added the same way instead of growing the firmware image.

Pressing any key at the ESP-Nix console (Serial or PS/2) stops the server and returns to the shell — the WiFi radio turns off afterward, so it doesn't stay on unless you're actively using it.

**Joining your home WiFi instead of the ESP's own hotspot:**

```bash
nix:/$ web -join -list
Scanning for WiFi networks...
 1) MyHomeNetwork              -52dBm secured
 2) Neighbors5G                -71dBm secured
 3) CoffeeShopFree              -60dBm open
Join with: web -join <number> -pass=PASSWORD

nix:/$ web -join 1 -pass=THE_PASSWORD
Joining MyHomeNetwork ...
Joined MyHomeNetwork - IP: 192.168.1.42
Syncing time...
Time synced: 2026-07-03 13:24:07
Saved - future 'web' runs will join this network automatically.

nix:/$ web
Web file server running
Joined WiFi: MyHomeNetwork
Browse to: http://192.168.1.42
Press any key to stop.
```

Once joined, the credentials are saved to `WIFI_SSID`/`WIFI_PASS` in `/etc/settings/esp-nix.conf`, so every future `web` (or `web.sh`) run connects to that network directly instead of starting the ESP's own access point — useful when you want the file server reachable from any device already on your WiFi, not just one connected to the ESP32 directly. Open networks work too — just omit `-pass=`.

### NTP Time Sync

`date` reads a clock the ESP32 has no way to set on its own — it needs to be told the actual time from somewhere. `web -join` does this automatically the moment it connects. To sync on demand (using the saved `WIFI_SSID`, connecting briefly if not already online):

```bash
nix:/$ ntp
Connecting to MyHomeNetwork for time sync...
Syncing time...
Time synced: 2026-07-03 13:24:07

nix:/$ date
2026-07-03 13:24:11
```

Set `TZ_OFFSET` in `/etc/settings/esp-nix.conf` (seconds from UTC, e.g. `-18000` for EST) if you want `date` to show local time instead of UTC.

### Archives: extract and compress

`extract` unpacks `.zip`, `.tar.gz`/`.tgz`, `.gz`, and `.tar` archives; `compress` creates them. Real `.zip` files (the kind macOS/Windows create) go through a vendored copy of [miniz](https://github.com/richgel999/miniz); the tar-based formats go through [ESP32-targz](https://github.com/tobozo/ESP32-targz). Both stream through the archive rather than loading it into RAM, so size is limited by free space on the filesystem, not by the ESP32's 320KB of RAM.

```bash
nix:/$ extract project.zip
Extracted 12 file(s) to /

nix:/$ extract backup.tar.gz /data
Extracted to /data

nix:/$ compress /system system-backup.zip
Created system-backup.zip (8 file(s))

nix:/$ compress readme.txt readme.txt.gz
Created readme.txt.gz (312 bytes)
```

`extract [archive] [destdir]` defaults to the archive's own directory if no destination is given. `compress [source] [archive]` picks the output format from the archive's extension:

| Extension | Source must be |
|---|---|
| `.zip` | file or directory |
| `.tar.gz` / `.tgz` / `.tar` | directory (tar always wraps a tree, even a single file needs a folder) |
| `.gz` | single file |

**`.zip` entries are stored uncompressed.** Deflate's compressor state (`tdefl_compressor`) needs roughly 300KB in one allocation for its hash tables — more than the ESP32's entire free heap. Entries are written with the zip "stored" method instead, which skips that allocation entirely; the archive is still fully valid and opens fine in any zip tool, it just isn't smaller than the original files. `.gz`/`.tar.gz` compression isn't affected — `ESP32-targz` uses `uzlib`, a lighter-weight deflate implementation built for exactly this kind of memory constraint (~32KB rather than ~300KB), so those formats do actually compress.

**Not supported: `.7z`.** LZMA (7-Zip's compression) typically needs a 1MB–64MB dictionary buffer to decompress — the ESP32 only has 320KB of RAM total, so this is a hardware ceiling rather than a missing feature.

### OTA Firmware Updates

Build your firmware normally (`pio run`), rename `.pio/build/esp32dev/firmware.bin` to something ending in `.esp_update`, then push it to the SD card with `web`. Flash it from the shell:

```bash
nix:/$ update
Flashing /sd/firmware.esp_update (961184 bytes)...
20%
41%
63%
84%
Update successful. Rebooting...
```

`update` (no argument) auto-finds the first `*.esp_update` file on `/sd`, then `/`, and flashes it. Pass an explicit path if you have more than one: `update /sd/firmware-v3.esp_update`.

**What it is, and isn't:** the `.esp_update` file is just a renamed `firmware.bin` — not a zip archive. Renaming avoids the file being mistaken for something else when pushed around, but no decompression happens; it's streamed straight into the ESP32's OTA partition via the `Update` library. The board's default partition table already reserves two 1.3MB app slots for exactly this, so no partition changes were needed.

**Safety checks before flashing:** `update` refuses to touch the flash if the file is under 64KB (too small to be real firmware — almost certainly the wrong file or a truncated download) or doesn't start with the `0xE9` magic byte every ESP32 firmware image has. Both checks run before a single byte is written, so a bad file just gets rejected with a clear message instead of bricking the running firmware partway through:

```bash
nix:/$ update wrong-file.esp_update
Refusing to flash /sd/wrong-file.esp_update: missing ESP32 firmware
header (0xE9 magic byte) - not a valid firmware image
```

### Shell Editing

- **Up / Down arrows** — recall previous commands from history
- **Tab** — completes the current command name, or a filename if there's already a space in the line (shows a list when there's more than one match)

### Shell Scripts

Create and run shell scripts with `edit` and `./`:

```bash
nix:/$ edit setup.sh
--- Edit Mode ---
1: mkdir data
2: touch data/config.txt
3: echo "Config file created"
4: :!q
File saved: /setup.sh

nix:/$ ./setup.sh
--- Executing: ./setup.sh ---
[1] mkdir data
[2] touch data/config.txt
[3] echo "Config file created"
Config file created
--- Script complete ---
```

**Script features:**
- One command per line
- Lines starting with `#` are comments
- Empty lines are skipped
- Shows line numbers during execution
- Can use any available command

## Architecture

```
┌─────────────────────┐
│   Shell (input)     │
├─────────────────────┤
│  Commands (execute) │
├─────────────────────┤
│  FileSystem (SPIFFS)│
├─────────────────────┤
│ Terminal (LCD/Serial)
└─────────────────────┘
```

### File Structure

```
src/
├── main.cpp       - Initialization and boot
├── shell.h        - Command shell interpreter
├── commands.h     - Command implementations
├── filesystem.h   - SPIFFS abstraction
└── terminal.h     - LCD/Serial output
```

## Commands Reference

### System Information
- `uname` - Display system info
- `whoami` - Current user (always root)
- `date` - Current date and time
- `df` - Filesystem usage
- `free` - Memory usage

### File Operations  
- `ls [path]` - List files
- `cat [file]` - Display file contents
- `touch [file]` - Create empty file
- `rm [-r] [file|dir]` - Delete a file, or a directory tree with `-r`
- `cp [-r] [src] [dest]` - Copy a file, or a directory tree with `-r`
- `mv [-r] [src] [dest]` - Move/rename a file, or a directory tree with `-r`
- `grep [pattern] [file]` - Search file contents
- `head [file] [n]` - Show first n lines (default 10)
- `tail [file] [n]` - Show last n lines (default 10)
- `find [path] [pattern]` - Recursively search for files/dirs by name
- `wc [file]` - Count lines, words, and bytes
- `du [path]` - Total size of a file or directory tree
- `echo [text]` - Print text
- `edit [file]` - Edit file (`:!q` save+quit, `:d<n>` delete line n, `:i<n>` insert before line n, arrow keys move within a line, Up/Down reopen a line)
- `cmd > file` / `cmd >> file` - Redirect command output to a file (overwrite/append)

### Navigation
- `pwd` - Print working directory
- `cd [path]` - Change directory (supports `~` for `$HOME`)
- `mkdir [dir]` - Create directory

### Scripts
- `./[script]` - Execute shell script file

### System
- `clear` - Clear screen
- `help` - Show command list
- `nixos-rebuild` - Re-apply `/etc/settings/esp-nix.conf`
- `webserver` / `web` - Start WiFi file server for the SD card
- `update [file]` - Flash firmware from a `.esp_update` file
- `extract [archive] [destdir]` - Unzip/untar `.zip` `.tar.gz` `.tgz` `.gz` `.tar`
- `compress [source] [archive]` - Zip/tar/gzip a file or directory
- `reboot` - Restart the system
- `ntp` - Sync the clock over WiFi (also happens automatically on `web -join`)
- `exit` - Exit shell

## Limitations

- **Single user** (root only) — no accounts or permissions system
- **Limited to 255 char** command lines
- **History is in-memory only** — cleared on reboot
- **Browser terminal is request/response, not a live stream** — full-screen commands like `edit` need Serial or PS/2
- **No `.7z` support** — LZMA's dictionary requirements (1MB–64MB) exceed the ESP32's entire 320KB of RAM, a hardware ceiling rather than a missing feature
- **No real memory swap** — the ESP32 has no paging MMU, so this isn't feasible on this hardware at all

## Next Steps

Potential enhancements:
- Telnet/raw-socket remote shell (drop USB entirely for daily use)
- User accounts and permissions
- `PATH`-style multiple script directories, not just `/system`

## Credits

- The `web` file server started from [GPT_ESP32-File_network](https://github.com/RetroGigabyte/GPT_ESP32-File_network), extended here with the browser terminal, WiFi network joining, and SD-hosted page templates.

## References

- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [NixOS](https://nixos.org) - Inspiration for declarative configuration
- [FreeRTOS](https://www.freertos.org/) - Underlying RTOS
- [miniz](https://github.com/richgel999/miniz) - Vendored for real `.zip` support
- [ESP32-targz](https://github.com/tobozo/ESP32-targz) - `.tar`/`.gz`/`.tar.gz` support
