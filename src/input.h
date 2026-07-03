#pragma once

#include <Arduino.h>
#include "ps2keyboard.h"

// Merges Serial (USB) and an optional PS/2 keyboard into a single input
// stream, so the shell and editor can read keystrokes from whichever
// source has data without knowing which one it is.
class Input {
public:
  void attachPS2(PS2Keyboard* kb) {
    ps2 = kb;
  }

  bool available() {
    if (Serial.available()) return true;
    if (ps2 && ps2->available()) return true;
    return false;
  }

  int read() {
    if (Serial.available()) return Serial.read();
    if (ps2 && ps2->available()) return ps2->read();
    return -1;
  }

  int peek() {
    if (Serial.available()) return Serial.peek();
    if (ps2 && ps2->available()) return ps2->peek();
    return -1;
  }

private:
  PS2Keyboard* ps2 = nullptr;
};

inline Input input;
