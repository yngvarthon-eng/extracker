#include "extracker/chord_recorder.hpp"

#include <algorithm>

namespace extracker {

std::vector<RecordCellEdit> clearTrackLeftovers(PatternEditor& editor,
                                                const ChannelManager& channels,
                                                int row,
                                                int channel,
                                                const std::vector<int>& keep) {
  std::vector<RecordCellEdit> cleared;
  if (row < 0 || channel < 0 || static_cast<std::size_t>(row) >= editor.rows() ||
      !channels.inTrack(static_cast<std::size_t>(channel))) {
    return cleared;
  }
  const ChannelManager::TrackInfo track = channels.track(static_cast<std::size_t>(channel));
  for (std::size_t ch = track.first; ch <= track.last && ch < editor.channels(); ++ch) {
    const int c = static_cast<int>(ch);
    if (std::find(keep.begin(), keep.end(), c) != keep.end() || !editor.hasNoteAt(row, c)) {
      continue;
    }
    RecordCellEdit edit;
    edit.row = row;
    edit.channel = c;
    edit.beforeStep = captureStep(editor, row, c);
    const std::uint8_t effectCommand = editor.effectCommandAt(row, c);
    const std::uint8_t effectValue = editor.effectValueAt(row, c);
    editor.clearStep(row, c);
    if (effectCommand != 0 || effectValue != 0) {
      editor.setEffect(row, c, effectCommand, effectValue);
    }
    edit.afterStep = captureStep(editor, row, c);
    cleared.push_back(edit);
  }
  return cleared;
}

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
