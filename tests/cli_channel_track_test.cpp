#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string run(const std::string& script) {
  const std::string cmd = "echo '" + script + "' | ./extracker 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return {};
  std::string result;
  char buf[256];
  while (fgets(buf, sizeof(buf), pipe)) result += buf;
  pclose(pipe);
  return result;
}

int failures = 0;

void expect(const std::string& out, const std::string& needle, const char* what) {
  if (out.find(needle) == std::string::npos) {
    std::cerr << "FAIL: " << what << " (missing \"" << needle << "\")\n" << out << '\n';
    ++failures;
  }
}

void expectNot(const std::string& out, const std::string& needle, const char* what) {
  if (out.find(needle) != std::string::npos) {
    std::cerr << "FAIL: " << what << " (unexpected \"" << needle << "\")\n" << out << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  // Add, list, validation.
  {
    const auto out = run("ch track; ch track add 4 6 Pad Chord; ch track; ch list; ch track add 6 7; "
                         "ch track add 7 99; ch track mute 1; ch track bogus; quit");
    expect(out, "No tracks", "empty track list");
    expect(out, "Added Track 4-6 (Pad Chord)", "track added");
    expect(out, "ch  4- 6  Pad Chord        vol 100%", "track listed");
    expect(out, "ch  5  CH 5             vol 100%       [track 4-6 Pad Chord]", "channel list shows track");
    expect(out, "Track add failed: channel 6 is already in track 4-6", "overlap rejected");
    expect(out, "Invalid channel index: 99", "bad channel rejected");
    expect(out, "Channel 1 is not in a track", "non-track channel rejected");
    expect(out, "Unknown track subcommand: bogus", "unknown subcommand");
  }

  // Mute, solo, volume and name act on the whole track.
  {
    const auto out = run("ch track add 4 6; ch track mute 5; ch list; ch track mute 5 off; ch mute 5 on; "
                         "ch track solo 6; ch list; ch track vol 4 50; ch track vol 4; ch track vol 4 300; "
                         "ch track name 4 Strings; ch track name 4; ch track name 4 -; ch track name 4; quit");
    expect(out, "Track 4-6 muted", "track mute");
    expect(out, "ch  4  CH 4             vol 100%       (silent)  [track 4-6]", "muted track is silent");
    expect(out, "Track 4-6 solo on", "track solo");
    expect(out, "Channels (solo active):", "track solo is a solo");
    expect(out, "ch  6  CH 6             vol 100%       [track 4-6]", "soloed track audible");
    expect(out, "ch  5  CH 5             vol 100%  M    (silent)  [track 4-6]", "muted chord note stays silent");
    expect(out, "ch  0  CH 0             vol 100%       (silent)", "other channels silent");
    expect(out, "Track 4-6 volume set to 50%", "track volume set");
    expect(out, "Track 4-6 volume: 50%", "track volume shown");
    expect(out, "Volume out of range: 300", "track volume validated");
    expect(out, "Track 4-6 name set to Strings", "track name set");
    expect(out, "Track 4-6 (Strings) name: Strings", "track name shown");
    expect(out, "Track 4-6 (Strings) name cleared", "track name cleared");
    expect(out, "Track 4-6 name: -", "cleared name shown");
  }

  // Structural channel edits keep tracks contiguous; remove ungroups.
  {
    const auto out = run("ch track add 4 6; ch insert 5; ch track; ch delete 4; ch delete 4; ch track; "
                         "ch undo; ch track; ch track remove 5; ch track; quit");
    expect(out, "ch  4- 7  -", "insert inside a track grows it");
    expect(out, "ch  4- 5  -", "delete shrinks it");
    expect(out, "ch  4- 6  -", "undo restores it");
    expect(out, "Removed Track 4-6", "track removed");
    expect(out, "No tracks", "no tracks after remove");
  }

  // Chord notes take the track lead's instrument.
  {
    const auto out = run("ch track add 4 6; note set 0 5 64 -; note set 0 4 60 3; note set 0 5 64 -; "
                         "note set 0 6 67 9; note set 0 4 60 7; note set 0 2 60 -; note set dry 1 5 64 -; quit");
    expect(out, "No note on row 0 in track 4-6 to take the instrument from", "no lead yet");
    expect(out, "Instrument 3 taken from track lead", "inherited from lead");
    expect(out, "Instrument 7 applied to chord channel 5\n", "lead change reaches following notes only");
    expect(out, "Channel 2 is not in a track; give an instrument", "dash outside a track rejected");
    expect(out, "No note on row 1 in track 4-6", "dry run also needs a lead");
    expectNot(out, "applied to chord channels 5 6", "hand-picked instrument kept");
  }

  // Tracks survive save/load and are cleared by loading a song without them.
  {
    const std::string path = "/tmp/xt_cli_channel_track_test.xtp";
    const std::string plainPath = "/tmp/xt_cli_channel_track_plain_test.xtp";
    std::filesystem::remove(path);
    std::filesystem::remove(plainPath);
    run("save " + plainPath + "; ch track add 4 6 Pad; ch track mute 4; ch track vol 4 40; ch track solo 4; "
        "save " + path + "; quit");
    const auto out = run("load " + path + "; ch track; load " + plainPath + "; ch track; quit");
    std::filesystem::remove(path);
    std::filesystem::remove(plainPath);
    expect(out, "ch  4- 6  Pad              vol  40%  M  \n", "track restored without solo");
    expect(out, "No tracks", "loading a song without tracks clears them");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "cli channel track test passed\n";
  return 0;
}
