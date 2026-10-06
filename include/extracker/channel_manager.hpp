#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "extracker/biquad_filter.hpp"

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

  // Solo wins over mute. While any channel or track is soloed, a channel is
  // audible if it is soloed itself, or its track is soloed and the channel is
  // not muted. Otherwise it is audible unless it or its track is muted.
  bool isAudible(std::size_t channel) const;

  // The channel's own linear gain, 1.0 = unity, clamped to 0..kMaxVolume.
  float volume(std::size_t channel) const;
  void setVolume(std::size_t channel, float volume);
  // Gain playback uses: the channel volume times its track volume.
  float effectiveVolume(std::size_t channel) const;

  // Tracks group a contiguous run of channels (e.g. the notes of a chord) under
  // one name, mute, solo and volume. Each channel keeps its own state and is in
  // at most one track. Track functions take any channel inside the track.
  struct TrackInfo {
    std::size_t first = 0;
    std::size_t last = 0;
    std::string name;
    bool muted = false;
    bool soloed = false;
    float volume = 1.0f;
  };

  // Fails (with error set) if the range is invalid or overlaps another track.
  bool addTrack(std::size_t first, std::size_t last, const std::string& name, std::string& error);
  bool removeTrack(std::size_t channel);
  bool inTrack(std::size_t channel) const;
  // The track holding channel; only valid when inTrack(channel).
  TrackInfo track(std::size_t channel) const;
  // Every track, ordered by first channel.
  std::vector<TrackInfo> tracks() const;
  void setTrackName(std::size_t channel, const std::string& name);
  void setTrackMuted(std::size_t channel, bool muted);
  void setTrackSoloed(std::size_t channel, bool soloed);
  void setTrackVolume(std::size_t channel, float volume);

  // Base filter set from the GUI filter panel. Pattern effects 18/19/1A can
  // override it during playback; a sequencer reset falls back to this value.
  const BiquadParams& filter(std::size_t channel) const;
  void setFilter(std::size_t channel, const BiquadParams& filter);


  // Rebuilds the channel list so new channel i takes the state of old channel
  // sources[i], or defaults when sources[i] < 0. Tracks follow their channels
  // and stay contiguous: a channel landing between two channels of a track
  // joins it, and a channel moved away from its track leaves it.
  void remap(const std::vector<int>& sources);

private:
  struct Channel {
    std::string name;
    bool muted = false;
    bool soloed = false;
    float volume = 1.0f;
    BiquadParams filter;
    int track = -1;  // Track::id, or -1
  };
  struct Track {
    int id = 0;
    std::string name;
    bool muted = false;
    bool soloed = false;
    float volume = 1.0f;
  };

  Channel* ensure(std::size_t channel);
  const Channel* find(std::size_t channel) const;
  const Track* trackOf(std::size_t channel) const;
  Track* trackOf(std::size_t channel);
  void normalizeTracks();

  std::vector<Channel> channels_;
  std::vector<Track> tracks_;
  int nextTrackId_ = 0;
};

// Song file persistence. Writes CHANNEL_MUTED, CHANNEL_VOLUME, CHANNEL_NAMES and
// CHANNEL_FILTERS lines for channelCount channels, plus a CHANNEL_TRACKS line
// ("first last muted volume name" per track) when there are tracks; solo is audition state and is
// not saved. CHANNEL_INSTRUMENTS (per-channel default instruments, which notes
// no longer use) is still accepted from older files and ignored. CHANNEL_FILTERS holds one "type cutoff
// resonance" triple per channel (type 0-4, cutoff and resonance 0-255).
void writeChannelState(std::ostream& out, const ChannelManager& channels, std::size_t channelCount);

bool isChannelFileToken(const std::string& token);

// Consumes the rest of the line after a channel token. Returns false if the
// token is not a channel token (nothing is consumed).
bool applyChannelFileToken(std::istream& in, const std::string& token, ChannelManager& channels);

}  // namespace extracker
