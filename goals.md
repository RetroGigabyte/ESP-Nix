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
