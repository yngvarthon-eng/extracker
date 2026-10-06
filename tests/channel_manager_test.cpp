#include <iostream>
#include <sstream>
#include <string>

#include "extracker/channel_manager.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using extracker::ChannelManager;

  // Defaults and out-of-range reads.
  {
    ChannelManager channels(4);
    check(channels.count() == 4, "initial count");
    check(channels.isAudible(0) && !channels.isMuted(0) && !channels.isSoloed(0), "defaults audible");
    check(channels.volume(0) == 1.0f && channels.volume(99) == 1.0f, "default volume");
    check(channels.displayName(2) == "CH 2", "default display name");
    check(channels.isAudible(99), "out-of-range channel is audible");
  }

  // Solo wins over mute and silences everything else.
  {
    ChannelManager channels(4);
    channels.setMuted(1, true);
    check(!channels.isAudible(1) && channels.isAudible(0), "mute silences only that channel");

    channels.setSoloed(1, true);
    check(channels.anySoloed(), "solo detected");
    check(channels.isAudible(1), "soloed channel audible even when muted");
    check(!channels.isAudible(0) && !channels.isAudible(2), "non-soloed channels silent");

    channels.setSoloed(3, true);
    check(channels.isAudible(3) && channels.isAudible(1) && !channels.isAudible(2), "multiple solos");

    channels.clearSolo();
    check(!channels.anySoloed() && channels.isAudible(0) && !channels.isAudible(1),
          "clearing solo restores mute state");
  }

  // Volume clamps; setters grow up to kMaxChannels; resize truncates.
  {
    ChannelManager channels(2);
    channels.setVolume(0, 5.0f);
    channels.setVolume(1, -1.0f);
    check(channels.volume(0) == ChannelManager::kMaxVolume && channels.volume(1) == 0.0f, "volume clamped");

    channels.setMuted(5, true);
    check(channels.count() == 6 && channels.isMuted(5), "setter grows channel list");
    channels.setMuted(ChannelManager::kMaxChannels, true);
    check(channels.count() == 6, "setter ignores channels past kMaxChannels");

    channels.resize(3);
    channels.resize(6);
    check(!channels.isMuted(5), "truncated channel comes back with defaults");
  }

  // Save and load round trip, including names with spaces and quotes.
  {
    ChannelManager source(3);
    source.setMuted(0, true);
    source.setVolume(1, 0.5f);
    source.setName(2, "Lead \"Hot\" Synth");
    source.setSoloed(1, true);
    extracker::BiquadParams bandPass;
    bandPass.type = extracker::BiquadType::BandPass;
    bandPass.cutoffNorm = 128.0f / 255.0f;
    bandPass.resonanceNorm = 40.0f / 255.0f;
    source.setFilter(1, bandPass);

    std::ostringstream out;
    extracker::writeChannelState(out, source, 3);

    ChannelManager loaded(3);
    std::istringstream in(out.str() + "NEXT_TOKEN 1\n");
    std::string token;
    while (in >> token && token != "NEXT_TOKEN") {
      check(extracker::applyChannelFileToken(in, token, loaded), "channel token recognised");
    }
    check(token == "NEXT_TOKEN", "following token left intact");
    check(loaded.isMuted(0) && !loaded.isMuted(1), "mute round trip");
    check(loaded.volume(1) == 0.5f, "volume round trip");
    check(loaded.name(2) == "Lead \"Hot\" Synth" && loaded.name(0).empty(), "name round trip");
    check(!loaded.anySoloed(), "solo is not persisted");
    check(out.str().find("CHANNEL_INSTRUMENTS") == std::string::npos, "channel instruments are no longer written");
    check(out.str().find("CHANNEL_FILTERS 0 255 0 3 128 40 0 255 0\n") != std::string::npos,
          "filters written as type/cutoff/resonance triples");
    check(loaded.filter(1).type == extracker::BiquadType::BandPass &&
              loaded.filter(1).cutoffNorm == 128.0f / 255.0f && loaded.filter(1).resonanceNorm == 40.0f / 255.0f &&
              !loaded.filter(0).isActive(),
          "filter round trip");

    std::istringstream other("1 2 3\n");
    check(!extracker::applyChannelFileToken(other, "SONG_ORDER", loaded), "foreign token rejected");
  }

  // Older files may carry fewer values than there are channels.
  {
    ChannelManager channels(4);
    std::istringstream in(" 1 0\n");
    extracker::applyChannelFileToken(in, "CHANNEL_MUTED", channels);
    check(channels.isMuted(0) && !channels.isMuted(2), "short mute line applied");
  }

  // Tracks: grouping, audibility and volume.
  {
    ChannelManager channels(8);
    std::string error;
    check(channels.addTrack(2, 4, "Pad", error), "track added");
    check(!channels.addTrack(4, 5, "", error) && error.find("already in track 2-4") != std::string::npos,
          "overlapping track rejected");
    check(!channels.addTrack(5, 3, "", error), "reversed range rejected");
    check(channels.inTrack(3) && !channels.inTrack(1) && !channels.inTrack(5), "membership");
    const ChannelManager::TrackInfo info = channels.track(3);
    check(info.first == 2 && info.last == 4 && info.name == "Pad", "track info from a middle channel");

    channels.setTrackVolume(4, 0.5f);
    channels.setVolume(3, 0.5f);
    check(channels.volume(3) == 0.5f && channels.effectiveVolume(3) == 0.25f &&
              channels.effectiveVolume(1) == 1.0f,
          "track volume multiplies channel volume");

    channels.setTrackMuted(2, true);
    check(!channels.isAudible(2) && !channels.isAudible(4) && channels.isAudible(5), "track mute");
    channels.setTrackMuted(2, false);

    channels.setMuted(3, true);
    channels.setTrackSoloed(2, true);
    check(channels.anySoloed(), "track solo counts as solo");
    check(channels.isAudible(2) && !channels.isAudible(3) && !channels.isAudible(0),
          "track solo keeps a muted chord note silent and silences other channels");
    channels.setSoloed(3, true);
    check(channels.isAudible(3), "channel solo still wins over its own mute");
    channels.clearSolo();
    check(!channels.anySoloed() && !channels.track(2).soloed, "clearSolo clears track solo");

    check(channels.removeTrack(4) && !channels.inTrack(3) && channels.tracks().empty(), "track removed");
    check(!channels.removeTrack(4), "removing a non-track fails");
  }

  // Tracks follow remaps and stay contiguous.
  {
    auto make = []() {
      ChannelManager channels(8);
      std::string error;
      channels.addTrack(2, 4, "Pad", error);
      return channels;
    };
    auto range = [](const ChannelManager& channels) {
      const auto tracks = channels.tracks();
      return tracks.size() == 1 ? std::to_string(tracks[0].first) + "-" + std::to_string(tracks[0].last)
                                : std::to_string(tracks.size()) + " tracks";
    };

    ChannelManager inserted = make();
    inserted.remap({0, 1, 2, -1, 3, 4, 5, 6});
    check(range(inserted) == "2-5", "insert inside a track grows it");

    ChannelManager before = make();
    before.remap({0, 1, -1, 2, 3, 4, 5, 6});
    check(range(before) == "3-5", "insert at the track start shifts it");

    ChannelManager deleted = make();
    deleted.remap({0, 1, 2, 4, 5, 6, 7, -1});
    check(range(deleted) == "2-3", "delete inside a track shrinks it");

    ChannelManager movedIn = make();
    movedIn.remap({0, 1, 2, 6, 3, 4, 5, 7});
    check(range(movedIn) == "2-5", "channel moved into a track joins it");

    ChannelManager movedOut = make();
    movedOut.remap({0, 1, 3, 4, 5, 6, 2, 7});
    check(range(movedOut) == "2-3" && !movedOut.inTrack(6), "channel moved away leaves its track");

    ChannelManager gone(2);
    std::string error;
    gone.addTrack(0, 0, "", error);
    gone.remap({1, -1});
    check(gone.tracks().empty(), "track without channels is dropped");

    ChannelManager truncated = make();
    truncated.resize(3);
    check(range(truncated) == "2-2", "resize trims tracks");
  }

  // Tracks round trip through the song file; no line without tracks.
  {
    ChannelManager source(6);
    std::ostringstream plain;
    extracker::writeChannelState(plain, source, 6);
    check(plain.str().find("CHANNEL_TRACKS") == std::string::npos, "no track line without tracks");

    std::string error;
    source.addTrack(1, 3, "Big \"Pad\"", error);
    source.addTrack(4, 4, "", error);
    source.setTrackMuted(1, true);
    source.setTrackVolume(4, 0.4f);
    std::ostringstream out;
    extracker::writeChannelState(out, source, 6);
    check(out.str().find("CHANNEL_TRACKS 1 3 1 100 \"Big \\\"Pad\\\"\" 4 4 0 40 \"\"\n") != std::string::npos,
          "tracks written");

    ChannelManager loaded(6);
    std::istringstream in(out.str());
    std::string token;
    while (in >> token) {
      extracker::applyChannelFileToken(in, token, loaded);
    }
    const auto tracks = loaded.tracks();
    check(tracks.size() == 2 && tracks[0].first == 1 && tracks[0].last == 3 && tracks[0].name == "Big \"Pad\"" &&
              tracks[0].muted && tracks[1].first == 4 && tracks[1].volume == 0.4f,
          "tracks round trip");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "channel manager test passed" << '\n';
  return 0;
}
