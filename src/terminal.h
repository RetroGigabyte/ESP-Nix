#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string>
#include <vector>

class Terminal {
private:
  LiquidCrystal_I2C* lcd;
  std::vector<String> outputBuffer;
  String currentInput;
  String currentDir;
  unsigned long lastUpdate = 0;
  static const int MAX_LINES = 100;
  static const int LCD_WIDTH = 16;
  static const int LCD_HEIGHT = 2;
  static const int UPDATE_INTERVAL = 100;  // ms

public:
  Terminal(LiquidCrystal_I2C* l) : lcd(l), currentDir("/") {}

  void init() {
    if (lcd) {
      lcd->init();
      lcd->backlight();
      lcd->clear();
      showBoot();
    }
  }

  void setDirectory(const String& dir) {
    currentDir = dir;
    if (currentDir.length() > 10) {
      currentDir = "..." + currentDir.substring(currentDir.length() - 7);
    }
    updateLCD();
  }

  void setInput(const String& input) {
    currentInput = input;
    updateLCD();
  }

  void print(const String& text) {
    Serial.print(text);

    // Parse text into lines
    String current = "";
    for (char c : text) {
      if (c == '\n') {
        outputBuffer.push_back(current);
        current = "";
      } else if (c != '\r') {
        current += c;
      }
    }

    if (current.length() > 0) {
      outputBuffer.push_back(current);
    }

    // Keep buffer size manageable
    if (outputBuffer.size() > MAX_LINES) {
      outputBuffer.erase(outputBuffer.begin());
    }

    updateLCD(true);
  }

  void println(const String& text) {
    print(text + "\n");
  }

  void clear() {
    outputBuffer.clear();
    currentInput = "";
    if (lcd) {
      lcd->clear();
    }
    // ANSI clear screen + cursor home, so the Serial/browser terminal
    // itself actually clears too, not just our internal state
    Serial.print("\x1b[2J\x1b[H");
  }

private:
  void showBoot() {
    if (!lcd) return;
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print("ESP-Nix Boot");
    lcd->setCursor(0, 1);
    lcd->print("Initializing...");
  }

  void updateLCD(bool force = false) {
    if (!lcd) return;

    // Throttle updates to avoid flicker (but never skip real command output)
    if (!force && millis() - lastUpdate < UPDATE_INTERVAL) {
      return;
    }
    lastUpdate = millis();

    lcd->clear();

    // Line 0: Status bar + last output
    String line0 = "";

    if (!outputBuffer.empty()) {
      line0 = outputBuffer.back();
    }

    if (line0.length() > LCD_WIDTH) {
      line0 = line0.substring(line0.length() - LCD_WIDTH);
    }

    lcd->setCursor(0, 0);
    lcd->print(line0);

    // Pad to fill width
    for (int i = line0.length(); i < LCD_WIDTH; i++) {
      lcd->print(" ");
    }

    // Line 1: Directory + input prompt
    String line1 = currentDir + "$ ";

    if (!currentInput.isEmpty()) {
      line1 += currentInput;
    }

    if (line1.length() > LCD_WIDTH) {
      // Scroll input - show end of line
      line1 = line1.substring(line1.length() - LCD_WIDTH);
    }

    lcd->setCursor(0, 1);
    lcd->print(line1);

    // Pad to fill width
    for (int i = line1.length(); i < LCD_WIDTH; i++) {
      lcd->print(" ");
    }
  }
};
