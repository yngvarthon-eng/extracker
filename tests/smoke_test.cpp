#include <iostream>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdlib>
#include <cmath>

#include "extracker/audio_engine.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

int main() {
  const auto tmpRoot = std::filesystem::temp_directory_path() / "extracker_lv2_test";
  const auto lv2Bundle = tmpRoot / "dummy.lv2";
  const auto manifestPath = lv2Bundle / "manifest.ttl";

  std::error_code fsError;
  std::filesystem::remove_all(tmpRoot, fsError);
  std::filesystem::create_directories(lv2Bundle, fsError);
  if (fsError) {
    std::cerr << "Failed to prepare temporary LV2 bundle directory" << '\n';
    return 1;
  }

  {
    std::ofstream manifest(manifestPath);
    manifest << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    manifest << "<urn:extracker:test:instrument> a lv2:Plugin ; lv2:binary <instrument.so> .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH for smoke test" << '\n';
    return 1;
  }

  extracker::AudioEngine audio;
  if (!audio.start()) {
    std::cerr << "Audio engine failed to start" << '\n';
    return 1;
  }
  audio.stop();

  extracker::PatternEditor editor(16, 4);
  editor.insertNote(0, 0, 60, 2, 0, 100, false, 0x0F, 0x34);
  editor.insertNote(8, 2, 67, 3, 0, 100, false);
  editor.setVelocity(0, 0, 80);
  editor.setRetrigger(0, 0, true);
  editor.setEffect(0, 0, 0x0C, 0x20);

  if (!editor.hasNoteAt(0, 0) || editor.noteAt(0, 0) != 60) {
    std::cerr << "Pattern editor failed to store first note" << '\n';
    return 1;
  }

  if (editor.velocityAt(0, 0) != 80 || !editor.retriggerAt(0, 0)) {
    std::cerr << "Pattern editor failed to store velocity/retrigger" << '\n';
    return 1;
  }

  if (editor.effectCommandAt(0, 0) != 0x0C || editor.effectValueAt(0, 0) != 0x20) {
    std::cerr << "Pattern editor failed to store effect command/value" << '\n';
    return 1;
  }

  if (editor.instrumentAt(0, 0) != 2) {
    std::cerr << "Pattern editor failed to store instrument" << '\n';
    return 1;
  }

  if (!editor.hasNoteAt(8, 2) || editor.noteAt(8, 2) != 67) {
    std::cerr << "Pattern editor failed to store second note" << '\n';
    return 1;
  }

  editor.clearStep(8, 2);
  if (editor.hasNoteAt(8, 2) || editor.noteAt(8, 2) != -1) {
    std::cerr << "Pattern editor failed to clear note" << '\n';
    return 1;
  }

  if (editor.velocityAt(8, 2) != 100 || editor.retriggerAt(8, 2)) {
    std::cerr << "Pattern editor failed to reset velocity/retrigger" << '\n';
    return 1;
  }

  if (editor.effectCommandAt(8, 2) != 0 || editor.effectValueAt(8, 2) != 0) {
    std::cerr << "Pattern editor failed to reset effect command/value" << '\n';
    return 1;
  }

  if (editor.instrumentAt(8, 2) != 0) {
    std::cerr << "Pattern editor failed to reset instrument" << '\n';
    return 1;
  }

  extracker::PluginHost plugins;
  const auto adapterNames = plugins.externalAdapterNames();
  if (adapterNames.empty()) {
    std::cerr << "Plugin host has no external adapter scaffold" << '\n';
    return 1;
  }

  if (plugins.rescanExternalPlugins() == 0) {
    std::cerr << "External adapters did not discover expected synthetic plugin" << '\n';
    return 1;
  }

  const auto availablePlugins = plugins.discoverAvailablePlugins();
  if (availablePlugins.size() < 2) {
    std::cerr << "Plugin discovery returned too few plugins" << '\n';
    return 1;
  }

  if (!plugins.loadPlugin("lv2:urn:extracker:test:instrument")) {
    std::cerr << "Plugin host failed to load discovered LV2 plugin" << '\n';
    return 1;
  }

  if (!plugins.assignInstrument(4, "lv2:urn:extracker:test:instrument")) {
    std::cerr << "Plugin host failed to assign discovered LV2 plugin" << '\n';
    return 1;
  }

  if (!plugins.triggerNoteOn(4, 72, 110, true)) {
    std::cerr << "Discovered LV2 plugin did not accept note-on trigger" << '\n';
    return 1;
  }

  if (!plugins.triggerNoteOff(4, 72)) {
    std::cerr << "Discovered LV2 plugin did not accept note-off trigger" << '\n';
    return 1;
  }

  if (plugins.loadPlugin("unknown.plugin")) {
    std::cerr << "Plugin host should reject unknown plugin id" << '\n';
    return 1;
  }

  if (!plugins.loadPlugin("builtin.sine") || !plugins.loadPlugin("builtin.square")) {
    std::cerr << "Plugin host failed to load plugins" << '\n';
    return 1;
  }

  if (!plugins.assignInstrument(2, "builtin.sine") || !plugins.assignInstrument(3, "builtin.square")) {
    std::cerr << "Plugin host failed to assign instruments" << '\n';
    return 1;
  }

  if (!plugins.assignInstrument(1, "builtin.sine")) {
    std::cerr << "Plugin host failed to assign instrument 1" << '\n';
    return 1;
  }

  if (!plugins.setInstrumentParameter(2, "gain", 0.5) || !plugins.setInstrumentParameter(3, "release_ms", 80.0)) {
    std::cerr << "Plugin host failed to set instrument parameters" << '\n';
    return 1;
  }

  if (plugins.getInstrumentParameter(2, "gain") <= 0.0 || plugins.getInstrumentParameter(3, "release_ms") < 79.0) {
    std::cerr << "Plugin host parameter readback mismatch" << '\n';
    return 1;
  }

  if (!plugins.hasInstrumentAssignment(2) || plugins.pluginForInstrument(2) != "builtin.sine") {
    std::cerr << "Plugin host route mismatch for instrument 2" << '\n';
    return 1;
  }

  audio.setPluginHost(&plugins);

  {
    extracker::PatternEditor effectPattern(8, 2);
    effectPattern.insertNote(0, 0, 60, 1, 0, 100, false, 0x0C, 0x30);
    effectPattern.setEffect(0, 1, 0x0F, 150);
    effectPattern.setEffect(0, 0, 0x0B, 3);
    effectPattern.setEffect(3, 1, 0x0F, 4);
    effectPattern.setEffect(3, 0, 0x0D, 1);

    extracker::Transport effectTransport;
    effectTransport.setPatternRows(8);
    effectTransport.resetTickCount();

    extracker::AudioEngine effectAudio;
    extracker::PluginHost effectPlugins;
    effectPlugins.loadPlugin("builtin.sine");
    effectPlugins.assignInstrument(1, "builtin.sine");

    extracker::Sequencer effectSequencer;
    effectSequencer.update(effectPattern, effectTransport, effectAudio, effectPlugins);

    if (std::abs(effectTransport.tempoBpm() - 150.0) > 0.001) {
      std::cerr << "Tempo effect command did not update transport tempo" << '\n';
      return 1;
    }

    if (effectTransport.currentRow() != 3) {
      std::cerr << "Pattern jump effect command did not jump to target row" << '\n';
      return 1;
    }

    effectSequencer.update(effectPattern, effectTransport, effectAudio, effectPlugins);

    if (effectTransport.ticksPerRow() != 4) {
      std::cerr << "Speed effect command did not update ticks-per-row" << '\n';
      return 1;
    }

    if (effectTransport.currentRow() != 1) {
      std::cerr << "Pattern break effect command did not jump to break row" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor memoryPattern(4, 2);
    memoryPattern.setEffect(0, 0, 0x0F, 180);
    memoryPattern.setEffect(1, 0, 0x0F, 0);
    memoryPattern.setEffect(0, 1, 0x0B, 1);

    extracker::Transport memoryTransport;
    memoryTransport.setPatternRows(4);
    memoryTransport.resetTickCount();

    extracker::AudioEngine memoryAudio;
    extracker::PluginHost memoryPlugins;
    extracker::Sequencer memorySequencer;

    memorySequencer.update(memoryPattern, memoryTransport, memoryAudio, memoryPlugins);
    if (std::abs(memoryTransport.tempoBpm() - 180.0) > 0.001) {
      std::cerr << "Initial tempo effect did not apply" << '\n';
      return 1;
    }

    memorySequencer.update(memoryPattern, memoryTransport, memoryAudio, memoryPlugins);
    if (std::abs(memoryTransport.tempoBpm() - 180.0) > 0.001) {
      std::cerr << "Effect memory for tempo command did not persist across rows" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor arpPattern(8, 1);
    arpPattern.insertNote(0, 0, 60, 0, 0, 120, true, 0x00, 0x47);

    extracker::Transport arpTransport;
    arpTransport.setTempoBpm(1000.0);
    arpTransport.setTicksPerBeat(24);
    arpTransport.setTicksPerRow(8);
    arpTransport.setPatternRows(8);
    arpTransport.resetTickCount();

    extracker::AudioEngine arpAudio;
    extracker::PluginHost arpPlugins;
    extracker::Sequencer arpSequencer;

    arpSequencer.update(arpPattern, arpTransport, arpAudio, arpPlugins);
    double baseFrequency = arpAudio.testToneFrequencyHz();

    arpTransport.play();
    double peakFrequency = baseFrequency;
    for (int i = 0; i < 25; ++i) {
      arpSequencer.update(arpPattern, arpTransport, arpAudio, arpPlugins);
      peakFrequency = std::max(peakFrequency, arpAudio.testToneFrequencyHz());
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    arpTransport.stop();

    if (peakFrequency <= baseFrequency * 1.05) {
      std::cerr << "Arpeggio effect did not modulate note pitch across ticks" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor delayPattern(8, 1);
    delayPattern.insertNote(0, 0, 60, 0, 0, 120, true, 0x0E, 0xD3);

    extracker::Transport delayTransport;
    delayTransport.setTempoBpm(1000.0);
    delayTransport.setTicksPerBeat(24);
    delayTransport.setTicksPerRow(8);
    delayTransport.setPatternRows(8);
    delayTransport.resetTickCount();

    extracker::AudioEngine delayAudio;
    extracker::PluginHost delayPlugins;
    extracker::Sequencer delaySequencer;

    delaySequencer.update(delayPattern, delayTransport, delayAudio, delayPlugins);
    if (delayAudio.testToneVoiceCount() != 0) {
      std::cerr << "Note delay effect should suppress note-on at row start" << '\n';
      return 1;
    }

    delayTransport.play();
    bool delayedStartObserved = false;
    for (int i = 0; i < 24; ++i) {
      delaySequencer.update(delayPattern, delayTransport, delayAudio, delayPlugins);
      if (delayAudio.testToneVoiceCount() > 0) {
        delayedStartObserved = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    delayTransport.stop();

    if (!delayedStartObserved) {
      std::cerr << "Note delay effect did not trigger note-on later in the row" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor cutPattern(8, 1);
    cutPattern.insertNote(0, 0, 60, 0, 0, 120, true, 0x0E, 0xC2);

    extracker::Transport cutTransport;
    cutTransport.setTempoBpm(1000.0);
    cutTransport.setTicksPerBeat(24);
    cutTransport.setTicksPerRow(8);
    cutTransport.setPatternRows(8);
    cutTransport.resetTickCount();

    extracker::AudioEngine cutAudio;
    extracker::PluginHost cutPlugins;
    extracker::Sequencer cutSequencer;

    cutSequencer.update(cutPattern, cutTransport, cutAudio, cutPlugins);
    cutTransport.play();

    bool noteCutObserved = false;
    for (int i = 0; i < 30; ++i) {
      cutSequencer.update(cutPattern, cutTransport, cutAudio, cutPlugins);
      if (cutAudio.testToneVoiceCount() == 0) {
        noteCutObserved = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    cutTransport.stop();

    if (!noteCutObserved) {
      std::cerr << "Note cut effect did not stop note within row" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor retrigPattern(8, 1);
    retrigPattern.insertNote(0, 0, 60, 0, 0, 120, true, 0x09, 0x02);

    extracker::Transport retrigTransport;
    retrigTransport.setTempoBpm(1000.0);
    retrigTransport.setTicksPerBeat(24);
    retrigTransport.setTicksPerRow(8);
    retrigTransport.setPatternRows(8);
    retrigTransport.resetTickCount();

    extracker::AudioEngine retrigAudio;
    extracker::PluginHost retrigPlugins;
    retrigPlugins.loadPlugin("builtin.sine");
    retrigPlugins.assignInstrument(0, "builtin.sine");
    retrigAudio.setPluginHost(&retrigPlugins);

    extracker::Sequencer retrigSequencer;
    retrigSequencer.update(retrigPattern, retrigTransport, retrigAudio, retrigPlugins);
    std::size_t noteOnCountBefore = retrigPlugins.noteOnEventCount();

    retrigTransport.play();
    for (int i = 0; i < 24; ++i) {
      retrigSequencer.update(retrigPattern, retrigTransport, retrigAudio, retrigPlugins);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    retrigTransport.stop();

    if (retrigPlugins.noteOnEventCount() <= noteOnCountBefore + 1) {
      std::cerr << "Retrigger effect did not emit repeated note-on events" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor fineSlidePattern(8, 1);
    fineSlidePattern.insertNote(0, 0, 60, 0, 0, 120, true, 0x0E, 0x12);

    extracker::Transport fineSlideTransport;
    fineSlideTransport.setPatternRows(8);
    fineSlideTransport.resetTickCount();

    extracker::AudioEngine fineSlideAudio;
    extracker::PluginHost fineSlidePlugins;
    extracker::Sequencer fineSlideSequencer;

    fineSlideSequencer.update(fineSlidePattern, fineSlideTransport, fineSlideAudio, fineSlidePlugins);
    if (fineSlideAudio.testToneFrequencyHz() <= 261.0) {
      std::cerr << "Fine slide up effect did not increase initial pitch" << '\n';
      return 1;
    }
  }

  {
    extracker::PatternEditor comboPattern(8, 1);
    comboPattern.insertNote(0, 0, 60, 0, 0, 90, true, 0x03, 0x20);
    comboPattern.insertNote(1, 0, 67, 0, 0, 90, true, 0x05, 0x10);
    comboPattern.insertNote(2, 0, 67, 0, 0, 90, true, 0x04, 0x47);
    comboPattern.insertNote(3, 0, 67, 0, 0, 90, true, 0x06, 0x10);

    extracker::Transport comboTransport;
    comboTransport.setTempoBpm(1000.0);
    comboTransport.setTicksPerBeat(24);
    comboTransport.setTicksPerRow(4);
    comboTransport.setPatternRows(8);
    comboTransport.resetTickCount();

    extracker::AudioEngine comboAudio;
    extracker::PluginHost comboPlugins;
    extracker::Sequencer comboSequencer;

    comboSequencer.update(comboPattern, comboTransport, comboAudio, comboPlugins);
    comboTransport.play();

    for (int i = 0; i < 10; ++i) {
      comboSequencer.update(comboPattern, comboTransport, comboAudio, comboPlugins);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    double row1Frequency = comboAudio.testToneFrequencyHz();
    if (row1Frequency <= 390.0) {
      std::cerr << "0x05 did not keep tone-portamento behavior while applying volume slide" << '\n';
      comboTransport.stop();
      return 1;
    }

    for (int i = 0; i < 12; ++i) {
      comboSequencer.update(comboPattern, comboTransport, comboAudio, comboPlugins);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    double row3MinFrequency = 1e9;
    double row3MaxFrequency = 0.0;
    for (int i = 0; i < 16; ++i) {
      comboSequencer.update(comboPattern, comboTransport, comboAudio, comboPlugins);
      double hz = comboAudio.testToneFrequencyHz();
      row3MinFrequency = std::min(row3MinFrequency, hz);
      row3MaxFrequency = std::max(row3MaxFrequency, hz);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    comboTransport.stop();

    if (row3MaxFrequency - row3MinFrequency < 3.0) {
      std::cerr << "0x06 did not keep vibrato behavior while applying volume slide" << '\n';
      return 1;
    }
  }

  extracker::Transport transport;
  transport.setTempoBpm(240.0);
  transport.setTicksPerBeat(6);
  transport.setTicksPerRow(32);
  transport.setPatternRows(4);
  transport.resetTickCount();

  extracker::Sequencer sequencer;
  editor.setGateTicks(0, 0, 2);
  editor.insertNote(0, 1, 64, 1, 0, 120, true);
  editor.insertNote(0, 2, 64, 2, 0, 90, false);
  editor.insertNote(1, 1, 67, 1, 0, 50, false);
  editor.insertNote(2, 1, 71, 1, 0, 100, false);

  sequencer.update(editor, transport, audio, plugins);
  if (sequencer.dispatchCount() == 0 || sequencer.activeMidiNote() < 0) {
    std::cerr << "Sequencer failed to dispatch initial row" << '\n';
    return 1;
  }

  if (sequencer.activeVoiceCount() < 2 || audio.testToneVoiceCount() < 2) {
    std::cerr << "Sequencer failed to dispatch polyphonic row" << '\n';
    return 1;
  }

  std::size_t initialRowVoiceCount = sequencer.activeVoiceCount();

  bool sawPositiveToneFrequency = audio.testToneFrequencyHz() > 0.0;

  if (!transport.play()) {
    std::cerr << "Transport failed to enter play state" << '\n';
    return 1;
  }

  bool sawGateRelease = false;
  for (int i = 0; i < 80; ++i) {
    sequencer.update(editor, transport, audio, plugins);
    if (transport.currentRow() == 0 && sequencer.activeVoiceCount() < initialRowVoiceCount) {
      sawGateRelease = true;
      break;
    }
    if (transport.currentRow() != 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  transport.stop();

  transport.setTicksPerRow(1);
  transport.resetTickCount();

  if (!transport.play()) {
    std::cerr << "Transport failed to restart for row progression" << '\n';
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  for (int i = 0; i < 20; ++i) {
    sequencer.update(editor, transport, audio, plugins);
    if (audio.testToneFrequencyHz() > 0.0) {
      sawPositiveToneFrequency = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }

  transport.stop();

  transport.resetTickCount();
  transport.setPatternRows(4);
  extracker::PatternEditor emptyEditor(16, 4);

  bool sawNoteOff = false;
  bool sawActiveBeforeEmptyRow = sequencer.activeVoiceCount() > 0;
  transport.play();
  for (int i = 0; i < 120; ++i) {
    sequencer.update(emptyEditor, transport, audio, plugins);
    if (sequencer.activeVoiceCount() > 0) {
      sawActiveBeforeEmptyRow = true;
    }
    if (transport.tickCount() > 0 && sawActiveBeforeEmptyRow && sequencer.activeVoiceCount() == 0) {
      sawNoteOff = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  transport.stop();

  if (transport.isPlaying()) {
    std::cerr << "Transport failed to leave play state" << '\n';
    return 1;
  }

  if (transport.tickCount() == 0) {
    std::cerr << "Transport did not advance ticks" << '\n';
    return 1;
  }

  if (transport.rowAdvanceCount() == 0) {
    std::cerr << "Transport did not advance rows" << '\n';
    return 1;
  }

  if (transport.currentRow() >= transport.patternRows()) {
    std::cerr << "Transport row index exceeded pattern length" << '\n';
    return 1;
  }

  if (sequencer.dispatchCount() < 2) {
    std::cerr << "Sequencer did not dispatch multiple rows" << '\n';
    return 1;
  }

  if (!sawGateRelease) {
    std::cerr << "Sequencer did not apply gate release within row" << '\n';
    return 1;
  }

  if (!sawPositiveToneFrequency) {
    std::cerr << "Audio engine did not receive sequencer tone frequency" << '\n';
    return 1;
  }

  if (!sawNoteOff) {
    std::cerr << "Sequencer did not emit note-off on empty row" << '\n';
    return 1;
  }

  if (plugins.noteOnEventCount() == 0 || plugins.noteOffEventCount() == 0) {
    std::cerr << "Plugin callbacks were not triggered by sequencer events" << '\n';
    return 1;
  }

  std::filesystem::remove_all(tmpRoot, fsError);

  return 0;
}
