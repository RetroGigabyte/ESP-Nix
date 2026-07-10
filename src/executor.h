#pragma once

#include <Arduino.h>
#include <vector>
#include "parser.h"
#include "filesystem.h"
#include "terminal.h"
#include "commands.h"
#include "variables.h"

class OutputCapture {
public:
  static String buffer;
  static bool capturing;

  static void start() {
    buffer = "";
    capturing = true;
  }

  static void stop() {
    capturing = false;
  }

  static void write(const String& text) {
    if (capturing) {
      buffer += text;
    }
  }

  static String getBuffer() {
    return buffer;
  }
};

String OutputCapture::buffer = "";
bool OutputCapture::capturing = false;

class Executor {
private:
  FileSystem& fs;
  Terminal& term;
  Commands& commands;
  Variables& vars;

public:
  Executor(FileSystem& f, Terminal& t, Commands& c, Variables& v)
    : fs(f), term(t), commands(c), vars(v) {}

  bool execute(const String& input) {
    CommandSequence seq = Parser::parse(input);

    // Handle variable assignment
    if (handleVariableAssignment(input)) {
      return true;
    }

    // Execute sequence of pipelines
    bool lastResult = true;

    for (size_t i = 0; i < seq.pipelines.size(); i++) {
      // Check operator before executing
      if (i > 0) {
        String op = seq.operators[i - 1];

        if (op == "&&" && !lastResult) {
          break;  // Short-circuit on &&
        }

        if (op == "||" && lastResult) {
          continue;  // Skip on ||
        }

        if (op == ";") {
          // Always execute
        }
      }

      lastResult = executePipeline(seq.pipelines[i]);
    }

    return lastResult;
  }

private:
  bool handleVariableAssignment(const String& input) {
    // Check for NAME=VALUE pattern
    int eqPos = input.indexOf('=');
    if (eqPos <= 0) return false;

    String name = input.substring(0, eqPos);
    name.trim();

    // Check if it's a valid variable name
    if (name.isEmpty() || !isValidVarName(name)) {
      return false;
    }

    // Check if it's export
    bool isExport = false;
    if (name.startsWith("export ")) {
      name = name.substring(7);
      name.trim();
      isExport = true;
    }

    if (!isValidVarName(name)) {
      return false;
    }

    String value = input.substring(eqPos + 1);
    value.trim();
    value = vars.expand(value);

    vars.set(name, value);
    term.println("Set: " + name + "=" + value);
    return true;
  }

  bool isValidVarName(const String& name) {
    if (name.isEmpty()) return false;
    if (!isAlpha(name[0]) && name[0] != '_') return false;

    for (char c : name) {
      if (!isAlphaNumeric(c) && c != '_') return false;
    }

    return true;
  }

  bool executePipeline(const Pipeline& pipe) {
    if (pipe.commands.empty()) return true;

    // Handle single command with redirection
    if (pipe.commands.size() == 1) {
      return executeSingleCommand(pipe);
    }

    // Handle piped commands
    return executePipedCommands(pipe);
  }

  bool executeSingleCommand(const Pipeline& pipe) {
    const Command& cmd = pipe.commands[0];

    // Expand variables in arguments
    std::vector<String> expandedArgs;
    for (const auto& arg : cmd.args) {
      expandedArgs.push_back(vars.expand(arg));
    }

    // Handle input redirection
    String inputData = "";
    if (!pipe.redirectIn.isEmpty()) {
      String inputFile = vars.expand(pipe.redirectIn);
      inputFile = fs.resolvePath(inputFile);
      inputData = fs.readFile(inputFile);
    }

    // Capture output if redirecting
    if (!pipe.redirectOut.isEmpty()) {
      OutputCapture::start();
    }

    // Execute command
    std::vector<String> args;
    args.push_back(cmd.cmd);
    for (const auto& arg : expandedArgs) {
      args.push_back(arg);
    }

    bool result = commands.execute(args);

    // Handle output redirection
    if (!pipe.redirectOut.isEmpty()) {
      String output = OutputCapture::getBuffer();
      OutputCapture::stop();

      String outputFile = vars.expand(pipe.redirectOut);
      outputFile = fs.resolvePath(outputFile);

      if (pipe.redirectAppend) {
        String existing = fs.readFile(outputFile);
        output = existing + output;
      }

      fs.writeFile(outputFile, output);
      term.println("Redirected to: " + outputFile);
    } else {
      OutputCapture::stop();
    }

    return result;
  }

  bool executePipedCommands(const Pipeline& pipe) {
    String output = "";

    for (size_t i = 0; i < pipe.commands.size(); i++) {
      const Command& cmd = pipe.commands[i];

      // Expand variables
      std::vector<String> expandedArgs;
      for (const auto& arg : cmd.args) {
        expandedArgs.push_back(vars.expand(arg));
      }

      OutputCapture::start();

      std::vector<String> args;
      args.push_back(cmd.cmd);
      for (const auto& arg : expandedArgs) {
        args.push_back(arg);
      }

      commands.execute(args);
      output = OutputCapture::getBuffer();
      OutputCapture::stop();
    }

    // Handle final redirection
    if (!pipe.redirectOut.isEmpty()) {
      String outputFile = vars.expand(pipe.redirectOut);
      outputFile = fs.resolvePath(outputFile);

      if (pipe.redirectAppend) {
        String existing = fs.readFile(outputFile);
        output = existing + output;
      }

      fs.writeFile(outputFile, output);
      term.println("Redirected to: " + outputFile);
    } else {
      term.print(output);
    }

    return true;
  }
};
