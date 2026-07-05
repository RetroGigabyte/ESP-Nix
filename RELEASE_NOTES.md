# ESP-Nix v1.2

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

## New in v1.2: `runmod` stage 3 - global data, memory management, and multi-file linking

A major expansion of `runmod`, closing out every gap identified after stage 2: `.rodata`/`.data`/`.bss` (not just `.text`), `malloc`/`free`/`memcpy`/string functions exported for loaded code to use, a generalized up-to-6-argument calling convention (replacing the old fixed `int(int,int)`/`int()` pair), and linking multiple `.o` files together. All six verified independently on real hardware, not just compiled:

- **`.rodata`** (string literals, lookup tables) - gas commonly names this section `.rodata.str1.4` rather than plain `.rodata`, matched by prefix. Verified: a function indexing into a string literal by a runtime parameter (not foldable at compile time) returned the correct byte.
- **`.data`** (initialized mutable globals) - verified: a global initialized to 10, incremented by a runtime argument, returned the correct sum.
- **`.bss`** (zero-initialized globals) - verified: reading a never-before-written global returned 0, as it should.
- **`malloc`/`free`** (plus `calloc`/`realloc`/`memcpy`/`memset`/`memmove`/`memcmp`/`strlen`/`strcpy`/`strncpy`/`strcmp`/`strncmp`/`strcat`/`strchr`) - verified: a function that `malloc`'s a buffer, writes and reads through it, then `free`s it, produced the correct value.
- **Multi-file linking** - `runmod <file1.o> <file2.o> ... <function> [args]` loads every file's sections first (so every final address is known), builds one combined table of each file's externally-visible symbols, then resolves each file's relocations against that combined table before falling back to the firmware whitelist. Verified: two independently compiled `.o` files, one calling a function defined in the other, produced the correct result through a genuine cross-file call.
- **Generalized calling convention** - `runmod` now takes any function name plus up to 6 decimal or `0x`-hex arguments, rather than the old fixed two-int/zero-int split. Works because Xtensa's windowed ABI passes the first 6 register-sized arguments the same way regardless of the callee's real declared signature.

This is a full rewrite of `src/elfloader.h`'s `ElfModule` to a proper (if still minimal) two-pass loader/linker: allocate every section of every file, collect a combined symbol table, then relocate. Toward the original goal (something like the HTTPS/TLS stack loading dynamically), the remaining real gaps are: many more exported symbols (socket calls, ESP32 hardware crypto-acceleration hooks), and a real multi-file build/packaging step instead of hand-listing `.o` files on the command line.

## New in v1.1.1: `runmod` now handles address-taken symbols (e.g. function pointers)

The one real gap called out after confirming multi-function modules - a locally-defined symbol referenced *by address* rather than called directly (the pattern behind function pointers, callback tables, anything a real library like an HTTPS stack leans on heavily) - is now handled. gas emits this as an `R_XTENSA_32` relocation against the enclosing `SECTION` symbol (not the specific function symbol) with the real intra-section offset baked into the placeholder bytes already sitting at the relocation site, in addition to (not instead of) the relocation's own addend field - confirmed by reading the raw relocation bytes directly, since this isn't obvious from `readelf`'s summary output alone. `runmod` now combines both when resolving a same-module relocation. Verified on real hardware: a function that takes the address of a second internal function and returns it as a plain integer returned exactly `module_base + 8` (the second function's real offset) - not a coincidence, confirmed by comparing against the module's actual load address.

## Confirmed on hardware: `runmod` already handles multi-function modules

Tested whether a module with more than one internal function (one calling another, both defined in the same `.o`) needs new relocation handling in `runmod` beyond what already exists - it doesn't. A same-file function call compiles to a direct `call8`/PC-relative windowed call, fully resolved by the assembler at compile time with no relocation entry needed at all (only `-mlongcalls`'s indirect `L32R`+`callx8` sequence for genuinely *external* symbols produces the `R_XTENSA_32` relocation `runmod` already patches). Verified with `elf_examples/multifn.c` (`sum_of_squares()` calling a separate `square()` twice, then reporting the result via the existing `host_print` symbol): `runmod multifn.o sum_of_squares 3 4` correctly printed `25` (3²+4²) on real hardware. The actual remaining gap for nontrivial modules is symbols that are locally-defined but referenced *by address* (e.g. function pointers, struct/array data) rather than called directly - those still produce `R_XTENSA_32` relocations against a defined symbol, which `runmod` still skips today.

## New in v1.1: `runmod` - stage 2 of the ELF loader, real relocations and symbol resolution

`runmod <path.o> <function> [a] [b]` is the next real step past `runelf`: it loads a genuine `.o` relocatable object file (not a bare `objcopy`'d `.text` dump), parses its `.rela.text`/`.symtab`/`.strtab`, resolves undefined external symbols against a small firmware-exported whitelist (`kRunmodSymbols` in `commands.h` - currently just `host_print(int)`, a test function that prints via Serial), patches the resolved addresses into the relocations Xtensa's `-mlongcalls` produces (`R_XTENSA_32` against the literal-pool entry the `L32R`+`callx8` call sequence loads from), and calls a named function within the module by symbol name.

This is genuinely new capability, not just a bigger `runelf`: loaded code can now call back into the firmware. The implementation lives in the new `src/elfloader.h` (`ElfModule` class) - a minimal ELF32 parser, not a general one: no program headers, no multi-file linking, and only `R_XTENSA_32` relocations against undefined symbols are handled (the other relocation types `-mlongcalls` emits, `R_XTENSA_SLOT0_OP`/`R_XTENSA_ASM_EXPAND`, are linker-relaxation metadata that only matters if the linker were relaxing longcalls, which never happens here). Growing the exported symbol table (more firmware functions loaded modules can call) is now just adding entries to `kRunmodSymbols` - the real remaining work toward something like a dynamically-loadable HTTPS stack is exposing enough of the firmware's own functions this way, plus handling relocations against *defined* (not just undefined) symbols for modules with more than one internal reference.

## New in v1.0.2: `runelf` supports zero-argument functions - makes `.elf`s at boot actually useful

`runelf` required exactly two arguments (`int(int,int)`), so a `.elf` aliased with `mkali ... -boot` would always fail at boot time - a boot-time invocation never has arguments to forward. `runelf <path>` (no args) now calls the loaded code as `int()` instead, so a self-contained no-argument function is a genuinely useful thing to run at boot. `runelf <path> <a> <b>` still works exactly as before for two-argument functions.

## Bug Fix (v1.0.1): `runelf` crashed with a LoadStoreError

`runelf` crashed on real hardware every time: `MALLOC_CAP_EXEC` memory (IRAM) on the ESP32 only supports 32-bit-aligned word accesses - a byte-wise file read directly into that region (as `runelf` originally did) faults immediately with a `LoadStoreError`. Fixed by reading the file into a normal byte-addressable staging buffer first, then copying it into the executable region one 32-bit word at a time (padding the tail to a word boundary).

## New in v1.0: `runelf` - the first step toward running compiled code from SD

`runelf <path> <a> <b>` loads a flat blob of raw Xtensa machine code from SD into executable RAM (via `heap_caps_malloc(MALLOC_CAP_EXEC)`) and calls it with a fixed `int(int,int)` signature. This is deliberately the smallest possible slice of "run compiled code from SD" - no ELF parsing, no relocations, no symbol resolution against the firmware yet. It works today because a genuinely self-contained C function (no calls to other functions, no global data) compiles down to zero relocations against its own `.text` section, so copying that section verbatim into RAM and calling it directly is safe. Building a `.elf` file today means compiling a self-contained function with the Xtensa toolchain and extracting just its `.text` with `objcopy` - see the README's `runelf` section for the exact recipe. The real relocator/symbol-table work needed for arbitrary compiled programs is tracked on the `elf-loader` branch.

## New in v1.0: `mkali`/`rmali`/`ls-ali` - alias, remove, and list SD-hosted programs

`mkali <source> <name> [-boot]` now also recognizes `.elf` files (routing through `runelf`), alongside the existing `.sh`/`.retro` support. `rmali <name>` removes an alias from both `/system` and `/boot`. `ls-ali` lists every alias, what it actually runs, and whether it's set to run at boot.

## Bug Fix (v0.9.5.1): Retron addition/subtraction of two variables produced garbage

`/a + /b` (and `/a - /b`) evaluated to `nan` or otherwise wrong results whenever both operands were variables. The expression evaluator's division-operator detection couldn't tell a real `/` operator apart from a variable's own `/` prefix once more than one variable appeared in an expression - it only special-cased a slash at position 0, so `/a + /b`'s second slash (from `/b`) got misread as a division operator splitting the expression in the wrong place entirely. This affected the shipped `calculator.retro` example's own "a + b" line. Fixed by replacing the position-based special case with `findDivisionOp()`, which looks at the nearest non-space character before each `/` - a real division operator follows a completed value (a digit, letter, or `)`), while a variable's own slash follows the start of the expression or another operator.

Separately, `^` (power) is parsed before `*`/`+`/`-` and greedily consumes the rest of the expression as its exponent/base - this is a real precedence limitation, not something fixed here. Expressions mixing `^` with other operators (e.g. `/pi * /r ^ 2`) don't evaluate as expected; keep to one operator type per expression and store intermediate results in their own variables instead.

## New in v0.9.5: `mkali` - alias scripts to run by name, anywhere, optionally at boot

`mkali <source> <name> [-boot]` aliases any script - including ones living on `/sd` - so it runs by a short name from anywhere, the same way `/system/*.sh` scripts already do. It works by dropping a one-line wrapper into `/system/<name>.sh` that runs `<source>` the right way for its extension (`.sh` directly, `.retro` through `retron`); `-boot` drops the same wrapper into `/boot` too, so it also runs automatically at every startup. `$@` forwarding works exactly like any other `/system/*.sh` command.

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
