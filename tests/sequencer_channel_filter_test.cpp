#include <cmath>
#include <iostream>

#include "extracker/audio_engine.hpp"
#include "extracker/channel_manager.hpp"
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

extracker::BiquadParams makeFilter(extracker::BiquadType type, float cutoff, float resonance) {
  extracker::BiquadParams filter;
  filter.type = type;
  filter.cutoffNorm = cutoff;
  filter.resonanceNorm = resonance;
  return filter;
}

bool near(float a, float b) {
  return std::fabs(a - b) < 0.01f;
}

}  // namespace

int main() {
  using extracker::BiquadType;

  // Channel 0: plain note, note with a 19 (cutoff) effect, plain note.
  extracker::PatternEditor pattern(8, 2);
  pattern.insertNote(0, 0, 60);
  pattern.insertNote(2, 0, 62, 0, 0, 100, false, 0x19, 0x40);
  pattern.insertNote(4, 0, 64);

  extracker::Transport transport;
  transport.setTicksPerRow(2);
  transport.setPatternRows(8);
  transport.resetTickCount();

  extracker::AudioEngine audio;
  extracker::PluginHost plugins;
  plugins.loadPlugin("builtin.sine");
  plugins.assignInstrument(0, "builtin.sine");
  audio.setPluginHost(&plugins);
  extracker::Sequencer sequencer;

  extracker::ChannelManager channels(2);
  channels.setFilter(0, makeFilter(BiquadType::HighPass, 0.5f, 0.1f));

  auto runToRow = [&](std::uint32_t row) {
    while (transport.currentRow() != row) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins, &channels);
    }
  };

  sequencer.update(pattern, transport, audio, plugins, &channels);  // row 0
  auto applied = audio.getInstrumentFilterParams(0);
  check(applied.type == BiquadType::HighPass && near(applied.cutoffNorm, 0.5f), "base filter applied on note");

  // A reset (play/stop, pattern edits, channel edits) must not lose the base filter.
  audio.setInstrumentFilter(0, BiquadType::Off, 1.0f, 0.0f);
  sequencer.reset();
  sequencer.update(pattern, transport, audio, plugins, &channels);  // row 0 again
  applied = audio.getInstrumentFilterParams(0);
  check(applied.type == BiquadType::HighPass && near(applied.cutoffNorm, 0.5f), "base filter survives reset");

  runToRow(2);  // 19 40: pattern effect overrides the cutoff
  applied = audio.getInstrumentFilterParams(0);
  check(applied.type == BiquadType::HighPass && near(applied.cutoffNorm, 0x40 / 255.0f), "effect overrides cutoff");

  runToRow(4);  // plain note keeps the effect's filter
  applied = audio.getInstrumentFilterParams(0);
  check(near(applied.cutoffNorm, 0x40 / 255.0f), "effect override persists on later notes");

  channels.setFilter(0, makeFilter(BiquadType::BandPass, 0.8f, 0.0f));
  runToRow(5);  // changed base takes over again and reaches the sustained note
  applied = audio.getInstrumentFilterParams(0);
  check(applied.type == BiquadType::BandPass && near(applied.cutoffNorm, 0.8f), "changed base filter wins");

  sequencer.reset();
  transport.resetTickCount();
  sequencer.update(pattern, transport, audio, plugins, &channels);
  applied = audio.getInstrumentFilterParams(0);
  check(applied.type == BiquadType::BandPass, "reset drops the effect override and keeps the base");

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "sequencer channel filter test passed" << '\n';
  return 0;
}
