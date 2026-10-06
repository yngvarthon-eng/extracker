#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "extracker/pattern_editor.hpp"

namespace extracker {

class Transport;

struct RecordCellEdit {
  int row = -1;
  int channel = -1;
  PatternEditor::Step beforeStep{};
  PatternEditor::Step afterStep{};
};

struct RecordEditState {
  int row = -1;
  int channel = -1;
  int cursorBefore = 0;
  int cursorAfter = 0;
  PatternEditor::Step beforeStep{};
  PatternEditor::Step afterStep{};
  // Earlier cells of the same edit, oldest first (the other notes of a
  // recorded chord). Undo restores row/channel first, then these newest-first.
  std::vector<RecordCellEdit> chordCells;
};

struct RecordWorkflowState {
  bool enabled = false;
  int channel = 0;
  int cursorRow = 0;
  bool quantizeEnabled = true;
  bool overdubEnabled = false;
  int insertJump = 1;
  bool punchEnabled = false;
  int punchIn = 0;
  int punchOut = 0;

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
// Folds the edit that was the undo state before the latest applyRecordWrite
// into the latest one, so one undo takes both back (chord notes on one row).
void joinRecordWrite(RecordWorkflowState& state, const RecordEditState& earlier);
// Records a whole chord on row: notes[i] goes to channels[i]. One undo step;
// the cursor advances once.
void applyRecordChordWrite(PatternEditor& editor,
                           RecordWorkflowState& state,
                           int row,
                           const std::vector<int>& channels,
                           const std::vector<int>& notes,
                           std::uint8_t instrument,
                           std::uint8_t velocity);
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
