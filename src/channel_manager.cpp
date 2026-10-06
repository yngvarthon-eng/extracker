#include "extracker/channel_manager.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>

namespace extracker {

ChannelManager::ChannelManager(std::size_t channels) {
  resize(channels);
}

void ChannelManager::resize(std::size_t channels) {
  channels_.resize(std::min(channels, kMaxChannels));
  normalizeTracks();
}

const std::string& ChannelManager::name(std::size_t channel) const {
  static const std::string kEmpty;
  const Channel* ch = find(channel);
  return ch != nullptr ? ch->name : kEmpty;
}

std::string ChannelManager::displayName(std::size_t channel) const {
  const std::string& n = name(channel);
  return n.empty() ? "CH " + std::to_string(channel) : n;
}

void ChannelManager::setName(std::size_t channel, const std::string& name) {
  if (Channel* ch = ensure(channel)) {
    ch->name = name;
  }
}

bool ChannelManager::isMuted(std::size_t channel) const {
  const Channel* ch = find(channel);
  return ch != nullptr && ch->muted;
}

void ChannelManager::setMuted(std::size_t channel, bool muted) {
  if (Channel* ch = ensure(channel)) {
    ch->muted = muted;
  }
}

bool ChannelManager::isSoloed(std::size_t channel) const {
  const Channel* ch = find(channel);
  return ch != nullptr && ch->soloed;
}

void ChannelManager::setSoloed(std::size_t channel, bool soloed) {
  if (Channel* ch = ensure(channel)) {
    ch->soloed = soloed;
  }
}

bool ChannelManager::anySoloed() const {
  return std::any_of(channels_.begin(), channels_.end(), [](const Channel& ch) { return ch.soloed; }) ||
         std::any_of(tracks_.begin(), tracks_.end(), [](const Track& track) { return track.soloed; });
}

void ChannelManager::clearSolo() {
  for (Channel& ch : channels_) {
    ch.soloed = false;
  }
  for (Track& track : tracks_) {
    track.soloed = false;
  }
}

bool ChannelManager::isAudible(std::size_t channel) const {
  const Track* track = trackOf(channel);
  if (anySoloed()) {
    return isSoloed(channel) || (track != nullptr && track->soloed && !isMuted(channel));
  }
  return !isMuted(channel) && (track == nullptr || !track->muted);
}

float ChannelManager::volume(std::size_t channel) const {
  const Channel* ch = find(channel);
  return ch != nullptr ? ch->volume : 1.0f;
}

void ChannelManager::setVolume(std::size_t channel, float volume) {
  if (Channel* ch = ensure(channel)) {
    ch->volume = std::clamp(volume, 0.0f, kMaxVolume);
  }
}

float ChannelManager::effectiveVolume(std::size_t channel) const {
  const Track* track = trackOf(channel);
  return volume(channel) * (track != nullptr ? track->volume : 1.0f);
}

bool ChannelManager::addTrack(std::size_t first, std::size_t last, const std::string& name, std::string& error) {
  if (first > last) {
    error = "first channel " + std::to_string(first) + " is after last channel " + std::to_string(last);
    return false;
  }
  if (last >= kMaxChannels) {
    error = "channel " + std::to_string(last) + " is out of range";
    return false;
  }
  for (std::size_t ch = first; ch <= last; ++ch) {
    if (inTrack(ch)) {
      const TrackInfo other = track(ch);
      error = "channel " + std::to_string(ch) + " is already in track " + std::to_string(other.first) + "-" +
              std::to_string(other.last);
      return false;
    }
  }
  Track track;
  track.id = nextTrackId_++;
  track.name = name;
  tracks_.push_back(track);
  for (std::size_t ch = first; ch <= last; ++ch) {
    ensure(ch)->track = track.id;
  }
  return true;
}

bool ChannelManager::removeTrack(std::size_t channel) {
  const Track* track = trackOf(channel);
  if (track == nullptr) {
    return false;
  }
  const int id = track->id;
  for (Channel& ch : channels_) {
    if (ch.track == id) {
      ch.track = -1;
    }
  }
  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [id](const Track& t) { return t.id == id; }),
                tracks_.end());
  return true;
}

bool ChannelManager::inTrack(std::size_t channel) const {
  return trackOf(channel) != nullptr;
}

ChannelManager::TrackInfo ChannelManager::track(std::size_t channel) const {
  TrackInfo info;
  const Track* track = trackOf(channel);
  if (track == nullptr) {
    info.first = info.last = channel;
    return info;
  }
  info.first = channel;
  while (info.first > 0 && channels_[info.first - 1].track == track->id) {
    --info.first;
  }
  info.last = channel;
  while (info.last + 1 < channels_.size() && channels_[info.last + 1].track == track->id) {
    ++info.last;
  }
  info.name = track->name;
  info.muted = track->muted;
  info.soloed = track->soloed;
  info.volume = track->volume;
  return info;
}

std::vector<ChannelManager::TrackInfo> ChannelManager::tracks() const {
  std::vector<TrackInfo> result;
  for (std::size_t ch = 0; ch < channels_.size(); ++ch) {
    if (inTrack(ch) && (ch == 0 || channels_[ch - 1].track != channels_[ch].track)) {
      result.push_back(track(ch));
    }
  }
  return result;
}

void ChannelManager::setTrackName(std::size_t channel, const std::string& name) {
  if (Track* track = trackOf(channel)) {
    track->name = name;
  }
}

void ChannelManager::setTrackMuted(std::size_t channel, bool muted) {
  if (Track* track = trackOf(channel)) {
    track->muted = muted;
  }
}

void ChannelManager::setTrackSoloed(std::size_t channel, bool soloed) {
  if (Track* track = trackOf(channel)) {
    track->soloed = soloed;
  }
}

void ChannelManager::setTrackVolume(std::size_t channel, float volume) {
  if (Track* track = trackOf(channel)) {
    track->volume = std::clamp(volume, 0.0f, kMaxVolume);
  }
}

const BiquadParams& ChannelManager::filter(std::size_t channel) const {
  static const BiquadParams kNoFilter;
  const Channel* ch = find(channel);
  return ch != nullptr ? ch->filter : kNoFilter;
}

void ChannelManager::setFilter(std::size_t channel, const BiquadParams& filter) {
  if (Channel* ch = ensure(channel)) {
    ch->filter = filter;
  }
}

void ChannelManager::remap(const std::vector<int>& sources) {
  std::vector<Channel> remapped;
  remapped.reserve(std::min(sources.size(), kMaxChannels));
  for (std::size_t i = 0; i < sources.size() && i < kMaxChannels; ++i) {
    const int source = sources[i];
    remapped.push_back(source >= 0 && static_cast<std::size_t>(source) < channels_.size()
                           ? channels_[static_cast<std::size_t>(source)]
                           : Channel{});
  }
  channels_.swap(remapped);
  normalizeTracks();
}

void ChannelManager::normalizeTracks() {
  // A channel between two channels of the same track joins it (an inserted
  // channel, or one moved into the middle of a chord).
  for (std::size_t ch = 1; ch + 1 < channels_.size(); ++ch) {
    const int left = channels_[ch - 1].track;
    if (left >= 0 && left == channels_[ch + 1].track) {
      channels_[ch].track = left;
    }
  }

  // Keep each track's longest run (the first on a tie); other members leave.
  for (const Track& track : tracks_) {
    std::size_t bestFirst = 0;
    std::size_t bestLength = 0;
    for (std::size_t ch = 0; ch < channels_.size();) {
      if (channels_[ch].track != track.id) {
        ++ch;
        continue;
      }
      const std::size_t runFirst = ch;
      while (ch < channels_.size() && channels_[ch].track == track.id) {
        ++ch;
      }
      if (ch - runFirst > bestLength) {
        bestFirst = runFirst;
        bestLength = ch - runFirst;
      }
    }
    for (std::size_t ch = 0; ch < channels_.size(); ++ch) {
      if (channels_[ch].track == track.id && (ch < bestFirst || ch >= bestFirst + bestLength)) {
        channels_[ch].track = -1;
      }
    }
  }

  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                               [this](const Track& track) {
                                 return std::none_of(channels_.begin(), channels_.end(),
                                                     [&track](const Channel& ch) { return ch.track == track.id; });
                               }),
                tracks_.end());
}

ChannelManager::Channel* ChannelManager::ensure(std::size_t channel) {
  if (channel >= kMaxChannels) {
    return nullptr;
  }
  if (channel >= channels_.size()) {
    channels_.resize(channel + 1);
  }
  return &channels_[channel];
}

const ChannelManager::Channel* ChannelManager::find(std::size_t channel) const {
  return channel < channels_.size() ? &channels_[channel] : nullptr;
}

const ChannelManager::Track* ChannelManager::trackOf(std::size_t channel) const {
  const Channel* ch = find(channel);
  if (ch == nullptr || ch->track < 0) {
    return nullptr;
  }
  for (const Track& track : tracks_) {
    if (track.id == ch->track) {
      return &track;
    }
  }
  return nullptr;
}

ChannelManager::Track* ChannelManager::trackOf(std::size_t channel) {
  return const_cast<Track*>(static_cast<const ChannelManager*>(this)->trackOf(channel));
}

void writeChannelState(std::ostream& out, const ChannelManager& channels, std::size_t channelCount) {
  out << "CHANNEL_MUTED";
  for (std::size_t ch = 0; ch < channelCount; ++ch) {
    out << " " << (channels.isMuted(ch) ? 1 : 0);
  }
  out << "\n";

  out << "CHANNEL_VOLUME";
  for (std::size_t ch = 0; ch < channelCount; ++ch) {
    out << " " << static_cast<int>(std::lround(channels.volume(ch) * 100.0f));
  }
  out << "\n";

  out << "CHANNEL_NAMES";
  for (std::size_t ch = 0; ch < channelCount; ++ch) {
    out << " " << std::quoted(channels.name(ch));
  }
  out << "\n";

  out << "CHANNEL_FILTERS";
  for (std::size_t ch = 0; ch < channelCount; ++ch) {
    const BiquadParams& filter = channels.filter(ch);
    out << " " << static_cast<int>(filter.type)
        << " " << static_cast<int>(std::lround(filter.cutoffNorm * 255.0f))
        << " " << static_cast<int>(std::lround(filter.resonanceNorm * 255.0f));
  }
  out << "\n";

  bool anyTrack = false;
  for (const ChannelManager::TrackInfo& track : channels.tracks()) {
    if (track.last >= channelCount) {
      continue;
    }
    out << (anyTrack ? "" : "CHANNEL_TRACKS") << " " << track.first << " " << track.last << " "
        << (track.muted ? 1 : 0) << " " << static_cast<int>(std::lround(track.volume * 100.0f)) << " "
        << std::quoted(track.name);
    anyTrack = true;
  }
  if (anyTrack) {
    out << "\n";
  }
}

bool isChannelFileToken(const std::string& token) {
  return token == "CHANNEL_INSTRUMENTS" || token == "CHANNEL_MUTED" || token == "CHANNEL_VOLUME" ||
         token == "CHANNEL_NAMES" || token == "CHANNEL_FILTERS" || token == "CHANNEL_TRACKS";
}

bool applyChannelFileToken(std::istream& in, const std::string& token, ChannelManager& channels) {
  if (!isChannelFileToken(token)) {
    return false;
  }

  std::string line;
  std::getline(in, line);
  std::istringstream values(line);

  if (token == "CHANNEL_INSTRUMENTS") {
    // Older files: per-channel default instruments, no longer used.
  } else if (token == "CHANNEL_MUTED") {
    int muted = 0;
    for (std::size_t ch = 0; values >> muted; ++ch) {
      channels.setMuted(ch, muted != 0);
    }
  } else if (token == "CHANNEL_VOLUME") {
    int pct = 100;
    for (std::size_t ch = 0; values >> pct; ++ch) {
      channels.setVolume(ch, static_cast<float>(std::clamp(pct, 0, 200)) / 100.0f);
    }
  } else if (token == "CHANNEL_FILTERS") {
    int type = 0;
    int cutoff = 255;
    int resonance = 0;
    for (std::size_t ch = 0; values >> type >> cutoff >> resonance; ++ch) {
      BiquadParams filter;
      filter.type = static_cast<BiquadType>(std::clamp(type, 0, 4));
      filter.cutoffNorm = static_cast<float>(std::clamp(cutoff, 0, 255)) / 255.0f;
      filter.resonanceNorm = static_cast<float>(std::clamp(resonance, 0, 255)) / 255.0f;
      channels.setFilter(ch, filter);
    }
  } else if (token == "CHANNEL_TRACKS") {
    std::size_t first = 0;
    std::size_t last = 0;
    int muted = 0;
    int pct = 100;
    std::string name;
    std::string error;
    while (values >> first >> last >> muted >> pct >> std::quoted(name)) {
      if (channels.addTrack(first, last, name, error)) {
        channels.setTrackMuted(first, muted != 0);
        channels.setTrackVolume(first, static_cast<float>(std::clamp(pct, 0, 200)) / 100.0f);
      }
    }
  } else {
    std::string name;
    for (std::size_t ch = 0; values >> std::quoted(name); ++ch) {
      channels.setName(ch, name);
    }
  }
  return true;
}

}  // namespace extracker
