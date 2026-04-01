#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>

#include "extracker/audio_engine.hpp"
#include "extracker/midi_input.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

namespace extracker {

struct CoreCommandContext {
  Transport& transport;
  PatternEditor& editor;
  std::mutex& stateMutex;
  bool& loopEnabled;
  int& playRangeFrom;
  int& playRangeTo;
  bool& playRangeActive;
  const bool& recordEnabled;
  const bool& recordQuantizeEnabled;
  const bool& recordOverdubEnabled;
  const int& recordInsertJump;
  const bool& recordCanUndo;
  const bool& recordCanRedo;
  const int& recordChannel;
  const int& recordCursorRow;
  MidiInput& midiInput;
  const bool& midiThruEnabled;
  const int& midiInstrument;
  const bool& midiLearnEnabled;
  const bool& midiTransportSyncEnabled;
  const bool& midiTransportRunning;
  const std::chrono::milliseconds& midiClockTimeout;
  const bool& midiFallbackLockTempo;
  const bool& hasMidiClockTimestamp;
  const double& midiClockEstimatedBpm;
  const std::array<int, 16>& midiChannelMap;
  Sequencer& sequencer;
  PluginHost& plugins;
  AudioEngine& audio;
  const std::function<bool()>& midiClockAlive;
  const std::function<const char*()>& transportSource;
  const std::function<std::string(const std::string&)>& normalizeModulePath;
  const std::function<bool(const std::string&)>& savePatternToFile;
  const std::function<bool(const std::string&)>& loadPatternFromFile;
};

bool handleCoreCommand(const std::string& command,
                       std::istringstream& coreInput,
                       CoreCommandContext context);

}  // namespace extracker
