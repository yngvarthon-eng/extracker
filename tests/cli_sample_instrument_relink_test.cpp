// A sample instrument whose sample goes missing keeps its link and settings:
//  - a song whose sample file is missing loads with the instrument still
//    linked (silent); loading a WAV into that slot brings it back with the
//    instrument's own gain/root, not the new sample's defaults;
//  - unloading and reloading a sample keeps the instrument's settings;
//  - a leftover empty, unlinked sample instrument counts as a free slot.
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void writeWord(std::ofstream& out, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
  }
}

bool writeTestWav(const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  constexpr std::uint32_t frames = 64;
  out.write("RIFF", 4);
  writeWord(out, 36 + frames * 2, 4);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeWord(out, 16, 4);
  writeWord(out, 1, 2);
  writeWord(out, 1, 2);
  writeWord(out, 44100, 4);
  writeWord(out, 44100 * 2, 4);
  writeWord(out, 2, 2);
  writeWord(out, 16, 2);
  out.write("data", 4);
  writeWord(out, frames * 2, 4);
  for (std::uint32_t i = 0; i < frames; ++i) {
    writeWord(out, static_cast<std::uint16_t>((i % 8 < 4) ? 8000 : -8000), 2);
  }
  return static_cast<bool>(out);
}

std::string run(const std::string& script) {
  const std::string cmd = "printf '" + script + "' | timeout 20 ./extracker 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return {};
  std::string result;
  char buf[512];
  while (fgets(buf, sizeof(buf), pipe)) result += buf;
  pclose(pipe);
  return result;
}

int failures = 0;

void expect(const std::string& out, const std::string& needle, const std::string& what) {
  if (out.find(needle) == std::string::npos) {
    std::cerr << "FAIL: " << what << " (missing \"" << needle << "\")\n";
    ++failures;
  }
}

}  // namespace

int main() {
  const auto dir = std::filesystem::current_path() / "cli_sample_instrument_relink_test_files";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto wav = (dir / "hat.wav").string();
  if (!writeTestWav(wav)) {
    std::cerr << "Failed to write test WAV\n";
    return 1;
  }
  const std::string song = (dir / "song.xtp").string();

  // Instrument 4 plays sample slot 0 with its own gain and root.
  run("sample load 0 hat " + wav + "\\n"
      "instrument sample 4 0\\n"
      "instrument edit gain 4 0.3\\n"
      "instrument edit set 4 sample_root 48\\n"
      "note set 0 0 42 4\\n"
      "save " + song + "\\nquit\\n");

  // The song's sample file goes missing.
  std::filesystem::remove_all(dir / "song_samples");
  const auto missing = run("load " + song + "\\n"
                           "instrument list\\n"
                           "sample load 0 hat " + wav + "\\n"
                           "instrument edit get 4 gain\\n"
                           "instrument edit get 4 sample_root\\n"
                           "quit\\n");
  expect(missing, "[4] builtin.sample (sample slot 0)", "instrument 4 stays linked to the missing sample");
  // Instrument 2 was created by the first "sample load 0"; 4 was linked by hand.
  expect(missing, "Instruments 2, 4 play sample slot 0", "reloading the sample brings instrument 4 back");
  expect(missing, "Instrument 4 gain = 0.3", "instrument 4 keeps its gain");
  expect(missing, "Instrument 4 sample_root = 48", "instrument 4 keeps its root note");

  // Unloading and reloading a sample keeps the instrument's settings.
  const auto reload = run("sample load 3 hat " + wav + "\\n"
                          "instrument edit gain 3 0.4\\n"
                          "sample unload 3\\n"
                          "sample load 3 hat " + wav + "\\n"
                          "instrument edit get 3 gain\\n"
                          "quit\\n");
  // Only look after the unload: setting the gain also prints "gain = 0.4".
  const auto unloaded = reload.find("Unloaded sample slot 3");
  expect(unloaded == std::string::npos ? std::string() : reload.substr(unloaded), "Instrument 3 gain = 0.4",
         "unload + reload keeps the instrument's gain");

  // An empty, unlinked sample instrument is reused, not skipped.
  const auto dead = run("plugin assign 5 builtin.sample\\n"
                        "sample load 5 hat " + wav + "\\n"
                        "quit\\n");
  expect(dead, "Instrument 5 plays sample slot 5", "leftover empty sample instrument reused");

  std::filesystem::remove_all(dir);
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  return 0;
}
