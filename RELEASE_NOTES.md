# ESP-Nix v0.9.2.5

A declarative, Unix-like shell operating system for the ESP32 — built from scratch on FreeRTOS, running entirely off an I2C LCD and a serial console (with optional SD card, PS/2 keyboard, and WiFi).

> **AI Disclaimer:** This project was developed with the assistance of Claude AI. I believe AI-written code should be open source to benefit everyone and maintain transparency.

> **Upgrading from v0.8.1 or earlier? You must reflash over USB, not `update`.** v0.8.0/v0.8.1 shipped with a partition scheme (`huge_app.csv`) that turned out to break `update`/OTA entirely — see "Partition Layout Fix" below. This release corrects that with a new partition table, which again means a one-time USB-only migration (`pio run -t upload`). Every release after this one goes back to normal `update`/OTA. This also resets everything on internal storage (WiFi credentials, `/etc/settings`, `/boot` scripts, history) — the SD card is untouched.

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
- `wifi connect`/`disconnect`/`status`/`toggle`, `ip`, `ping` — a persistent WiFi connection independent of `web`'s connect-then-disconnect lifecycle, so the shell can check status, IP, and host reachability at will
- `curl [-X METHOD] [-d data] <url>` — basic HTTP/HTTPS client (HTTPS via `WiFiClientSecure::setInsecure()`, no certificate validation)
- `wget <url> [-O output]` — downloads a URL straight to a file, streaming the response instead of buffering it in RAM
- `ftp get`/`put`/`ls` — an FTP *client* (not a server) for pushing/pulling files to/from a remote FTP server, `ftp://user:pass@host/path` style URLs
- OTA updates via `update`, with pre-flight validation (size and firmware-header checks) before touching flash

**Flash usage:** HTTPS support (`WiFiClientSecure`'s TLS stack) added ~140KB to the firmware image — this build sits at 94.7% of the 1.3MB OTA partition (~70KB headroom left). Worth checking before adding anything else substantial.

**Tested:** `wifi connect` → `curl`/`ping` → `wifi disconnect` cycle confirmed via `free` — heap climbs while connected (WiFi + TLS runtime buffers) and drops back down after `wifi disconnect`, with no growth across repeated cycles.

## Editor & Extras
- Full line editor (`edit`) with mid-line cursor movement, line insert/delete, and Up/Down navigation between lines
- `nixfetch` — a neofetch-style system summary with a customizable ASCII logo
- `loop <count|inf> [-i seconds] <command...>` — repeats a command, since the script engine has no real loop construct; any keypress stops it early
- `retron <file.retro>` — runs scripts in [Retron](https://github.com/RetroGigabyte/Retron) (real variables, `if`/`loop`/functions, `INPUT`/`OPEN`/`READ`/`WRITE`/`LOAD`/`QUIT` - genuinely new logic, not just sequences of existing commands). `DRAW` (graphics) errors clearly rather than silently no-op'ing, since composite video output isn't wired up yet
- `PATH` — script directories (like `/system`) are now configurable, colon-separated, checked in order
- Shell prompt now shows the current user and hostname (`root@esp-nix:/$`), reading from `USER`/`HOSTNAME` variables rather than hardcoded strings - same for `whoami`. First piece of an eventual accounts/permissions framework; still one user, nothing access-controlled yet
- `hostname [-v] | hostname -s <name>` - shows/sets `HOSTNAME`, also applied as the real WiFi hostname on next connect (`WiFi.setHostname()`/`softAPsetHostname()`)
- `backup -m|-l|-r#` - backs up internal LittleFS ("user space") to `/sd/backups` as `hostname-YYYYMMDD-HHMMSS.esp_bak`, lists backups numbered, restores by number. Internally just zip archives with a renamed extension (format dispatch keys off the archive path's extension)
- `sleep <seconds>` — pauses, interruptible by any keypress
- `web`'s file manager now zips folders on the fly for download, and shows a `nixfetch` snapshot taken when `web` starts

## Partition Layout Change (v0.8.0) — and Fix (v0.9.1.1)

v0.8.0 switched from the default two 1.25MB OTA slots to a single 3MB app partition (`huge_app.csv`), to recover flash headroom after HTTPS support ate into it. **This turned out to be broken, not just less safe.** A single `app0` doesn't just drop the OTA rollback fallback - it breaks `update`/OTA outright: the Arduino `Update` library needs a genuinely separate partition to write a new image into while the current one keeps executing, and with only one slot, `Update.begin()` ends up overwriting the flash pages the CPU is actively running from, crashing with `abort()` the moment it happens. Confirmed live: `update`-ing to v0.8.1 crashed at 0% and rebooted back into the previously-running v0.8.0 (no data lost, but OTA was non-functional the whole time).

v0.9.1.1 fixes this with `min_spiffs.csv` — two real 1.875MB OTA slots (up from the default's 1.25MB, and with real fallback safety this time), LittleFS at 128KB. Full details, including why the first attempt failed, in the README's "Partition Layout" section.

## Bug Fix (v0.9.1.1): mkdir + ls on SD card

`mkdir` on the SD card would report success, and `cd` into the new directory would confirm it genuinely existed - but a bare `ls` in that same directory wouldn't show it. Root cause: the shell's current-directory string always carries a trailing slash (e.g. `/sd/etc/`), and `SD_MMC`'s VFS layer doesn't reliably enumerate a directory opened with one present, even though `exists()`/`isDir()` (what `cd` uses) tolerate it fine. Fixed by normalizing trailing slashes out of every path in `FileSystem::stripSd()` - the single choke point all file operations already route through, so this fixes it everywhere at once rather than just in `ls`.

## Bug Fix (v0.9.1.1): mv/cp with multiple glob matches to a non-existent destination

`mv *.retro retron` (destination folder not yet created) silently clobbered every matched file onto the same literal path one after another, instead of erroring or creating a folder - `resolveDestPath()` only treated the destination as a directory if it *already* existed as one. Fixed in `cmdCp`/`cmdMv`: when a glob expands to more than one source file and the destination doesn't already exist as a directory, it's auto-created as one first, so each file lands inside it under its own name instead of overwriting the last.

## Bug Fix (v0.9.1.1): nixfetch (and any single unpiped command) printed to the console instead of the web page

The `nixfetch` snapshot on `web`'s file manager page rendered as an empty `<pre></pre>` - the text itself printed correctly to the physical Serial console instead. Root cause: `Shell::executePipeline()` unconditionally set the shared capture buffer to `nullptr` for any pipeline stage that doesn't need its own local capture (`needsCapture = false` - true for any single command with no pipe or redirect, like a bare `nixfetch`). That clobbered the *outer* capture buffer the browser terminal's executor had already set up before calling into the shell, so `out()` fell through to the real terminal instead of the caller's buffer. Fixed by only touching the capture buffer when a stage actually needs one, leaving an outer caller's context untouched otherwise.

## Bug Fix (v0.9.1.1): backup -m failed on the filesystem root

`backup -m` compresses internal storage root (`/`) directly, but `Archiver::compressZip()` computes a wrapping directory name from the last path segment - for `/` itself, that's an empty string, producing an invalid zip entry name (`/`) that miniz rejects with "Failed to add directory entry: /". Fixed by special-casing root: instead of wrapping everything in a (nonexistent) top-level folder, `/`'s direct children are added to the zip individually, with no wrapping folder at all - which is also the more useful behavior for a backup, since restoring extracts straight back onto `/` without an extra nested layer.

## Bug Fix (v0.9.2.5): `ftp get` crashed with a stack overflow

`ftp get` crashed immediately with "Stack canary watchpoint triggered (loopTask)" - a stack overflow, not a logic bug. The Arduino core's default main-loop task stack (8KB) is tight once the shell's nested command dispatch is combined with a command that puts a sizable object on the stack, like `ESP32_FTPClient` (its internal 1.5KB `clientBuf`, plus other members). Fixed by overriding the core's weak `getArduinoLoopTaskStackSize()` in `main.cpp` to 16KB, giving real headroom for `curl`/`wget`/`ftp` and anything similar added later.

## Bug Fix (v0.9.2.4): `ftp ls` sent a malformed LIST command

Diagnosed via temporary debug logging (v0.9.2.3): the vendored library's `ContentListWithListCommand()` sent `client.print(F("LIST"))` with no trailing space before appending the directory argument, producing a single malformed token like `LIST/` instead of `LIST /` - both tnftpd and pyftpdlib rejected it outright ("500 Command not understood"), which is why `ftp ls` returned nothing. Fixed by adding the missing space (`"LIST "`). Debug verbosity reverted back to 0.

## Bug Fix (v0.9.2.1): failed FTP login silently reported as success

`ESP32_FTPClient::OpenConnection()`'s connected/not-connected flag reflects whichever FTP command ran most recently, not specifically whether login succeeded - and some servers (tnftpd included) reply successfully to the `SYST` command sent right after login regardless of whether `PASS` was actually accepted, silently overwriting a failed login back to "connected". Every `ftp` subcommand would then proceed as if authenticated, doing nothing and printing nothing. Fixed in the vendored copy by capturing the `PASS` response specifically and keeping the connection marked failed if it wasn't accepted, regardless of what runs after it.

## Bug Fix (v0.9.2.1): `ftp ls` memory-unsafe listing

`ftp ls` called the vendored library's `ContentList()`/`ContentListWithListCommand()` passing the address of a single `String` where the library actually expects (and indexes into) an array of `String` objects - one per line of output. With more than one line in a directory listing, this wrote past the single `String`'s memory. Also switched from `MLSD` to the more universally-supported `LIST` command, since `MLSD` isn't implemented by every FTP server (tnftpd included) and was silently returning nothing. Fixed by passing a real `String listing[128]` array and using `ContentListWithListCommand`.

## New in v0.9.2: FTP client, and a vendored/patched library

`ftp get`/`put`/`ls` is built on [`ldab/ESP32_FTPClient`](https://github.com/ldab/ESP32_FTPClient), but its `DownloadFile()` is unsafe for real use: it reads a caller-supplied byte count via `readBytes()` and discards the actual number of bytes read, so it can't distinguish real file data from trailing garbage on a short read, or avoid truncating a longer one - confirmed by the library's own upstream TODO list, which still lists "Implement download" as outstanding. Rather than ship that or skip download entirely, the library is now vendored directly into `src/ftpclient/` (not a `platformio.ini` dependency) with a new method, `DownloadFileToFile()`, that reads from the passive FTP data connection until the server closes it - correct `RETR` semantics - and returns the real byte count. `ftp put` uses the library's existing (correct) upload path unchanged.

## Hardware
- ESP32 (required)
- I2C LCD1602 and SD card (via SD_MMC) — both recommended, not required. The shell works entirely over Serial/browser terminal without the LCD, and without SD the system just runs off internal LittleFS — you'd just lose `web`, `extract`/`compress` of large archives, and general extra storage.
- PS/2 keyboard — optional, auto-detected at boot

**Recommended kit:** the [SunFounder ESP32 Ultimate Starter Kit (with camera extension board and battery)](https://www.sunfounder.com/products/sunfounder-esp32-ultimate-starter-kit-with-esp32-camera-extension-board-battery) covers the ESP32 board, LCD, and SD adapter this project was built and tested against, in one bundle.

## Credits
- The `web` file server started from [GPT_ESP32-File_network](https://github.com/RetroGigabyte/GPT_ESP32-File_network), extended with the browser terminal, WiFi network joining, and SD-hosted page templates in this project.
