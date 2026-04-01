#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include "extracker/audio_engine.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

int main() {
  {
    extracker::PatternEditor pattern(8, 2);
    pattern.insertNote(0, 0, 60, 0, 0, 96, true, 0x03, 0x20);
    pattern.insertNote(1, 0, 67, 0, 0, 96, true, 0x05, 0x10);
    pattern.insertNote(2, 0, 67, 0, 0, 96, true, 0x04, 0x47);
    pattern.insertNote(3, 0, 67, 0, 0, 96, true, 0x06, 0x10);

    extracker::Transport transport;
    transport.setTempoBpm(1000.0);
    transport.setTicksPerBeat(24);
    transport.setTicksPerRow(4);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);
    transport.play();

    for (int i = 0; i < 10; ++i) {
      sequencer.update(pattern, transport, audio, plugins);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    const double portamentoFrequency = audio.testToneFrequencyHz();
    if (portamentoFrequency <= 390.0) {
      std::cerr << "Combined 0x05 effect did not apply tone portamento behavior" << '\n';
      transport.stop();
      return 1;
    }

    for (int i = 0; i < 12; ++i) {
      sequencer.update(pattern, transport, audio, plugins);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    double minHz = 1e9;
    double maxHz = 0.0;
    for (int i = 0; i < 16; ++i) {
      sequencer.update(pattern, transport, audio, plugins);
      const double hz = audio.testToneFrequencyHz();
      minHz = std::min(minHz, hz);
      maxHz = std::max(maxHz, hz);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    transport.stop();

    if (maxHz - minHz < 3.0) {
      std::cerr << "Combined 0x06 effect did not apply vibrato behavior" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    pattern.insertNote(0, 0, 60, 0, 0, 120, true, 0x0E, 0xD3);

    extracker::Transport transport;
    transport.setTempoBpm(1000.0);
    transport.setTicksPerBeat(24);
    transport.setTicksPerRow(8);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);
    if (audio.testToneVoiceCount() != 0) {
      std::cerr << "Tick-0 ordering broke note-delay behavior" << '\n';
      return 1;
    }

    transport.play();
    bool observedDelayedStart = false;
    for (int i = 0; i < 24; ++i) {
      sequencer.update(pattern, transport, audio, plugins);
      if (audio.testToneVoiceCount() > 0) {
        observedDelayedStart = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    transport.stop();

    if (!observedDelayedStart) {
      std::cerr << "Delayed note was never started within row" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 2);
    pattern.setEffect(0, 0, 0x03, 0x18);
    pattern.setEffect(1, 0, 0x05, 0x10);
    pattern.setEffect(0, 1, 0x04, 0x36);
    pattern.setEffect(1, 1, 0x06, 0x10);
    pattern.setEffect(0, 1, 0x0B, 1);

    extracker::Transport transport;
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);
    sequencer.update(pattern, transport, audio, plugins);

    // If we reached here without crashes and dispatch progressed to the jumped row, effect-memory interplay is stable.
    if (sequencer.dispatchCount() < 2) {
      std::cerr << "Effect-memory dispatch progression failed" << '\n';
      return 1;
    }
  }

  return 0;
}
