#include "extracker/note_cli.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

namespace extracker {
namespace {

bool parseStrictIntToken(const std::string& token, int& outValue) {
  std::istringstream parse(token);
  parse >> outValue;
  return static_cast<bool>(parse) && parse.eof();
}

bool parseOptionalDryAndLeadingInt(std::istringstream& input, bool& dryRun, int& firstValue) {
  std::string firstArg;
  if (!(input >> firstArg)) {
    return false;
  }

  dryRun = false;
  if (firstArg == "dry") {
    dryRun = true;
    if (!(input >> firstArg)) {
      return false;
    }
  }

  return parseStrictIntToken(firstArg, firstValue);
}

bool hasTrailingTokens(std::istringstream& input) {
  return !input.eof();
}

}  // namespace

void handleNoteCommand(PatternEditor& editor,
                       std::mutex& stateMutex,
                       std::istringstream& noteInput) {
  std::string subcommand;
  noteInput >> subcommand;
  auto printNoteUsage = [&](const std::string& usage) {
    std::cout << usage << '\n';
    std::cout << "exTracker> " << std::flush;
  };

  auto parseDryLeadingInt = [&](const std::string& usage, bool& dryRun, int& firstValue) {
    if (!parseOptionalDryAndLeadingInt(noteInput, dryRun, firstValue)) {
      printNoteUsage(usage);
      return false;
    }

    return true;
  };

  auto ensureNoTrailing = [&](const std::string& usage) {
    if (hasTrailingTokens(noteInput)) {
      printNoteUsage(usage);
      return false;
    }
    return true;
  };

  if (subcommand == "set") {
    const std::string usage = "Usage: note set [dry] <row> <ch> <midi> <instr> [vel] [fx] [fxval]";
    bool dryRun = false;
    int row = -1;
    if (!parseDryLeadingInt(usage, dryRun, row)) {
      return;
    }

    int channel = -1;
    int midi = -1;
    int instrument = -1;
    int velocity = 100;
    int effectCommand = 0;
    int effectValue = 0;

    if (!(noteInput >> channel >> midi >> instrument)) {
      std::cout << usage << '\n';
    } else {
      if (noteInput >> velocity) {
        if (noteInput >> effectCommand) {
          if (!(noteInput >> effectValue)) {
            printNoteUsage(usage);
            return;
          }
        }
      }

      if (!ensureNoTrailing(usage)) {
        return;
      }

      if (dryRun) {
        std::cout << "Note set dry-run: row " << row
                  << ", channel " << channel
                  << ", note " << midi
                  << ", instr " << std::clamp(instrument, 0, 255)
                  << ", vel " << std::clamp(velocity, 1, 127)
                  << ", fx " << std::clamp(effectCommand, 0, 255)
                  << ":" << std::clamp(effectValue, 0, 255)
                  << '\n';
      } else {
        std::lock_guard<std::mutex> lock(stateMutex);
        editor.insertNote(
            row,
            channel,
            midi,
            static_cast<std::uint8_t>(std::clamp(instrument, 0, 255)),
            0,
            static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)),
            false,
            static_cast<std::uint8_t>(std::clamp(effectCommand, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(effectValue, 0, 255)));
        std::cout << "Note set at row " << row << ", channel " << channel << '\n';
      }
    }
  } else if (subcommand == "clear") {
    const std::string usage = "Usage: note clear [dry] <row> <ch>";
    bool dryRun = false;
    int row = -1;
    if (!parseDryLeadingInt(usage, dryRun, row)) {
      return;
    }

    int channel = -1;

    if (!(noteInput >> channel)) {
      std::cout << usage << '\n';
    } else {
      if (!ensureNoTrailing(usage)) {
        return;
      }

      if (dryRun) {
        std::cout << "Note clear dry-run: row " << row << ", channel " << channel << '\n';
      } else {
        std::lock_guard<std::mutex> lock(stateMutex);
        editor.clearStep(row, channel);
        std::cout << "Cleared row " << row << ", channel " << channel << '\n';
      }
    }
  } else if (subcommand == "vel") {
    const std::string usage = "Usage: note vel [dry] <row> <ch> <vel>";
    bool dryRun = false;
    int row = -1;
    if (!parseDryLeadingInt(usage, dryRun, row)) {
      return;
    }

    int channel = -1;
    int velocity = -1;

    if (!(noteInput >> channel >> velocity)) {
      std::cout << usage << '\n';
    } else {
      if (!ensureNoTrailing(usage)) {
        return;
      }

      if (dryRun) {
        std::cout << "Note vel dry-run: row " << row
                  << ", channel " << channel
                  << ", vel " << std::clamp(velocity, 1, 127)
                  << '\n';
      } else {
        std::lock_guard<std::mutex> lock(stateMutex);
        editor.setVelocity(row, channel, static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)));
        std::cout << "Velocity set at row " << row << ", channel " << channel << '\n';
      }
    }
  } else if (subcommand == "gate") {
    const std::string usage = "Usage: note gate [dry] <row> <ch> <ticks>";
    bool dryRun = false;
    int row = -1;
    if (!parseDryLeadingInt(usage, dryRun, row)) {
      return;
    }

    int channel = -1;
    int gateTicks = -1;

    if (!(noteInput >> channel >> gateTicks)) {
      std::cout << usage << '\n';
    } else {
      if (!ensureNoTrailing(usage)) {
        return;
      }

      if (dryRun) {
        std::cout << "Note gate dry-run: row " << row
                  << ", channel " << channel
                  << ", ticks " << std::max(gateTicks, 0)
                  << '\n';
      } else {
        std::lock_guard<std::mutex> lock(stateMutex);
        editor.setGateTicks(row, channel, static_cast<std::uint32_t>(std::max(gateTicks, 0)));
        std::cout << "Gate ticks set at row " << row << ", channel " << channel << '\n';
      }
    }
  } else if (subcommand == "fx") {
    const std::string usage = "Usage: note fx [dry] <row> <ch> <fx> <fxval>";
    bool dryRun = false;
    int row = -1;
    if (!parseDryLeadingInt(usage, dryRun, row)) {
      return;
    }

    int channel = -1;
    int fx = -1;
    int fxValue = -1;

    if (!(noteInput >> channel >> fx >> fxValue)) {
      std::cout << usage << '\n';
    } else {
      if (!ensureNoTrailing(usage)) {
        return;
      }

      if (dryRun) {
        std::cout << "Note fx dry-run: row " << row
                  << ", channel " << channel
                  << ", fx " << std::clamp(fx, 0, 255)
                  << ":" << std::clamp(fxValue, 0, 255)
                  << '\n';
      } else {
        std::lock_guard<std::mutex> lock(stateMutex);
        editor.setEffect(
            row,
            channel,
            static_cast<std::uint8_t>(std::clamp(fx, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(fxValue, 0, 255)));
        std::cout << "Effect set at row " << row << ", channel " << channel << '\n';
      }
    }
  } else {
    std::cout << "Usage: note <set|clear|vel|gate|fx> ..." << '\n';
  }
}

}  // namespace extracker
