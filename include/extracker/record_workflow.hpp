#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

#include "extracker/pattern_editor.hpp"

namespace extracker {

class Transport;

struct RecordEditState {
  int row = -1;
  int channel = -1;
  int cursorBefore = 0;
  int cursorAfter = 0;
  PatternEditor::Step beforeStep{};
  PatternEditor::Step afterStep{};
};

struct RecordWorkflowState {
  bool enabled = false;
  int channel = 0;
  int cursorRow = 0;
  bool quantizeEnabled = true;
  bool overdubEnabled = false;
  int insertJump = 1;

  RecordEditState undoState{};
  RecordEditState redoState{};
  bool canUndo = false;
  bool canRedo = false;
};

PatternEditor::Step captureStep(const PatternEditor& editor, int row, int channel);
void restoreStep(PatternEditor& editor, int row, int channel, const PatternEditor::Step& step);
int chooseRecordRow(const PatternEditor& editor, const Transport& transport, const RecordWorkflowState& state);
void applyRecordWrite(
    PatternEditor& editor,
    RecordWorkflowState& state,
    int row,
    int note,
    std::uint8_t instrument,
    std::uint32_t gateTicks,
    std::uint8_t velocity,
    bool retrigger,
    std::uint8_t effectCommand,
    std::uint8_t effectValue);
bool undoRecordWrite(PatternEditor& editor, RecordWorkflowState& state);
bool redoRecordWrite(PatternEditor& editor, RecordWorkflowState& state);
void writeRecordState(std::ostream& out, const RecordWorkflowState& state);
bool applyRecordFileToken(
  std::istream& in,
  const std::string& token,
  std::size_t patternRows,
  RecordWorkflowState& state,
  bool& handled);

}  // namespace extracker
