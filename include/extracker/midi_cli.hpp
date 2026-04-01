#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "extracker/audio_engine.hpp"
#include "extracker/midi_input.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

namespace extracker {

struct MidiPortEntry {
  int client = -1;
  int port = -1;
  std::string clientName;
  std::string portName;
};

struct MidiCommandContext {
  MidiInput& midiInput;
  const std::function<void(const MidiEvent&)>& onMidiEvent;
  bool& midiThruEnabled;
  int& midiInstrument;
  bool& midiLearnEnabled;
  std::array<int, 16>& midiChannelMap;
  bool& midiTransportSyncEnabled;
  bool& midiTransportRunning;
  std::mutex& stateMutex;
  const std::function<bool()>& midiClockAlive;
  std::chrono::milliseconds& midiClockTimeout;
  bool& midiFallbackLockTempo;
  const std::function<const char*()>& transportSource;
  bool& hasMidiClockTimestamp;
  std::chrono::steady_clock::time_point& lastMidiClockTimestamp;
  double& midiClockEstimatedBpm;
  const std::function<bool(const std::string&, std::string&)>& readCommandOutput;
  const std::function<std::vector<MidiPortEntry>(const std::string&)>& parseAconnectPorts;
  const std::function<std::string(std::string)>& toLower;
  const std::function<bool(const std::string&, int&, int&)>& parseHintEndpoint;
};

struct MidiEventContext {
  Transport& transport;
  Sequencer& sequencer;
  AudioEngine& audio;
  PluginHost& plugins;
  bool& midiTransportSyncEnabled;
  bool& midiTransportRunning;
  bool& hasMidiClockTimestamp;
  std::chrono::steady_clock::time_point& lastMidiClockTimestamp;
  double& midiClockEstimatedBpm;
  int& midiInstrument;
  std::array<int, 16>& midiChannelMap;
  bool& midiLearnEnabled;
  bool& midiThruEnabled;
  bool& recordEnabled;
  int& recordChannel;
  const std::function<int(int)>& chooseRecordRow;
  const std::function<void(int,
                           int,
                           int,
                           std::uint8_t,
                           std::uint32_t,
                           std::uint8_t,
                           bool,
                           std::uint8_t,
                           std::uint8_t)>& applyRecordWrite;
  const std::function<double(int)>& midiNoteToFrequencyHz;
};

void handleMidiEventLocked(const MidiEvent& event,
                           MidiEventContext context);

void handleMidiCommand(std::istringstream& midiInputStream,
                       MidiCommandContext context);

}  // namespace extracker
