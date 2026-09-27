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

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "channel manager test passed" << '\n';
  return 0;
}
