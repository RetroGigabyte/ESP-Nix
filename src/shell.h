#pragma once

#include <Arduino.h>
#include <string>
#include <vector>
#include "filesystem.h"
#include "terminal.h"
#include "commands.h"
#include "parser.h"
#include "input.h"

class Shell {
private:
  FileSystem& fs;
  Terminal& term;
  Commands commands;
  Variables* varsPtr;
  String inputBuffer;
  bool running = true;
  std::vector<String> history;
  static const int MAX_HISTORY = 30;
  int historyIndex = -1;  // -1 = not currently browsing history

public:
  Shell(FileSystem& f, Terminal& t, Variables& v)
    : fs(f), term(t), commands(f, t), varsPtr(&v) {
    inputBuffer.reserve(256);

    // Gives Commands (and anything it starts, like the browser terminal)
    // a way to run a line through full shell processing - variable
    // assignment, $VAR expansion, pipes, redirection, && || ; - instead
    // of just dispatching a single bare command.
    commands.setFullExecutor([this](const String& line) { return processLine(line); });
  }

  // Runs one line exactly like the interactive prompt would: variable
  // assignment/export, then the full pipeline/operator engine. Returns
  // false only for "exit" (matches run()'s loop-continuation convention).
  bool processLine(const String& line) {
    if (line.isEmpty()) return true;
    if (handleVariableCommand(line)) return true;
    return executeCommand(line);
  }

  void setVariables(Variables* v) {
    varsPtr = v;
    commands.setVariables(v);
  }

  // Applies /etc/settings/esp-nix.conf if it exists. Called once at boot, and
  // again on demand via the "nixos-rebuild" command.
  bool applyBootConfig() {
    return commands.applyConfig();
  }

  // Syncs the clock over WiFi at boot, if a network is already saved.
  bool syncBootTime() {
    return commands.syncTimeAtBoot();
  }

  // Runs every *.sh script in /boot at startup, in alphabetical order.
  void runBootScripts() {
    commands.runBootScripts();
  }

  void init() {
    term.println("ESP-Nix 0.7.2");
    term.println("Type 'help' for command list\n");
  }

  void run() {
    running = true;

    while (running) {
      showPrompt();
      readCommand();

      if (!inputBuffer.isEmpty()) {
        pushHistory(inputBuffer);
        if (!processLine(inputBuffer)) {
          running = false;
        }
      }

      inputBuffer = "";
      delay(50);
    }
  }

  bool handleVariableCommand(const String& input) {
    // Check for export VAR=value
    if (input.startsWith("export ")) {
      String rest = input.substring(7);
      int eqPos = rest.indexOf('=');
      if (eqPos > 0) {
        String name = rest.substring(0, eqPos);
        String value = rest.substring(eqPos + 1);
        name.trim();
        value.trim();
        value = varsPtr->expand(value);
        varsPtr->set(name, value);
        term.println("export " + name + "=" + value);
        return true;
      }
    }

    // Check for VAR=value (without export)
    int eqPos = input.indexOf('=');
    if (eqPos > 0 && eqPos < 30) {  // Reasonable variable name length
      String name = input.substring(0, eqPos);
      name.trim();

      // Simple check if it looks like a variable assignment
      bool validVarName = true;
      if (name.isEmpty() || !(isAlpha(name[0]) || name[0] == '_')) {
        validVarName = false;
      }
      for (char c : name) {
        if (!(isAlphaNumeric(c) || c == '_')) {
          validVarName = false;
          break;
        }
      }

      if (validVarName && name.indexOf(' ') < 0) {
        String value = input.substring(eqPos + 1);
        value.trim();
        value = varsPtr->expand(value);
        varsPtr->set(name, value);
        term.println(name + "=" + value);
        return true;
      }
    }

    return false;
  }

  bool executeCommand(String input) {
    // Expand variables in the input
    if (varsPtr) {
      input = varsPtr->expand(input);
    }

    CommandSequence seq = Parser::parse(input);

    // Execute pipelines with && || ; operators between them
    bool lastResult = true;
    for (size_t i = 0; i < seq.pipelines.size(); i++) {
      if (i > 0) {
        String op = seq.operators[i - 1];
        if (op == "&&" && !lastResult) break;
        if (op == "||" && lastResult) continue;
      }

      lastResult = executePipeline(seq.pipelines[i]);
    }

    return lastResult;
  }

  bool executePipeline(Pipeline& pipe) {
    if (pipe.commands.empty()) return true;

    String stdinContent = "";
    bool hasStdin = false;

    // Input redirection (<) feeds the first command's stdin
    if (pipe.redirectIn.length() > 0) {
      String path = fs.resolvePath(pipe.redirectIn);
      if (!fs.exists(path)) {
        term.println("File not found: " + pipe.redirectIn);
        return false;
      }
      stdinContent = fs.readFile(path);
      hasStdin = true;
    }

    bool result = true;

    for (size_t i = 0; i < pipe.commands.size(); i++) {
      Command& c = pipe.commands[i];
      bool isLast = (i == pipe.commands.size() - 1);
      bool needsCapture = !isLast || pipe.redirectOut.length() > 0;

      commands.setStdinBuffer(hasStdin ? &stdinContent : nullptr);

      String captured = "";
      commands.setCaptureBuffer(needsCapture ? &captured : nullptr);

      result = commands.executeParsed(c.cmd, c.args);

      commands.setStdinBuffer(nullptr);
      commands.setCaptureBuffer(nullptr);

      if (needsCapture) {
        stdinContent = captured;
        hasStdin = true;
      }
    }

    // Output redirection (> or >>) on the last command in the pipeline
    if (pipe.redirectOut.length() > 0) {
      String path = fs.resolvePath(pipe.redirectOut);
      String finalContent = stdinContent;

      if (pipe.redirectAppend && fs.exists(path)) {
        String existing = fs.readFile(path);
        if (existing.length() > 0 && !existing.endsWith("\n")) {
          existing += "\n";
        }
        finalContent = existing + finalContent;
      }

      if (finalContent.endsWith("\n")) {
        finalContent.remove(finalContent.length() - 1);
      }

      fs.writeFile(path, finalContent);
    }

    return result;
  }

  void showPrompt() {
    String prompt = "nix:";
    String cwd = fs.getCurrentPath();

    if (cwd.length() > 20) {
      cwd = "..." + cwd.substring(cwd.length() - 17);
    }

    prompt += cwd + "$ ";
    Serial.print(prompt);

    // Update LCD with directory
    term.setDirectory(cwd);
  }

  void readCommand() {
    unsigned long timeout = millis() + 60000;  // 60 second timeout
    historyIndex = -1;

    while (millis() < timeout) {
      if (input.available()) {
        char c = input.read();

        if (c == '\r' || c == '\n') {
          if (c == '\r') {
            delay(5);
            if (input.available() && input.peek() == '\n') {
              input.read();
            }
          }
          Serial.println();
          term.setInput("");
          return;
        } else if (c == 27) {  // ESC - possible arrow key sequence
          delay(5);
          if (input.available() && input.read() == '[') {
            delay(5);
            if (input.available()) {
              char code = input.read();
              if (code == 'A') {
                navigateHistory(-1);
              } else if (code == 'B') {
                navigateHistory(1);
              }
              // Left/Right (C/D) intentionally ignored - no cursor editing
            }
          }
        } else if (c == '\t') {  // Tab completion
          handleTabComplete();
        } else if (c == 8 || c == 127) {  // Backspace
          if (inputBuffer.length() > 0) {
            inputBuffer.remove(inputBuffer.length() - 1);
            Serial.write(8);
            Serial.print(" ");
            Serial.write(8);
            term.setInput(inputBuffer);
          }
        } else if (c >= 32 && c < 127) {  // Printable ASCII
          if (inputBuffer.length() < 255) {
            inputBuffer += c;
            Serial.write(c);
            term.setInput(inputBuffer);
          }
        }
      }

      delay(10);
    }

    Serial.println("(timeout)");
  }

  void redrawInputLine(const String& oldValue) {
    for (size_t i = 0; i < oldValue.length(); i++) {
      Serial.write(8);
      Serial.print(" ");
      Serial.write(8);
    }
    Serial.print(inputBuffer);
    term.setInput(inputBuffer);
  }

  void navigateHistory(int direction) {
    if (history.empty()) return;

    String oldValue = inputBuffer;

    if (historyIndex == -1) {
      historyIndex = (int)history.size();
    }

    historyIndex += direction;

    if (historyIndex < 0) {
      historyIndex = 0;
    }

    if (historyIndex >= (int)history.size()) {
      historyIndex = (int)history.size();
      inputBuffer = "";
    } else {
      inputBuffer = history[historyIndex];
    }

    redrawInputLine(oldValue);
  }

  void pushHistory(const String& cmd) {
    if (cmd.length() == 0) return;
    if (!history.empty() && history.back() == cmd) return;

    history.push_back(cmd);
    if (history.size() > MAX_HISTORY) {
      history.erase(history.begin());
    }
  }

  void handleTabComplete() {
    int lastSpace = inputBuffer.lastIndexOf(' ');
    String prefix = (lastSpace >= 0) ? inputBuffer.substring(lastSpace + 1) : inputBuffer;

    std::vector<String> candidates;

    if (lastSpace < 0) {
      for (const auto& name : Commands::commandNames()) {
        if (name.startsWith(prefix)) candidates.push_back(name);
      }

      // /system/*.sh scripts are runnable by name - offer them too
      if (fs.exists("/system") && fs.isDir("/system")) {
        for (const auto& f : fs.listDir("/system")) {
          if (f.endsWith(".sh")) {
            String name = f.substring(0, f.length() - 3);
            if (name.startsWith(prefix)) candidates.push_back(name);
          }
        }
      }
    } else {
      auto files = fs.listDir(fs.getCurrentPath());
      for (const auto& f : files) {
        if (f.startsWith(prefix)) candidates.push_back(f);
      }
    }

    if (candidates.size() == 1) {
      String completion = candidates[0].substring(prefix.length());
      inputBuffer += completion;
      Serial.print(completion);
      term.setInput(inputBuffer);
    } else if (candidates.size() > 1) {
      Serial.println();
      for (const auto& c : candidates) {
        Serial.print(c);
        Serial.print("  ");
      }
      Serial.println();
      showPrompt();
      Serial.print(inputBuffer);
    }
  }

  void stop() {
    running = false;
  }

  bool isRunning() const {
    return running;
  }
};
