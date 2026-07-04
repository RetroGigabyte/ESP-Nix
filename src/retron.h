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
  bool quit = false;
  String scriptDir = "/";  // directory the current script was loaded from, for LOAD's relative paths

  RetronInterpreter(FileSystem& f, Terminal& t) : fs(f), term(t) {}

  bool loadScript(const String& path) {
    String content = fs.readFile(path);
    if (content.length() == 0) {
      term.println("retron: file not found or empty: " + path);
      return false;
    }

    int slash = path.lastIndexOf('/');
    scriptDir = (slash >= 0) ? path.substring(0, slash + 1) : "/";

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
    quit = false;
    while (pc < (int)lines.size() && !quit) {
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

  // A '/' is ambiguous in this language: it's both the division operator
  // and every variable's prefix (e.g. "/a"). Finds the position of a
  // genuine division operator, distinguishing it from a variable's
  // leading slash by looking at the nearest non-space character before
  // it - a real division op follows a completed value (alphanumeric or
  // ')'), while a variable's own slash follows the start of the
  // expression or another operator. Returns -1 if there's no real
  // division operator (every slash found is a variable prefix).
  int findDivisionOp(const String& e) {
    for (int i = 0; i < (int)e.length(); i++) {
      if (e[i] != '/') continue;
      int j = i - 1;
      while (j >= 0 && e[j] == ' ') j--;
      if (j < 0) continue;  // start of expression - variable prefix
      char prev = e[j];
      if (isAlphaNumeric(prev) || prev == ')') return i;
      // prev is an operator or '(' - this slash starts a variable, not
      // a division; keep scanning for a real one.
    }
    return -1;
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

    {
      int pos = e.indexOf('*');
      if (pos >= 0) {
        String left = e.substring(0, pos);
        String right = e.substring(pos + 1);
        return evalExpression(left) * evalExpression(right);
      }
    }

    {
      int pos = findDivisionOp(e);
      if (pos >= 0) {
        String left = e.substring(0, pos);
        String right = e.substring(pos + 1);
        return evalExpression(left) / evalExpression(right);
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
    } else if (cmd == "OPEN") {
      executeOpen(args);
    } else if (cmd == "READ") {
      executeRead(args);
    } else if (cmd == "WRITE") {
      executeWrite(args);
    } else if (cmd == "LOAD") {
      executeLoad(args);
    } else if (cmd == "QUIT") {
      quit = true;
    } else if (lineIn.indexOf('=') >= 0 && lineIn.indexOf('<') < 0 && lineIn.indexOf('>') < 0) {
      executeAssignment(lineIn);
    }
  }

  // OPEN file - reads and prints the whole file, bracketed like the
  // reference implementation.
  void executeOpen(const String& args) {
    String filename = resolveScriptPath(args);
    filename.trim();
    if (!fs.exists(filename)) {
      term.println("ERROR: Cannot open " + args);
      return;
    }
    term.println("--- " + args + " ---");
    term.println(fs.readFile(filename));
    term.println("--- End ---");
  }

  // READ "function_name" runs a defined function inline (like calling
  // it); READ file line# prints one line (1-indexed) from a file.
  void executeRead(const String& args) {
    String a = args;
    a.trim();

    if (a.startsWith("\"") && a.endsWith("\"") && a.length() >= 2) {
      String funcName = a.substring(1, a.length() - 1);
      if (!functions.count(funcName)) {
        term.println("ERROR: Undefined function: " + funcName);
        return;
      }
      RetronFunctionDef def = functions[funcName];
      int savedPc = pc;
      pc = def.start;
      while (pc < def.end) {
        executeLine(lines[pc]);
        pc++;
      }
      pc = savedPc;
      return;
    }

    int lastSpace = a.lastIndexOf(' ');
    if (lastSpace < 0) {
      term.println("ERROR: Usage: READ file line#");
      return;
    }
    String filename = resolveScriptPath(a.substring(0, lastSpace));
    int lineNum = (int)getValue(a.substring(lastSpace + 1)) - 1;  // 1-indexed -> 0-indexed

    if (!fs.exists(filename)) {
      term.println("ERROR: Cannot read " + a.substring(0, lastSpace));
      return;
    }
    std::vector<String> fileLines = splitFileLines(fs.readFile(filename));
    if (lineNum >= 0 && lineNum < (int)fileLines.size()) {
      term.println(fileLines[lineNum]);
    } else {
      term.println("ERROR: Line " + String(lineNum + 1) + " not found in " + a.substring(0, lastSpace));
    }
  }

  // WRITE "text" file OR WRITE /var file - appends a line to a file.
  void executeWrite(const String& args) {
    int lastSpace = args.lastIndexOf(' ');
    if (lastSpace < 0) {
      term.println("ERROR: Usage: WRITE \"text\"|/var file");
      return;
    }
    String contentStr = args.substring(0, lastSpace);
    String filename = resolveScriptPath(args.substring(lastSpace + 1));
    contentStr.trim();

    String text;
    if (contentStr.startsWith("\"") && contentStr.endsWith("\"") && contentStr.length() >= 2) {
      text = contentStr.substring(1, contentStr.length() - 1);
    } else if (contentStr.startsWith("/")) {
      String varName = contentStr.substring(1);
      text = stringVariables.count(varName) ? stringVariables[varName] : String((int)variables[varName]);
    } else {
      text = contentStr;
    }

    String existing = fs.readFile(filename);
    if (existing.length() > 0 && !existing.endsWith("\n")) existing += "\n";
    fs.writeFile(filename, existing + text + "\n");
  }

  // LOAD file.retro - runs a sub-script sharing this interpreter's
  // variables (copied in, then copied back out when it finishes).
  void executeLoad(const String& args) {
    String filename = args;
    filename.trim();
    String resolved = resolveScriptPath(filename);
    if (!fs.exists(resolved) && !filename.endsWith(".retro")) {
      String withExt = resolveScriptPath(filename + ".retro");
      if (fs.exists(withExt)) resolved = withExt;
    }

    term.println("[Loading module: " + filename + "]");
    RetronInterpreter sub(fs, term);
    sub.variables = variables;
    sub.stringVariables = stringVariables;
    if (sub.loadScript(resolved)) {
      sub.execute();
      variables = sub.variables;
      stringVariables = sub.stringVariables;
    }
    term.println("[Module loaded]");
  }

  // Resolves a script-referenced filename against the directory the
  // currently-running script was loaded from, unless it's already
  // absolute - so scripts can reference sibling files by plain name.
  String resolveScriptPath(String p) {
    p.trim();
    if (p.startsWith("/")) return p;
    return scriptDir + p;
  }

  std::vector<String> splitFileLines(const String& content) {
    std::vector<String> result;
    int start = 0;
    for (int i = 0; i <= (int)content.length(); i++) {
      if (i == (int)content.length() || content[i] == '\n') {
        result.push_back(content.substring(start, i));
        start = i + 1;
      }
    }
    return result;
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
    for (int i = 0; i < count && !quit; i++) {
      pc = startPc;
      while (pc < endPc && !quit) {
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
      while (pc < endPc && !quit) {
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
