#pragma once

#include <unordered_map>
#include <vector>

#include "extracker/channel_manager.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/record_workflow.hpp"

namespace extracker {

// A chord recorded on a track row replaces the row's old chord: clears the
// notes (and note-offs) on the other channels of channel's track on that row,
// keeping their effects. `keep` are the channels the new chord uses. Returns
// the changed cells (for record undo); nothing outside a track.
std::vector<RecordCellEdit> clearTrackLeftovers(PatternEditor& editor,
                                                const ChannelManager& channels,
                                                int row,
                                                int channel,
                                                const std::vector<int>& keep);

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
