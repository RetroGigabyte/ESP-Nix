#pragma once

#include <Arduino.h>

struct Variable {
  String name;
  String value;
};

class Variables {
private:
  Variable vars[32];
  int varCount = 0;
  static const int MAX_VARS = 32;

public:
  void set(const String& name, const String& value) {
    // Check if exists
    for (int i = 0; i < varCount; i++) {
      if (vars[i].name == name) {
        vars[i].value = value;
        return;
      }
    }

    // Add new
    if (varCount < MAX_VARS) {
      vars[varCount].name = name;
      vars[varCount].value = value;
      varCount++;
    }
  }

  String get(const String& name) const {
    for (int i = 0; i < varCount; i++) {
      if (vars[i].name == name) {
        return vars[i].value;
      }
    }
    return "";
  }

  bool exists(const String& name) const {
    for (int i = 0; i < varCount; i++) {
      if (vars[i].name == name) {
        return true;
      }
    }
    return false;
  }

  void unset(const String& name) {
    for (int i = 0; i < varCount; i++) {
      if (vars[i].name == name) {
        for (int j = i; j < varCount - 1; j++) {
          vars[j] = vars[j + 1];
        }
        varCount--;
        return;
      }
    }
  }

  // Expand variables in a string (e.g., "Hello $NAME" -> "Hello World")
  String expand(const String& input) const {
    String result = "";
    int i = 0;

    while (i < (int)input.length()) {
      if (input[i] == '$' && i + 1 < (int)input.length()) {
        // Extract variable name
        int j = i + 1;
        while (j < (int)input.length() &&
               (isAlphaNumeric(input[j]) || input[j] == '_')) {
          j++;
        }

        if (j > i + 1) {
          String varName = input.substring(i + 1, j);
          result += get(varName);
          i = j;
        } else {
          result += input[i];
          i++;
        }
      } else {
        result += input[i];
        i++;
      }
    }

    return result;
  }

  void printAll() {
    for (int i = 0; i < varCount; i++) {
      Serial.print(vars[i].name);
      Serial.print("=");
      Serial.println(vars[i].value);
    }
  }
};

// Applies a "VAR=value" or "export VAR=value" line to vars, if the line
// looks like an assignment. Returns true if it was applied. Shared by the
// interactive shell and script/config execution so both support setting
// variables the same way.
inline bool applyVariableAssignment(Variables& vars, const String& inputRaw) {
  String input = inputRaw;

  if (input.startsWith("export ")) {
    String rest = input.substring(7);
    int eqPos = rest.indexOf('=');
    if (eqPos <= 0) return false;

    String name = rest.substring(0, eqPos);
    String value = rest.substring(eqPos + 1);
    name.trim();
    value.trim();
    value = vars.expand(value);
    vars.set(name, value);
    return true;
  }

  int eqPos = input.indexOf('=');
  if (eqPos <= 0 || eqPos >= 30) return false;

  String name = input.substring(0, eqPos);
  name.trim();

  bool validVarName = !name.isEmpty() && (isAlpha(name[0]) || name[0] == '_');
  for (char c : name) {
    if (!(isAlphaNumeric(c) || c == '_')) {
      validVarName = false;
      break;
    }
  }

  if (!validVarName || name.indexOf(' ') >= 0) return false;

  String value = input.substring(eqPos + 1);
  value.trim();
  value = vars.expand(value);
  vars.set(name, value);
  return true;
}
