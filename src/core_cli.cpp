#include "extracker/core_cli.hpp"

#include "extracker/cli_parse_utils.hpp"

#include <algorithm>
#include <iostream>

namespace extracker {
bool handleCoreCommand(const std::string& command,
                       std::istringstream& coreInput,
                       CoreCommandContext context) {
  auto& transport = context.transport;
  auto& editor = context.editor;
  auto& stateMutex = context.stateMutex;
  auto& loopEnabled = context.loopEnabled;
  auto& playRangeFrom = context.playRangeFrom;
  auto& playRangeTo = context.playRangeTo;
  auto& playRangeActive = context.playRangeActive;
  const auto& recordEnabled = context.recordEnabled;
  const auto& recordQuantizeEnabled = context.recordQuantizeEnabled;
  const auto& recordOverdubEnabled = context.recordOverdubEnabled;
  const auto& recordInsertJump = context.recordInsertJump;
  const auto& recordCanUndo = context.recordCanUndo;
  const auto& recordCanRedo = context.recordCanRedo;
  const auto& recordChannel = context.recordChannel;
  const auto& recordCursorRow = context.recordCursorRow;
  auto& midiInput = context.midiInput;
  const auto& midiThruEnabled = context.midiThruEnabled;
  const auto& midiInstrument = context.midiInstrument;
  const auto& midiLearnEnabled = context.midiLearnEnabled;
  const auto& midiTransportSyncEnabled = context.midiTransportSyncEnabled;
  const auto& midiTransportRunning = context.midiTransportRunning;
  const auto& midiClockTimeout = context.midiClockTimeout;
  const auto& midiFallbackLockTempo = context.midiFallbackLockTempo;
  const auto& hasMidiClockTimestamp = context.hasMidiClockTimestamp;
  const auto& midiClockEstimatedBpm = context.midiClockEstimatedBpm;
  const auto& midiChannelMap = context.midiChannelMap;
  auto& sequencer = context.sequencer;
  auto& plugins = context.plugins;
  auto& audio = context.audio;
  const auto& midiClockAlive = context.midiClockAlive;
  const auto& transportSource = context.transportSource;
  const auto& normalizeModulePath = context.normalizeModulePath;
  const auto& savePatternToFile = context.savePatternToFile;
  const auto& loadPatternFromFile = context.loadPatternFromFile;
  if (command == "play") {
    if (transport.play()) {
      std::cout << "Playback started" << '\n';
    } else {
      std::cout << "Playback already running" << '\n';
    }
    return true;
  }

  if (command == "stop") {
    transport.stop();
    std::cout << "Playback stopped" << '\n';
    return true;
  }

  if (command == "tempo") {
    double bpm = 0.0;
    if (cli::parseStrictDoubleFromStream(coreInput, bpm) && bpm > 0.0 && !cli::hasExtraTokens(coreInput)) {
      transport.setTempoBpm(bpm);
      std::cout << "Tempo set to " << transport.tempoBpm() << " BPM" << '\n';
    } else {
      std::cout << "Usage: tempo <positive_bpm>" << '\n';
    }
    return true;
  }

  if (command == "loop") {
    std::string mode;
    coreInput >> mode;
    if (mode == "on") {
      loopEnabled = true;
      std::cout << "Loop enabled" << '\n';
    } else if (mode == "off") {
      loopEnabled = false;
      std::cout << "Loop disabled" << '\n';
    } else if (mode == "range") {
      int from = -1;
      int to = -1;
      if (!cli::parseStrictIntFromStream(coreInput, from) ||
          !cli::parseStrictIntFromStream(coreInput, to) ||
          cli::hasExtraTokens(coreInput)) {
        std::cout << "Usage: loop range <from> <to>" << '\n';
      } else {
        if (from > to) {
          std::swap(from, to);
        }
        from = std::max(from, 0);
        to = std::min(to, static_cast<int>(editor.rows()) - 1);
        std::lock_guard<std::mutex> lock(stateMutex);
        playRangeFrom = from;
        playRangeTo = to;
        playRangeActive = true;
        std::cout << "Loop/play range set to " << playRangeFrom << ".." << playRangeTo << '\n';
      }
    } else {
      std::cout << "Usage: loop <on|off|range>" << '\n';
    }
    return true;
  }

  if (command == "status") {
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      midiClockAlive();
    }
    std::cout << audio.status() << '\n';
    std::cout << editor.status() << '\n';
    std::cout << "Transport playing: " << (transport.isPlaying() ? "yes" : "no") << '\n';
    std::cout << "Transport tempo: " << transport.tempoBpm() << '\n';
    std::cout << "Transport ticks: " << transport.tickCount() << '\n';
    std::cout << "Transport row: " << transport.currentRow() << '\n';
    std::cout << "Loop enabled: " << (loopEnabled ? "yes" : "no") << '\n';
    if (playRangeActive) {
      std::cout << "Play range: " << playRangeFrom << ".." << playRangeTo << '\n';
    } else {
      std::cout << "Play range: (full pattern)" << '\n';
    }
    std::cout << "Record enabled: " << (recordEnabled ? "yes" : "no") << '\n';
    std::cout << "Record quantize: " << (recordQuantizeEnabled ? "on" : "off") << '\n';
    std::cout << "Record overdub: " << (recordOverdubEnabled ? "on" : "off") << '\n';
    std::cout << "Record jump: " << recordInsertJump << '\n';
    std::cout << "Record undo available: " << (recordCanUndo ? "yes" : "no") << '\n';
    std::cout << "Record redo available: " << (recordCanRedo ? "yes" : "no") << '\n';
    if (recordEnabled) {
      std::cout << "Record channel: " << recordChannel << '\n';
      std::cout << "Record cursor row: " << recordCursorRow << '\n';
    }
    std::cout << "MIDI running: " << (midiInput.isRunning() ? "yes" : "no") << '\n';
    std::cout << "MIDI thru: " << (midiThruEnabled ? "on" : "off") << '\n';
    std::cout << "MIDI instrument: " << midiInstrument << '\n';
    std::cout << "MIDI learn: " << (midiLearnEnabled ? "on" : "off") << '\n';
    std::cout << "MIDI transport sync: " << (midiTransportSyncEnabled ? "on" : "off") << '\n';
    std::cout << "MIDI transport running: " << (midiTransportRunning ? "yes" : "no") << '\n';
    std::cout << "MIDI clock timeout ms: " << midiClockTimeout.count() << '\n';
    std::cout << "MIDI fallback tempo lock: " << (midiFallbackLockTempo ? "on" : "off") << '\n';
    std::cout << "Transport source: " << transportSource() << '\n';
    if (hasMidiClockTimestamp && midiClockEstimatedBpm > 0.0) {
      std::cout << "MIDI clock active: yes" << '\n';
      std::cout << "MIDI clock BPM (estimated): " << midiClockEstimatedBpm << '\n';
    } else {
      std::cout << "MIDI clock active: no" << '\n';
      std::cout << "MIDI clock BPM (estimated): n/a" << '\n';
    }
    std::cout << "MIDI channel map:";
    bool hasMidiMap = false;
    for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
      if (midiChannelMap[ch] >= 0) {
        hasMidiMap = true;
        std::cout << " ch" << ch << "->" << midiChannelMap[ch];
      }
    }
    if (!hasMidiMap) {
      std::cout << " (empty)";
    }
    std::cout << '\n';
    std::cout << "Sequencer dispatch count: " << sequencer.dispatchCount() << '\n';
    std::cout << "Sequencer active voices: " << sequencer.activeVoiceCount() << '\n';
    std::cout << "Plugin note-on events: " << plugins.noteOnEventCount() << '\n';
    std::cout << "Plugin note-off events: " << plugins.noteOffEventCount() << '\n';
    std::cout << "Instrument 0 plugin: " << plugins.pluginForInstrument(0) << '\n';
    std::cout << "Instrument 1 plugin: " << plugins.pluginForInstrument(1) << '\n';
    std::cout << plugins.status() << '\n';
    return true;
  }

  if (command == "reset") {
    transport.stop();
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      transport.resetTickCount();
      sequencer.reset();
      audio.allNotesOff();
      playRangeActive = false;
    }
    std::cout << "Playback state reset" << '\n';
    return true;
  }

  if (command == "save") {
    std::string path;
    coreInput >> path;
    if (path.empty()) {
      std::cout << "Usage: save <file>" << '\n';
    } else {
      std::lock_guard<std::mutex> lock(stateMutex);
      std::string resolvedPath = normalizeModulePath(path);
      if (savePatternToFile(resolvedPath)) {
        std::cout << "Module saved to " << resolvedPath << '\n';
      } else {
        std::cout << "Failed to save module to " << resolvedPath << '\n';
      }
    }
    return true;
  }

  if (command == "load") {
    std::string path;
    coreInput >> path;
    if (path.empty()) {
      std::cout << "Usage: load <file>" << '\n';
    } else {
      std::lock_guard<std::mutex> lock(stateMutex);
      std::string resolvedPath = normalizeModulePath(path);
      if (loadPatternFromFile(resolvedPath)) {
        sequencer.reset();
        audio.allNotesOff();
        std::cout << "Module loaded from " << resolvedPath << '\n';
      } else {
        std::cout << "Failed to load module from " << resolvedPath << '\n';
      }
    }
    return true;
  }

  return false;
}

}  // namespace extracker
