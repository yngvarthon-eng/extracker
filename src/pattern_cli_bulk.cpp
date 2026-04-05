#include "pattern_cli_internal.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "extracker/cli_parse_utils.hpp"

namespace extracker::pattern_cli_internal {

namespace {

constexpr std::size_t kMaxPreviewLines = 10;

bool parseSelection(std::istringstream& patternInput,
                    const char* usage,
                    PatternEditor& editor,
                    RangeChannelSelection& selection) {
  return parseRangeAndOptionalChannel(
      patternInput,
      usage,
      static_cast<int>(editor.rows()),
      static_cast<int>(editor.channels()),
      selection);
}

int selectedChannelStart(bool hasChannel, int channel) {
  return hasChannel ? channel : 0;
}

int selectedChannelEnd(PatternEditor& editor, bool hasChannel, int channel) {
  return hasChannel ? channel : (static_cast<int>(editor.channels()) - 1);
}

void printChannelScope(bool hasChannel, int channel) {
  if (hasChannel) {
    std::cout << " (channel " << channel;
  } else {
    std::cout << " (all channels";
  }
}

}  // namespace

bool handlePatternBulkSubcommand(PatternCommandContext context,
                                 const std::string& subcommand,
                                 std::istringstream& patternInput) {
  auto& editor = context.editor;
  auto& stateMutex = context.stateMutex;
  auto& sequencer = context.sequencer;
  auto& audio = context.audio;
  auto& recordCanUndo = context.recordCanUndo;
  auto& recordCanRedo = context.recordCanRedo;

  if (subcommand == "transpose") {
    constexpr const char* usage =
        "Usage: pattern transpose [dry [preview [verbose]]] <semitones> [from] [to] [ch] [step <n>]";
    BulkEditMode mode;
    if (!parseBulkEditMode(patternInput, usage, mode)) {
      return true;
    }

    int semitones = 0;
    if (!cli::parseStrictIntToken(mode.valueToken, semitones)) {
      std::cout << usage << '\n';
      return true;
    }

    RangeChannelSelection selection;
    if (!parseSelection(patternInput, usage, editor, selection)) {
      return true;
    }
    const int from = selection.from;
    const int to = selection.to;
    const int channel = selection.channel;
    const bool hasChannel = selection.hasChannel;
    const int rowStep = selection.rowStep;

    int changedSteps = 0;
    int clampedSteps = 0;
    int channelStart = selectedChannelStart(hasChannel, channel);
    int channelEnd = selectedChannelEnd(editor, hasChannel, channel);
    struct PreviewLine {
      int row = 0;
      int channel = 0;
      int fromNote = 0;
      int toNote = 0;
      bool clamped = false;
      int instrument = 0;
      int velocity = 0;
      int effectCommand = 0;
      int effectValue = 0;
    };
    std::vector<PreviewLine> previewLines;
    bool undoCaptured = false;
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      for (int row = from; row <= to; row += rowStep) {
        for (int ch = channelStart; ch <= channelEnd; ++ch) {
          int note = editor.noteAt(row, ch);
          if (note < 0) {
            continue;
          }

          int shifted = std::clamp(note + semitones, 0, 127);
          bool clamped = shifted != note + semitones;
          if (clamped) {
            ++clampedSteps;
          }

          int instrument = static_cast<int>(editor.instrumentAt(row, ch));
          int velocity = static_cast<int>(editor.velocityAt(row, ch));
          int effectCommand = static_cast<int>(editor.effectCommandAt(row, ch));
          int effectValue = static_cast<int>(editor.effectValueAt(row, ch));

          if (mode.dryRun && mode.previewMode && previewLines.size() < kMaxPreviewLines) {
            previewLines.push_back(PreviewLine{row,
                                               ch,
                                               note,
                                               shifted,
                                               clamped,
                                               instrument,
                                               velocity,
                                               effectCommand,
                                               effectValue});
          }

          if (!mode.dryRun) {
            if (!undoCaptured) {
              captureBulkUndoSnapshot(editor);
              undoCaptured = true;
            }
            editor.insertNote(
                row,
                ch,
                shifted,
                static_cast<std::uint8_t>(instrument),
                editor.gateTicksAt(row, ch),
                static_cast<std::uint8_t>(velocity),
                editor.retriggerAt(row, ch),
                static_cast<std::uint8_t>(effectCommand),
                static_cast<std::uint8_t>(effectValue));
          }
          ++changedSteps;
        }
      }

      if (!mode.dryRun) {
        resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
      }
    }

    std::cout << "Transposed " << changedSteps
              << " step(s) by " << semitones
              << " semitones in rows " << from << ".." << to;
    printChannelScope(hasChannel, channel);
    std::cout << ", " << clampedSteps << " clamped)";
    if (rowStep > 1) {
      std::cout << " [step " << rowStep << "]";
    }
    if (mode.dryRun) {
      std::cout << " [dry-run]";
    }
    std::cout << '\n';

    if (mode.dryRun && mode.previewMode) {
      printPreviewBlock("Transpose preview:", changedSteps, previewLines, [&](const PreviewLine& line) {
        std::cout << "row " << line.row
                  << " ch " << line.channel
                  << " " << line.fromNote
                  << " -> " << line.toNote;
        if (line.clamped) {
          std::cout << " [clamped]";
        }
        if (mode.verboseMode) {
          std::cout << " i" << line.instrument
                    << " v" << line.velocity
                    << " fx" << line.effectCommand
                    << ":" << line.effectValue;
        }
      });
    }
    return true;
  }

  if (subcommand == "velocity") {
    constexpr const char* usage =
        "Usage: pattern velocity [dry [preview [verbose]]] <percent> [from] [to] [ch] [step <n>]";
    BulkEditMode mode;
    if (!parseBulkEditMode(patternInput, usage, mode)) {
      return true;
    }

    int percent = 0;
    if (!cli::parseStrictIntToken(mode.valueToken, percent)) {
      std::cout << usage << '\n';
      return true;
    }

    RangeChannelSelection selection;
    if (!parseSelection(patternInput, usage, editor, selection)) {
      return true;
    }
    const int from = selection.from;
    const int to = selection.to;
    const int channel = selection.channel;
    const bool hasChannel = selection.hasChannel;
    const int rowStep = selection.rowStep;

    int changedSteps = 0;
    int clampedSteps = 0;
    int channelStart = selectedChannelStart(hasChannel, channel);
    int channelEnd = selectedChannelEnd(editor, hasChannel, channel);
    struct PreviewLine {
      int row = 0;
      int channel = 0;
      int fromVelocity = 0;
      int toVelocity = 0;
      bool clamped = false;
      int note = 0;
      int instrument = 0;
      int effectCommand = 0;
      int effectValue = 0;
    };
    std::vector<PreviewLine> previewLines;
    bool undoCaptured = false;
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      for (int row = from; row <= to; row += rowStep) {
        for (int ch = channelStart; ch <= channelEnd; ++ch) {
          int note = editor.noteAt(row, ch);
          if (note < 0) {
            continue;
          }

          int velocity = static_cast<int>(editor.velocityAt(row, ch));
          double scaled = static_cast<double>(velocity) * static_cast<double>(percent) / 100.0;
          int scaledVelocity = static_cast<int>(std::lround(scaled));
          int clampedVelocity = std::clamp(scaledVelocity, 1, 127);
          bool clamped = clampedVelocity != scaledVelocity;
          if (clamped) {
            ++clampedSteps;
          }

          int instrument = static_cast<int>(editor.instrumentAt(row, ch));
          int effectCommand = static_cast<int>(editor.effectCommandAt(row, ch));
          int effectValue = static_cast<int>(editor.effectValueAt(row, ch));

          if (mode.dryRun && mode.previewMode && previewLines.size() < kMaxPreviewLines) {
            previewLines.push_back(PreviewLine{row,
                                               ch,
                                               velocity,
                                               clampedVelocity,
                                               clamped,
                                               note,
                                               instrument,
                                               effectCommand,
                                               effectValue});
          }

          if (!mode.dryRun) {
            if (!undoCaptured) {
              captureBulkUndoSnapshot(editor);
              undoCaptured = true;
            }
            editor.setVelocity(row, ch, static_cast<std::uint8_t>(clampedVelocity));
          }
          ++changedSteps;
        }
      }

      if (!mode.dryRun) {
        resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
      }
    }

    std::cout << "Scaled velocity on " << changedSteps
              << " step(s) by " << percent
              << "% in rows " << from << ".." << to;
    printChannelScope(hasChannel, channel);
    std::cout << ", " << clampedSteps << " clamped)";
    if (rowStep > 1) {
      std::cout << " [step " << rowStep << "]";
    }
    if (mode.dryRun) {
      std::cout << " [dry-run]";
    }
    std::cout << '\n';

    if (mode.dryRun && mode.previewMode) {
      printPreviewBlock("Velocity preview:", changedSteps, previewLines, [&](const PreviewLine& line) {
        std::cout << "row " << line.row
                  << " ch " << line.channel
                  << " v" << line.fromVelocity
                  << " -> v" << line.toVelocity;
        if (line.clamped) {
          std::cout << " [clamped]";
        }
        if (mode.verboseMode) {
          std::cout << " n" << line.note
                    << " i" << line.instrument
                    << " fx" << line.effectCommand
                    << ":" << line.effectValue;
        }
      });
    }
    return true;
  }

  if (subcommand == "gate") {
    constexpr const char* usage =
        "Usage: pattern gate [dry [preview [verbose]]] <percent> [from] [to] [ch] [step <n>]";
    BulkEditMode mode;
    if (!parseBulkEditMode(patternInput, usage, mode)) {
      return true;
    }

    int percent = 0;
    if (!cli::parseStrictIntToken(mode.valueToken, percent)) {
      std::cout << usage << '\n';
      return true;
    }

    RangeChannelSelection selection;
    if (!parseSelection(patternInput, usage, editor, selection)) {
      return true;
    }
    const int from = selection.from;
    const int to = selection.to;
    const int channel = selection.channel;
    const bool hasChannel = selection.hasChannel;
    const int rowStep = selection.rowStep;

    int changedSteps = 0;
    int clampedSteps = 0;
    int channelStart = selectedChannelStart(hasChannel, channel);
    int channelEnd = selectedChannelEnd(editor, hasChannel, channel);
    struct PreviewLine {
      int row = 0;
      int channel = 0;
      std::uint32_t fromGate = 0;
      std::uint32_t toGate = 0;
      bool clamped = false;
      int note = 0;
      int instrument = 0;
      int velocity = 0;
      int effectCommand = 0;
      int effectValue = 0;
    };
    std::vector<PreviewLine> previewLines;
    bool undoCaptured = false;
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      for (int row = from; row <= to; row += rowStep) {
        for (int ch = channelStart; ch <= channelEnd; ++ch) {
          int note = editor.noteAt(row, ch);
          if (note < 0) {
            continue;
          }

          std::uint32_t gate = editor.gateTicksAt(row, ch);
          double scaled = static_cast<double>(gate) * static_cast<double>(percent) / 100.0;
          long long rounded = static_cast<long long>(std::llround(scaled));
          long long clampedRaw = std::clamp(
              rounded,
              0LL,
              static_cast<long long>(std::numeric_limits<std::uint32_t>::max()));
          bool clamped = clampedRaw != rounded;
          if (clamped) {
            ++clampedSteps;
          }
          std::uint32_t scaledGate = static_cast<std::uint32_t>(clampedRaw);

          int instrument = static_cast<int>(editor.instrumentAt(row, ch));
          int velocity = static_cast<int>(editor.velocityAt(row, ch));
          int effectCommand = static_cast<int>(editor.effectCommandAt(row, ch));
          int effectValue = static_cast<int>(editor.effectValueAt(row, ch));

          if (mode.dryRun && mode.previewMode && previewLines.size() < kMaxPreviewLines) {
            previewLines.push_back(PreviewLine{row,
                                               ch,
                                               gate,
                                               scaledGate,
                                               clamped,
                                               note,
                                               instrument,
                                               velocity,
                                               effectCommand,
                                               effectValue});
          }

          if (!mode.dryRun) {
            if (!undoCaptured) {
              captureBulkUndoSnapshot(editor);
              undoCaptured = true;
            }
            editor.setGateTicks(row, ch, scaledGate);
          }
          ++changedSteps;
        }
      }

      if (!mode.dryRun) {
        resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
      }
    }

    std::cout << "Scaled gate on " << changedSteps
              << " step(s) by " << percent
              << "% in rows " << from << ".." << to;
    printChannelScope(hasChannel, channel);
    std::cout << ", " << clampedSteps << " clamped)";
    if (rowStep > 1) {
      std::cout << " [step " << rowStep << "]";
    }
    if (mode.dryRun) {
      std::cout << " [dry-run]";
    }
    std::cout << '\n';

    if (mode.dryRun && mode.previewMode) {
      printPreviewBlock("Gate preview:", changedSteps, previewLines, [&](const PreviewLine& line) {
        std::cout << "row " << line.row
                  << " ch " << line.channel
                  << " g" << line.fromGate
                  << " -> g" << line.toGate;
        if (line.clamped) {
          std::cout << " [clamped]";
        }
        if (mode.verboseMode) {
          std::cout << " n" << line.note
                    << " i" << line.instrument
                    << " v" << line.velocity
                    << " fx" << line.effectCommand
                    << ":" << line.effectValue;
        }
      });
    }
    return true;
  }

  if (subcommand == "effect") {
    constexpr const char* usage =
        "Usage: pattern effect [dry [preview [verbose]]] <fx> <fxval> [from] [to] [ch] [step <n>]";
    BulkEditMode mode;
    if (!parseBulkEditMode(patternInput, usage, mode)) {
      return true;
    }

    int fx = 0;
    if (!cli::parseStrictIntToken(mode.valueToken, fx)) {
      std::cout << usage << '\n';
      return true;
    }

    int fxValue = 0;
    if (!cli::parseStrictIntFromStream(patternInput, fxValue)) {
      std::cout << usage << '\n';
      return true;
    }

    RangeChannelSelection selection;
    if (!parseSelection(patternInput, usage, editor, selection)) {
      return true;
    }
    const int from = selection.from;
    const int to = selection.to;
    const int channel = selection.channel;
    const bool hasChannel = selection.hasChannel;
    const int rowStep = selection.rowStep;

    std::uint8_t fxOut = static_cast<std::uint8_t>(std::clamp(fx, 0, 255));
    std::uint8_t fxValueOut = static_cast<std::uint8_t>(std::clamp(fxValue, 0, 255));
    int changedSteps = 0;
    int clampedValues = 0;
    if (fxOut != fx || fxValueOut != fxValue) {
      clampedValues = 1;
    }
    int channelStart = selectedChannelStart(hasChannel, channel);
    int channelEnd = selectedChannelEnd(editor, hasChannel, channel);
    struct PreviewLine {
      int row = 0;
      int channel = 0;
      int fromFx = 0;
      int fromFxValue = 0;
      int toFx = 0;
      int toFxValue = 0;
      int note = 0;
      int instrument = 0;
      int velocity = 0;
    };
    std::vector<PreviewLine> previewLines;
    bool undoCaptured = false;
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      for (int row = from; row <= to; row += rowStep) {
        for (int ch = channelStart; ch <= channelEnd; ++ch) {
          int note = editor.noteAt(row, ch);
          if (note < 0) {
            continue;
          }

          int fromFx = static_cast<int>(editor.effectCommandAt(row, ch));
          int fromFxValue = static_cast<int>(editor.effectValueAt(row, ch));
          int instrument = static_cast<int>(editor.instrumentAt(row, ch));
          int velocity = static_cast<int>(editor.velocityAt(row, ch));

          if (mode.dryRun && mode.previewMode && previewLines.size() < kMaxPreviewLines) {
            previewLines.push_back(PreviewLine{row,
                                               ch,
                                               fromFx,
                                               fromFxValue,
                                               static_cast<int>(fxOut),
                                               static_cast<int>(fxValueOut),
                                               note,
                                               instrument,
                                               velocity});
          }

          if (!mode.dryRun) {
            if (!undoCaptured) {
              captureBulkUndoSnapshot(editor);
              undoCaptured = true;
            }
            editor.setEffect(row, ch, fxOut, fxValueOut);
          }
          ++changedSteps;
        }
      }

      if (!mode.dryRun) {
        resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
      }
    }

    std::cout << "Filled effect on " << changedSteps
              << " step(s) with " << static_cast<int>(fxOut)
              << ":" << static_cast<int>(fxValueOut)
              << " in rows " << from << ".." << to;
    printChannelScope(hasChannel, channel);
    std::cout << ", " << clampedValues << " input clamped)";
    if (rowStep > 1) {
      std::cout << " [step " << rowStep << "]";
    }
    if (mode.dryRun) {
      std::cout << " [dry-run]";
    }
    std::cout << '\n';

    if (mode.dryRun && mode.previewMode) {
      printPreviewBlock("Effect preview:", changedSteps, previewLines, [&](const PreviewLine& line) {
        std::cout << "row " << line.row
                  << " ch " << line.channel
                  << " fx" << line.fromFx
                  << ":" << line.fromFxValue
                  << " -> fx" << line.toFx
                  << ":" << line.toFxValue;
        if (mode.verboseMode) {
          std::cout << " n" << line.note
                    << " i" << line.instrument
                    << " v" << line.velocity;
        }
      });
    }
    return true;
  }

  if (subcommand == "copy") {
    constexpr const char* usage = "Usage: pattern copy <from> <to> [chFrom] [chTo] [step <n>]";
    int from = 0;
    int to = 0;
    if (!cli::parseStrictIntFromStream(patternInput, from) ||
        !cli::parseStrictIntFromStream(patternInput, to)) {
      std::cout << usage << '\n';
      return true;
    }

    int chFrom = 0;
    int chTo = static_cast<int>(editor.channels()) - 1;
    int rowStep = 1;
    std::string token;
    if (patternInput >> token) {
      if (token == "step") {
        if (!cli::parseStrictIntFromStream(patternInput, rowStep) || cli::hasExtraTokens(patternInput)) {
          std::cout << usage << '\n';
          return true;
        }
      } else {
        if (!cli::parseStrictIntToken(token, chFrom) || !cli::parseStrictIntFromStream(patternInput, chTo)) {
          std::cout << usage << '\n';
          return true;
        }

        if (patternInput >> token) {
          if (token != "step" || !cli::parseStrictIntFromStream(patternInput, rowStep) ||
              cli::hasExtraTokens(patternInput)) {
            std::cout << usage << '\n';
            return true;
          }
        }
      }
    }

    if (rowStep < 1) {
      std::cout << "Step must be >= 1" << '\n';
      return true;
    }

    if (from > to) {
      std::swap(from, to);
    }
    if (chFrom > chTo) {
      std::swap(chFrom, chTo);
    }

    from = std::max(from, 0);
    to = std::min(to, static_cast<int>(editor.rows()) - 1);
    chFrom = std::max(chFrom, 0);
    chTo = std::min(chTo, static_cast<int>(editor.channels()) - 1);

    std::lock_guard<std::mutex> lock(stateMutex);
    gPatternClipboard.valid = false;
    gPatternClipboard.rows = ((to - from) / rowStep) + 1;
    gPatternClipboard.channels = (chTo - chFrom + 1);
    gPatternClipboard.sourceFromRow = from;
    gPatternClipboard.sourceFromChannel = chFrom;
    gPatternClipboard.sourceRowStep = rowStep;
    gPatternClipboard.steps.assign(
        static_cast<std::size_t>(gPatternClipboard.rows * gPatternClipboard.channels),
        ClipboardStep{});

    for (int r = 0; r < gPatternClipboard.rows; ++r) {
      for (int c = 0; c < gPatternClipboard.channels; ++c) {
      int row = from + (r * rowStep);
        int ch = chFrom + c;
        ClipboardStep step;
        step.hasNote = editor.hasNoteAt(row, ch);
        if (step.hasNote) {
          step.note = editor.noteAt(row, ch);
          step.instrument = editor.instrumentAt(row, ch);
          step.gateTicks = editor.gateTicksAt(row, ch);
          step.velocity = editor.velocityAt(row, ch);
          step.retrigger = editor.retriggerAt(row, ch);
          step.effectCommand = editor.effectCommandAt(row, ch);
          step.effectValue = editor.effectValueAt(row, ch);
        }
        gPatternClipboard.steps[static_cast<std::size_t>(r * gPatternClipboard.channels + c)] = step;
      }
    }
    gPatternClipboard.valid = true;

    std::cout << "Copied rows " << from << ".." << to
              << " channels " << chFrom << ".." << chTo
              << " (" << gPatternClipboard.rows << "x" << gPatternClipboard.channels << ")";
    if (rowStep > 1) {
      std::cout << " [step " << rowStep << "]";
    }
    std::cout << '\n';
    return true;
  }

  if (subcommand == "paste") {
    constexpr const char* usage =
        "Usage: pattern paste [dry [preview [verbose]]] <destRow> [channelOffset] [step <n>]";
    BulkEditMode mode;
    if (!parseBulkEditMode(patternInput, usage, mode)) {
      return true;
    }

    int destRow = 0;
    if (!cli::parseStrictIntToken(mode.valueToken, destRow)) {
      std::cout << usage << '\n';
      return true;
    }

    int channelOffset = 0;
    int rowStep = 1;
    std::string token;
    if (patternInput >> token) {
      if (token == "step") {
        if (!cli::parseStrictIntFromStream(patternInput, rowStep) || cli::hasExtraTokens(patternInput)) {
          std::cout << usage << '\n';
          return true;
        }
      } else {
        if (!cli::parseStrictIntToken(token, channelOffset)) {
          std::cout << usage << '\n';
          return true;
        }
        if (patternInput >> token) {
          if (token != "step" || !cli::parseStrictIntFromStream(patternInput, rowStep) ||
              cli::hasExtraTokens(patternInput)) {
            std::cout << usage << '\n';
            return true;
          }
        }
      }
    }

    if (rowStep < 1) {
      std::cout << "Step must be >= 1" << '\n';
      return true;
    }

    if (!hasUsableClipboard()) {
      std::cout << "Clipboard is empty. Use pattern copy first." << '\n';
      return true;
    }

    int changedSteps = 0;
    int skippedSteps = 0;
    int baseChannel = gPatternClipboard.sourceFromChannel + channelOffset;
    struct PreviewLine {
      int sourceRow = 0;
      int sourceChannel = 0;
      int destRow = 0;
      int destChannel = 0;
      int note = -1;
      int instrument = 0;
      int velocity = 0;
      int fx = 0;
      int fxValue = 0;
    };
    std::vector<PreviewLine> previewLines;
    bool undoCaptured = false;

    {
      std::lock_guard<std::mutex> lock(stateMutex);
      for (int r = 0; r < gPatternClipboard.rows; ++r) {
        for (int c = 0; c < gPatternClipboard.channels; ++c) {
          const ClipboardStep& step =
              gPatternClipboard.steps[static_cast<std::size_t>(r * gPatternClipboard.channels + c)];
          if (!step.hasNote) {
            continue;
          }

          int targetRow = destRow + (r * rowStep);
          int targetChannel = baseChannel + c;
          if (targetRow < 0 || targetRow >= static_cast<int>(editor.rows()) ||
              targetChannel < 0 || targetChannel >= static_cast<int>(editor.channels())) {
            ++skippedSteps;
            continue;
          }

          if (mode.dryRun && mode.previewMode && previewLines.size() < kMaxPreviewLines) {
            previewLines.push_back(PreviewLine{gPatternClipboard.sourceFromRow + (r * gPatternClipboard.sourceRowStep),
                                               gPatternClipboard.sourceFromChannel + c,
                                               targetRow,
                                               targetChannel,
                                               step.note,
                                               static_cast<int>(step.instrument),
                                               static_cast<int>(step.velocity),
                                               static_cast<int>(step.effectCommand),
                                               static_cast<int>(step.effectValue)});
          }

          if (!mode.dryRun) {
            if (!undoCaptured) {
              captureBulkUndoSnapshot(editor);
              undoCaptured = true;
            }
            editor.insertNote(
                targetRow,
                targetChannel,
                step.note,
                step.instrument,
                step.gateTicks,
                step.velocity,
                step.retrigger,
                step.effectCommand,
                step.effectValue);
          }
          ++changedSteps;
        }
      }

      if (!mode.dryRun && undoCaptured) {
        resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
      }
    }

    std::cout << "Pasted " << changedSteps
              << " step(s) at row " << destRow
              << " (channel offset " << channelOffset
              << ", " << skippedSteps << " skipped)";
    if (rowStep > 1) {
      std::cout << " [step " << rowStep << "]";
    }
    if (mode.dryRun) {
      std::cout << " [dry-run]";
    }
    std::cout << '\n';

    if (mode.dryRun && mode.previewMode) {
      printPreviewBlock("Paste preview:", changedSteps, previewLines, [&](const PreviewLine& line) {
        std::cout << "src " << line.sourceRow << ":" << line.sourceChannel
                  << " -> dst " << line.destRow << ":" << line.destChannel
                  << " note " << line.note;
        if (mode.verboseMode) {
          std::cout << " i" << line.instrument
                    << " v" << line.velocity
                    << " fx" << line.fx << ":" << line.fxValue;
        }
      });
    }
    return true;
  }

  if (subcommand == "humanize") {
    constexpr const char* usage =
        "Usage: pattern humanize [dry [preview [verbose]]] <velRange> <gateRangePercent> <seed> [from] [to] [ch] [step <n>]";
    BulkEditMode mode;
    if (!parseBulkEditMode(patternInput, usage, mode)) {
      return true;
    }

    int velRange = 0;
    if (!cli::parseStrictIntToken(mode.valueToken, velRange)) {
      std::cout << usage << '\n';
      return true;
    }
    int gateRangePercent = 0;
    int seed = 0;
    if (!cli::parseStrictIntFromStream(patternInput, gateRangePercent) ||
        !cli::parseStrictIntFromStream(patternInput, seed)) {
      std::cout << usage << '\n';
      return true;
    }

    RangeChannelSelection selection;
    if (!parseSelection(patternInput, usage, editor, selection)) {
      return true;
    }
    const int from = selection.from;
    const int to = selection.to;
    const int channel = selection.channel;
    const bool hasChannel = selection.hasChannel;
    const int rowStep = selection.rowStep;

    std::mt19937 rng(static_cast<std::uint32_t>(seed));
    std::uniform_int_distribution<int> velDeltaDist(-std::abs(velRange), std::abs(velRange));
    std::uniform_int_distribution<int> gateDeltaDist(-std::abs(gateRangePercent), std::abs(gateRangePercent));

    int changedSteps = 0;
    int clampedSteps = 0;
    int channelStart = selectedChannelStart(hasChannel, channel);
    int channelEnd = selectedChannelEnd(editor, hasChannel, channel);
    struct PreviewLine {
      int row = 0;
      int channel = 0;
      int fromVel = 0;
      int toVel = 0;
      std::uint32_t fromGate = 0;
      std::uint32_t toGate = 0;
      int note = 0;
      int instrument = 0;
      bool clamped = false;
    };
    std::vector<PreviewLine> previewLines;
    bool undoCaptured = false;

    {
      std::lock_guard<std::mutex> lock(stateMutex);
      for (int row = from; row <= to; row += rowStep) {
        for (int ch = channelStart; ch <= channelEnd; ++ch) {
          int note = editor.noteAt(row, ch);
          if (note < 0) {
            continue;
          }

          int oldVel = static_cast<int>(editor.velocityAt(row, ch));
          std::uint32_t oldGate = editor.gateTicksAt(row, ch);

          int newVelRaw = oldVel + velDeltaDist(rng);
          int newVel = std::clamp(newVelRaw, 1, 127);

          int gateDeltaPercent = gateDeltaDist(rng);
          double gateDelta = static_cast<double>(oldGate) * static_cast<double>(gateDeltaPercent) / 100.0;
          long long newGateRaw = static_cast<long long>(oldGate) + static_cast<long long>(std::llround(gateDelta));
          long long newGateClampedRaw = std::clamp(
              newGateRaw,
              0LL,
              static_cast<long long>(std::numeric_limits<std::uint32_t>::max()));
          std::uint32_t newGate = static_cast<std::uint32_t>(newGateClampedRaw);

          bool clamped = (newVelRaw != newVel) || (newGateRaw != newGateClampedRaw);
          if (clamped) {
            ++clampedSteps;
          }

          if (mode.dryRun && mode.previewMode && previewLines.size() < kMaxPreviewLines) {
            previewLines.push_back(PreviewLine{row,
                                               ch,
                                               oldVel,
                                               newVel,
                                               oldGate,
                                               newGate,
                                               note,
                                               static_cast<int>(editor.instrumentAt(row, ch)),
                                               clamped});
          }

          if (!mode.dryRun) {
            if (!undoCaptured) {
              captureBulkUndoSnapshot(editor);
              undoCaptured = true;
            }
            editor.setVelocity(row, ch, static_cast<std::uint8_t>(newVel));
            editor.setGateTicks(row, ch, newGate);
          }
          ++changedSteps;
        }
      }

      if (!mode.dryRun && undoCaptured) {
        resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
      }
    }

    std::cout << "Humanized " << changedSteps
              << " step(s) with vel +/-" << std::abs(velRange)
              << " and gate +/-" << std::abs(gateRangePercent)
              << "% in rows " << from << ".." << to;
    printChannelScope(hasChannel, channel);
    std::cout << ", " << clampedSteps << " clamped, seed " << seed << ")";
    if (rowStep > 1) {
      std::cout << " [step " << rowStep << "]";
    }
    if (mode.dryRun) {
      std::cout << " [dry-run]";
    }
    std::cout << '\n';

    if (mode.dryRun && mode.previewMode) {
      printPreviewBlock("Humanize preview:", changedSteps, previewLines, [&](const PreviewLine& line) {
        std::cout << "row " << line.row
                  << " ch " << line.channel
                  << " v" << line.fromVel << " -> v" << line.toVel
                  << " g" << line.fromGate << " -> g" << line.toGate;
        if (line.clamped) {
          std::cout << " [clamped]";
        }
        if (mode.verboseMode) {
          std::cout << " n" << line.note << " i" << line.instrument;
        }
      });
    }
    return true;
  }

  if (subcommand == "randomize") {
    constexpr const char* usage =
        "Usage: pattern randomize [dry [preview [verbose]]] <probabilityPercent> <seed> [from] [to] [ch] [step <n>]";
    BulkEditMode mode;
    if (!parseBulkEditMode(patternInput, usage, mode)) {
      return true;
    }

    int probabilityPercent = 0;
    if (!cli::parseStrictIntToken(mode.valueToken, probabilityPercent)) {
      std::cout << usage << '\n';
      return true;
    }
    int seed = 0;
    if (!cli::parseStrictIntFromStream(patternInput, seed)) {
      std::cout << usage << '\n';
      return true;
    }

    int clampedInputs = 0;
    int clampedProbability = std::clamp(probabilityPercent, 0, 100);
    if (clampedProbability != probabilityPercent) {
      clampedInputs = 1;
    }

    RangeChannelSelection selection;
    if (!parseSelection(patternInput, usage, editor, selection)) {
      return true;
    }
    const int from = selection.from;
    const int to = selection.to;
    const int channel = selection.channel;
    const bool hasChannel = selection.hasChannel;
    const int rowStep = selection.rowStep;

    std::mt19937 rng(static_cast<std::uint32_t>(seed));
    std::bernoulli_distribution pick(static_cast<double>(clampedProbability) / 100.0);
    std::uniform_int_distribution<int> velDist(1, 127);
    std::uniform_int_distribution<int> fxDist(0, 15);
    std::uniform_int_distribution<int> fxValDist(0, 255);

    int changedSteps = 0;
    int channelStart = selectedChannelStart(hasChannel, channel);
    int channelEnd = selectedChannelEnd(editor, hasChannel, channel);
    struct PreviewLine {
      int row = 0;
      int channel = 0;
      int fromVel = 0;
      int toVel = 0;
      int fromFx = 0;
      int fromFxValue = 0;
      int toFx = 0;
      int toFxValue = 0;
      int note = 0;
      int instrument = 0;
    };
    std::vector<PreviewLine> previewLines;
    bool undoCaptured = false;

    {
      std::lock_guard<std::mutex> lock(stateMutex);
      for (int row = from; row <= to; row += rowStep) {
        for (int ch = channelStart; ch <= channelEnd; ++ch) {
          int note = editor.noteAt(row, ch);
          if (note < 0 || !pick(rng)) {
            continue;
          }

          int oldVel = static_cast<int>(editor.velocityAt(row, ch));
          int oldFx = static_cast<int>(editor.effectCommandAt(row, ch));
          int oldFxValue = static_cast<int>(editor.effectValueAt(row, ch));
          int newVel = velDist(rng);
          int newFx = fxDist(rng);
          int newFxValue = fxValDist(rng);

          if (mode.dryRun && mode.previewMode && previewLines.size() < kMaxPreviewLines) {
            previewLines.push_back(PreviewLine{row,
                                               ch,
                                               oldVel,
                                               newVel,
                                               oldFx,
                                               oldFxValue,
                                               newFx,
                                               newFxValue,
                                               note,
                                               static_cast<int>(editor.instrumentAt(row, ch))});
          }

          if (!mode.dryRun) {
            if (!undoCaptured) {
              captureBulkUndoSnapshot(editor);
              undoCaptured = true;
            }
            editor.setVelocity(row, ch, static_cast<std::uint8_t>(newVel));
            editor.setEffect(row, ch, static_cast<std::uint8_t>(newFx), static_cast<std::uint8_t>(newFxValue));
          }
          ++changedSteps;
        }
      }

      if (!mode.dryRun && undoCaptured) {
        resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
      }
    }

    std::cout << "Randomized " << changedSteps
              << " step(s) at " << clampedProbability
              << "% probability in rows " << from << ".." << to;
    printChannelScope(hasChannel, channel);
    std::cout << ", " << clampedInputs << " input clamped, seed " << seed << ")";
    if (rowStep > 1) {
      std::cout << " [step " << rowStep << "]";
    }
    if (mode.dryRun) {
      std::cout << " [dry-run]";
    }
    std::cout << '\n';

    if (mode.dryRun && mode.previewMode) {
      printPreviewBlock("Randomize preview:", changedSteps, previewLines, [&](const PreviewLine& line) {
        std::cout << "row " << line.row
                  << " ch " << line.channel
                  << " v" << line.fromVel << " -> v" << line.toVel
                  << " fx" << line.fromFx << ":" << line.fromFxValue
                  << " -> fx" << line.toFx << ":" << line.toFxValue;
        if (mode.verboseMode) {
          std::cout << " n" << line.note << " i" << line.instrument;
        }
      });
    }
    return true;
  }

  if (subcommand == "undo") {
    if (cli::hasExtraTokens(patternInput)) {
      std::cout << "Usage: pattern undo" << '\n';
      return true;
    }

    std::lock_guard<std::mutex> lock(stateMutex);
    if (!gBulkUndoSnapshot.has_value()) {
      std::cout << "No bulk undo state available" << '\n';
      return true;
    }

    gBulkRedoSnapshot = editor;
    editor = *gBulkUndoSnapshot;
    gBulkUndoSnapshot.reset();
    resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
    std::cout << "Bulk pattern undo applied" << '\n';
    return true;
  }

  if (subcommand == "redo") {
    if (cli::hasExtraTokens(patternInput)) {
      std::cout << "Usage: pattern redo" << '\n';
      return true;
    }

    std::lock_guard<std::mutex> lock(stateMutex);
    if (!gBulkRedoSnapshot.has_value()) {
      std::cout << "No bulk redo state available" << '\n';
      return true;
    }

    gBulkUndoSnapshot = editor;
    editor = *gBulkRedoSnapshot;
    gBulkRedoSnapshot.reset();
    resetStateAfterPatternEdit(sequencer, audio, recordCanUndo, recordCanRedo);
    std::cout << "Bulk pattern redo applied" << '\n';
    return true;
  }

  return false;
}

}  // namespace extracker::pattern_cli_internal
