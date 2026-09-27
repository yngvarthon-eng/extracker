#pragma once

#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "extracker/pattern_cli.hpp"
#include "extracker/pattern_clipboard.hpp"

namespace extracker::pattern_cli_internal {

struct BulkEditMode {
  bool dryRun = false;
  bool previewMode = false;
  bool verboseMode = false;
  std::string valueToken;
};

struct RangeChannelSelection {
  int from = 0;
  int to = 0;
  int channel = -1;
  bool hasChannel = false;
  int rowStep = 1;
  int chancePercent = 100;
  bool hasChance = false;
};

extern std::optional<PatternEditor> gBulkUndoSnapshot;
extern std::optional<PatternEditor> gBulkRedoSnapshot;
extern ClipboardHistory gClipboardHistory;

bool readOptionalStrictInt(std::istringstream& input, bool& provided, int& value);

bool parseBulkEditMode(std::istringstream& input,
                       const char* usage,
                       BulkEditMode& mode);

bool parseRangeAndOptionalChannel(std::istringstream& input,
                                  const char* usage,
                                  int rowCount,
                                  int channelCount,
                                  RangeChannelSelection& selection);

void resetStateAfterPatternEdit(Sequencer& sequencer,
                                AudioEngine& audio,
                                bool& recordCanUndo,
                                bool& recordCanRedo);

void captureBulkUndoSnapshot(PatternEditor& editor);

bool hasUsableClipboard();

bool handlePatternBasicSubcommand(PatternCommandContext context,
                                  const std::string& subcommand,
                                  std::istringstream& patternInput);

bool handlePatternBulkSubcommand(PatternCommandContext context,
                                 const std::string& subcommand,
                                 std::istringstream& patternInput);

template <typename PreviewLine, typename Printer>
void printPreviewBlock(const char* header,
                       int changedSteps,
                       const std::vector<PreviewLine>& lines,
                       Printer printer) {
  std::cout << header << '\n';
  if (changedSteps == 0) {
    std::cout << "  (no affected steps)" << '\n';
    return;
  }

  for (const PreviewLine& line : lines) {
    std::cout << "  ";
    printer(line);
    std::cout << '\n';
  }

  if (static_cast<std::size_t>(changedSteps) > lines.size()) {
    std::cout << "  ... and "
              << (changedSteps - static_cast<int>(lines.size()))
              << " more" << '\n';
  }
}

}  // namespace extracker::pattern_cli_internal
