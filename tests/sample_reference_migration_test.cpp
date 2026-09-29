// migrateSampleReferences on a song mixing every old way of reaching a sample:
//  - a sample column naming a loaded sample becomes a sample instrument, placed
//    in a slot no note uses (so no other note changes sound) and shared by all
//    notes naming that sample;
//  - an empty instrument slot borrowing the same-numbered sample is linked in
//    place, keeping its number;
//  - a sample column naming an empty bank slot is just cleared;
//  - a song with nothing to convert is left untouched.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "extracker/module.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sample_instrument_migration.hpp"

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

int failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  const auto wav = std::filesystem::current_path() / "sample_reference_migration_test.wav";
  if (!writeTestWav(wav)) {
    std::cerr << "Failed to write test WAV\n";
    return 1;
  }

  extracker::PluginHost plugins;
  plugins.assignInstrument(0, "builtin.sine");
  plugins.loadSampleToSlot(0, wav.string());   // named by sample columns
  plugins.loadSampleToSlot(20, wav.string());  // borrowed by empty instrument 20

  extracker::Module module;
  module.reset(8, 2, 2);
  auto& p0 = module.patternEditor(0);
  auto& p1 = module.patternEditor(1);
  p0.insertNote(0, 0, 60, 0, 0, 100, true);   // sample 0 over the sine
  p0.setSample(0, 0, 0);
  p0.insertNote(1, 0, 62, 5, 0, 100, true);   // sample 0 again, other instrument
  p0.setSample(1, 0, 0);
  p0.insertNote(2, 0, 64, 0, 0, 100, true);   // plain sine note
  p0.insertNote(0, 1, 48, 1, 0, 100, true);   // silent note on empty instrument 1
  p1.insertNote(0, 0, 60, 20, 0, 100, true);  // borrows bank 20
  p1.insertNote(1, 0, 60, 0, 0, 100, true);   // sample column naming empty bank 7
  p1.setSample(1, 0, 7);
  p1.setEffect(2, 1, 0x0C, 0x20);             // effect-only step with a stray sample column
  p1.setSample(2, 1, 0);

  const auto report = extracker::migrateSampleReferences(module, plugins);
  std::filesystem::remove(wav);

  // Instruments 0 (sine), 1 (silent note), 5 and 20 are used by notes, so the
  // sample-0 instrument must land in the lowest slot that is neither used nor
  // assigned: 2.
  check(p0.instrumentAt(0, 0) == 2 && p0.instrumentAt(1, 0) == 2, "both sample-0 notes use instrument 2");
  check(plugins.pluginForInstrument(2) == "builtin.sample" && plugins.sampleSlotForInstrument(2) == 0,
        "instrument 2 plays sample 0");
  check(p0.instrumentAt(2, 0) == 0 && plugins.pluginForInstrument(0) == "builtin.sine", "sine note unchanged");
  check(p0.instrumentAt(0, 1) == 1 && plugins.pluginForInstrument(1).empty(), "silent note stays silent");
  check(plugins.pluginForInstrument(5).empty(), "instrument 5 not taken over");

  check(p1.instrumentAt(0, 0) == 20 && plugins.sampleSlotForInstrument(20) == 20,
        "instrument 20 linked to bank 20 in place");
  check(p1.instrumentAt(1, 0) == 0 && p1.sampleAt(1, 0) == 0xFFFF, "empty-bank sample column cleared");
  check(p1.sampleAt(2, 1) == 0xFFFF, "stray sample column on an effect step cleared");

  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    const auto& editor = module.patternEditor(p);
    for (int r = 0; r < static_cast<int>(editor.rows()); ++r) {
      for (int c = 0; c < static_cast<int>(editor.channels()); ++c) {
        check(editor.sampleAt(r, c) == 0xFFFF, "no sample column left in pattern " + std::to_string(p));
      }
    }
  }

  check(report.notesRemapped == 2, "two notes renumbered, got " + std::to_string(report.notesRemapped));
  const std::string summary = report.summary();
  check(summary.find("I02=S000") != std::string::npos && summary.find("I14=S014") != std::string::npos,
        "summary lists new instruments: " + summary);

  // Converting again changes nothing.
  const auto again = extracker::migrateSampleReferences(module, plugins);
  check(!again.changed() && again.summary().empty(), "second conversion is a no-op");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  return 0;
}
