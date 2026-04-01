#include "extracker/record_cli.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

void handleRecordCommand(std::istringstream& recordInput,
                         RecordCommandContext context) {
  auto& editor = context.editor;
  auto& transport = context.transport;
  auto& stateMutex = context.stateMutex;
  auto& recordState = context.recordState;
  auto& recordChannel = context.recordChannel;
  auto& recordCursorRow = context.recordCursorRow;
  auto& recordEnabled = context.recordEnabled;
  auto& recordQuantizeEnabled = context.recordQuantizeEnabled;
  auto& recordOverdubEnabled = context.recordOverdubEnabled;
  auto& recordInsertJump = context.recordInsertJump;
  const auto& midiInstrument = context.midiInstrument;
  const auto& chooseRecordRow = context.chooseRecordRow;
  const auto& applyRecordWrite = context.applyRecordWrite;
  std::string subcommand;
  recordInput >> subcommand;
  const std::string recordNoteUsage =
      "Usage: record note [dry] <midi> [instr] [vel] [fx] [fxval] | record note [dry] <midi> vel <vel> [fx] [fxval] | record note [dry] <midi> fx <fx> <fxval> | record note [dry] <midi> instr <i> [vel <v>] [fx <f> <fv>]";

  auto printRecordNoteUsage = [&]() {
    std::cout << recordNoteUsage << '\n';
    std::cout << "exTracker> " << std::flush;
  };

  auto parseRecordDryMidi = [&](bool& dryRun, int& midiValue) {
    if (!parseOptionalDryAndLeadingInt(recordInput, dryRun, midiValue)) {
      printRecordNoteUsage();
      return false;
    }

    return true;
  };

  auto ensureRecordNoTrailing = [&]() {
    if (hasTrailingTokens(recordInput)) {
      printRecordNoteUsage();
      return false;
    }
    return true;
  };

  if (subcommand == "on") {
    int channel = recordChannel;
    if (recordInput >> channel) {
      channel = std::clamp(channel, 0, static_cast<int>(editor.channels()) - 1);
      recordChannel = channel;
    }
    recordEnabled = true;
    if (recordQuantizeEnabled) {
      recordCursorRow = static_cast<int>(transport.currentRow());
    }
    std::cout << "Record enabled on channel " << recordChannel << " from row " << recordCursorRow << '\n';
  } else if (subcommand == "off") {
    recordEnabled = false;
    std::cout << "Record disabled" << '\n';
  } else if (subcommand == "channel") {
    std::string channelArg;
    recordInput >> channelArg;
    if (channelArg.empty() || channelArg == "status") {
      std::cout << "Record channel: " << recordChannel << '\n';
    } else {
      int channel = -1;
      std::istringstream parse(channelArg);
      parse >> channel;
      if (!parse || !parse.eof()) {
        std::cout << "Usage: record channel <0.." << (editor.channels() - 1) << "|status>" << '\n';
      } else {
        channel = std::clamp(channel, 0, static_cast<int>(editor.channels()) - 1);
        recordChannel = channel;
        std::cout << "Record channel set to " << recordChannel << '\n';
      }
    }
  } else if (subcommand == "cursor") {
    std::string rowArg;
    recordInput >> rowArg;
    if (rowArg.empty() || rowArg == "status") {
      std::cout << "Record cursor row: " << recordCursorRow << '\n';
    } else {
      int row = -1;
      if (rowArg == "start") {
        row = 0;
      } else if (rowArg == "end") {
        row = static_cast<int>(editor.rows()) - 1;
      } else if (rowArg == "next") {
        row = recordCursorRow + std::max(recordInsertJump, 1);
      } else if (rowArg == "prev") {
        row = recordCursorRow - std::max(recordInsertJump, 1);
      } else {
        bool isRelative = (rowArg.size() > 1 && (rowArg.front() == '+' || rowArg.front() == '-'));
        int value = 0;
        std::istringstream parse(rowArg);
        parse >> value;
        if (!parse || !parse.eof()) {
          std::cout << "Usage: record cursor <0.." << (editor.rows() - 1)
                    << "|+n|-n|start|end|next|prev|status>" << '\n';
          std::cout << "exTracker> " << std::flush;
          return;
        }
        row = isRelative ? (recordCursorRow + value) : value;
      }

      row = std::clamp(row, 0, static_cast<int>(editor.rows()) - 1);
      recordCursorRow = row;
      std::cout << "Record cursor set to row " << recordCursorRow << '\n';
    }
  } else if (subcommand == "note") {
    if (!recordEnabled) {
      std::cout << "Record is not enabled. Use: record on [channel]" << '\n';
    } else {
      bool dryRun = false;
      int midi = -1;
      int instrument = midiInstrument;
      int velocity = 100;
      int fx = 0;
      int fxValue = 0;

      if (!parseRecordDryMidi(dryRun, midi)) {
        return;
      }

      std::vector<std::string> args;
      std::string token;
      while (recordInput >> token) {
        args.push_back(token);
      }

      if (!ensureRecordNoTrailing()) {
        return;
      }

      bool parsedOk = true;
      auto isKeywordToken = [](const std::string& text) {
        return text == "instr" || text == "vel" || text == "fx";
      };

      bool hasKeywordArgs = false;
      for (const auto& arg : args) {
        if (isKeywordToken(arg)) {
          hasKeywordArgs = true;
          break;
        }
      }

      if (args.empty()) {
      } else if (args[0] == "vel" &&
                 (args.size() == 2 || (args.size() == 4 && !isKeywordToken(args[2])))) {
        if (args.size() != 2 && args.size() != 4) {
          parsedOk = false;
        } else {
          parsedOk = parseStrictIntToken(args[1], velocity);
          if (parsedOk && args.size() == 4) {
            parsedOk = parseStrictIntToken(args[2], fx) && parseStrictIntToken(args[3], fxValue);
          }
        }
      } else if (args[0] == "fx" && args.size() == 3) {
        parsedOk = parseStrictIntToken(args[1], fx) && parseStrictIntToken(args[2], fxValue);
      } else if (hasKeywordArgs) {
        bool seenInstr = false;
        bool seenVel = false;
        bool seenFx = false;
        for (std::size_t i = 0; i < args.size() && parsedOk; ++i) {
          const std::string& key = args[i];
          if (key == "instr") {
            if (seenInstr || (i + 1) >= args.size()) {
              parsedOk = false;
              break;
            }
            seenInstr = true;
            parsedOk = parseStrictIntToken(args[++i], instrument);
          } else if (key == "vel") {
            if (seenVel || (i + 1) >= args.size()) {
              parsedOk = false;
              break;
            }
            seenVel = true;
            parsedOk = parseStrictIntToken(args[++i], velocity);
          } else if (key == "fx") {
            if (seenFx || (i + 2) >= args.size()) {
              parsedOk = false;
              break;
            }
            seenFx = true;
            parsedOk = parseStrictIntToken(args[i + 1], fx) && parseStrictIntToken(args[i + 2], fxValue);
            i += 2;
          } else {
            parsedOk = false;
            break;
          }
        }
      } else if (args.size() == 1) {
        parsedOk = parseStrictIntToken(args[0], instrument);
      } else if (args.size() == 2) {
        parsedOk = parseStrictIntToken(args[0], instrument) && parseStrictIntToken(args[1], velocity);
      } else if (args.size() == 4) {
        parsedOk = parseStrictIntToken(args[0], instrument) &&
                   parseStrictIntToken(args[1], velocity) &&
                   parseStrictIntToken(args[2], fx) &&
                   parseStrictIntToken(args[3], fxValue);
      } else {
        parsedOk = false;
      }

      if (!parsedOk) {
        printRecordNoteUsage();
        return;
      }

      std::lock_guard<std::mutex> lock(stateMutex);
      int targetRow = chooseRecordRow(recordChannel);

      if (targetRow < 0) {
        std::cout << "No empty rows available in channel " << recordChannel << '\n';
      } else if (dryRun) {
        std::cout << "Record dry-run: row " << targetRow
                  << ", channel " << recordChannel
                  << ", note " << midi
                  << ", instr " << std::clamp(instrument, 0, 255)
                  << ", vel " << std::clamp(velocity, 1, 127)
                  << ", fx " << std::clamp(fx, 0, 255)
                  << ":" << std::clamp(fxValue, 0, 255)
                  << '\n';
      } else {
        applyRecordWrite(
            targetRow,
            recordChannel,
            midi,
            static_cast<std::uint8_t>(std::clamp(instrument, 0, 255)),
            0,
            static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)),
            false,
            static_cast<std::uint8_t>(std::clamp(fx, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(fxValue, 0, 255)));
        std::cout << "Recorded note at row " << targetRow << ", channel " << recordChannel << '\n';
      }
    }
  } else if (subcommand == "quantize") {
    std::string mode;
    recordInput >> mode;
    if (mode == "on") {
      recordQuantizeEnabled = true;
      std::cout << "Record quantize enabled" << '\n';
    } else if (mode == "off") {
      recordQuantizeEnabled = false;
      std::cout << "Record quantize disabled" << '\n';
    } else if (mode.empty() || mode == "status") {
      std::cout << "Record quantize: " << (recordQuantizeEnabled ? "on" : "off") << '\n';
    } else {
      std::cout << "Usage: record quantize <on|off|status>" << '\n';
    }
  } else if (subcommand == "overdub") {
    std::string mode;
    recordInput >> mode;
    if (mode == "on") {
      recordOverdubEnabled = true;
      std::cout << "Record overdub enabled" << '\n';
    } else if (mode == "off") {
      recordOverdubEnabled = false;
      std::cout << "Record overdub disabled" << '\n';
    } else if (mode.empty() || mode == "status") {
      std::cout << "Record overdub: " << (recordOverdubEnabled ? "on" : "off") << '\n';
    } else {
      std::cout << "Usage: record overdub <on|off|status>" << '\n';
    }
  } else if (subcommand == "jump") {
    std::string jumpArg;
    recordInput >> jumpArg;
    if (jumpArg.empty() || jumpArg == "status") {
      std::cout << "Record jump: " << recordInsertJump << '\n';
    } else {
      int jump = -1;
      bool parsed = false;
      std::size_t slash = jumpArg.find('/');
      if (slash != std::string::npos) {
        double numerator = -1.0;
        double denominator = -1.0;
        std::istringstream numParse(jumpArg.substr(0, slash));
        std::istringstream denParse(jumpArg.substr(slash + 1));
        numParse >> numerator;
        denParse >> denominator;
        if (numParse && denParse && numParse.eof() && denParse.eof() && denominator > 0.0) {
          jump = std::max(1, static_cast<int>(std::lround(numerator / denominator)));
          parsed = true;
        }
      } else {
        std::istringstream parse(jumpArg);
        parse >> jump;
        parsed = static_cast<bool>(parse) && parse.eof();
      }

      if (!parsed || jump < 1 || jump > static_cast<int>(editor.rows())) {
        std::cout << "Usage: record jump <1.." << editor.rows() << "|a/b|status>" << '\n';
      } else {
        recordInsertJump = jump;
        std::cout << "Record jump " << jumpArg << " -> " << recordInsertJump << '\n';
      }
    }
  } else if (subcommand == "undo") {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!undoRecordWrite(editor, recordState)) {
      std::cout << "No record action to undo" << '\n';
    } else {
      std::cout << "Undid record write at row " << recordState.redoState.row
                << ", channel " << recordState.redoState.channel << '\n';
    }
  } else if (subcommand == "redo") {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!redoRecordWrite(editor, recordState)) {
      std::cout << "No record action to redo" << '\n';
    } else {
      std::cout << "Redid record write at row " << recordState.undoState.row
                << ", channel " << recordState.undoState.channel << '\n';
    }
  } else {
    std::cout << "Usage: record <on|off|channel|cursor|note|quantize|overdub|jump|undo|redo> ..." << '\n';
  }
}

}  // namespace extracker
