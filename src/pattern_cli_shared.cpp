#include "pattern_cli_internal.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

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
  selection.chancePercent = 100;
  selection.hasChance = false;

  bool hasFrom = false;
  bool hasTo = false;
  if (!readOptionalStrictInt(input, hasFrom, selection.from)) {
    std::cout << usage << '\n';
    return false;
  }

  if (hasFrom) {
    if (!readOptionalStrictInt(input, hasTo, selection.to)) {
      std::cout << usage << '\n';
      return false;
    }
    if (!hasTo) {
      selection.to = selection.from;
    }
  }

  std::vector<std::string> tailTokens;
  std::string token;
  while (input >> token) {
    tailTokens.push_back(token);
  }

  std::size_t index = 0;
  if (!tailTokens.empty()) {
    const std::string& first = tailTokens.front();
    if (first != "step" && first != "chance") {
      if (!cli::parseStrictIntToken(first, selection.channel)) {
        std::cout << usage << '\n';
        return false;
      }
      selection.hasChannel = true;
      index = 1;
    }
  }

  bool sawStep = false;
  bool sawChance = false;
  while (index < tailTokens.size()) {
    const std::string& key = tailTokens[index++];
    if (index >= tailTokens.size()) {
      std::cout << usage << '\n';
      return false;
    }

    int value = 0;
    if (!cli::parseStrictIntToken(tailTokens[index++], value)) {
      std::cout << usage << '\n';
      return false;
    }

    if (key == "step") {
      if (sawStep) {
        std::cout << usage << '\n';
        return false;
      }
      sawStep = true;
      selection.rowStep = value;
      continue;
    }

    if (key == "chance") {
      if (sawChance) {
        std::cout << usage << '\n';
        return false;
      }
      sawChance = true;
      selection.hasChance = true;
      selection.chancePercent = std::clamp(value, 0, 100);
      continue;
    }

    std::cout << usage << '\n';
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
