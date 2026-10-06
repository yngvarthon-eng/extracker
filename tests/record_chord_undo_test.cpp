#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "extracker/pattern_editor.hpp"
#include "extracker/record_workflow.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using extracker::PatternEditor;
  using extracker::RecordWorkflowState;

  // A chord write is one undo step, and redo puts the whole chord back.
  {
    PatternEditor editor(16, 8);
    editor.insertNote(2, 6, 40, 9);  // replaced by the chord, must come back on undo
    RecordWorkflowState state;
    state.cursorRow = 2;
    state.insertJump = 4;
    extracker::applyRecordChordWrite(editor, state, 2, {4, 5, 6}, {60, 64, 67}, 3, 90);
    check(editor.noteAt(2, 4) == 60 && editor.noteAt(2, 5) == 64 && editor.noteAt(2, 6) == 67, "chord written");
    check(editor.instrumentAt(2, 6) == 3 && editor.velocityAt(2, 5) == 90, "instrument and velocity");
    check(state.cursorRow == 6 && state.undoState.chordCells.size() == 2, "cursor moved once, one step");

    check(extracker::undoRecordWrite(editor, state), "undo");
    check(!editor.hasNoteAt(2, 4) && !editor.hasNoteAt(2, 5) && editor.noteAt(2, 6) == 40 &&
              editor.instrumentAt(2, 6) == 9,
          "undo takes the whole chord back and restores the replaced note");
    check(state.cursorRow == 2, "undo restores the cursor");

    check(extracker::redoRecordWrite(editor, state), "redo");
    check(editor.noteAt(2, 4) == 60 && editor.noteAt(2, 5) == 64 && editor.noteAt(2, 6) == 67 && state.cursorRow == 6,
          "redo puts the chord back");
  }

  // joinRecordWrite folds separate writes (MIDI chord notes) into one step.
  {
    PatternEditor editor(16, 8);
    RecordWorkflowState state;
    state.channel = 4;
    extracker::applyRecordWrite(editor, state, 0, 60, 1, 0, 100, false, 0, 0);
    const extracker::RecordEditState earlier = state.undoState;
    state.channel = 5;
    extracker::applyRecordWrite(editor, state, 0, 64, 1, 0, 100, false, 0, 0);
    extracker::joinRecordWrite(state, earlier);
    check(extracker::undoRecordWrite(editor, state), "undo joined");
    check(!editor.hasNoteAt(0, 4) && !editor.hasNoteAt(0, 5) && state.cursorRow == 0, "joined writes undone together");
  }

  // Chord cells survive a save/load of the record state.
  {
    PatternEditor editor(16, 8);
    RecordWorkflowState state;
    extracker::applyRecordChordWrite(editor, state, 1, {2, 3}, {50, 55}, 0, 100);
    std::ostringstream out;
    extracker::writeRecordState(out, state);
    check(out.str().find("RECORD_UNDO_CHORD 1 1 2 ") != std::string::npos, "chord cells written");

    RecordWorkflowState loaded;
    std::istringstream in(out.str());
    std::string token;
    bool ok = true;
    while (in >> token) {
      bool handled = false;
      ok = ok && extracker::applyRecordFileToken(in, token, editor.rows(), loaded, handled);
    }
    check(ok && loaded.canUndo && loaded.undoState.chordCells.size() == 1 && loaded.undoState.chordCells[0].channel == 2,
          "chord cells read back");
    check(extracker::undoRecordWrite(editor, loaded) && !editor.hasNoteAt(1, 2) && !editor.hasNoteAt(1, 3),
          "loaded chord undoes as one");

    std::ostringstream single;
    RecordWorkflowState plain;
    extracker::writeRecordState(single, plain);
    check(single.str().find("CHORD") == std::string::npos, "no chord line without chord cells");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "record chord undo test passed" << '\n';
  return 0;
}
