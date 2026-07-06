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

## Multi-Chip Architecture ("ESP Computer")

A distributed build across four chips instead of one - three ESP32-S3 boards (8MB RAM, 16MB flash each) plus the existing ESP32 WROOM-32E, each with a dedicated role, tying directly into the CRT project above.

```
Key Board <--> USB-1 <--OTG--> S3-3 (MAIN) <-----------> S3-1 (CPU) <--OTG--> USB-2 <--> Mouse
                                     |    \                     |
                                  READ SD   \-------Render------>|
                                     |               (also)      |
                                     v                           v
                          WROM-32E (SD + Display Out) <-- S3-2 (GPU)
                                     |               Video-out
                                  RCA OUT
                                     |
                                     v
                                   CRT
```

(v2: adds a direct MAIN↔GPU link alongside the original CPU↔GPU one - see `ESP_computer_v2.png`.)

- **S3-3 (MAIN):** orchestrator; owns keyboard input over USB OTG; reads the SD card
- **S3-1 (CPU):** owns mouse input over USB OTG; general compute, hands rendering work to the GPU
- **S3-2 (GPU):** rendering; produces a fully-rendered frame, already in the WROOM-32E's expected output format, and sends it over as video-out
- **WROOM-32E (SD + Display Out):** storage plus composite video output to a CRT over RCA - purely a dumb framebuffer relay for video (no rendering of its own), same RCA output path as the single-chip CRT project above

**Status:** hardware ordered, not yet arrived. Diagram exists (see `ESP_computer.png`); no firmware or wiring started.

**Interconnect (tentative): SPI.** Compared against I2C (too slow for video), UART (point-to-point only, still too slow), WiFi/ESP-NOW (jitter is a dealbreaker for anything timing-sensitive like driving a CRT), a custom parallel GPIO bus (highest possible throughput, but most complex and pin-hungry), and I2S (streaming-friendly, mostly one-directional, less common outside audio). SPI is the practical middle ground: fast, low-jitter, quick to bring up. The GPU→WROOM-32E video-out leg is the one most likely to need something faster if SPI can't sustain real CRT timing at the target resolution - worth prototyping that leg in isolation first, before committing to the full four-chip build.

**Fallback if SPI throughput falls short (resolved):** add more SPI connections in parallel (shard the frame data across multiple simultaneous SPI links) rather than switching to a different bus entirely - a multiplicative scale-up on the same, already-proven interconnect instead of a redesign.

**SPI topology (resolved, 2026-07-06):** split by how much contention each link can tolerate, not one topology for everything:
- **GPU↔WROOM-32E** (the video feed) gets its own dedicated, non-shared bus - the highest-bandwidth, most timing-critical leg, and per the fallback above, the one most likely to need *multiple* parallel dedicated buses (sharded) if a single one can't sustain the frame rate. First estimate: SPI×2 (two parallel buses, e.g. each carrying half the frame's scanlines/rows) - confirmed as needing the most connections of any link in the system; exact count to be verified once real throughput is measured against the ~9.2Mbit/s sustained target.
- **MAIN↔CPU** also gets its own dedicated bus - carries varied, frequent traffic (input relay, sync, SD forwarding, WiFi relay, offloaded compute). Since SPI is master/slave, one of the two is picked as the fixed bus master for this link specifically, independent of which one `run set` currently targets.
- **MAIN↔GPU and CPU↔GPU share one physical bus** into GPU, each with its own CS line - safe because only one of MAIN/CPU is ever the active `-g` render source at a time, so they're never talking to GPU simultaneously by design. An escalation ladder if the simple version doesn't hold up: nominally SPI×1 (one shared bus, ~4 wires plus the two CS lines); worse case SPI×1.5 (6 wires) if extra signaling beyond the two CS lines turns out to be needed; worst case SPI×2 - fully split into two dedicated buses, one MAIN→GPU and one CPU→GPU, abandoning the shared-bus assumption entirely.
- **MAIN↔WROOM-32E** (READ SD) is the least critical, and may not need SPI at all - at most ~1Mbit/s, well within what a lower-pin-count link (I2C or UART, 2 wires instead of SPI's 4) can handle. Worth using the cheaper-on-pins option here specifically, freeing up GPIO for the links that actually need SPI's speed.
- Still needs verifying once hardware arrives: whether the S3's GPIO budget comfortably fits this many simultaneous buses (dedicated buses cost more pins than one shared bus).
- A breadboard is available for prototyping, so the escalation ladders above (SD link wire count, MAIN/CPU↔GPU SPI×1 vs 1.5 vs 2) can actually be tried and measured rather than only reasoned about on paper before committing to a permanent build.

**Power (2026-07-06).** A separate, real issue from the interconnect - powering four chips plus two USB peripherals reliably:
- **USB OTG host-mode power** is the trickiest part: acting as a USB host (required for keyboard/mouse), the ESP32 side is expected to supply 5V/up to 500mA to the peripheral over VBUS. Many dev boards only deliver USB power as a *device*, not source it out in host mode - worth checking whether the S3 boards' OTG circuitry can actually power the keyboard/mouse, or whether an external 5V feed (possibly a small boost converter) is needed at each OTG connector.
- **Shared rail vs. individual supplies:** one beefy 5V supply (USB-PD brick or bench supply, real current headroom - likely 3A+ once all four chips and two USB peripherals are counted) distributing to all four boards is cleaner than four separate wall adapters, but risks a current spike on one board (e.g. MAIN's WiFi TX burst) sagging the rail enough to brown out or reset a different board if wiring/supply margin is thin. A known failure mode in multi-board builds, not hypothetical.
- **Common ground: confirmed, will be used on the breadboard.** Every chip sharing an SPI/I2C/UART link needs a shared ground reference, or the signaling itself becomes unreliable.
- **The CRT is a non-issue here** - it has its own AC mains supply; only the composite video signal comes from the WROOM-32E, no shared power relationship.
- **Per-chip power plan (resolved):** each chip gets its own independent supply, safer than one shared rail (see above). A 3-port USB PD brick is available, allocated as: **MAIN** and **CPU** each get one PD port (both need the extra headroom, since each sources power to a USB peripheral - keyboard and mouse respectively - on top of their own draw); **GPU** gets the third PD port (moderate draw, no OTG-sourcing duty, doesn't need the same headroom as MAIN/CPU); **WROOM-32E** gets its own separate, simpler supply (a phone charger or spare USB port is plenty - it's the least power-demanding of the four, no OTG host duty and no rendering load). Common ground still ties all boards together regardless of each having independent power.

**Resolved:** "render" means the GPU hands over an already-fully-rendered frame, pre-converted to whatever format the WROOM-32E's display-out path expects - the WROOM-32E does no rendering itself, just outputs what it's given. This puts the real bandwidth pressure squarely on the GPU→WROOM-32E link (a full frame every refresh, not lightweight draw commands), reinforcing that this is the leg most likely to need something beyond SPI.

**Frame format (2026-07-06):** 320×240 at 16 colors (4 bits/pixel, palette supports shading rather than just 16 flat hues) - the same target already budgeted in the CRT section above, at 38,400 bytes (~37.5KB) per frame.

**Frame rate: 60fps out, 30fps render.** The WROOM-32E's own output loop runs at real NTSC field rate (60Hz), but the GPU only needs to produce a new unique frame every other output cycle (30fps) - each rendered frame gets held/repeated across two 60Hz output cycles. This halves the GPU→WROOM-32E bandwidth requirement versus naively assuming 60fps of unique data: ~1.15MB/s (~9.2Mbit/s) sustained, not ~2.3MB/s. SPI on the S3 (up to 80MHz) has comfortable headroom over that on paper - real-world throughput with DMA and transfer overhead will be lower than the theoretical max, so this is the first number worth actually measuring once hardware arrives, not just assuming from the clock rate.

**Resolved:** the MAIN↔CPU link carries USB input (keyboard events from MAIN relayed over), synchronization signals, SD read data (MAIN forwards what it reads from the WROOM-32E on the CPU's behalf), WiFi data (MAIN owns the radio; CPU gets network data relayed over this link rather than having its own), and general offloaded processing - treating MAIN+CPU together as a cooperative 4-core, 16MB-RAM pair (2 cores/8MB each) rather than two fully independent chips with one narrow-purpose link between them.

**Boot sequence (resolved):** MAIN is the initiator - it sends a "start" signal to CPU and, in parallel, kicks off the initial SD read on the WROOM-32E. CPU then sends the first "render" command to GPU. GPU sends its first completed frame to the WROOM-32E, which is the point real video output begins. Each stage waits on the previous one rather than all four chips racing to initialize independently.

**Failure handling (resolved):** retry - both at boot and if a chip disconnects mid-operation later, the response is the same: retry the connection. Whatever was actively running on the disconnected chip at the time is lost (no state recovery/resume) - retry re-establishes the link, it doesn't restore in-progress work.

**SD ownership (resolved):** a single SD card, physically connected only to the WROOM-32E - confirms the diagram's implication. MAIN and CPU have no SD of their own; any file access from either goes through the WROOM-32E over the interconnect.

**Chip-to-ESP-Nix mapping (resolved):** both MAIN and CPU run full, independent ESP-Nix instances (not a split where one owns the shell and the other only owns compute) - each capable of running the whole OS on its own. Launching an app lets you pick which one actually runs it via a "run set" (MAIN or CPU), rather than that being fixed per-app or per-role. GPU is not implied to run full ESP-Nix - presumably a thinner, rendering-only firmware, though that's not explicitly confirmed yet.

**Terminal ownership (resolved):** only CPU actually runs the interactive Terminal - even though MAIN also runs a full ESP-Nix instance, MAIN isn't the one presenting the interactive shell prompt to the user. This changes the OTG-merge question below: MAIN's keyboard input has to reach CPU's terminal somehow, rather than MAIN needing its own terminal UI at all.

**GPU's content source (resolved, refines the above):** GPU reads/pulls the terminal content it needs to render from whichever of MAIN or CPU is actually the source of the active output at the time - not hardwired to always pull from CPU. This lines up with "run set" (an app can be launched on MAIN or CPU): whichever chip is producing the currently-displayed output is the one GPU draws from. Confirmed and wired into the diagram in v2 (`ESP_computer_v2.png`), which adds a direct MAIN↔GPU link alongside the original CPU↔GPU one - still open is what decides/switches which chip is the "current source" at any given moment.

**GPU role (resolved):** GPU does not run ESP-Nix at all - it runs its own small, custom, single-purpose "operating system" whose only job is to receive render data from CPU, render the frame, and send it out to the WROOM-32E. This is the same role a Mac used to play in [ESP32-WROM-32E-RCA-Graphics](https://github.com/RetroGigabyte/ESP32-WROM-32E-RCA-Graphics) (the precursor to the single-chip CRT project above) - GPU is effectively taking over that external render-source role, just running on dedicated hardware instead of a full computer.

**Render protocol (resolved):** CPU sends GPU draw commands, not pixel/framebuffer data - e.g. "draw `root@esp-nix:/$` at (x,y) in 12pt [font]" (the specific font/text was just an illustrative example, not a decision). This means GPU's firmware needs an actual text/font rendering engine (glyph lookup, rasterization at the given point size), not just a dumb pixel blitter - real scope to account for, since font rendering on a microcontroller (storing glyph data, scaling, rasterizing to the 4bpp/16-color framebuffer) is a nontrivial piece of the GPU firmware on its own, not a minor detail.

**GUI presets (resolved):** GPU has built-in, pre-set ways to draw common GUI elements - e.g. a whole "terminal window" as one canned drawing routine - rather than CPU always having to specify every individual primitive (text runs, borders, etc.) from scratch. So the draw-command protocol has (at least) two tiers: raw primitives (positioned text) and higher-level widgets (a terminal window preset) that CPU can invoke as a single command.

**"run set" command (resolved):** `run set -s (MAIN or CPU)` - same `-s`-sets-a-new-value convention as `hostname -s <name>`; sets which chip a launched app runs on. `run set -g (MAIN or CPU)` is a separate, independent selector: which chip GPU currently sources its display content from. Decoupling the two (`-s` for where things run, `-g` for what's on screen) means you can run something on one chip while still watching the other's terminal on the CRT - e.g. kick off a long-running process on MAIN (`run set -s MAIN`, start it), then `run set -g CPU` and switch to playing a NES emulator (Mario) on CPU while MAIN keeps working in the background. Both are manually set by the user, not automatic.

**Variables for current state (added):** the current `-s` and `-g` values should be readable as ordinary shell/Retron variables (same convention as `USER`/`HOSTNAME` today) - e.g. something like `RUN_TARGET` (what the app is running on) and `DISPLAY_SOURCE` (what the display is currently showing), so scripts can check current state without needing `run set` to also have a query mode. Exact variable names not yet finalized.

**`run -i` / `run set -i` (added):** a dedicated info view showing all of this current multi-chip state at once (run target, display source, whatever else accumulates) rather than checking each variable individually. `nixfetch` should show the same info too, alongside its existing OS/host/uptime/memory/disk summary - this hardware is exactly the kind of thing a system-summary command should surface.

`-b` ("both") is a third value accepted by either flag, alongside `MAIN`/`CPU` - e.g. `run set -s -b` runs the launched app on both chips at once, `run set -g -b` would have GPU source from both simultaneously.

**`-g -b` semantics (clarified):** `run set -g -b -d`/`-f` reuses the dynamic/fixed distinction, but it's purely a **CPU-side compute setting** - it decides how MAIN+CPU split the underlying processing work, not how GPU displays anything. The GPU's actual rendered output is the same regardless of whether `-d` or `-f` is set.

**`-g -b` on-screen behavior (resolved):** whichever chip updates first is what's shown - not split-screen, not an overlay. GPU displays the most recently produced output between MAIN and CPU, switching to whichever one refreshes next.

**MAIN and CPU can read each other's output (resolved):** each chip has visibility into what the other is currently producing, not just GPU pulling from whichever is `-g`-selected. This is likely the underlying mechanism that makes `-g -b` possible at all (GPU could receive a merged/combined stream via one chip that's already read the other's output) - and it's independently useful beyond that, e.g. for `run set -s -b` (same app on both) to keep the two in sync, or just for one chip to check on the other's state without switching `-g`.

**Possible future mode: a single process genuinely shared/split across both chips**, pooling MAIN+CPU's combined processing power and/or RAM (up to 4 cores/16MB total) for one task that needs more than either chip alone provides - distinct from `-b` (which runs independent copies of the same app on each chip). This is a real distributed-computing problem, not just a scheduling choice: splitting one process's actual work across two physically separate chips means partitioning the workload, keeping shared state in sync over the interconnect, and handling the latency of that sync being far higher than accessing local RAM. Worth treating as its own substantial sub-project when the time comes, not something to bolt on alongside `-s`/`-g`/`-b`.

**Fixed vs. dynamic work split (resolved):** both are supported, with **dynamic as the default**. `run set -b -f` forces fixed-by-role splitting; `run set -b -d` switches back to dynamic (or sets it explicitly, matching the default). Confirmed: `-b` consistently means "both (chips)" everywhere it appears - `-d`/`-f` are modifiers *within* that both-chips context (deciding how work is split between the two), not a separate, colliding definition of `-b`. No naming conflict after all.

**Keyboard path (resolved):** MAIN forwards keystrokes raw over the interconnect to CPU's terminal - no local handling/interpretation on MAIN's side, it's a straight passthrough from its USB OTG input to whichever chip is running the terminal.

**Preset design (resolved):** presets are parameterized, not fully hardcoded - most of a preset's actual layout/chrome is stored on GPU itself (e.g. a "window" preset's frame, borders, structure), and MAIN/CPU only need to supply the small bits that vary per use (like a window's title text) when invoking it. So calling a preset is cheap - a short command plus a few parameters - rather than MAIN/CPU needing to describe the whole thing from scratch or GPU only supporting one fixed, uncustomizable version of each widget.

**Preset parameters (resolved):** title text, color (re-coloring the preset's default white), size, and position are all exposed as parameters from the start - not deferred to "later" as originally planned. Border style and anything beyond these four is still open.

**Reference: how real computers do this (2026-07-06).** This architecture maps closely onto how a real PC's CPU/GPU/display split actually works:
- **CPU→GPU is a command buffer, not pixels** - a real app calls a graphics API (OpenGL/Direct3D/Vulkan/Metal), the driver translates that into a GPU-specific command buffer the GPU reads over PCIe. Your draw-command link is the same idea, just coarser-grained (whole widgets instead of triangles/vertices) - a reasonable simplification given there's no parallel shader hardware to feed here.
- **The GPU keeps its own local memory** - real GPUs have dedicated VRAM (textures, vertex buffers, the framebuffer itself), decoupled from system RAM. Presets stored on GPU, with MAIN/CPU only sending what varies per call, is the same idea in miniature.
- **Display output is a separate, independent hardware block** - real GPUs have a "display engine," historically called the **CRTC (Cathode Ray Tube Controller)** - literally named for driving a CRT. It scans out whatever's in the framebuffer at a fixed rate, decoupled from how fast the GPU renders new frames, via double/triple buffering (render into a back buffer while the front buffer is scanned out, then swap). This is exactly the WROOM-32E's role, and exactly why "30fps render, 60fps out" works - real GPUs do the same decoupling.
- **Synchronization is fences/interrupts, not polling** - the GPU signals "frame done," the CPU/display engine reacts, rather than anyone spinning waiting for the other.
- **Where this design diverges from a real GPU:** real GPUs are massively parallel (thousands of shader cores); this is a single microcontroller doing CPU-driven software rasterization, closer to how PCs rendered graphics before dedicated GPU hardware existed.
- **The "pixel mode reader" idea maps onto real framebuffer readback** - reading raw pixel data straight out of VRAM, bypassing the command-buffer abstraction entirely. Real uses: screenshots, video capture, debugging what's actually in the framebuffer versus what was intended.

**Font (resolved):** starting with a simple pixel/bitmap font, the same style as the existing I2C LCD1602's built-in character font, plus a set of non-standard/custom characters beyond standard ASCII (extra symbols, presumably for terminal UI elements like box-drawing/line characters). Bitmap fonts avoid the whole on-device outline-rasterization question entirely - each glyph is just a fixed pixel pattern per size, cheap to draw and consistent with the LCD's existing look.

**Open questions for when this starts** - all of these are extensible later, not hard blockers (SPI topology, the last real blocker, is now resolved above). Can start with the simplest option and grow:
- Exact custom/non-standard character set beyond CP437 - can start with just CP437 + ASCII and add glyphs incrementally (upside-down "^", filled/unfilled square, etc.) as they're actually needed.
- CJK character support - already parked as optional/stretch, not blocking anything.
- Full list of GUI presets beyond "window" - can add new presets one at a time as they're needed, no need to design the full set up front.
- Border style and other per-preset parameters beyond title text/color/size/position - same, extend per-preset as needed.

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
