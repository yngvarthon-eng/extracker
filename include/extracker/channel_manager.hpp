#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace extracker {

// Per-channel mixer state shared by the CLI, the GUI and the sequencer.
// Reads outside the current channel count return defaults; setters grow the
// channel list as needed (up to kMaxChannels) so load order does not matter.
class ChannelManager {
public:
  static constexpr std::size_t kMaxChannels = 64;
  static constexpr float kMaxVolume = 2.0f;

  explicit ChannelManager(std::size_t channels = 0);

  std::size_t count() const { return channels_.size(); }
  // Grows with default channels or truncates; existing channels keep state.
  void resize(std::size_t channels);

  const std::string& name(std::size_t channel) const;
  // Name if set, otherwise "CH <n>".
  std::string displayName(std::size_t channel) const;
  void setName(std::size_t channel, const std::string& name);

  bool isMuted(std::size_t channel) const;
  void setMuted(std::size_t channel, bool muted);

  bool isSoloed(std::size_t channel) const;
  void setSoloed(std::size_t channel, bool soloed);
  bool anySoloed() const;
  void clearSolo();

  // Solo wins over mute: while any channel is soloed, exactly the soloed
  // channels are audible; otherwise every unmuted channel is.
  bool isAudible(std::size_t channel) const;

  // Linear gain, 1.0 = unity, clamped to 0..kMaxVolume.
  float volume(std::size_t channel) const;
  void setVolume(std::size_t channel, float volume);

  // Default instrument slot for notes entered on this channel.
  std::uint8_t instrument(std::size_t channel) const;
  void setInstrument(std::size_t channel, std::uint8_t instrument);

  // Rebuilds the channel list so new channel i takes the state of old channel
  // sources[i], or defaults when sources[i] < 0.
  void remap(const std::vector<int>& sources);

private:
  struct Channel {
    std::string name;
    bool muted = false;
    bool soloed = false;
    float volume = 1.0f;
    std::uint8_t instrument = 0;
  };

  Channel* ensure(std::size_t channel);
  const Channel* find(std::size_t channel) const;

  std::vector<Channel> channels_;
};

// Song file persistence. Writes CHANNEL_INSTRUMENTS, CHANNEL_MUTED, CHANNEL_VOLUME
// and CHANNEL_NAMES lines for channelCount channels; solo is audition state and is
// not saved.
void writeChannelState(std::ostream& out, const ChannelManager& channels, std::size_t channelCount);

bool isChannelFileToken(const std::string& token);

// Consumes the rest of the line after a channel token. Returns false if the
// token is not a channel token (nothing is consumed).
bool applyChannelFileToken(std::istream& in, const std::string& token, ChannelManager& channels);

}  // namespace extracker
