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
