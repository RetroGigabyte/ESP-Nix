#pragma once

#include <Arduino.h>
#include <vector>
#include <string>

struct Command {
  String cmd;
  std::vector<String> args;
};

struct Pipeline {
  std::vector<Command> commands;
  String redirectOut;      // > file or >> file
  bool redirectAppend = false;
  String redirectIn;       // < file
};

struct CommandSequence {
  std::vector<Pipeline> pipelines;
  std::vector<String> operators;  // &&, ||, ;
};

class Parser {
public:
  static CommandSequence parse(const String& input) {
    CommandSequence seq;

    // Split by operators (&&, ||, ;)
    std::vector<String> parts = splitByOperators(input, seq.operators);

    for (const auto& part : parts) {
      seq.pipelines.push_back(parsePipeline(part));
    }

    return seq;
  }

private:
  static std::vector<String> splitByOperators(const String& input,
                                               std::vector<String>& ops) {
    std::vector<String> parts;
    String current = "";

    for (int i = 0; i < (int)input.length(); i++) {
      if (i + 1 < (int)input.length()) {
        String twoChar = input.substring(i, i + 2);

        if (twoChar == "&&" || twoChar == "||") {
          if (current.length() > 0) {
            parts.push_back(current);
            current = "";
          }
          ops.push_back(twoChar);
          i++;
          continue;
        }
      }

      if (input[i] == ';') {
        if (current.length() > 0) {
          parts.push_back(current);
          current = "";
        }
        ops.push_back(";");
      } else {
        current += input[i];
      }
    }

    if (current.length() > 0) {
      parts.push_back(current);
    }

    return parts;
  }

  static Pipeline parsePipeline(const String& input) {
    Pipeline pipe;

    // Split by pipe |
    std::vector<String> cmdParts = splitByChar(input, '|');

    // Parse redirection from last command
    if (!cmdParts.empty()) {
      String lastCmd = cmdParts.back();
      int redirectPos = lastCmd.indexOf('>');

      if (redirectPos >= 0) {
        String beforeRedirect = lastCmd.substring(0, redirectPos);
        String afterRedirect = lastCmd.substring(redirectPos + 1);

        if (afterRedirect.startsWith(">")) {
          pipe.redirectAppend = true;
          afterRedirect = afterRedirect.substring(1);
        }

        cmdParts.back() = beforeRedirect;
        pipe.redirectOut = afterRedirect;
        pipe.redirectOut.trim();
      }
    }

    // Check for input redirection
    if (!cmdParts.empty()) {
      String firstCmd = cmdParts[0];
      int redirectPos = firstCmd.indexOf('<');

      if (redirectPos >= 0) {
        String beforeRedirect = firstCmd.substring(0, redirectPos);
        String afterRedirect = firstCmd.substring(redirectPos + 1);

        cmdParts[0] = beforeRedirect;
        pipe.redirectIn = afterRedirect;
        pipe.redirectIn.trim();
      }
    }

    // Parse individual commands
    for (const auto& cmdStr : cmdParts) {
      String trimmed = cmdStr;
      trimmed.trim();

      if (trimmed.isEmpty()) continue;

      std::vector<String> parts = parseArgs(trimmed);
      if (!parts.empty()) {
        Command cmd;
        cmd.cmd = parts[0];
        cmd.cmd.toLowerCase();

        for (size_t i = 1; i < parts.size(); i++) {
          cmd.args.push_back(parts[i]);
        }

        pipe.commands.push_back(cmd);
      }
    }

    return pipe;
  }

  static std::vector<String> splitByChar(const String& input, char sep) {
    std::vector<String> parts;
    String current = "";

    for (char c : input) {
      if (c == sep) {
        parts.push_back(current);
        current = "";
      } else {
        current += c;
      }
    }

    parts.push_back(current);
    return parts;
  }

  static std::vector<String> parseArgs(const String& input) {
    std::vector<String> args;
    String current = "";
    bool inQuote = false;

    for (char c : input) {
      if (c == '"') {
        inQuote = !inQuote;
      } else if ((c == ' ' || c == '\t') && !inQuote) {
        if (current.length() > 0) {
          args.push_back(current);
          current = "";
        }
      } else {
        current += c;
      }
    }

    if (current.length() > 0) {
      args.push_back(current);
    }

    return args;
  }
};
