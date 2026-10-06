#pragma once

#include <vector>

#include "extracker/channel_manager.hpp"
#include "extracker/pattern_editor.hpp"

namespace extracker {

// Chord notes in a track share an instrument through the track's lead note:
// the note (not a note-off) on the lowest channel of the track on a row.

// Instrument of the lead note on row in channel's track, ignoring skipChannel
// (-1 to ignore nothing). Returns -1 if channel is not in a track or the row
// has no other note in it.
int trackLeadInstrument(const PatternEditor& editor, const ChannelManager& channels, int row, int channel,
                        int skipChannel = -1);

// Call after writing a note with an explicit instrument at (row, channel),
// passing the lead instrument from before the write. If channel now holds the
// lead note, the track's other notes on that row that followed the old lead
// (instrument == previousLead, or all of them when previousLead < 0) take the
// new instrument. Returns the channels changed.
std::vector<int> followTrackLead(PatternEditor& editor, const ChannelManager& channels, int row, int channel,
                                 int previousLead);

}  // namespace extracker
