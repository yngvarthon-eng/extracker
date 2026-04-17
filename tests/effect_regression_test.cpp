#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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

  {
    auto sampleRow2PortamentoHz = [](bool enableGlissando) {
      extracker::PatternEditor pattern(8, 1);
      pattern.setEffect(0, 0, 0x0E, enableGlissando ? 0x31 : 0x30);      // E3x glissando control
      pattern.insertNote(1, 0, 60, 0, 0, 120, true, 0x0E, 0x15);         // raise current pitch by a stronger fractional semitone
      pattern.insertNote(2, 0, 60, 0, 0, 120, true, 0x03, 0x04);         // slow portamento toward base pitch

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

      for (int i = 0; i < 100 && transport.currentRow() != 2; ++i) {
        sequencer.update(pattern, transport, audio, plugins);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }

      if (transport.currentRow() == 2) {
        sequencer.update(pattern, transport, audio, plugins);  // row start
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        sequencer.update(pattern, transport, audio, plugins);  // first modulation tick
      }

      const double hz = audio.testToneFrequencyHz();
      transport.stop();
      return hz;
    };

    const double glissOnHz = sampleRow2PortamentoHz(true);
    const double glissOffHz = sampleRow2PortamentoHz(false);

    const double baseHz = 261.6255653;
    const double glissOnSemitone = 12.0 * std::log2(glissOnHz / baseHz);
    const double glissOffSemitone = 12.0 * std::log2(glissOffHz / baseHz);
    const double glissOnDistance = std::abs(glissOnSemitone - std::round(glissOnSemitone));
    const double glissOffDistance = std::abs(glissOffSemitone - std::round(glissOffSemitone));

    if (glissOnDistance > 0.08) {
      std::cerr << "E3x glissando did not quantize portamento to semitone steps" << '\n';
      return 1;
    }

    if (glissOffDistance < 0.20) {
      std::cerr << "Portamento without E3x unexpectedly behaved as quantized glissando" << '\n';
      return 1;
    }
  }

  {
    auto sampleFirstVibratoTickHz = [](std::uint8_t waveformSubValue) {
      extracker::PatternEditor pattern(8, 1);
      pattern.setEffect(0, 0, 0x0E, static_cast<std::uint8_t>(0x40 | (waveformSubValue & 0x0F)));  // E4x waveform
      pattern.insertNote(1, 0, 60, 0, 0, 120, true, 0x04, 0x4F);                                     // vibrato

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

      for (int i = 0; i < 100 && transport.currentRow() != 1; ++i) {
        sequencer.update(pattern, transport, audio, plugins);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }

      if (transport.currentRow() == 1) {
        sequencer.update(pattern, transport, audio, plugins);  // row start
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        sequencer.update(pattern, transport, audio, plugins);  // first vibrato tick
      }

      const double hz = audio.testToneFrequencyHz();
      transport.stop();
      return hz;
    };

    const double sineHz = sampleFirstVibratoTickHz(0x0);
    const double sawHz = sampleFirstVibratoTickHz(0x1);
    const double baseHz = 261.6255653;

    if (sineHz <= baseHz * 1.005) {
      std::cerr << "E40 vibrato waveform did not produce expected positive first LFO phase" << '\n';
      return 1;
    }

    if (sawHz >= baseHz * 0.995) {
      std::cerr << "E41 vibrato waveform did not alter LFO shape as expected" << '\n';
      return 1;
    }
  }

  {
    auto sampleFirstTremoloTickLevel = [](std::uint8_t waveformSubValue) {
      extracker::PatternEditor pattern(8, 1);
      pattern.setEffect(0, 0, 0x0E, static_cast<std::uint8_t>(0x70 | (waveformSubValue & 0x0F)));  // E7x waveform
      pattern.insertNote(1, 0, 60, 0, 0, 40, true, 0x07, 0x44);                                     // tremolo speed/depth

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

      for (int i = 0; i < 100 && transport.currentRow() != 1; ++i) {
        sequencer.update(pattern, transport, audio, plugins);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }

      if (transport.currentRow() == 1) {
        sequencer.update(pattern, transport, audio, plugins);  // row start
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        sequencer.update(pattern, transport, audio, plugins);  // first tremolo tick
      }

      const double level = audio.testToneVoiceLevel(0);
      transport.stop();
      return level;
    };

    const double sineLevel = sampleFirstTremoloTickLevel(0x0);
    const double squareLevel = sampleFirstTremoloTickLevel(0x2);

    if (sineLevel <= 0.0 || squareLevel <= 0.0) {
      std::cerr << "E7x tremolo waveform regression did not capture active voice level" << '\n';
      return 1;
    }

    if (squareLevel <= sineLevel + 0.01) {
      std::cerr << "E72 tremolo waveform did not alter modulation shape compared to E70" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    pattern.setEffect(0, 0, 0x0E, 0x8F);                // E8F: coarse pan hard-right
    pattern.insertNote(1, 0, 60, 0, 0, 120, true);      // note inherits channel pan

    extracker::Transport transport;
    transport.setTempoBpm(1000.0);
    transport.setTicksPerBeat(24);
    transport.setTicksPerRow(1);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);
    transport.play();

    bool reachedRow1 = false;
    for (int i = 0; i < 100; ++i) {
      sequencer.update(pattern, transport, audio, plugins);
      if (transport.currentRow() == 1) {
        reachedRow1 = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    if (!reachedRow1) {
      std::cerr << "E8x pan regression did not reach note row" << '\n';
      transport.stop();
      return 1;
    }

    // Update once more on row 1 to ensure note dispatch happened.
    sequencer.update(pattern, transport, audio, plugins);
    const double pan = audio.testToneVoicePan(0);
    transport.stop();

    if (pan < 0.95) {
      std::cerr << "E8x coarse pan did not apply expected hard-right channel pan" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    pattern.setEffect(0, 0, 0x0E, 0x60);  // E60: mark loop start
    pattern.setEffect(1, 0, 0x0E, 0x62);  // E62: repeat loop two times

    extracker::Transport transport;
    transport.setTicksPerRow(1);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);  // row 0 marker

    bool reachedRow2 = false;
    for (int i = 0; i < 32; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
      if (transport.currentRow() == 2) {
        reachedRow2 = true;
        break;
      }
    }

    if (!reachedRow2) {
      std::cerr << "E6x loop regression did not progress past loop row" << '\n';
      return 1;
    }

    if (sequencer.dispatchCount() != 5) {
      std::cerr << "E62 loop regression dispatch count mismatch before reaching row 2 (got "
                << sequencer.dispatchCount() << ")" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    pattern.setEffect(0, 0, 0x0E, 0xE2);  // EE2: delay next row progression by 2 rows

    extracker::Transport transport;
    transport.setTicksPerRow(1);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);  // process row 0 EE2

    int heldOnRow0Count = 0;
    bool reachedRow1 = false;
    for (int i = 0; i < 10; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
      if (transport.currentRow() == 0) {
        heldOnRow0Count += 1;
      }
      if (transport.currentRow() == 1) {
        reachedRow1 = true;
        break;
      }
    }

    if (!reachedRow1) {
      std::cerr << "EEx row delay regression did not progress to next row" << '\n';
      return 1;
    }

    if (heldOnRow0Count < 2) {
      std::cerr << "EE2 row delay did not hold current row for expected additional rows" << '\n';
      return 1;
    }
  }

  {
    auto sampleRow1Level = [](bool enableLegacyFilter) {
      extracker::PatternEditor pattern(8, 1);
      if (enableLegacyFilter) {
        pattern.setEffect(0, 0, 0x0E, 0x01);  // E01 legacy filter on
      }
      pattern.insertNote(1, 0, 60, 0, 0, 120, true);

      extracker::Transport transport;
      transport.setTicksPerRow(1);
      transport.setPatternRows(8);
      transport.resetTickCount();

      extracker::AudioEngine audio;
      extracker::PluginHost plugins;
      extracker::Sequencer sequencer;

      sequencer.update(pattern, transport, audio, plugins);
      for (int i = 0; i < 3 && transport.currentRow() != 1; ++i) {
        transport.advanceExternalTick();
        sequencer.update(pattern, transport, audio, plugins);
      }

      // Ensure row 1 note dispatch has executed.
      sequencer.update(pattern, transport, audio, plugins);
      return audio.testToneVoiceLevel(0);
    };

    const double unfilteredLevel = sampleRow1Level(false);
    const double filteredLevel = sampleRow1Level(true);

    if (unfilteredLevel <= 0.0 || filteredLevel <= 0.0) {
      std::cerr << "E0x legacy filter regression failed to capture voice levels" << '\n';
      return 1;
    }

    if (filteredLevel >= unfilteredLevel * 0.9) {
      std::cerr << "E01 legacy filter did not attenuate internal synth voice level" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    pattern.setEffect(0, 0, 0x0E, 0xF2);  // EF2: legacy funk-repeat approximation (retrigger every 2 ticks)
    pattern.insertNote(1, 0, 60, 0, 0, 120, true);

    extracker::Transport transport;
    transport.setTicksPerRow(8);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    plugins.loadPlugin("builtin.sine");
    plugins.assignInstrument(0, "builtin.sine");
    audio.setPluginHost(&plugins);
    extracker::Sequencer sequencer;

    // Row 0 applies EF2 memory.
    sequencer.update(pattern, transport, audio, plugins);

    // Advance deterministically to row 1.
    for (int i = 0; i < 16 && transport.currentRow() != 1; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
    }

    if (transport.currentRow() != 1) {
      std::cerr << "EFx regression did not reach note row" << '\n';
      return 1;
    }

    sequencer.update(pattern, transport, audio, plugins);  // row 1 note dispatch
    const std::size_t baselineNoteOn = plugins.noteOnEventCount();

    for (int i = 0; i < 8; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
    }

    if (plugins.noteOnEventCount() < baselineNoteOn + 2) {
      std::cerr << "EF2 legacy funk-repeat approximation did not retrigger notes across ticks" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    pattern.insertNote(0, 0, 60, 0, 0, 120, true, 0x0E, 0xC2);  // EC2: cut note on tick 2

    extracker::Transport transport;
    transport.setTicksPerRow(8);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    plugins.loadPlugin("builtin.sine");
    plugins.assignInstrument(0, "builtin.sine");
    audio.setPluginHost(&plugins);
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);  // row 0 note dispatch
    const std::size_t baseNoteOff = plugins.noteOffEventCount();

    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 1: should still be active
    const std::size_t tick1NoteOff = plugins.noteOffEventCount();

    if (tick1NoteOff != baseNoteOff) {
      std::cerr << "EC2 note cut triggered too early before cut tick" << '\n';
      return 1;
    }

    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 2: cut should occur

    if (plugins.noteOffEventCount() <= baseNoteOff) {
      std::cerr << "EC2 note cut did not trigger note-off on configured tick" << '\n';
      return 1;
    }
  }

  {
    auto sampleNoteOnEventsForE9 = [](std::uint8_t subValue) {
      extracker::PatternEditor pattern(8, 1);
      pattern.insertNote(0, 0, 60, 0, 0, 120, true, 0x0E, static_cast<std::uint8_t>(0x90 | (subValue & 0x0F)));

      extracker::Transport transport;
      transport.setTicksPerRow(8);
      transport.setPatternRows(8);
      transport.resetTickCount();

      extracker::AudioEngine audio;
      extracker::PluginHost plugins;
      plugins.loadPlugin("builtin.sine");
      plugins.assignInstrument(0, "builtin.sine");
      audio.setPluginHost(&plugins);
      extracker::Sequencer sequencer;

      sequencer.update(pattern, transport, audio, plugins);  // row 0 note dispatch
      for (int i = 0; i < 8; ++i) {
        transport.advanceExternalTick();
        sequencer.update(pattern, transport, audio, plugins);
      }

      return plugins.noteOnEventCount();
    };

    const std::size_t e90NoteOn = sampleNoteOnEventsForE9(0x0);
    const std::size_t e93NoteOn = sampleNoteOnEventsForE9(0x3);

    if (e93NoteOn < e90NoteOn + 2) {
      std::cerr << "E93 retrigger did not add expected note-on activity over E90 baseline" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    pattern.setEffect(0, 0, 0x0E, 0xF2);                    // EF2: carry retrigger memory (every 2 ticks)
    pattern.insertNote(1, 0, 60, 0, 0, 120, true, 0x0E, 0xD3);  // ED3: delay start until tick 3

    extracker::Transport transport;
    transport.setTicksPerRow(8);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    plugins.loadPlugin("builtin.sine");
    plugins.assignInstrument(0, "builtin.sine");
    audio.setPluginHost(&plugins);
    extracker::Sequencer sequencer;

    // Row 0 applies EF2 memory.
    sequencer.update(pattern, transport, audio, plugins);

    // Advance deterministically to row 1 tick 0.
    for (int i = 0; i < 16 && transport.currentRow() != 1; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
    }

    if (transport.currentRow() != 1) {
      std::cerr << "ED/EF timing regression did not reach delayed-note row" << '\n';
      return 1;
    }

    sequencer.update(pattern, transport, audio, plugins);  // row 1 dispatch at tick 0
    const std::size_t noteOnAtTick0 = plugins.noteOnEventCount();

    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 1
    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 2

    if (plugins.noteOnEventCount() != noteOnAtTick0) {
      std::cerr << "ED3 triggered note-on activity before delayed start tick" << '\n';
      return 1;
    }

    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 3 delayed start
    const std::size_t noteOnAfterDelayedStart = plugins.noteOnEventCount();
    if (noteOnAfterDelayedStart <= noteOnAtTick0) {
      std::cerr << "ED3 delayed start did not trigger note-on on scheduled tick" << '\n';
      return 1;
    }

    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 4 retrigger from EF2 memory
    if (plugins.noteOnEventCount() <= noteOnAfterDelayedStart) {
      std::cerr << "EF2 memory did not retrigger delayed note after ED3 start" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    // Gate reaches threshold before delayed start; current behavior should start
    // the note on ED tick and release it immediately in the same tick update.
    pattern.insertNote(0, 0, 60, 0, 1, 120, true, 0x0E, 0xD3);  // gate=1, ED3

    extracker::Transport transport;
    transport.setTicksPerRow(8);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    plugins.loadPlugin("builtin.sine");
    plugins.assignInstrument(0, "builtin.sine");
    audio.setPluginHost(&plugins);
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);  // row 0 tick 0 dispatch
    const std::size_t baseNoteOn = plugins.noteOnEventCount();
    const std::size_t baseNoteOff = plugins.noteOffEventCount();

    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 1
    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 2

    if (plugins.noteOnEventCount() != baseNoteOn || plugins.noteOffEventCount() != baseNoteOff) {
      std::cerr << "ED3+gate regression emitted note activity before delayed start tick" << '\n';
      return 1;
    }

    transport.advanceExternalTick();
    sequencer.update(pattern, transport, audio, plugins);  // tick 3 delayed start and immediate gate release

    if (plugins.noteOnEventCount() != baseNoteOn + 1) {
      std::cerr << "ED3+gate regression did not emit delayed note-on on start tick" << '\n';
      return 1;
    }

    if (plugins.noteOffEventCount() != baseNoteOff + 1) {
      std::cerr << "ED3+gate regression did not apply gate release on delayed start tick" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor pattern(8, 1);
    pattern.setEffect(0, 0, 0x0E, 0xF2);               // row 0: EF2 (carry retrigger every 2 ticks)
    pattern.insertNote(1, 0, 60, 0, 0, 120, false);    // row 1: inherits EF2 retrigger memory
    pattern.setEffect(2, 0, 0x0E, 0xF0);               // row 2: EF0 clears retrigger memory
    pattern.insertNote(3, 0, 62, 0, 0, 120, false);    // row 3: should no longer inherit retrigger

    extracker::Transport transport;
    transport.setTicksPerRow(8);
    transport.setPatternRows(8);
    transport.resetTickCount();

    extracker::AudioEngine audio;
    extracker::PluginHost plugins;
    plugins.loadPlugin("builtin.sine");
    plugins.assignInstrument(0, "builtin.sine");
    audio.setPluginHost(&plugins);
    extracker::Sequencer sequencer;

    sequencer.update(pattern, transport, audio, plugins);  // row 0 EF2

    // Reach row 1, then capture note-on count and run one full row to observe EF2 retriggers.
    for (int i = 0; i < 16 && transport.currentRow() != 1; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
    }
    if (transport.currentRow() != 1) {
      std::cerr << "EF clear regression did not reach row 1" << '\n';
      return 1;
    }
    sequencer.update(pattern, transport, audio, plugins);  // row 1 dispatch
    const std::size_t row1StartNoteOn = plugins.noteOnEventCount();
    for (int i = 0; i < 8; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
    }
    const std::size_t row1EndNoteOn = plugins.noteOnEventCount();
    if (row1EndNoteOn < row1StartNoteOn + 2) {
      std::cerr << "EF2 carry regression did not retrigger row-1 note" << '\n';
      return 1;
    }
    const std::size_t row1Delta = row1EndNoteOn - row1StartNoteOn;

    // Reach row 3 (row 2 applies EF0 clear), then verify retriggers no longer occur.
    for (int i = 0; i < 24 && transport.currentRow() != 3; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
    }
    if (transport.currentRow() != 3) {
      std::cerr << "EF clear regression did not reach row 3" << '\n';
      return 1;
    }
    sequencer.update(pattern, transport, audio, plugins);  // row 3 dispatch
    const std::size_t row3StartNoteOn = plugins.noteOnEventCount();
    for (int i = 0; i < 8; ++i) {
      transport.advanceExternalTick();
      sequencer.update(pattern, transport, audio, plugins);
    }
    const std::size_t row3EndNoteOn = plugins.noteOnEventCount();
    const std::size_t row3Delta = row3EndNoteOn - row3StartNoteOn;
    if (row1Delta < row3Delta + 2) {
      std::cerr << "EF0 clear regression did not reduce extra retrigger activity on later notes" << '\n';
      return 1;
    }
  }

  return 0;
}
