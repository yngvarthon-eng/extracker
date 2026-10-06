#include <iostream>
#include <string>

#include "extracker/channel_manager.hpp"
#include "extracker/chord_recorder.hpp"

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

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "chord recorder test passed" << '\n';
  return 0;
}
