#include "pattern_cli_internal.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "extracker/cli_parse_utils.hpp"
#include "extracker/pattern_templates.hpp"

namespace extracker::pattern_cli_internal {

namespace {

const char* const kNoteNames[] = {
    "C ", "C#", "D ", "D#", "E ", "F ",
    "F#", "G ", "G#", "A ", "A#", "B "};

std::string getNoteSymbol(int note) {
  if (note < 0) {
    return "  ";
  }
  int octave = note / 12;
  int pitchClass = note % 12;
  return std::string(kNoteNames[pitchClass]) + std::to_string(octave);
}

std::string getChannelHeaderPadding(int channelNum) {
  if (channelNum < 10) {
    return " ";
  }
  return "";
}

}  // namespace

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
  auto& playRangeStep = context.playRangeStep;
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
    constexpr const char* usage = "Usage: pattern play [from] [to] [step <n>]";
    int from = 0;
    int to = static_cast<int>(editor.rows()) - 1;
    int rowStep = 1;
    bool hasFrom = false;
    bool hasTo = false;
    if (!readOptionalStrictInt(patternInput, hasFrom, from)) {
      std::cout << usage << '\n';
      return true;
    }
    if (hasFrom) {
      if (!readOptionalStrictInt(patternInput, hasTo, to)) {
        std::cout << usage << '\n';
        return true;
      }
      if (!hasTo) {
        to = from;
      }
    }

    std::string token;
    if (patternInput >> token) {
      if (token != "step" || !cli::parseStrictIntFromStream(patternInput, rowStep) ||
          cli::hasExtraTokens(patternInput)) {
        std::cout << usage << '\n';
        return true;
      }
    }

    if (rowStep < 1) {
      std::cout << "Step must be >= 1" << '\n';
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
    playRangeStep = rowStep;
    playRangeActive = true;
    transport.stop();
    transport.jumpToRow(static_cast<std::uint32_t>(playRangeFrom));
    sequencer.reset();
    audio.allNotesOff();
    if (transport.play()) {
      std::cout << "Playing range " << playRangeFrom << ".." << playRangeTo;
      if (playRangeStep > 1) {
        std::cout << " [step " << playRangeStep << "]";
      }
      std::cout << (loopEnabled ? " (loop on)" : " (loop off)") << '\n';
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

  if (subcommand == "display") {
    int from = 0;
    int to = 15;
    bool hasFrom = false;
    bool hasTo = false;
    if (!readOptionalStrictInt(patternInput, hasFrom, from)) {
      std::cout << "Usage: pattern display [from] [to]" << '\n';
      return true;
    }
    if (hasFrom) {
      if (!readOptionalStrictInt(patternInput, hasTo, to)) {
        std::cout << "Usage: pattern display [from] [to]" << '\n';
        return true;
      }
      if (!hasTo) {
        to = from + 15;
      }
    }
    if (cli::hasExtraTokens(patternInput)) {
      std::cout << "Usage: pattern display [from] [to]" << '\n';
      return true;
    }

    if (from > to) {
      std::swap(from, to);
    }
    from = std::max(from, 0);
    to = std::min(to, static_cast<int>(editor.rows()) - 1);

    std::lock_guard<std::mutex> lock(stateMutex);
    std::size_t numChannels = editor.channels();

    // Print header
    std::cout << "    ";
    for (std::size_t ch = 0; ch < numChannels; ++ch) {
      std::cout << "  C" << ch << "    ";
    }
    std::cout << '\n';
    std::cout << "────";
    for (std::size_t ch = 0; ch < numChannels; ++ch) {
      std::cout << "────────";
    }
    std::cout << '\n';

    // Print pattern
    for (int row = from; row <= to; ++row) {
      std::cout << "Row " << (row < 10 ? " " : "") << row << ": ";
      for (std::size_t ch = 0; ch < numChannels; ++ch) {
        int note = editor.noteAt(row, static_cast<int>(ch));
        std::string noteStr = getNoteSymbol(note);
        std::cout << noteStr << "  ";
      }
      std::cout << '\n';
    }
    return true;
  }

  if (subcommand == "watch") {
    constexpr std::size_t kMaxRows = 16;
    int updateMs = 100;
    if (patternInput >> updateMs && updateMs < 1) {
      std::cout << "Usage: pattern watch [update_interval_ms(default 100)]" << '\n';
      return true;
    }
    if (cli::hasExtraTokens(patternInput)) {
      std::cout << "Usage: pattern watch [update_interval_ms(default 100)]" << '\n';
      return true;
    }

    std::cout << "Watching pattern (Ctrl+C to stop, update interval: " << updateMs << "ms)\n";
    std::cout << "Cursor: > = playing, · = not playing\n\n";

    auto lastDisplayRow = -1;
    while (transport.isPlaying()) {
      auto currentRow = static_cast<int>(transport.currentRow());
      std::size_t numChannels = editor.channels();

      {
        std::lock_guard<std::mutex> lock(stateMutex);

        // Simple clearing (not perfect, but works in most terminals)
        // In a real app, you'd use ANSI codes or a proper TUI library
        if (currentRow != lastDisplayRow) {
          // Print header every 4 rows
          if (currentRow % 4 == 0) {
            std::cout << "\n    ";
            for (std::size_t ch = 0; ch < numChannels; ++ch) {
              std::cout << "  C" << ch << "    ";
            }
            std::cout << '\n';
          }

          // Print current and next rows
          for (int r = currentRow; r < currentRow + 4 && r < static_cast<int>(editor.rows()); ++r) {
            std::cout << (r == currentRow ? ">" : " ");
            std::cout << "Row " << (r < 10 ? " " : "") << r << ": ";
            for (std::size_t ch = 0; ch < numChannels; ++ch) {
              int note = editor.noteAt(r, static_cast<int>(ch));
              std::string noteStr = getNoteSymbol(note);
              std::cout << noteStr << "  ";
            }
            std::cout << '\n';
          }

          lastDisplayRow = currentRow;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(updateMs));
    }

    std::cout << "\nPlayback stopped\n";
    return true;
  }

  return false;
}

}  // namespace extracker::pattern_cli_internal
