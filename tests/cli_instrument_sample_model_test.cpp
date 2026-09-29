// The CLI side of "notes play instruments; an instrument is a plugin or a
// bank sample": replacing an instrument is reported, sample list/instrument
// list show which instrument plays which sample, unloading a sample reports
// the instruments it silences (they stay linked and play again on reload),
// `instrument clear` empties a slot, and loading a song never keeps the
// previous song's instruments.
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void expectNot(const std::string& out, const std::string& needle, const std::string& what) {
  if (out.find(needle) != std::string::npos) {
    std::cerr << "FAIL: " << what << " (unexpected \"" << needle << "\")\n";
    ++failures;
  }
}

}  // namespace

int main() {
  const auto dir = std::filesystem::current_path() / "cli_instrument_sample_model_test_files";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto wav = (dir / "kick.wav").string();
  if (!writeTestWav(wav)) {
    std::cerr << "Failed to write test WAV\n";
    return 1;
  }

  const auto out = run("sample load 3 kick " + wav + "\\n"
                       "plugin assign 3 builtin.sine\\n"
                       "sample load 3 kick " + wav + "\\n"
                       "sample list\\n"
                       "sample status 3\\n"
                       "instrument list\\n"
                       "sample unload 3\\n"
                       "sample load 3 kick " + wav + "\\n"
                       "instrument sample 3 3\\n"
                       "instrument clear 2\\n"
                       "instrument clear 2\\n"
                       "quit\\n");
  expect(out, "Instrument 3 plays sample slot 3", "sample load creates instrument 3");
  expect(out, "Replaced sample slot 3 \"kick\" on instrument 3", "plugin assign reports what it replaced");
  expect(out, "Instrument 2 plays sample slot 3", "reloading the sample does not take over the sine");
  expect(out, "[3] \"kick\" -> " + wav + " (instrument 2)", "sample list shows the playing instrument");
  expect(out, "Sample slot 3: \"kick\" -> " + wav + " (instrument 2)", "sample status shows the instrument");
  expect(out, "[2] builtin.sample (sample slot 3) \"kick\"", "instrument list shows the sample name");
  expect(out, "Instrument 2 is silent until sample slot 3 is loaded again", "unload reports silenced instruments");
  // After the reload, instrument 2 (still linked) plays the sample again, so
  // no new instrument is created.
  expectNot(out, "Instrument 4 plays sample slot 3", "reload relinks instead of creating an instrument");
  expect(out, "Replaced builtin.sine on instrument 3", "instrument sample reports what it replaced");
  expect(out, "Cleared instrument 2 (was sample slot 3 \"kick\")", "instrument clear empties the slot");
  expect(out, "Instrument 2 is already empty", "clearing an empty slot says so");

  // Loading a song starts from the default instruments, not the previous song's.
  const std::string song = (dir / "song.xtp").string();
  run("save " + song + "\\nquit\\n");
  const auto loaded = run("plugin assign 9 builtin.square\\nload " + song + "\\ninstrument list\\nquit\\n");
  const auto listing = loaded.substr(loaded.find("Module loaded from"));
  expect(listing, "[0] builtin.sine", "default instrument 0 after load");
  expectNot(listing, "[9]", "previous song's instrument 9 is gone after load");

  std::filesystem::remove_all(dir);
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  return 0;
}
