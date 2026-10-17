// Loading a song written before "a note plays only its instrument":
//  - notes on an empty instrument 3 that borrowed sample slot 3 (what older
//    builds saved as a bare INSTRUMENT_ASSIGN 3 "builtin.sample"), and
//  - a note on the sine instrument whose sample column named sample slot 3,
// are converted to a sample instrument, reported once, and saved in the new
// form. Also: `sample load` creates the sample instrument, and a sample
// instrument's loop/root settings survive save + load.
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Drops every line starting with one of `prefixes`, and rewrites `from` to `to`.
void rewriteSong(const std::filesystem::path& path, const std::vector<std::string>& prefixes,
                 const std::string& from = "", const std::string& to = "") {
  std::istringstream in(readFile(path));
  std::ostringstream out;
  std::string line;
  while (std::getline(in, line)) {
    bool drop = false;
    for (const auto& prefix : prefixes) {
      drop = drop || line.rfind(prefix, 0) == 0;
    }
    if (drop) continue;
    if (!from.empty() && line == from) line = to;
    out << line << '\n';
  }
  std::ofstream(path) << out.str();
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
  const auto dir = std::filesystem::current_path() / "cli_sample_instrument_convert_test_files";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto wav = dir / "kick.wav";
  if (!writeTestWav(wav)) {
    std::cerr << "Failed to write test WAV\n";
    return 1;
  }
  const std::string song = (dir / "song.xtp").string();

  // Loading a sample creates its sample instrument (same number, slot free).
  const auto created = run("sample load 3 kick " + wav.string() + "\\n"
                           "plugin set 3 loop_mode 1\\n"
                           "plugin set 3 sample_root 48\\n"
                           "note set 0 0 36 3\\n"
                           "save " + song + "\\nquit\\n");
  expect(created, "Instrument 3 plays sample slot 3", "sample load creates instrument 3");
  expect(readFile(song), "INSTR_SAMPLE_SLOT 3 3", "song links instrument 3 to sample 3");

  // Sample instrument settings survive save + load.
  const auto reloaded = run("load " + song + "\\nplugin get 3 loop_mode\\nplugin get 3 sample_root\\nquit\\n");
  expect(reloaded, "parameter loop_mode = 1", "loop mode kept after reload");
  expect(reloaded, "parameter sample_root = 48", "root note kept after reload");
  expectNot(reloaded, "Converted song", "a current song needs no conversion");

  // An older song: instrument 3 saved as a bare "builtin.sample" with no link,
  // its notes borrowing sample slot 3 by number.
  rewriteSong(song, {"INSTR_SAMPLE_SLOT", "INSTRUMENT_PARAM 3 "});
  const auto legacy = run("load " + song + "\\ninstrument list\\nsave " + song + "\\nquit\\n");
  expect(legacy, "Converted song to sample instruments: I03=S003; 0 note(s) renumbered",
         "same-number song converted in place");
  expect(readFile(song), "INSTR_SAMPLE_SLOT 3 3", "converted song saved with the link");

  // An older song whose note on the sine instrument named sample 3 in its
  // sample column (row line: row ch hasNote note instr sample gate vel ...).
  rewriteSong(song, {"INSTR_SAMPLE_SLOT", "INSTRUMENT_ASSIGN 3 ", "INSTRUMENT_NAME 3 ", "INSTRUMENT_PARAM 3 ",
                     "INSTRUMENT_DEPTH 3 "});
  {
    std::string text = readFile(song);
    const auto pos = text.find("\n0 0 1 36 3 65535 ");
    if (pos == std::string::npos) {
      std::cerr << "FAIL: row 0 line not found in saved song\n";
      std::filesystem::remove_all(dir);
      return 1;
    }
    text.replace(pos, std::string("\n0 0 1 36 3 65535 ").size(), "\n0 0 1 36 0 3 ");
    std::ofstream(song) << text;
  }
  const auto column = run("load " + song + "\\nsave " + song + "\\nquit\\n");
  expect(column, "Converted song to sample instruments: I03=S003; 1 note(s) renumbered",
         "sample-column note moved to a sample instrument");
  const auto saved = readFile(song);
  expect(saved, "\n0 0 1 36 3 65535 ", "note saved on instrument 3 without a sample column");
  expect(saved, "INSTRUMENT_ASSIGN 0 \"builtin.sine\"", "sine instrument left alone");

  const auto again = run("load " + song + "\\nquit\\n");
  expectNot(again, "Converted song", "converted song loads without converting again");

  std::filesystem::remove_all(dir);
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  return 0;
}
