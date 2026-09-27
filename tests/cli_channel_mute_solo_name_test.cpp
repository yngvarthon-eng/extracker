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

}  // namespace

int main() {
  // Mute toggles and explicit on/off.
  {
    const auto out = run("ch mute 2; ch mute 2; ch mute 3 on; ch list; quit");
    expect(out, "Channel 2 muted", "first mute toggles on");
    expect(out, "Channel 2 unmuted", "second mute toggles off");
    expect(out, "Channel 3 muted", "explicit mute on");
    expect(out, "ch  3  CH 3             vol 100%  M    (silent)", "list shows muted channel");
  }

  // Solo silences other channels; solo off clears it.
  {
    const auto out = run("ch solo 1; ch list; ch solo off; ch list; quit");
    expect(out, "Channel 1 solo on", "solo on");
    expect(out, "Channels (solo active):", "list flags active solo");
    expect(out, "ch  0  CH 0             vol 100%       (silent)", "non-soloed channel silent");
    expect(out, "Solo cleared", "solo off");
  }

  // Names with spaces, clearing, and validation.
  {
    const auto out = run("ch name 4 Lead Synth; ch name 4; ch name 4 -; ch name 4; ch mute 99; ch mute 1 maybe; quit");
    expect(out, "Channel 4 name set to Lead Synth", "set name");
    expect(out, "Channel 4 name: Lead Synth", "show name");
    expect(out, "Channel 4 name cleared", "clear name");
    expect(out, "Channel 4 name: CH 4", "default name after clear");
    expect(out, "Invalid channel index: 99", "bad channel rejected");
    expect(out, "Usage: channel", "bad on/off rejected");
  }

  // Mute, volume and names survive save/load; solo does not.
  {
    const std::string path = "/tmp/xt_cli_channel_state_test.xtp";
    std::filesystem::remove(path);
    run("ch mute 5 on; ch vol 6 40; ch name 7 Drums; ch solo 2; save " + path + "; quit");
    const auto out = run("load " + path + "; ch list; quit");
    std::filesystem::remove(path);
    expect(out, "ch  5  CH 5             vol 100%  M    (silent)", "mute persisted");
    expect(out, "ch  6  CH 6             vol  40%", "volume persisted");
    expect(out, "ch  7  Drums", "name persisted");
    if (out.find("(solo active)") != std::string::npos) {
      std::cerr << "FAIL: solo should not be persisted\n";
      ++failures;
    }
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "cli channel mute/solo/name test passed" << '\n';
  return 0;
}
