#pragma once

#include <Arduino.h>
#include <vector>
#include <map>
#include <cmath>
#include "filesystem.h"
#include "terminal.h"
#include "input.h"

// Port of RetroGigabyte/Retron (github.com/RetroGigabyte/Retron) into
// ESP-Nix, adapted from the reference retron_mac.cpp interpreter. Graphics
// (DRAW) depended on that build's SimpleGraphics/SDL - here it errors
// clearly instead, since ESP-Nix has no composite video output yet (see
// goals.md). Everything else - variables, expressions, if/loop/else,
// print with string interpolation, functions - is a faithful port.
struct RetronFunctionDef {
  int start;
  int end;
};

class RetronInterpreter {
public:
  std::map<String, float> variables;
  std::map<String, String> stringVariables;  // INPUT results - raw text, not float
  std::vector<String> lines;
  int pc = 0;
  std::map<String, RetronFunctionDef> functions;

  RetronInterpreter(FileSystem& f, Terminal& t) : fs(f), term(t) {}

  bool loadScript(const String& path) {
    String content = fs.readFile(path);
    if (content.length() == 0) {
      term.println("retron: file not found or empty: " + path);
      return false;
    }

    lines.clear();
    int start = 0;
    for (int i = 0; i <= (int)content.length(); i++) {
      if (i == (int)content.length() || content[i] == '\n') {
        String line = content.substring(start, i);
        start = i + 1;

        int hashPos = line.indexOf('#');
        if (hashPos >= 0) line = line.substring(0, hashPos);
        int slashPos = line.indexOf("//");
        if (slashPos >= 0) line = line.substring(0, slashPos);

        line.trim();
        if (line.length() > 0) lines.push_back(line);
      }
    }

    parseFunctions();
    variables["platform"] = 1;  // 1 = ESP32
    return true;
  }

  void parseFunctions() {
    for (size_t i = 0; i < lines.size(); i++) {
      if (lines[i].startsWith("$\"") && lines[i].endsWith("\"")) {
        String funcName = lines[i].substring(2, lines[i].length() - 1);
        int start = i + 1;
        for (size_t j = start; j < lines.size(); j++) {
          if (lines[j] == "$") {
            functions[funcName] = {start, (int)j};
            break;
          }
        }
      }
    }
  }

  void execute() {
    pc = 0;
    while (pc < (int)lines.size()) {
      const String& line = lines[pc];
      if ((line.startsWith("$\"")) || line == "$") {
        pc++;
        continue;
      }
      executeLine(line);
      pc++;
    }
  }

private:
  FileSystem& fs;
  Terminal& term;

  float getValue(String t) {
    t.trim();
    if (t.length() == 0) return 0;

    if (t[0] == '/') {
      String varName = t.substring(1);
      if (stringVariables.count(varName)) return stringVariables[varName].toFloat();
      if (variables.count(varName)) return variables[varName];
      return 0;
    }
    return t.toFloat();
  }

  // Reads one line from Serial/PS2 (whichever has data), same character
  // handling as the shell's own prompt - used by the INPUT command.
  String readLine() {
    String line = "";
    while (true) {
      if (input.available()) {
        char c = input.read();
        if (c == '\r' || c == '\n') {
          if (c == '\r') {
            delay(5);
            if (input.available() && input.peek() == '\n') input.read();
          }
          Serial.println();
          return line;
        } else if (c == 8 || c == 127) {
          if (line.length() > 0) {
            line.remove(line.length() - 1);
            Serial.print("\b \b");
          }
        } else if (c >= 32 && c < 127) {
          line += c;
          Serial.print(c);
        }
      }
      delay(5);
    }
  }

  float evalExpression(String e) {
    e.trim();
    if (e.length() == 0) return 0;

    int powPos = e.indexOf('^');
    if (powPos >= 0 && (powPos == 0 || e[powPos - 1] != '^')) {
      String left = e.substring(0, powPos);
      String right = e.substring(powPos + 1);
      if (right.length() > 0 && right[0] == 'r') {
        right = right.substring(1);
        float val = evalExpression(left);
        float root = getValue(right);
        return pow(val, 1.0f / root);
      }
      float base = evalExpression(left);
      float exp = evalExpression(right);
      return pow(base, exp);
    }

    for (char op : {'*', '/'}) {
      int pos = e.indexOf(op);
      if (pos >= 0) {
        if (op == '/' && pos == 0) {
          pos = e.indexOf('/', 1);
          if (pos < 0) continue;
        }
        String left = e.substring(0, pos);
        String right = e.substring(pos + 1);
        float lVal = evalExpression(left);
        float rVal = evalExpression(right);
        return (op == '*') ? lVal * rVal : lVal / rVal;
      }
    }

    for (char op : {'+', '-'}) {
      int pos = e.lastIndexOf(op);
      if (pos > 0) {
        String left = e.substring(0, pos);
        String right = e.substring(pos + 1);
        float lVal = evalExpression(left);
        float rVal = evalExpression(right);
        return (op == '+') ? lVal + rVal : lVal - rVal;
      }
    }

    return getValue(e);
  }

  void executeLine(const String& lineIn) {
    if (lineIn.length() == 0) return;

    int spacePos = lineIn.indexOf(' ');
    String cmd = (spacePos >= 0) ? lineIn.substring(0, spacePos) : lineIn;
    String args = (spacePos >= 0) ? lineIn.substring(spacePos + 1) : "";
    cmd.toUpperCase();

    if (cmd == "PRINT") {
      term.println(evaluateString(args));
    } else if (cmd == "INPUT") {
      String key = args;
      key.trim();
      stringVariables[key] = readLine();
    } else if (cmd == "DRAW") {
      term.println("retron: DRAW needs composite video output, which isn't wired up on this ESP-Nix build yet - skipping.");
    } else if (cmd == "LOOP") {
      executeLoop((int)getValue(args));
    } else if (cmd == "IF") {
      executeIf(args);
    } else if (lineIn.indexOf('=') >= 0 && lineIn.indexOf('<') < 0 && lineIn.indexOf('>') < 0) {
      executeAssignment(lineIn);
    }
  }

  String evaluateString(const String& str) {
    String result = "";
    int i = 0;
    while (i < (int)str.length()) {
      if (str[i] == '"') {
        int end = str.indexOf('"', i + 1);
        if (end >= 0) {
          result += str.substring(i + 1, end);
          i = end + 1;
        } else {
          i++;
        }
      } else if (str[i] == '&') {
        i++;
      } else if (str[i] == '/') {
        int end = i + 1;
        while (end < (int)str.length() && (isAlphaNumeric(str[end]) || str[end] == '_')) end++;
        String varName = str.substring(i + 1, end);
        if (stringVariables.count(varName)) {
          result += stringVariables[varName];
        } else {
          result += String((int)variables[varName]);
        }
        i = end;
      } else {
        i++;
      }
    }
    return result;
  }

  void executeLoop(int count) {
    int startPc = pc + 1;
    int endPc = findEnd(startPc);
    for (int i = 0; i < count; i++) {
      pc = startPc;
      while (pc < endPc) {
        executeLine(lines[pc]);
        pc++;
      }
    }
    pc = endPc;
  }

  void executeIf(const String& condition) {
    int startPc = pc + 1;
    if (evalCondition(condition)) {
      int endPc = findEnd(startPc);
      pc = startPc;
      while (pc < endPc) {
        String upper = lines[pc];
        upper.toUpperCase();
        if (upper.indexOf("ELSE") >= 0) break;
        executeLine(lines[pc]);
        pc++;
      }
      pc = findEnd(startPc);
    } else {
      pc = startPc;
      while (pc < (int)lines.size()) {
        String upper = lines[pc];
        upper.toUpperCase();
        if (upper.indexOf("ELSE") >= 0) {
          pc++;
          int endPc = findEnd(startPc);
          while (pc < endPc) {
            if (lines[pc].indexOf("END") >= 0) break;
            executeLine(lines[pc]);
            pc++;
          }
          pc = endPc;
          return;
        } else if (upper.indexOf("END") >= 0 || lines[pc] == "@!") {
          return;
        }
        pc++;
      }
    }
  }

  void executeAssignment(const String& lineIn) {
    int eqPos = lineIn.indexOf('=');
    String varName = lineIn.substring(0, eqPos);
    String expr = lineIn.substring(eqPos + 1);

    varName.trim();
    expr.trim();

    if (varName.length() > 0 && varName[0] == '/') {
      varName = varName.substring(1);
    }

    variables[varName] = evalExpression(expr);
  }

  bool evalCondition(const String& condition) {
    static const char* ops[] = {"==", "!=", "<=", ">=", "<", ">", "="};
    for (const char* op : ops) {
      int pos = condition.indexOf(op);
      if (pos >= 0) {
        String left = condition.substring(0, pos);
        String right = condition.substring(pos + strlen(op));
        float lNum = evalExpression(left);
        float rNum = evalExpression(right);

        String o(op);
        if (o == "<") return lNum < rNum;
        if (o == ">") return lNum > rNum;
        if (o == "<=") return lNum <= rNum;
        if (o == ">=") return lNum >= rNum;
        if (o == "==" || o == "=") return lNum == rNum;
        if (o == "!=") return lNum != rNum;
      }
    }
    return false;
  }

  int findEnd(int start) {
    int depth = 1;
    for (size_t i = start; i < lines.size(); i++) {
      String upper = lines[i];
      upper.toUpperCase();
      if (upper.indexOf("IF") >= 0 || upper.indexOf("LOOP") >= 0) {
        depth++;
      } else if (upper.indexOf("END") >= 0 || lines[i] == "@!") {
        depth--;
        if (depth == 0) return i;
      }
    }
    return lines.size() - 1;
  }
};
