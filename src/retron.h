#pragma once

#include <Arduino.h>
#include <vector>
#include <map>
#include <cmath>
#include "filesystem.h"
#include "terminal.h"
#include "input.h"
#include "elfloader.h"
#include "hostsymbols.h"
#include "version.h"

// Port of RetroGigabyte/Retron (github.com/RetroGigabyte/Retron) into
// ESP-Nix, adapted from the reference retron_mac.cpp interpreter. Graphics
// (DRAW) depended on that build's SimpleGraphics/SDL - here it errors
// clearly instead, since ESP-Nix has no composite video output yet (see
// goals.md). Everything else - variables, expressions, if/loop/else,
// print with string interpolation, functions - is a faithful port.
struct RetronFunctionDef {
  int start;
  int end;
  std::vector<String> params;
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

  // Local variable scope for function calls (READ "name" arg1 arg2 ...):
  // each call pushes a new frame here, so a function's parameters and
  // any variables it assigns don't leak into (or collide with) the
  // caller's - a real call stack, not just shared globals, which is what
  // makes recursion actually usable rather than merely not-crashing
  // (the underlying pc save/restore already nested correctly via the
  // native C++ call stack even before this, since each executeRead()
  // invocation has its own local savedPc - but every recursive call
  // sharing one flat global variable map meant a function like
  // factorial(n) would corrupt its own /n across recursion levels).
  std::vector<std::map<String, float>> localVarScope;
  std::vector<std::map<String, String>> localStrScope;
  float retval = 0;
  bool returning = false;

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
    stringVariables["version"] = ESP_NIX_VERSION;
    return true;
  }

  // A function definition line is $"name" optionally followed by
  // space-separated parameter names, e.g. $"add" a b - each becomes a
  // local variable bound to the corresponding argument on every call,
  // shadowing any global of the same name for the duration of the call
  // (see localVarScope/localStrScope and the CALL-arg-binding in
  // executeRead below).
  void parseFunctions() {
    for (size_t i = 0; i < lines.size(); i++) {
      if (lines[i].startsWith("$\"")) {
        int endQuote = lines[i].indexOf('"', 2);
        if (endQuote < 0) continue;
        String funcName = lines[i].substring(2, endQuote);

        std::vector<String> params;
        String rest = lines[i].substring(endQuote + 1);
        rest.trim();
        int p = 0;
        while (p < (int)rest.length()) {
          int sp = rest.indexOf(' ', p);
          String tok = (sp >= 0) ? rest.substring(p, sp) : rest.substring(p);
          tok.trim();
          if (tok.length() > 0) params.push_back(tok);
          if (sp < 0) break;
          p = sp + 1;
        }

        int start = i + 1;
        for (size_t j = start; j < lines.size(); j++) {
          if (lines[j] == "$") {
            functions[funcName] = {start, (int)j, params};
            break;
          }
        }
      }
    }
  }

  void execute() {
    pc = 0;
    quit = false;
    returning = false;  // a stray RETURN outside any function is meaningless; don't let it linger
    while (pc < (int)lines.size() && !quit) {
      const String& line = lines[pc];
      if ((line.startsWith("$\"")) || line == "$") {
        pc++;
        continue;
      }
      executeLine(line);
      returning = false;
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
      if (!localVarScope.empty()) {
        if (localStrScope.back().count(varName)) return localStrScope.back()[varName].toFloat();
        if (localVarScope.back().count(varName)) return localVarScope.back()[varName];
      }
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
      String line = readLine();
      if (!localVarScope.empty()) {
        localStrScope.back()[key] = line;
      } else {
        stringVariables[key] = line;
      }
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
    } else if (cmd == "CALL") {
      executeCall(args);
    } else if (cmd == "RETURN") {
      retval = args.length() > 0 ? evalExpression(args) : 0;
      returning = true;
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

  // READ "function_name" [arg1] [arg2] ... calls a defined function
  // inline; READ file line# prints one line (1-indexed) from a file.
  // Each parameter named on the function's $"name" param1 param2 line
  // gets bound to the corresponding argument (evaluated in the CALLER's
  // scope, before the new one is pushed) in a fresh local scope frame -
  // this is what makes recursive calls actually usable rather than just
  // not-crashing, since each call's variables are isolated from every
  // other's rather than all sharing one flat global map. RETURN <expr>
  // inside the function body stores its value where the caller can read
  // it back via the ordinary global variable /retval.
  void executeRead(const String& args) {
    String a = args;
    a.trim();

    if (a.startsWith("\"")) {
      int endQuote = a.indexOf('"', 1);
      if (endQuote < 0) {
        term.println("ERROR: Unterminated function name");
        return;
      }
      String funcName = a.substring(1, endQuote);
      if (!functions.count(funcName)) {
        term.println("ERROR: Undefined function: " + funcName);
        return;
      }
      RetronFunctionDef def = functions[funcName];

      // Argument tokens are evaluated in the CALLER's current scope
      // (whatever's on top of the stack right now, or globals if none)
      // before the new frame is pushed, so a call like
      // READ "helper" /x passes the CALLER's /x, not an as-yet-unbound
      // parameter of the same name in the callee.
      std::vector<float> argValues;
      String rest = a.substring(endQuote + 1);
      rest.trim();
      int p = 0;
      while (p < (int)rest.length()) {
        int sp = rest.indexOf(' ', p);
        String tok = (sp >= 0) ? rest.substring(p, sp) : rest.substring(p);
        tok.trim();
        if (tok.length() > 0) argValues.push_back(getValue(tok));
        if (sp < 0) break;
        p = sp + 1;
      }

      std::map<String, float> frame;
      std::map<String, String> strFrame;
      for (size_t i = 0; i < def.params.size(); i++) {
        frame[def.params[i]] = (i < argValues.size()) ? argValues[i] : 0;
      }
      localVarScope.push_back(frame);
      localStrScope.push_back(strFrame);

      bool savedReturning = returning;
      returning = false;

      int savedPc = pc;
      pc = def.start;
      while (pc < def.end && !returning) {
        executeLine(lines[pc]);
        pc++;
      }
      pc = savedPc;

      localVarScope.pop_back();
      localStrScope.pop_back();
      variables["retval"] = retval;  // readable by the caller as /retval

      returning = savedReturning;
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

  // CALL "file.o" "function" resultVar [arg1] [arg2] ... - loads a
  // compiled .o (the same loader runmod uses - real relocations, real
  // symbol resolution against the firmware whitelist, up to 6 register-
  // sized arguments) and stores its return value into resultVar. Bridges
  // Retron to native code for anything the interpreter itself is slow
  // or awkward at (real arithmetic precision, anything CPU-heavy).
  // resultVar is written as a plain global (not the caller's local
  // scope) for the same reason /retval is: it's meant to be read back
  // immediately after the CALL line, not tied to any function's frame.
  void executeCall(const String& args) {
    String a = args;
    a.trim();
    if (!a.startsWith("\"")) {
      term.println("ERROR: Usage: CALL \"file.o\" \"function\" resultVar [args...]");
      return;
    }

    int fileEnd = a.indexOf('"', 1);
    if (fileEnd < 0) {
      term.println("ERROR: Unterminated file name in CALL");
      return;
    }
    String filePath = resolveScriptPath(a.substring(1, fileEnd));

    String rest = a.substring(fileEnd + 1);
    rest.trim();
    if (!rest.startsWith("\"")) {
      term.println("ERROR: Usage: CALL \"file.o\" \"function\" resultVar [args...]");
      return;
    }
    int funcEnd = rest.indexOf('"', 1);
    if (funcEnd < 0) {
      term.println("ERROR: Unterminated function name in CALL");
      return;
    }
    String funcName = rest.substring(1, funcEnd);

    rest = rest.substring(funcEnd + 1);
    rest.trim();

    std::vector<String> tokens;
    int p = 0;
    while (p < (int)rest.length()) {
      int sp = rest.indexOf(' ', p);
      String tok = (sp >= 0) ? rest.substring(p, sp) : rest.substring(p);
      tok.trim();
      if (tok.length() > 0) tokens.push_back(tok);
      if (sp < 0) break;
      p = sp + 1;
    }

    if (tokens.empty()) {
      term.println("ERROR: Usage: CALL \"file.o\" \"function\" resultVar [args...]");
      return;
    }

    String resultVar = tokens[0];
    if (resultVar.length() > 0 && resultVar[0] == '/') resultVar = resultVar.substring(1);

    std::vector<uint32_t> callArgs;
    for (size_t i = 1; i < tokens.size(); i++) {
      callArgs.push_back((uint32_t)(int32_t)getValue(tokens[i]));
    }

    ElfModule mod(fs, term);
    if (!mod.load({filePath}, kRunmodSymbols)) return;  // load() already printed a specific error

    if (!mod.hasFunction(funcName)) {
      term.println("ERROR: CALL: no such function: " + funcName);
      return;
    }

    uint32_t result;
    if (!mod.call(funcName, callArgs, result)) {
      term.println("ERROR: CALL: call failed");
      return;
    }

    variables[resultVar] = (float)(int32_t)result;
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
        if (!localVarScope.empty() && localStrScope.back().count(varName)) {
          result += localStrScope.back()[varName];
        } else if (!localVarScope.empty() && localVarScope.back().count(varName)) {
          result += String((int)localVarScope.back()[varName]);
        } else if (stringVariables.count(varName)) {
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
    for (int i = 0; i < count && !quit && !returning; i++) {
      pc = startPc;
      while (pc < endPc && !quit && !returning) {
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
      while (pc < endPc && !quit && !returning) {
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
          while (pc < endPc && !returning) {
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

    float val = evalExpression(expr);
    // Inside a function call, assignment targets that call's own local
    // scope (even for a name that also exists globally - shadowing, not
    // overwriting the global) so recursive/nested calls don't stomp on
    // each other's variables of the same name.
    if (!localVarScope.empty()) {
      localVarScope.back()[varName] = val;
    } else {
      variables[varName] = val;
    }
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
