#include "pattern_cli_internal.hpp"

#include <algorithm>
#include <iostream>

#include "extracker/cli_parse_utils.hpp"

namespace extracker::pattern_cli_internal {

std::optional<PatternEditor> gBulkUndoSnapshot;
std::optional<PatternEditor> gBulkRedoSnapshot;
PatternClipboard gPatternClipboard;

bool readOptionalStrictInt(std::istringstream& input, bool& provided, int& value) {
  std::string token;
  if (!(input >> token)) {
    provided = false;
    return true;
  }

  provided = true;
  return cli::parseStrictIntToken(token, value);
}

bool parseBulkEditMode(std::istringstream& input,
                       const char* usage,
                       BulkEditMode& mode) {
  if (!(input >> mode.valueToken)) {
    std::cout << usage << '\n';
    return false;
  }

  if (mode.valueToken == "dry") {
    mode.dryRun = true;
    if (!(input >> mode.valueToken)) {
      std::cout << usage << '\n';
      return false;
    }

    if (mode.valueToken == "preview") {
      mode.previewMode = true;
      if (!(input >> mode.valueToken)) {
        std::cout << usage << '\n';
        return false;
      }

      if (mode.valueToken == "verbose") {
        mode.verboseMode = true;
        if (!(input >> mode.valueToken)) {
          std::cout << usage << '\n';
          return false;
        }
      }
    }
  }

  if (!mode.dryRun && (mode.valueToken == "preview" || mode.valueToken == "verbose")) {
    std::cout << usage << '\n';
    return false;
  }

  if (mode.valueToken == "verbose" || (mode.verboseMode && !mode.previewMode)) {
    std::cout << usage << '\n';
    return false;
  }

  return true;
}

bool parseRangeAndOptionalChannel(std::istringstream& input,
                                  const char* usage,
                                  int rowCount,
                                  int channelCount,
                                  RangeChannelSelection& selection) {
  selection.from = 0;
  selection.to = rowCount - 1;
  selection.channel = -1;
  selection.hasChannel = false;
  selection.rowStep = 1;

  bool hasFrom = false;
  bool hasTo = false;
  if (!readOptionalStrictInt(input, hasFrom, selection.from)) {
    std::cout << usage << '\n';
    return false;
  }

  auto parseStepClause = [&](bool keywordAlreadyRead) -> bool {
    if (!keywordAlreadyRead) {
      std::string stepKeyword;
      if (!(input >> stepKeyword)) {
        return true;
      }
      if (stepKeyword != "step") {
        std::cout << usage << '\n';
        return false;
      }
    }

    if (!cli::parseStrictIntFromStream(input, selection.rowStep) || cli::hasExtraTokens(input)) {
      std::cout << usage << '\n';
      return false;
    }

    return true;
  };

  if (hasFrom) {
    if (!readOptionalStrictInt(input, hasTo, selection.to)) {
      std::cout << usage << '\n';
      return false;
    }
    if (!hasTo) {
      selection.to = selection.from;
    }

    std::string token;
    if (input >> token) {
      if (token == "step") {
        if (!parseStepClause(true)) {
          return false;
        }
      } else {
        if (!cli::parseStrictIntToken(token, selection.channel)) {
          std::cout << usage << '\n';
          return false;
        }
        selection.hasChannel = true;

        if (input >> token) {
          if (token != "step" || !parseStepClause(true)) {
            std::cout << usage << '\n';
            return false;
          }
        }
      }
    }
  } else if (!parseStepClause(false)) {
    return false;
  }

  if (selection.hasChannel && (selection.channel < 0 || selection.channel >= channelCount)) {
    std::cout << "Channel out of range: " << selection.channel << " (valid 0.."
              << (channelCount - 1) << ")" << '\n';
    return false;
  }

  if (selection.rowStep < 1) {
    std::cout << "Step must be >= 1" << '\n';
    return false;
  }

  if (selection.from > selection.to) {
    std::swap(selection.from, selection.to);
  }

  selection.from = std::max(selection.from, 0);
  selection.to = std::min(selection.to, rowCount - 1);

  return true;
}

void resetStateAfterPatternEdit(Sequencer& sequencer,
                                AudioEngine& audio,
                                bool& recordCanUndo,
                                bool& recordCanRedo) {
  sequencer.reset();
  audio.allNotesOff();
  recordCanUndo = false;
  recordCanRedo = false;
}

void captureBulkUndoSnapshot(PatternEditor& editor) {
  gBulkUndoSnapshot = editor;
  gBulkRedoSnapshot.reset();
}

bool hasUsableClipboard() {
  return gPatternClipboard.valid && gPatternClipboard.rows > 0 && gPatternClipboard.channels > 0 &&
         !gPatternClipboard.steps.empty();
}

}  // namespace extracker::pattern_cli_internal
