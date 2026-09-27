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
  return std::any_of(channels_.begin(), channels_.end(), [](const Channel& ch) { return ch.soloed; });
}

void ChannelManager::clearSolo() {
  for (Channel& ch : channels_) {
    ch.soloed = false;
  }
}

bool ChannelManager::isAudible(std::size_t channel) const {
  if (anySoloed()) {
    return isSoloed(channel);
  }
  return !isMuted(channel);
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

std::uint8_t ChannelManager::instrument(std::size_t channel) const {
  const Channel* ch = find(channel);
  return ch != nullptr ? ch->instrument : 0;
}

void ChannelManager::setInstrument(std::size_t channel, std::uint8_t instrument) {
  if (Channel* ch = ensure(channel)) {
    ch->instrument = instrument;
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

void writeChannelState(std::ostream& out, const ChannelManager& channels, std::size_t channelCount) {
  out << "CHANNEL_INSTRUMENTS";
  for (std::size_t ch = 0; ch < channelCount; ++ch) {
    out << " " << static_cast<int>(channels.instrument(ch));
  }
  out << "\n";

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
}

bool isChannelFileToken(const std::string& token) {
  return token == "CHANNEL_INSTRUMENTS" || token == "CHANNEL_MUTED" || token == "CHANNEL_VOLUME" ||
         token == "CHANNEL_NAMES";
}

bool applyChannelFileToken(std::istream& in, const std::string& token, ChannelManager& channels) {
  if (!isChannelFileToken(token)) {
    return false;
  }

  std::string line;
  std::getline(in, line);
  std::istringstream values(line);

  if (token == "CHANNEL_INSTRUMENTS") {
    int slot = 0;
    for (std::size_t ch = 0; values >> slot; ++ch) {
      channels.setInstrument(ch, static_cast<std::uint8_t>(std::clamp(slot, 0, 15)));
    }
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
  } else {
    std::string name;
    for (std::size_t ch = 0; values >> std::quoted(name); ++ch) {
      channels.setName(ch, name);
    }
  }
  return true;
}

}  // namespace extracker
