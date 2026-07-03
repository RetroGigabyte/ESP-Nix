#pragma once

#include <Arduino.h>

// Minimal PS/2 keyboard driver (Scan Code Set 2) using an interrupt on the
// clock line. Decodes common keys (letters, digits, punctuation, space,
// enter, backspace, tab, and arrow keys) into a plain char stream that can
// be read the same way as Serial input. Arrow keys are emitted as the
// same 3-byte ANSI escape sequences (ESC [ A/B/C/D) the shell already
// understands for history navigation.
class PS2Keyboard {
public:
  void begin(int clockPin, int dataPin) {
    _clockPin = clockPin;
    _dataPin = dataPin;
    pinMode(_clockPin, INPUT_PULLUP);
    pinMode(_dataPin, INPUT_PULLUP);
    instance = this;
    attachInterrupt(digitalPinToInterrupt(_clockPin), isrTrampoline, FALLING);
  }

  bool available() {
    processQueue();
    return charAvailable;
  }

  int read() {
    processQueue();
    if (!charAvailable) return -1;
    charAvailable = false;
    return pendingChar;
  }

  int peek() {
    processQueue();
    if (!charAvailable) return -1;
    return pendingChar;
  }

private:
  static inline PS2Keyboard* instance = nullptr;

  int _clockPin = -1;
  int _dataPin = -1;

  static const uint8_t RAW_BUF_SIZE = 32;
  volatile uint8_t rawBuffer[RAW_BUF_SIZE];
  volatile uint8_t rawHead = 0;
  volatile uint8_t rawTail = 0;

  volatile uint8_t bitCount = 0;
  volatile uint16_t currentFrame = 0;
  volatile unsigned long lastBitTime = 0;

  bool shiftActive = false;
  bool extendedFlag = false;
  bool breakFlag = false;

  static const uint8_t DEC_BUF_SIZE = 16;
  char decodedQueue[DEC_BUF_SIZE];
  uint8_t decHead = 0;
  uint8_t decTail = 0;

  bool charAvailable = false;
  char pendingChar = 0;

  static void IRAM_ATTR isrTrampoline() {
    if (instance) instance->isr();
  }

  void IRAM_ATTR isr() {
    unsigned long now = micros();

    // If too long since the last clock edge, a new frame is starting
    if (now - lastBitTime > 1000) {
      bitCount = 0;
      currentFrame = 0;
    }
    lastBitTime = now;

    int dataBit = digitalRead(_dataPin);

    // Frame layout: start(0), 8 data bits LSB-first, parity, stop(1)
    if (bitCount == 0) {
      if (dataBit != 0) return;  // not a valid start bit, wait for next
      bitCount = 1;
      return;
    }

    if (bitCount <= 8) {
      if (dataBit) currentFrame |= (1 << (bitCount - 1));
      bitCount++;
      return;
    }

    if (bitCount == 9) {
      // parity bit - not validated
      bitCount++;
      return;
    }

    // bitCount == 10: stop bit, frame complete
    uint8_t nextHead = (rawHead + 1) % RAW_BUF_SIZE;
    if (nextHead != rawTail) {
      rawBuffer[rawHead] = currentFrame & 0xFF;
      rawHead = nextHead;
    }
    bitCount = 0;
    currentFrame = 0;
  }

  void processQueue() {
    while (rawTail != rawHead) {
      noInterrupts();
      uint8_t code = rawBuffer[rawTail];
      rawTail = (rawTail + 1) % RAW_BUF_SIZE;
      interrupts();

      handleScancode(code);
    }

    if (!charAvailable && decHead != decTail) {
      pendingChar = decodedQueue[decTail];
      decTail = (decTail + 1) % DEC_BUF_SIZE;
      charAvailable = true;
    }
  }

  void pushDecoded(char c) {
    uint8_t next = (decHead + 1) % DEC_BUF_SIZE;
    if (next == decTail) return;  // queue full, drop
    decodedQueue[decHead] = c;
    decHead = next;
  }

  void handleScancode(uint8_t code) {
    if (code == 0xF0) { breakFlag = true; return; }
    if (code == 0xE0) { extendedFlag = true; return; }

    bool isBreak = breakFlag;
    bool isExtended = extendedFlag;
    breakFlag = false;
    extendedFlag = false;

    if (code == 0x12 || code == 0x59) {  // left/right shift
      shiftActive = !isBreak;
      return;
    }

    if (isBreak) return;  // only handle key-down for simplicity

    if (isExtended) {
      switch (code) {
        case 0x75: pushDecoded(27); pushDecoded('['); pushDecoded('A'); return;  // up
        case 0x72: pushDecoded(27); pushDecoded('['); pushDecoded('B'); return;  // down
        case 0x74: pushDecoded(27); pushDecoded('['); pushDecoded('C'); return;  // right
        case 0x6B: pushDecoded(27); pushDecoded('['); pushDecoded('D'); return;  // left
        case 0x5A: pushDecoded('\n'); return;  // keypad enter
        default: return;
      }
    }

    char c = scancodeToAscii(code, shiftActive);
    if (c != 0) pushDecoded(c);
  }

  char scancodeToAscii(uint8_t code, bool shift) {
    switch (code) {
      case 0x1C: return shift ? 'A' : 'a';
      case 0x32: return shift ? 'B' : 'b';
      case 0x21: return shift ? 'C' : 'c';
      case 0x23: return shift ? 'D' : 'd';
      case 0x24: return shift ? 'E' : 'e';
      case 0x2B: return shift ? 'F' : 'f';
      case 0x34: return shift ? 'G' : 'g';
      case 0x33: return shift ? 'H' : 'h';
      case 0x43: return shift ? 'I' : 'i';
      case 0x3B: return shift ? 'J' : 'j';
      case 0x42: return shift ? 'K' : 'k';
      case 0x4B: return shift ? 'L' : 'l';
      case 0x3A: return shift ? 'M' : 'm';
      case 0x31: return shift ? 'N' : 'n';
      case 0x44: return shift ? 'O' : 'o';
      case 0x4D: return shift ? 'P' : 'p';
      case 0x15: return shift ? 'Q' : 'q';
      case 0x2D: return shift ? 'R' : 'r';
      case 0x1B: return shift ? 'S' : 's';
      case 0x2C: return shift ? 'T' : 't';
      case 0x3C: return shift ? 'U' : 'u';
      case 0x2A: return shift ? 'V' : 'v';
      case 0x1D: return shift ? 'W' : 'w';
      case 0x22: return shift ? 'X' : 'x';
      case 0x35: return shift ? 'Y' : 'y';
      case 0x1A: return shift ? 'Z' : 'z';

      case 0x45: return shift ? ')' : '0';
      case 0x16: return shift ? '!' : '1';
      case 0x1E: return shift ? '@' : '2';
      case 0x26: return shift ? '#' : '3';
      case 0x25: return shift ? '$' : '4';
      case 0x2E: return shift ? '%' : '5';
      case 0x36: return shift ? '^' : '6';
      case 0x3D: return shift ? '&' : '7';
      case 0x3E: return shift ? '*' : '8';
      case 0x46: return shift ? '(' : '9';

      case 0x29: return ' ';   // space
      case 0x5A: return '\n';  // enter
      case 0x66: return 8;     // backspace
      case 0x0D: return '\t';  // tab

      case 0x4E: return shift ? '_' : '-';
      case 0x55: return shift ? '+' : '=';
      case 0x4C: return shift ? ':' : ';';
      case 0x52: return shift ? '"' : '\'';
      case 0x41: return shift ? '<' : ',';
      case 0x49: return shift ? '>' : '.';
      case 0x4A: return shift ? '?' : '/';
      case 0x54: return shift ? '{' : '[';
      case 0x5B: return shift ? '}' : ']';
      case 0x5D: return shift ? '|' : '\\';
      case 0x0E: return shift ? '~' : '`';

      default: return 0;
    }
  }
};
