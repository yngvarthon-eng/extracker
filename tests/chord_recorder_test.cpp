#include <iostream>
#include <string>

#include "extracker/channel_manager.hpp"
#include "extracker/chord_recorder.hpp"
#include "extracker/pattern_editor.hpp"

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
  using extracker::ChannelManager;
  using extracker::ChordRecorder;

  ChannelManager channels(8);
  std::string error;
  channels.addTrack(4, 6, "Pad", error);

  // Notes held together spread over the track from its first channel.
  {
    ChordRecorder recorder;
    check(recorder.heldChordRow(channels, 5) == -1, "nothing held");
    check(recorder.noteOn(channels, 5, 3, 60).channel == 4, "first note on the track's first channel");
    check(recorder.heldChordRow(channels, 5) == 3 && recorder.heldChordRow(channels, 6) == 3,
          "held chord row seen from any track channel");
    check(recorder.heldChordRow(channels, 2) == -1, "outside the track: no chord row");
    check(recorder.noteOn(channels, 5, 3, 64).channel == 5, "second note on the next channel");
    check(recorder.noteOn(channels, 5, 3, 67).channel == 6, "third note on the last channel");
    const auto full = recorder.noteOn(channels, 5, 3, 71);
    check(full.channel == -1, "track full: note not placed");

    const auto released = recorder.noteOff(64);
    check(released.row == 3 && released.channel == 5, "note-off returns the placement");
    check(recorder.noteOn(channels, 5, 3, 65).channel == 5, "freed channel is reused");
    check(recorder.noteOff(99).row == -1, "unknown note-off");

    recorder.noteOff(60);
    recorder.noteOff(65);
    recorder.noteOff(67);
    check(recorder.heldChordRow(channels, 5) == -1, "all released: no chord row");
  }

  // A held note on another row does not block channels of a new row.
  {
    ChordRecorder recorder;
    recorder.noteOn(channels, 4, 0, 48);
    check(recorder.noteOn(channels, 4, 8, 60).channel == 4, "other row: first channel again");
  }

  // Retriggering a held note picks its channel again instead of taking a second one.
  {
    ChordRecorder recorder;
    recorder.noteOn(channels, 4, 0, 60);
    check(recorder.noteOn(channels, 4, 0, 60).channel == 4, "retrigger reuses the channel");
    check(recorder.noteOn(channels, 4, 0, 64).channel == 5, "next note still on the next channel");
  }

  // Outside a track every note goes to the record channel.
  {
    ChordRecorder recorder;
    check(recorder.noteOn(channels, 2, 0, 60).channel == 2 && recorder.noteOn(channels, 2, 0, 64).channel == 2,
          "no track: record channel");
    check(recorder.heldChordRow(channels, 2) == -1, "no track: never a chord row");
  }

  // A new chord clears the old chord's other notes on that track row, keeping effects.
  {
    extracker::PatternEditor editor(16, 8);
    editor.insertNote(1, 4, 60, 1);
    editor.insertNote(1, 5, 64, 1);
    editor.insertNote(1, 6, 67, 1);
    editor.setEffect(1, 6, 0x0C, 0x20);
    editor.insertNote(1, 7, 71, 1);  // outside the track
    editor.insertNote(2, 6, 50, 1);  // other row
    const auto cleared = extracker::clearTrackLeftovers(editor, channels, 1, 5, {4, 5});
    check(cleared.size() == 1 && cleared[0].channel == 6, "only the unused track channel is cleared");
    check(!editor.hasNoteAt(1, 6) && editor.effectCommandAt(1, 6) == 0x0C && editor.effectValueAt(1, 6) == 0x20,
          "note cleared, effect kept");
    check(editor.hasNoteAt(1, 4) && editor.hasNoteAt(1, 7) && editor.hasNoteAt(2, 6), "other cells untouched");
    check(cleared[0].beforeStep.hasNote && cleared[0].beforeStep.note == 67 && !cleared[0].afterStep.hasNote,
          "undo cell records the old note");
    check(extracker::clearTrackLeftovers(editor, channels, 1, 2, {}).empty(), "nothing outside a track");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "chord recorder test passed" << '\n';
  return 0;
}
