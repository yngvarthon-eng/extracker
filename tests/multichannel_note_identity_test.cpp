#include <iostream>

#include "extracker/audio_engine.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

int main() {
  extracker::PatternEditor pattern(8, 4);

  // Same note/instrument on all channels; channels must remain independent voices.
  for (int ch = 0; ch < 4; ++ch) {
    pattern.insertNote(0, ch, 60, 0, 0, 100, false);
  }

  extracker::Transport transport;
  transport.setPatternRows(8);
  transport.resetTickCount();

  extracker::AudioEngine audio;
  extracker::PluginHost plugins;
  extracker::Sequencer sequencer;

  sequencer.update(pattern, transport, audio, plugins);

  const std::size_t voices = sequencer.activeVoiceCount();
  if (voices != 4) {
    std::cerr << "Expected 4 independent channel voices, got " << voices << '\n';
    return 1;
  }

  return 0;
}
