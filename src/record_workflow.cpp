#include "extracker/record_workflow.hpp"

#include <algorithm>
#include <istream>
#include <ostream>

#include "extracker/transport.hpp"

namespace extracker {

namespace {

void writeStep(std::ostream& out, const PatternEditor::Step& step) {
  out << (step.hasNote ? 1 : 0) << " "
      << step.note << " "
      << static_cast<int>(step.instrument) << " "
      << step.gateTicks << " "
      << static_cast<int>(step.velocity) << " "
      << (step.retrigger ? 1 : 0) << " "
      << static_cast<int>(step.effectCommand) << " "
      << static_cast<int>(step.effectValue);
}

bool readStep(std::istream& in, PatternEditor::Step& step) {
  int hasNote = 0;
  int note = -1;
  int instrument = 0;
  unsigned int gateTicks = 0;
  int velocity = 100;
  int retrigger = 0;
  int effectCommand = 0;
  int effectValue = 0;
  if (!(in >> hasNote >> note >> instrument >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue)) {
    return false;
  }

  step.hasNote = (hasNote != 0);
  step.note = note;
  step.instrument = static_cast<std::uint8_t>(std::clamp(instrument, 0, 255));
  step.gateTicks = gateTicks;
  step.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
  step.retrigger = (retrigger != 0);
  step.effectCommand = static_cast<std::uint8_t>(std::clamp(effectCommand, 0, 255));
  step.effectValue = static_cast<std::uint8_t>(std::clamp(effectValue, 0, 255));
  return true;
}

}  // namespace

PatternEditor::Step captureStep(const PatternEditor& editor, int row, int channel) {
  PatternEditor::Step step;
  step.hasNote = editor.hasNoteAt(row, channel);
  step.note = editor.noteAt(row, channel);
  step.instrument = editor.instrumentAt(row, channel);
  step.gateTicks = editor.gateTicksAt(row, channel);
  step.velocity = editor.velocityAt(row, channel);
  step.retrigger = editor.retriggerAt(row, channel);
  step.effectCommand = editor.effectCommandAt(row, channel);
  step.effectValue = editor.effectValueAt(row, channel);
  return step;
}

void restoreStep(PatternEditor& editor, int row, int channel, const PatternEditor::Step& step) {
  if (step.hasNote) {
    editor.insertNote(
        row,
        channel,
        step.note,
        step.instrument,
        step.gateTicks,
        step.velocity,
        step.retrigger,
        step.effectCommand,
        step.effectValue);
  } else {
    editor.clearStep(row, channel);
    if (step.effectCommand != 0 || step.effectValue != 0) {
      editor.setEffect(row, channel, step.effectCommand, step.effectValue);
    }
  }
}

int chooseRecordRow(const PatternEditor& editor, const Transport& transport, const RecordWorkflowState& state) {
  int baseRow = state.quantizeEnabled ? static_cast<int>(transport.currentRow()) : state.cursorRow;
  baseRow = std::clamp(baseRow, 0, static_cast<int>(editor.rows()) - 1);

  if (state.overdubEnabled) {
    return baseRow;
  }

  for (std::size_t i = 0; i < editor.rows(); ++i) {
    int candidate = (baseRow + static_cast<int>(i)) % static_cast<int>(editor.rows());
    if (!editor.hasNoteAt(candidate, state.channel)) {
      return candidate;
    }
  }

  return -1;
}

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
    std::uint8_t effectValue) {
  RecordEditState edit;
  edit.row = row;
  edit.channel = state.channel;
  edit.cursorBefore = state.cursorRow;
  edit.beforeStep = captureStep(editor, row, state.channel);

  editor.insertNote(row, state.channel, note, instrument, gateTicks, velocity, retrigger, effectCommand, effectValue);

  int jump = std::max(state.insertJump, 1);
  edit.cursorAfter = (row + jump) % static_cast<int>(editor.rows());
  edit.afterStep = captureStep(editor, row, state.channel);
  state.cursorRow = edit.cursorAfter;

  state.undoState = edit;
  state.canUndo = true;
  state.canRedo = false;
}

bool undoRecordWrite(PatternEditor& editor, RecordWorkflowState& state) {
  if (!state.canUndo) {
    return false;
  }

  restoreStep(editor, state.undoState.row, state.undoState.channel, state.undoState.beforeStep);
  state.cursorRow = state.undoState.cursorBefore;
  state.redoState = state.undoState;
  state.canRedo = true;
  state.canUndo = false;
  return true;
}

bool redoRecordWrite(PatternEditor& editor, RecordWorkflowState& state) {
  if (!state.canRedo) {
    return false;
  }

  restoreStep(editor, state.redoState.row, state.redoState.channel, state.redoState.afterStep);
  state.cursorRow = state.redoState.cursorAfter;
  state.undoState = state.redoState;
  state.canUndo = true;
  state.canRedo = false;
  return true;
}

void writeRecordState(std::ostream& out, const RecordWorkflowState& state) {
  out << "RECORD_SETTINGS " << (state.quantizeEnabled ? 1 : 0) << " "
      << (state.overdubEnabled ? 1 : 0) << "\n";
  out << "RECORD_INSERT_JUMP " << state.insertJump << "\n";
  out << "RECORD_UNDO " << (state.canUndo ? 1 : 0) << " "
      << state.undoState.row << " "
      << state.undoState.channel << " "
      << state.undoState.cursorBefore << " "
      << state.undoState.cursorAfter << " ";
  writeStep(out, state.undoState.beforeStep);
  out << " ";
  writeStep(out, state.undoState.afterStep);
  out << "\n";
  out << "RECORD_REDO " << (state.canRedo ? 1 : 0) << " "
      << state.redoState.row << " "
      << state.redoState.channel << " "
      << state.redoState.cursorBefore << " "
      << state.redoState.cursorAfter << " ";
  writeStep(out, state.redoState.beforeStep);
  out << " ";
  writeStep(out, state.redoState.afterStep);
  out << "\n";
}

bool applyRecordFileToken(
    std::istream& in,
    const std::string& token,
    std::size_t patternRows,
    RecordWorkflowState& state,
    bool& handled) {
  handled = true;
  if (token == "RECORD_SETTINGS") {
    int quantize = state.quantizeEnabled ? 1 : 0;
    int overdub = state.overdubEnabled ? 1 : 0;
    if (!(in >> quantize >> overdub)) {
      return false;
    }
    state.quantizeEnabled = (quantize != 0);
    state.overdubEnabled = (overdub != 0);
    return true;
  }

  if (token == "RECORD_INSERT_JUMP") {
    int jump = state.insertJump;
    if (!(in >> jump)) {
      return false;
    }
    state.insertJump = std::clamp(jump, 1, static_cast<int>(patternRows));
    return true;
  }

  if (token == "RECORD_UNDO") {
    int canUndo = 0;
    if (!(in >> canUndo
             >> state.undoState.row
             >> state.undoState.channel
             >> state.undoState.cursorBefore
             >> state.undoState.cursorAfter)) {
      return false;
    }
    if (!readStep(in, state.undoState.beforeStep) || !readStep(in, state.undoState.afterStep)) {
      return false;
    }
    state.canUndo = (canUndo != 0);
    return true;
  }

  if (token == "RECORD_REDO") {
    int canRedo = 0;
    if (!(in >> canRedo
             >> state.redoState.row
             >> state.redoState.channel
             >> state.redoState.cursorBefore
             >> state.redoState.cursorAfter)) {
      return false;
    }
    if (!readStep(in, state.redoState.beforeStep) || !readStep(in, state.redoState.afterStep)) {
      return false;
    }
    state.canRedo = (canRedo != 0);
    return true;
  }

  handled = false;
  return true;
}

}  // namespace extracker
