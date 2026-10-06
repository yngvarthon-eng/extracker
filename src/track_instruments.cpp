#include "extracker/track_instruments.hpp"

namespace extracker {

namespace {

bool isCell(const PatternEditor& editor, int row, int channel) {
  return row >= 0 && channel >= 0 && static_cast<std::size_t>(row) < editor.rows() &&
         static_cast<std::size_t>(channel) < editor.channels();
}

bool isPlayedNote(const PatternEditor& editor, int row, int channel) {
  return editor.hasNoteAt(row, channel) && editor.noteAt(row, channel) >= 0;
}

// Lowest channel in the track with a played note on row, or -1.
int leadChannel(const PatternEditor& editor, const ChannelManager::TrackInfo& track, int row, int skipChannel) {
  for (std::size_t ch = track.first; ch <= track.last && ch < editor.channels(); ++ch) {
    const int c = static_cast<int>(ch);
    if (c != skipChannel && isPlayedNote(editor, row, c)) {
      return c;
    }
  }
  return -1;
}

}  // namespace

int trackLeadInstrument(const PatternEditor& editor, const ChannelManager& channels, int row, int channel,
                        int skipChannel) {
  if (!isCell(editor, row, channel) || !channels.inTrack(static_cast<std::size_t>(channel))) {
    return -1;
  }
  const int lead = leadChannel(editor, channels.track(static_cast<std::size_t>(channel)), row, skipChannel);
  return lead < 0 ? -1 : editor.instrumentAt(row, lead);
}

std::uint8_t chordEntryInstrument(const PatternEditor& editor, const ChannelManager& channels, int row, int channel,
                                  std::uint8_t selected) {
  if (!isCell(editor, row, channel) || !channels.inTrack(static_cast<std::size_t>(channel))) {
    return selected;
  }
  const int lead = leadChannel(editor, channels.track(static_cast<std::size_t>(channel)), row, channel);
  return lead >= 0 && lead < channel ? editor.instrumentAt(row, lead) : selected;
}

std::vector<int> followTrackLead(PatternEditor& editor, const ChannelManager& channels, int row, int channel,
                                 int previousLead) {
  std::vector<int> changed;
  if (!isCell(editor, row, channel) || !channels.inTrack(static_cast<std::size_t>(channel))) {
    return changed;
  }
  const ChannelManager::TrackInfo track = channels.track(static_cast<std::size_t>(channel));
  if (leadChannel(editor, track, row, -1) != channel) {
    return changed;
  }
  const std::uint8_t instrument = editor.instrumentAt(row, channel);
  for (std::size_t ch = track.first; ch <= track.last && ch < editor.channels(); ++ch) {
    const int c = static_cast<int>(ch);
    if (c == channel || !isPlayedNote(editor, row, c) || editor.instrumentAt(row, c) == instrument) {
      continue;
    }
    if (previousLead < 0 || editor.instrumentAt(row, c) == previousLead) {
      editor.setInstrument(row, c, instrument);
      changed.push_back(c);
    }
  }
  return changed;
}

}  // namespace extracker
