#include <iostream>
#include <string>

#include "extracker/channel_manager.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/track_instruments.hpp"

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
  using extracker::PatternEditor;

  ChannelManager channels(8);
  std::string error;
  channels.addTrack(2, 4, "Pad", error);

  // The lead is the lowest note in the track on that row; note-offs do not count.
  {
    PatternEditor editor(16, 8);
    check(extracker::trackLeadInstrument(editor, channels, 0, 3) == -1, "empty row has no lead");
    editor.insertNoteOff(0, 2);
    editor.insertNote(0, 3, 60, 5);
    editor.insertNote(0, 1, 60, 9);  // outside the track
    check(extracker::trackLeadInstrument(editor, channels, 0, 4) == 5, "lead skips note-offs and other channels");
    check(extracker::trackLeadInstrument(editor, channels, 0, 3, 3) == -1, "skipChannel ignores the target");
    check(extracker::trackLeadInstrument(editor, channels, 0, 1) == -1, "channel outside a track has no lead");
    check(extracker::trackLeadInstrument(editor, channels, 99, 3) == -1, "bad row");
  }

  // A new lead instrument reaches the notes that followed the old lead only.
  {
    PatternEditor editor(16, 8);
    editor.insertNote(0, 2, 60, 3);
    editor.insertNote(0, 3, 64, 3);
    editor.insertNote(0, 4, 67, 9);  // chosen by hand
    editor.insertNote(0, 5, 72, 3);  // outside the track
    const int previous = extracker::trackLeadInstrument(editor, channels, 0, 2);
    editor.insertNote(0, 2, 60, 7);
    const auto changed = extracker::followTrackLead(editor, channels, 0, 2, previous);
    check(changed.size() == 1 && changed[0] == 3, "only the following note changes");
    check(editor.instrumentAt(0, 3) == 7 && editor.instrumentAt(0, 4) == 9 && editor.instrumentAt(0, 5) == 3,
          "instruments after follow");

    // A note above the lead does not lead.
    const int beforeUpper = extracker::trackLeadInstrument(editor, channels, 0, 4);
    editor.insertNote(0, 4, 67, 11);
    check(extracker::followTrackLead(editor, channels, 0, 4, beforeUpper).empty(), "non-lead does not propagate");
  }

  // Without an earlier lead, every other note in the track follows.
  {
    PatternEditor editor(16, 8);
    editor.insertNote(0, 3, 64, 1);
    editor.insertNote(0, 4, 67, 2);
    const int previous = extracker::trackLeadInstrument(editor, channels, 0, 2, 2);
    check(previous == 1, "old lead was channel 3");
    editor.insertNote(0, 2, 60, 6);
    const auto changed = extracker::followTrackLead(editor, channels, 0, 2, previous);
    check(changed.size() == 1 && editor.instrumentAt(0, 3) == 6 && editor.instrumentAt(0, 4) == 2,
          "lead placed below takes over notes with the old lead's instrument");
    check(extracker::followTrackLead(editor, channels, 0, 2, -1).size() == 1 && editor.instrumentAt(0, 4) == 6,
          "no previous lead: all notes follow");
  }

  // Typed notes: above the lead they take its instrument, otherwise the selection.
  {
    PatternEditor editor(16, 8);
    check(extracker::chordEntryInstrument(editor, channels, 0, 3, 4) == 4, "no lead: selected instrument");
    editor.insertNote(0, 2, 60, 8);
    check(extracker::chordEntryInstrument(editor, channels, 0, 3, 4) == 8, "above the lead: lead instrument");
    check(extracker::chordEntryInstrument(editor, channels, 0, 2, 4) == 4, "typing over the lead: selected");
    check(extracker::chordEntryInstrument(editor, channels, 0, 1, 4) == 4, "outside the track: selected");
    editor.insertNote(0, 4, 67, 1);
    editor.clearStep(0, 2);
    check(extracker::chordEntryInstrument(editor, channels, 0, 2, 4) == 4, "below the lead: becomes the lead");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "track instruments test passed" << '\n';
  return 0;
}
