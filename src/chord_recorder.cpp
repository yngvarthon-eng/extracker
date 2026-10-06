#include "extracker/chord_recorder.hpp"

namespace extracker {

namespace {

bool sameTrack(const ChannelManager& channels, int a, int b) {
  if (a < 0 || b < 0 || !channels.inTrack(static_cast<std::size_t>(a)) ||
      !channels.inTrack(static_cast<std::size_t>(b))) {
    return false;
  }
  return channels.track(static_cast<std::size_t>(a)).first == channels.track(static_cast<std::size_t>(b)).first;
}

}  // namespace

int ChordRecorder::heldChordRow(const ChannelManager& channels, int recordChannel) const {
  for (const auto& [note, placement] : held_) {
    (void)note;
    if (sameTrack(channels, placement.channel, recordChannel)) {
      return placement.row;
    }
  }
  return -1;
}

ChordRecorder::Placement ChordRecorder::noteOn(const ChannelManager& channels, int recordChannel, int row,
                                               int midiNote) {
  held_.erase(midiNote);  // a retriggered note picks its channel again
  Placement placement;
  placement.row = row;
  if (recordChannel < 0 || !channels.inTrack(static_cast<std::size_t>(recordChannel))) {
    placement.channel = recordChannel;
  } else {
    const ChannelManager::TrackInfo track = channels.track(static_cast<std::size_t>(recordChannel));
    for (std::size_t ch = track.first; ch <= track.last && placement.channel < 0; ++ch) {
      bool taken = false;
      for (const auto& [note, held] : held_) {
        (void)note;
        taken = taken || (held.row == row && held.channel == static_cast<int>(ch));
      }
      if (!taken) {
        placement.channel = static_cast<int>(ch);
      }
    }
  }
  if (placement.channel >= 0) {
    held_[midiNote] = placement;
  }
  return placement;
}

ChordRecorder::Placement ChordRecorder::noteOff(int midiNote) {
  const auto it = held_.find(midiNote);
  if (it == held_.end()) {
    return {};
  }
  const Placement placement = it->second;
  held_.erase(it);
  return placement;
}

}  // namespace extracker
