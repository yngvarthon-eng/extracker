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
  bool midiInputRunning = false;
  std::string midiLastError;
  std::string midiEndpointHint = "ALSA target: invalid";
  bool parseHintSucceeds = false;
  int parsedClient = -1;
  int parsedPort = -1;
  int executeSystemStatus = 0;
  std::string lastSystemCommand;
  bool readCommandOutputSucceeds = true;
};

std::string runMidiCommand(TestState& state, const std::string& command) {
  std::ostringstream captured;
  auto* original = std::cout.rdbuf(captured.rdbuf());

  std::function<void(const extracker::MidiEvent&)> onMidiEvent = [](const extracker::MidiEvent&) {};
  std::function<bool()> midiClockAlive = []() { return false; };
  std::function<const char*()> transportSource = []() { return "internal"; };

  std::function<bool(const std::string&, std::string&)> readCommandOutput =
      [&state](const std::string&, std::string& output) {
        if (!state.readCommandOutputSucceeds) {
          return false;
        }
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
      [&state](const std::string&, int& client, int& port) {
        if (!state.parseHintSucceeds) {
          return false;
        }
        client = state.parsedClient;
        port = state.parsedPort;
        return true;
      };

  std::function<bool()> midiInputRunning = [&state]() {
    return state.midiInputRunning;
  };

  std::function<std::string()> midiLastError = [&state]() {
    return state.midiLastError;
  };

  std::function<std::string()> midiEndpointHint = [&state]() {
    return state.midiEndpointHint;
  };

  std::function<int(const std::string&)> executeSystemCommand =
      [&state](const std::string& command) {
        state.lastSystemCommand = command;
        return state.executeSystemStatus;
      };

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
      parseHintEndpoint,
      midiInputRunning,
      midiLastError,
      midiEndpointHint,
      executeSystemCommand};

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

bool testAutoconnectOutOfRangeIndex() {
  TestState state;
  state.midiInputRunning = true;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};

  const std::string output = runMidiCommand(state, "clock autoconnect clock 4");
  return contains(output, "Selected index 4 is out of range for 1 match(es).") &&
         contains(output, "Use: midi clock sources clock");
}

bool testAutoconnectNegativeIndexOutOfRange() {
  TestState state;
  state.midiInputRunning = true;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};

  const std::string output = runMidiCommand(state, "clock autoconnect clock -1");
  return contains(output, "Selected index -1 is out of range for 1 match(es).") &&
         contains(output, "Use: midi clock sources clock");
}

bool testAutoconnectMalformedIndexToken() {
  TestState state;
  state.midiInputRunning = true;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};

  const std::string output = runMidiCommand(state, "clock autoconnect clock 1foo");
  return contains(output, "Usage: midi clock autoconnect [name] [index]");
}

bool testAutoconnectNeedleWithDigitSuffix() {
  TestState state;
  state.midiInputRunning = true;
  state.parseHintSucceeds = false;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock2", "Main"}};

  const std::string output = runMidiCommand(state, "clock autoconnect clock2");
  return !contains(output, "Usage: midi clock autoconnect [name] [index]") &&
         contains(output, "Could not parse exTracker MIDI target endpoint.");
}

bool testAutoconnectEndpointParseFailure() {
  TestState state;
  state.midiInputRunning = true;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};
  state.midiEndpointHint = "ALSA target: malformed";
  state.parseHintSucceeds = false;

  const std::string output = runMidiCommand(state, "clock autoconnect clock 0");
  return contains(output, "Could not parse exTracker MIDI target endpoint.") &&
         contains(output, "ALSA target: malformed");
}

bool testAutoconnectSuccessConnectsSelectedSource() {
  TestState state;
  state.midiInputRunning = true;
  state.parseHintSucceeds = true;
  state.parsedClient = 128;
  state.parsedPort = 0;
  state.executeSystemStatus = 0;
  state.ports = {
      extracker::MidiPortEntry{24, 0, "Clock A", "Main"},
      extracker::MidiPortEntry{25, 1, "Clock B", "Out"}};

  const std::string output = runMidiCommand(state, "clock autoconnect clock 1");
  return state.lastSystemCommand == "aconnect 25:1 128:0" &&
         contains(output, "Connected MIDI clock source 'Clock B / Out' -> 128:0") &&
         contains(output, "Matched 2 source(s); selected index 1.");
}

bool testAutoconnectConnectFailureReportsCommand() {
  TestState state;
  state.midiInputRunning = true;
  state.parseHintSucceeds = true;
  state.parsedClient = 128;
  state.parsedPort = 0;
  state.executeSystemStatus = 1;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};

  const std::string output = runMidiCommand(state, "clock autoconnect clock 0");
  return state.lastSystemCommand == "aconnect 24:0 128:0" &&
         contains(output, "Failed to connect with command: aconnect 24:0 128:0");
}

bool testQuickStatusSummary() {
  TestState state;
  state.midiInputRunning = true;
  state.parseHintSucceeds = true;
  state.parsedClient = 128;
  state.parsedPort = 0;
  state.hasMidiClockTimestamp = true;
  state.lastMidiClockTimestamp = std::chrono::steady_clock::now() - std::chrono::milliseconds(200);
  state.midiClockTimeout = std::chrono::milliseconds(1000);
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};

  const std::string output = runMidiCommand(state, "clock quick Clock");
  return contains(output, "MIDI clock quick:") &&
         contains(output, "running: yes") &&
         contains(output, "endpoint: 128:0") &&
         contains(output, "clock: fresh") &&
         contains(output, "source matches: 1 (first 24:0)");
}

bool testQuickStatusTabSeparatedNeedle() {
  TestState state;
  state.midiInputRunning = true;
  state.parseHintSucceeds = true;
  state.parsedClient = 128;
  state.parsedPort = 0;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};

  const std::string output = runMidiCommand(state, "clock quick\tClock");
  return contains(output, "MIDI clock quick:") &&
         contains(output, "source matches: 1 (first 24:0)");
}

bool testQuickStatusListingFailure() {
  TestState state;
  state.midiInputRunning = false;
  state.parseHintSucceeds = false;
  state.readCommandOutputSucceeds = false;

  const std::string output = runMidiCommand(state, "clock quick");
  return contains(output, "MIDI clock quick:") &&
         contains(output, "running: no") &&
         contains(output, "endpoint: unavailable") &&
         contains(output, "source matches: n/a (aconnect failed)");
}

bool testClockHelpRejectsTrailingArgs() {
  TestState state;

  const std::string output = runMidiCommand(state, "clock help extra");
  return contains(output,
                  "Usage: midi clock <help|quick [name]|sources [name]|autoconnect [name] [index]|diagnose [name]|diagnose live [name]>");
}

bool testDiagnoseLiveProbeFlag() {
  TestState state;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};
  state.midiInputRunning = true;
  state.parseHintSucceeds = true;
  state.parsedClient = 128;
  state.parsedPort = 0;

  const std::string output = runMidiCommand(state, "clock diagnose live");
  return contains(output, "Mode: live health probe (1 second)") &&
         contains(output, "MIDI input running: yes");
}

bool testDiagnoseWithCustomFilter() {
  TestState state;
  state.ports = {extracker::MidiPortEntry{24, 0, "My Custom Clock", "Main"}};
  state.midiInputRunning = true;

  const std::string output = runMidiCommand(state, "clock diagnose My");
  return contains(output, "Source filter: 'My'") &&
         contains(output, "Matching sources: 1");
}

bool testDiagnoseLiveFlagCaseInsensitive() {
  TestState state;
  state.ports = {extracker::MidiPortEntry{24, 0, "Clock A", "Main"}};
  state.midiInputRunning = true;

  const std::string output = runMidiCommand(state, "clock diagnose LIVE");
  return contains(output, "Mode: live health probe (1 second)") &&
         contains(output, "Source filter: 'exTracker Virtual Clock'");
}

bool testDiagnoseLivePrefixIsFilterText() {
  TestState state;
  state.ports = {extracker::MidiPortEntry{24, 0, "liveclock", "Main"}};
  state.midiInputRunning = true;

  const std::string output = runMidiCommand(state, "clock diagnose liveclock");
  return !contains(output, "Mode: live health probe (1 second)") &&
         contains(output, "Source filter: 'liveclock'");
}

bool testDiagnoseLiveProbeRunsWhenListingFails() {
  TestState state;
  state.midiInputRunning = true;
  state.readCommandOutputSucceeds = false;

  const std::string output = runMidiCommand(state, "clock diagnose live");
  return contains(output, "Mode: live health probe (1 second)") &&
         contains(output, "ALSA port listing: failed (is aconnect installed?)") &&
         contains(output, "Live probe result:");
}

}  // namespace

int main() {
  if (!testNoSourceDiagnostics()) {
    std::cerr << "No-source diagnostic behavior regression" << '\n';
    return 1;
  }

  if (!testDiagnoseLiveProbeFlag()) {
    std::cerr << "Diagnose live probe behavior regression" << '\n';
    return 1;
  }

  if (!testDiagnoseWithCustomFilter()) {
    std::cerr << "Diagnose custom filter behavior regression" << '\n';
    return 1;
  }

  if (!testDiagnoseLiveFlagCaseInsensitive()) {
    std::cerr << "Diagnose live flag case-insensitive behavior regression" << '\n';
    return 1;
  }

  if (!testDiagnoseLivePrefixIsFilterText()) {
    std::cerr << "Diagnose live-prefix filter behavior regression" << '\n';
    return 1;
  }

  if (!testDiagnoseLiveProbeRunsWhenListingFails()) {
    std::cerr << "Diagnose live probe listing-failure behavior regression" << '\n';
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

  if (!testAutoconnectOutOfRangeIndex()) {
    std::cerr << "Autoconnect out-of-range behavior regression" << '\n';
    return 1;
  }

  if (!testAutoconnectNegativeIndexOutOfRange()) {
    std::cerr << "Autoconnect negative-index behavior regression" << '\n';
    return 1;
  }

  if (!testAutoconnectMalformedIndexToken()) {
    std::cerr << "Autoconnect malformed-index behavior regression" << '\n';
    return 1;
  }

  if (!testAutoconnectNeedleWithDigitSuffix()) {
    std::cerr << "Autoconnect digit-suffix needle behavior regression" << '\n';
    return 1;
  }

  if (!testAutoconnectEndpointParseFailure()) {
    std::cerr << "Autoconnect endpoint parse behavior regression" << '\n';
    return 1;
  }

  if (!testAutoconnectSuccessConnectsSelectedSource()) {
    std::cerr << "Autoconnect success behavior regression" << '\n';
    return 1;
  }

  if (!testAutoconnectConnectFailureReportsCommand()) {
    std::cerr << "Autoconnect connect failure behavior regression" << '\n';
    return 1;
  }

  if (!testQuickStatusSummary()) {
    std::cerr << "Quick status behavior regression" << '\n';
    return 1;
  }

  if (!testQuickStatusTabSeparatedNeedle()) {
    std::cerr << "Quick status tab-separated needle behavior regression" << '\n';
    return 1;
  }

  if (!testQuickStatusListingFailure()) {
    std::cerr << "Quick status listing failure behavior regression" << '\n';
    return 1;
  }

  if (!testClockHelpRejectsTrailingArgs()) {
    std::cerr << "Clock help trailing-args behavior regression" << '\n';
    return 1;
  }

  return 0;
}
