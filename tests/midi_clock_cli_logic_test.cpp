#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "extracker/midi_cli.hpp"

namespace {

struct TestState {
  extracker::MidiInput midiInput;
  bool midiThruEnabled = false;
  int midiInstrument = 0;
  bool midiLearnEnabled = false;
  std::array<int, 16> midiChannelMap{};
  bool midiTransportSyncEnabled = false;
  bool midiTransportRunning = false;
  std::mutex stateMutex;
  std::chrono::milliseconds midiClockTimeout{1500};
  bool midiFallbackLockTempo = true;
  bool hasMidiClockTimestamp = false;
  std::chrono::steady_clock::time_point lastMidiClockTimestamp{};
  double midiClockEstimatedBpm = 0.0;
  std::vector<extracker::MidiPortEntry> ports;
};

std::string runMidiCommand(TestState& state, const std::string& command) {
  std::ostringstream captured;
  auto* original = std::cout.rdbuf(captured.rdbuf());

  std::function<void(const extracker::MidiEvent&)> onMidiEvent = [](const extracker::MidiEvent&) {};
  std::function<bool()> midiClockAlive = []() { return false; };
  std::function<const char*()> transportSource = []() { return "internal"; };

  std::function<bool(const std::string&, std::string&)> readCommandOutput =
      [](const std::string&, std::string& output) {
        output = "mock";
        return true;
      };

  std::function<std::vector<extracker::MidiPortEntry>(const std::string&)> parseAconnectPorts =
      [&state](const std::string&) {
        return state.ports;
      };

  std::function<std::string(std::string)> toLower = [](std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    return value;
  };

  std::function<bool(const std::string&, int&, int&)> parseHintEndpoint =
      [](const std::string&, int&, int&) { return false; };

  extracker::MidiCommandContext context{
      state.midiInput,
      onMidiEvent,
      state.midiThruEnabled,
      state.midiInstrument,
      state.midiLearnEnabled,
      state.midiChannelMap,
      state.midiTransportSyncEnabled,
      state.midiTransportRunning,
      state.stateMutex,
      midiClockAlive,
      state.midiClockTimeout,
      state.midiFallbackLockTempo,
      transportSource,
      state.hasMidiClockTimestamp,
      state.lastMidiClockTimestamp,
      state.midiClockEstimatedBpm,
      readCommandOutput,
      parseAconnectPorts,
      toLower,
      parseHintEndpoint};

  std::istringstream input(command);
  extracker::handleMidiCommand(input, context);

  std::cout.rdbuf(original);
  return captured.str();
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

bool testNoSourceDiagnostics() {
  TestState state;
  state.ports.clear();

  const std::string output = runMidiCommand(state, "clock diagnose Missing Clock");
  return contains(output, "Matching sources: 0") &&
         contains(output, "Suggestion: start source script first");
}

bool testMultipleSourceDiagnostics() {
  TestState state;
  state.ports = {
      extracker::MidiPortEntry{24, 0, "Clock A", "Main"},
      extracker::MidiPortEntry{25, 0, "Clock B", "Main"}};

  const std::string output = runMidiCommand(state, "clock diagnose clock");
  return contains(output, "Matching sources: 2") &&
         contains(output, "Multiple matching sources found; choose an explicit index");
}

bool testStaleClockDiagnostics() {
  TestState state;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};
  state.hasMidiClockTimestamp = true;
  state.lastMidiClockTimestamp = std::chrono::steady_clock::now() - std::chrono::seconds(5);
  state.midiClockTimeout = std::chrono::milliseconds(1000);
  state.midiClockEstimatedBpm = 120.0;

  const std::string output = runMidiCommand(state, "clock diagnose clock");
  return contains(output, "Clock state: stale") &&
         contains(output, "Suggestion: run 'midi transport reset' if clock source changed.");
}

}  // namespace

int main() {
  if (!testNoSourceDiagnostics()) {
    std::cerr << "No-source diagnostic behavior regression" << '\n';
    return 1;
  }

  if (!testMultipleSourceDiagnostics()) {
    std::cerr << "Multiple-source diagnostic behavior regression" << '\n';
    return 1;
  }

  if (!testStaleClockDiagnostics()) {
    std::cerr << "Stale-clock diagnostic behavior regression" << '\n';
    return 1;
  }

  return 0;
}
