// A plain note must be sent once per row, however many ticks the row has.
// Instruments like SF2 treat every note-on as a new attack, so re-sending a
// note on each tick re-struck it TPR-1 times per row. Notes with a pitch or
// volume effect still need their per-tick updates.

#include <iostream>

#include "extracker/audio_engine.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

// Plays row 0 of the pattern at 6 ticks per row and returns how many note-ons
// reached the plugin host.
std::size_t noteOnsInFirstRow(const extracker::PatternEditor& pattern) {
  extracker::Transport transport;
  transport.setTicksPerRow(6);
  transport.setPatternRows(static_cast<std::uint32_t>(pattern.rows()));
  transport.resetTickCount();

  extracker::AudioEngine audio;
  extracker::PluginHost plugins;
  plugins.loadPlugin("builtin.sine");
  plugins.assignInstrument(0, "builtin.sine");
  audio.setPluginHost(&plugins);
  extracker::Sequencer sequencer;

  const std::size_t before = plugins.noteOnEventCount();
  sequencer.update(pattern, transport, audio, plugins);  // row 0, tick 0
  while (transport.currentRow() == 0) {
    transport.advanceExternalTick();
    if (transport.currentRow() != 0) {
      break;
    }
    sequencer.update(pattern, transport, audio, plugins);  // ticks 1-5
  }
  return plugins.noteOnEventCount() - before;
}

}  // namespace

int main() {
  extracker::PatternEditor plain(4, 1);
  plain.insertNote(0, 0, 60);
  check(noteOnsInFirstRow(plain) == 1, "plain note is sent once per row at TPR 6");

  extracker::PatternEditor vibrato(4, 1);
  vibrato.insertNote(0, 0, 60, 0, 0, 100, false, 0x04, 0x46);
  check(noteOnsInFirstRow(vibrato) > 1, "vibrato note still gets per-tick updates");

  extracker::PatternEditor arpeggio(4, 1);
  arpeggio.insertNote(0, 0, 60, 0, 0, 100, false, 0x00, 0x47);
  check(noteOnsInFirstRow(arpeggio) > 1, "arpeggio note still gets per-tick updates");

  if (failures == 0) {
    std::cout << "sequencer_tick_update_test passed\n";
  }
  return failures == 0 ? 0 : 1;
}
