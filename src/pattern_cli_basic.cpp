#include "pattern_cli_internal.hpp"

#include <algorithm>
#include <iostream>

#include "extracker/cli_parse_utils.hpp"
#include "extracker/pattern_templates.hpp"

namespace extracker::pattern_cli_internal {

bool handlePatternBasicSubcommand(PatternCommandContext context,
                                  const std::string& subcommand,
                                  std::istringstream& patternInput) {
  auto& editor = context.editor;
  auto& stateMutex = context.stateMutex;
  auto& transport = context.transport;
  auto& sequencer = context.sequencer;
  auto& audio = context.audio;
  auto& playRangeFrom = context.playRangeFrom;
  auto& playRangeTo = context.playRangeTo;
  auto& playRangeActive = context.playRangeActive;
  auto& loopEnabled = context.loopEnabled;
  auto& recordCanUndo = context.recordCanUndo;
  auto& recordCanRedo = context.recordCanRedo;
  auto& recordCursorRow = context.recordCursorRow;

  if (subcommand == "print") {
    int from = 0;
    int to = 15;
    bool hasFrom = false;
    bool hasTo = false;
    if (!readOptionalStrictInt(patternInput, hasFrom, from)) {
      std::cout << "Usage: pattern print [from] [to]" << '\n';
      return true;
    }
    if (hasFrom) {
      if (!readOptionalStrictInt(patternInput, hasTo, to)) {
        std::cout << "Usage: pattern print [from] [to]" << '\n';
        return true;
      }
      if (!hasTo) {
        to = from + 15;
      }
    }
    if (cli::hasExtraTokens(patternInput)) {
      std::cout << "Usage: pattern print [from] [to]" << '\n';
      return true;
    }

    if (from > to) {
      std::swap(from, to);
    }

    from = std::max(from, 0);
    to = std::min(to, static_cast<int>(editor.rows()) - 1);

    std::lock_guard<std::mutex> lock(stateMutex);
    for (int row = from; row <= to; ++row) {
      std::cout << "Row " << row << ":";
      for (std::size_t ch = 0; ch < editor.channels(); ++ch) {
        int note = editor.noteAt(row, static_cast<int>(ch));
        if (note < 0) {
          std::cout << " [--]";
        } else {
          int instr = editor.instrumentAt(row, static_cast<int>(ch));
          int vel = editor.velocityAt(row, static_cast<int>(ch));
          int fx = editor.effectCommandAt(row, static_cast<int>(ch));
          int fv = editor.effectValueAt(row, static_cast<int>(ch));
          std::cout << " [" << note << ":i" << instr << ":v" << vel << ":f" << fx << ":" << fv << "]";
        }
      }
      std::cout << '\n';
    }
    return true;
  }

  if (subcommand == "play") {
    int from = 0;
    int to = static_cast<int>(editor.rows()) - 1;
    bool hasFrom = false;
    bool hasTo = false;
    if (!readOptionalStrictInt(patternInput, hasFrom, from)) {
      std::cout << "Usage: pattern play [from] [to]" << '\n';
      return true;
    }
    if (hasFrom) {
      if (!readOptionalStrictInt(patternInput, hasTo, to)) {
        std::cout << "Usage: pattern play [from] [to]" << '\n';
        return true;
      }
      if (!hasTo) {
        to = from;
      }
    }
    if (cli::hasExtraTokens(patternInput)) {
      std::cout << "Usage: pattern play [from] [to]" << '\n';
      return true;
    }

    if (from > to) {
      std::swap(from, to);
    }
    from = std::max(from, 0);
    to = std::min(to, static_cast<int>(editor.rows()) - 1);

    std::lock_guard<std::mutex> lock(stateMutex);
    playRangeFrom = from;
    playRangeTo = to;
    playRangeActive = true;
    transport.stop();
    transport.jumpToRow(static_cast<std::uint32_t>(playRangeFrom));
    sequencer.reset();
    audio.allNotesOff();
    if (transport.play()) {
      std::cout << "Playing range " << playRangeFrom << ".." << playRangeTo
                << (loopEnabled ? " (loop on)" : " (loop off)") << '\n';
    } else {
      std::cout << "Failed to start playback for selected range" << '\n';
    }
    return true;
  }

  if (subcommand == "template") {
    std::string templateName;
    patternInput >> templateName;
    if (templateName.empty() || cli::hasExtraTokens(patternInput)) {
      std::cout << "Usage: pattern template <blank|house|electro>" << '\n';
    } else {
      std::lock_guard<std::mutex> lock(stateMutex);
      if (!applyPatternTemplate(editor, templateName)) {
        std::cout << "Unknown pattern template: " << templateName << '\n';
      } else {
        sequencer.reset();
        audio.allNotesOff();
        recordCanUndo = false;
        recordCanRedo = false;
        recordCursorRow = 0;
        std::cout << "Applied pattern template: " << templateName << '\n';
      }
    }
    return true;
  }

  return false;
}

}  // namespace extracker::pattern_cli_internal
