#include <cstdint>
#include <iostream>

#include "extracker/pattern_clipboard.hpp"
#include "extracker/pattern_editor.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

using extracker::PatternClipboard;
using extracker::PatternEditor;
using Mode = PatternClipboard::PasteMode;

PatternClipboard::PasteOptions withMode(Mode mode) {
  PatternClipboard::PasteOptions options;
  options.mode = mode;
  return options;
}

}  // namespace

int main() {
  // Copy/paste preserves every step field, including note-offs and fx-only steps.
  {
    PatternEditor editor(16, 4);
    editor.insertNote(0, 0, 60, static_cast<std::uint8_t>(2), 24, static_cast<std::uint8_t>(90), true, 0x0A, 0x0F);
    editor.setSample(0, 0, 7);
    editor.insertNoteOff(1, 0);
    editor.setEffect(2, 1, 0x0C, 0x20);

    PatternClipboard clipboard;
    check(clipboard.empty(), "new clipboard is empty");
    check(clipboard.copy(editor, 0, 2, 0, 1), "copy succeeds");
    check(clipboard.rows() == 3 && clipboard.channels() == 2, "copy dimensions");

    const auto result = clipboard.paste(editor, 8, 2);
    check(result.changes.size() == 6 && result.skippedOutOfBounds == 0, "overwrite writes full block");

    check(editor.hasNoteAt(8, 2) && editor.noteAt(8, 2) == 60 && editor.instrumentAt(8, 2) == 2 &&
              editor.sampleAt(8, 2) == 7 && editor.gateTicksAt(8, 2) == 24 && editor.velocityAt(8, 2) == 90 &&
              editor.retriggerAt(8, 2) && editor.effectCommandAt(8, 2) == 0x0A && editor.effectValueAt(8, 2) == 0x0F,
          "note step fields preserved");
    check(editor.hasNoteAt(9, 2) && editor.noteAt(9, 2) == PatternEditor::kNoteOff, "note-off preserved");
    check(!editor.hasNoteAt(10, 3) && editor.effectCommandAt(10, 3) == 0x0C && editor.effectValueAt(10, 3) == 0x20,
          "fx-only step preserved");
  }

  // Overwrite replaces the whole cell: no stale sample left from the target.
  {
    PatternEditor editor(4, 1);
    editor.insertNote(0, 0, 60);
    editor.insertNote(1, 0, 62);
    editor.setSample(1, 0, 9);

    PatternClipboard clipboard;
    clipboard.copy(editor, 0, 0, 0, 0);
    clipboard.paste(editor, 1, 0);
    check(editor.noteAt(1, 0) == 60 && editor.sampleAt(1, 0) == PatternClipboard::kNoSample,
          "overwrite drops target sample");
  }

  // Row stride on copy and paste.
  {
    PatternEditor editor(16, 1);
    editor.insertNote(0, 0, 60);
    editor.insertNote(1, 0, 61);
    editor.insertNote(2, 0, 62);

    PatternClipboard clipboard;
    check(clipboard.copy(editor, 0, 2, 0, 0, 2), "stride copy succeeds");
    check(clipboard.rows() == 2 && clipboard.sourceRowStep() == 2, "stride copy picks every 2nd row");
    PatternClipboard::PasteOptions options;
    options.rowStep = 3;
    clipboard.paste(editor, 8, 0, options);
    check(editor.noteAt(8, 0) == 60 && editor.noteAt(11, 0) == 62 && !editor.hasNoteAt(9, 0),
          "stride paste spaces rows");
  }

  // Overwrite clears with empty source cells, Merge leaves them, Mix only fills empties.
  {
    PatternEditor source(4, 1);
    source.insertNote(0, 0, 60);
    PatternClipboard clipboard;
    clipboard.copy(source, 0, 1, 0, 0);  // row 0 note, row 1 empty

    PatternEditor merged(4, 1);
    merged.insertNote(0, 0, 70);
    merged.insertNote(1, 0, 71);
    const auto mergePlan = clipboard.paste(merged, 0, 0, withMode(Mode::Merge));
    check(mergePlan.changes.size() == 1 && merged.noteAt(0, 0) == 60 && merged.noteAt(1, 0) == 71,
          "merge replaces only from non-empty cells");

    PatternEditor mixed(4, 1);
    mixed.insertNote(0, 0, 70);
    clipboard.paste(mixed, 0, 0, withMode(Mode::Mix));
    check(mixed.noteAt(0, 0) == 70, "mix keeps an occupied target");
    clipboard.paste(mixed, 2, 0, withMode(Mode::Mix));
    check(mixed.noteAt(2, 0) == 60, "mix fills an empty target");

    PatternEditor overwritten(4, 1);
    overwritten.insertNote(1, 0, 71);
    clipboard.paste(overwritten, 0, 0, withMode(Mode::Overwrite));
    check(overwritten.noteAt(0, 0) == 60 && !overwritten.hasNoteAt(1, 0), "overwrite clears with empty cells");
  }

  // Column masks.
  {
    PatternEditor source(2, 1);
    source.insertNote(0, 0, 60, static_cast<std::uint8_t>(3), 0, static_cast<std::uint8_t>(40), false, 0x0A, 0x01);
    PatternClipboard clipboard;
    clipboard.copy(source, 0, 0, 0, 0);

    PatternEditor target(2, 1);
    target.insertNote(0, 0, 72, static_cast<std::uint8_t>(5), 0, static_cast<std::uint8_t>(110), false, 0x0C, 0x30);

    PatternClipboard::PasteOptions fxOnly;
    fxOnly.columns = PatternClipboard::kEffectColumn;
    clipboard.paste(target, 0, 0, fxOnly);
    check(target.noteAt(0, 0) == 72 && target.instrumentAt(0, 0) == 5 && target.velocityAt(0, 0) == 110 &&
              target.effectCommandAt(0, 0) == 0x0A && target.effectValueAt(0, 0) == 0x01,
          "effect-only paste keeps note, instrument and volume");

    PatternClipboard::PasteOptions volumeOnly;
    volumeOnly.mode = Mode::Merge;
    volumeOnly.columns = PatternClipboard::kVolumeColumn;
    clipboard.paste(target, 0, 0, volumeOnly);
    check(target.noteAt(0, 0) == 72 && target.instrumentAt(0, 0) == 5 && target.velocityAt(0, 0) == 40,
          "volume-only merge onto an existing note");

    PatternEditor empty(2, 1);
    const auto instrumentPlan = clipboard.paste(empty, 0, 0, [] {
      PatternClipboard::PasteOptions options;
      options.columns = PatternClipboard::kInstrumentColumn;
      return options;
    }());
    check(instrumentPlan.changes.empty() && !empty.hasNoteAt(0, 0), "instrument without a note is not written");

    PatternClipboard::PasteOptions noteOnly;
    noteOnly.columns = PatternClipboard::kNoteColumn;
    clipboard.paste(target, 1, 0, noteOnly);
    check(target.noteAt(1, 0) == 60 && target.instrumentAt(1, 0) == 0 && target.effectCommandAt(1, 0) == 0,
          "note-only paste leaves the other columns at their target values");
  }

  // Insert pushes rows down in the pasted channels only.
  {
    PatternEditor editor(6, 2);
    editor.insertNote(0, 0, 60);
    editor.insertNote(2, 0, 62);
    editor.insertNote(5, 0, 65);  // pushed off the end
    editor.insertNote(2, 1, 50);  // other channel, untouched

    PatternClipboard clipboard;
    clipboard.copy(editor, 0, 0, 0, 0);
    const auto plan = clipboard.paste(editor, 2, 0, withMode(Mode::Insert));
    check(editor.noteAt(2, 0) == 60 && editor.noteAt(3, 0) == 62 && !editor.hasNoteAt(5, 0),
          "insert pushes the column down");
    check(editor.noteAt(2, 1) == 50, "insert leaves other channels alone");
    check(plan.changes.size() == 3, "insert plan lists pasted and moved cells");
  }

  // Flood repeats the block to the end of the pattern.
  {
    PatternEditor editor(8, 1);
    editor.insertNote(0, 0, 60);
    PatternClipboard clipboard;
    clipboard.copy(editor, 0, 1, 0, 0);  // note + empty row

    PatternClipboard::PasteOptions options;
    options.flood = true;
    const auto plan = clipboard.paste(editor, 2, 0, options);
    check(editor.noteAt(2, 0) == 60 && editor.noteAt(4, 0) == 60 && editor.noteAt(6, 0) == 60 &&
              !editor.hasNoteAt(7, 0),
          "flood repeats every block height");
    check(plan.skippedOutOfBounds == 0, "flood does not count its cut-off tail as skipped");
  }

  // Ranges are clamped; paste counts steps that fall off the pattern; dry plans do not write.
  {
    PatternEditor editor(8, 2);
    editor.insertNote(7, 1, 64);

    PatternClipboard clipboard;
    check(!clipboard.copy(editor, 20, 30, 0, 1), "fully out-of-range copy fails");
    check(clipboard.empty(), "failed copy leaves clipboard empty");
    check(clipboard.copy(editor, 6, 99, 1, 99), "partially out-of-range copy clamps");
    check(clipboard.rows() == 2 && clipboard.channels() == 1, "clamped dimensions");

    const auto dry = clipboard.plan(editor, 7, 0, withMode(Mode::Overwrite));
    check(dry.changes.size() == 1 && !editor.hasNoteAt(7, 0), "plan does not modify the editor");

    const auto result = clipboard.paste(editor, 7, 0);
    check(result.changes.size() == 1 && result.skippedOutOfBounds == 1, "out-of-bounds paste steps counted");
  }

  // Clipboard history: newest first, dedupe, select, cap, describe.
  {
    PatternEditor editor(16, 2);
    editor.insertNote(0, 0, 60);
    editor.insertNote(1, 0, 63);
    editor.insertNoteOff(2, 0);
    editor.setEffect(3, 1, 0x0A, 0x01);

    extracker::ClipboardHistory history;
    check(history.empty() && history.active().empty(), "empty history has an empty active clipboard");

    PatternClipboard a;
    a.copy(editor, 0, 3, 0, 1);
    check(a.describe() == "4x2  rows 0-3 ch 0-1  3 notes 1 fx C-5 D#5 ^^^", "describe summarises the block");

    PatternClipboard b;
    b.copy(editor, 0, 0, 0, 0);
    history.push(a);
    history.push(b);
    check(history.size() == 2 && history.entry(0).rows() == 1 && history.activeIndex() == 0, "newest first");

    check(history.select(1) && history.active().rows() == 4, "select older slot");
    check(!history.select(5), "select out of range fails");

    PatternClipboard aAgain;
    aAgain.copy(editor, 0, 3, 0, 1);
    history.push(aAgain);
    check(history.size() == 2 && history.entry(0).rows() == 4 && history.activeIndex() == 0,
          "copying the same content again moves it to the front");

    for (int row = 4; row < 16; ++row) {
      PatternClipboard single;
      editor.insertNote(row, 0, 40 + row);
      single.copy(editor, row, row, 0, 0);
      history.push(single);
    }
    check(history.size() == extracker::ClipboardHistory::kMaxEntries, "history is capped");

    history.clear();
    check(history.empty(), "clear empties history");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "pattern clipboard test passed" << '\n';
  return 0;
}
