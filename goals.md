# ESP-Nix Goals

Future directions, not yet scheduled or in progress.

## CRT / Composite Video Output

Output ESP-Nix over RCA composite video to a CRT, instead of (or alongside) the 16x2 LCD. There's already a base to build from: [ESP32-WROM-32E-RCA-Graphics](https://github.com/RetroGigabyte/ESP32-WROM-32E-RCA-Graphics).

**Status:** blocked — the RCA cable needs re-soldering before this can be picked back up.

**Why it's interesting:** the LCD's 16x2 character display is the main output bottleneck right now (the browser terminal and Serial console already give a "real" full-size terminal, but there's no local-hardware equivalent). A CRT output would give ESP-Nix an actual screen without needing a second device to read it.

**Open questions for when this starts:**
- Does composite video replace the LCD's status role, or run alongside it as a second output?
- How much RAM/CPU headroom does the RCA graphics generation leave for the shell itself?
- Character-only display (like a real text terminal) vs. anything more graphical?

**RAM budget (2026-07-03):** target resolution is 320×240 at ~16 colors (4 bits/pixel) = 38,400 bytes (~37.5KB) per framebuffer, or ~75KB double-buffered. Measured separately, `wifi`/`curl`/`ping` push heap usage up to ~109KB while actively connected (WiFi + TLS runtime buffers). Combined, that's already over half the ESP32's 320KB total RAM before the shell, LittleFS, or anything else runs — CRT output and heavy networking likely can't both run at full intensity simultaneously without real budgeting. Worth deciding up front whether video is single-buffered (accept some flicker, save ~37.5KB) or whether networking gets scaled back while video is active.

**Flash budget (2026-07-03):** ESP-Nix is currently at 94.2% of the 1.3MB OTA partition (~70KB headroom), almost entirely from the HTTPS/TLS stack (`WiFiClientSecure`, ~140KB). The actual code size of `ESP32-WROM-32E-RCA-Graphics` (signal timing, DMA/RMT output, any font tables) hasn't been checked against this yet - do that first before assuming it fits. If it doesn't, dropping HTTPS from `curl` recovers ~140KB, which would likely be enough on its own - meaning it may come down to choosing between "talks to HTTPS APIs" and "drives a CRT" rather than having both.

## Programs on SD (scripting VM)

Real "install a program by dropping a file on the SD card" - not just config/data (already true for `/etc/settings`, `/boot`, `/system`, the web UI pages, the `nixfetch` logo) but genuinely new logic, without recompiling firmware.

**Why this needs more than what exists today:** the ESP32 Arduino environment has no dynamic linker - there's no safe way to load arbitrary new compiled code from SD at runtime. `/system/*.sh` and `/boot/*.sh` already are "programs on SD" in the sense that they're interpreted, not compiled in, but they can only sequence *existing* built-in commands (`test`/`[`, `loop`, `&&`/`||`/`;`, pipes, redirection) - they can't introduce logic the shell doesn't already have a primitive for.

**What it would take:** a small scripting language interpreter (a tiny Forth, a custom bytecode VM, something in that weight class) compiled into firmware once. After that, a "program" is a script/bytecode file on SD that the VM interprets - genuinely new behavior without ever touching firmware again.

**Scope note:** this is a real build, comparable in size to the CRT project - not a quick add. Revisit when there's bandwidth for it specifically, likely after (or alongside) the CRT work rather than squeezed in as a side feature.

## Sensor/Actuator Commands (SunFounder kit)

The recommended kit (see README) includes far more hardware than ESP-Nix currently touches. Full inventory as of 2026-07-03:

- **Displays:** LED, RGB LED, 7-segment display, WS2812 RGB 8-LED strip (I2C LCD1602 already used)
- **Sound:** buzzer, audio module + speaker
- **Drivers:** DC motor, servo, centrifugal pump, L293D (motor driver), 74HC595 (shift register)
- **Controllers/input:** button, tilt switch, potentiometer, joystick module, IR receiver
- **Sensors:** photoresistor, thermistor, DHT11 (humidity/temperature), PIR motion sensor, line tracking module, soil moisture module, obstacle avoidance module, ultrasonic module
- **Also in the kit:** ESP32 camera extension board, battery (see "Camera" note below and RAM/flash budget notes above)

**Natural fit for ESP-Nix:** expose these as shell commands rather than one-off sketches - e.g. `sensor read dht11`, `gpio set <pin> <value>`, `servo <pin> <angle>` - consistent with everything else being a command you can script, pipe, or call from `/boot`/`/system`. Scope is much smaller per-component than CRT or the scripting VM (most of these are a single I2C/analog/digital read or write), so this could be tackled incrementally, one sensor/actuator at a time, rather than as one big project.

**Not yet decided:** which specific sensors/actuators to wire up first - revisit when ready to pick one.
