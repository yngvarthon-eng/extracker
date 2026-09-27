#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string run(const std::string& script) {
  const std::string cmd = "printf '" + script + "' | ./extracker 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return {};
  std::string result;
  char buf[512];
  while (fgets(buf, sizeof(buf), pipe)) result += buf;
  pclose(pipe);
  return result;
}

int failures = 0;

void expect(const std::string& out, const std::string& needle, const char* what) {
  if (out.find(needle) == std::string::npos) {
    std::cerr << "FAIL: " << what << " (missing \"" << needle << "\")\n";
    ++failures;
  }
}

}  // namespace

int main() {
  const std::string path = "/tmp/xt_cli_reverb_persist_test.xtp";
  std::filesystem::remove(path);

  // Global reverb is saved with the song and restored on load.
  run("reverb set 200 100 50 180\\nsave " + path + "\\nquit\\n");
  {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    expect(buffer.str(), "\nREVERB 200 100 50 180\n", "song file has the REVERB line");
  }
  const auto loaded = run("reverb set 10 10 10 10\\nload " + path + "\\nreverb get\\nquit\\n");
  expect(loaded, "Reverb: room=0.784314 damp=0.392157 wet=0.196078 width=0.705882", "reverb restored on load");

  // A song without a REVERB line loads with the default (bypassed) reverb.
  {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string song = buffer.str();
    song.erase(song.find("REVERB 200 100 50 180\n"), std::string("REVERB 200 100 50 180\n").size());
    std::ofstream(path) << song;
  }
  const auto legacy = run("reverb set 10 10 10 10\\nload " + path + "\\nreverb get\\nquit\\n");
  expect(legacy, "Reverb: room=0.5 damp=0.5 wet=0 width=1", "older song resets reverb to defaults");

  std::filesystem::remove(path);
  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "cli reverb persist test passed" << '\n';
  return 0;
}
