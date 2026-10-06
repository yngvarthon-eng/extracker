#pragma once

#include <unordered_map>

#include "extracker/channel_manager.hpp"

namespace extracker {

// Spreads recorded chords over a track: while a note is held on a track, the
// next notes join its row, each on the track's lowest channel not already
// holding a note of that row. Outside a track notes go to the record channel.
class ChordRecorder {
public:
  struct Placement {
    int row = -1;
    int channel = -1;  // -1: every channel of the track is taken
  };

  // Row of a chord still held on recordChannel's track, or -1 (always -1
  // outside a track). Callers record the next note on this row when set.
  int heldChordRow(const ChannelManager& channels, int recordChannel) const;

  // Picks the channel for midiNote recorded at row and remembers it as held.
  Placement noteOn(const ChannelManager& channels, int recordChannel, int row, int midiNote);

  // Forgets a released note; returns where it was recorded (row -1 if unknown).
  Placement noteOff(int midiNote);

  void clear() { held_.clear(); }

private:
  std::unordered_map<int, Placement> held_;
};

}  // namespace extracker
