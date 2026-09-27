#include "midi_cli_internal.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace extracker::midi_cli_internal {

namespace {

constexpr char kClockUsage[] =
    "Usage: midi clock <help|quick [name]|sources [name]|autoconnect [name] [index]|"
    "diagnose [name]|diagnose live [name]>";

void printMidiClockHelp() {
  std::cout << "External MIDI clock setup (ALSA):" << '\n';
  std::cout << "  1) Start MIDI input: midi on" << '\n';
  std::cout << "  2) Enable sync:     midi transport on" << '\n';
  std::cout << "  3) List ports:      aconnect -l" << '\n';
  std::cout << "  4) Connect source->exTracker using client:port ids" << '\n';
  std::cout << "     Example: aconnect 24:0 128:0" << '\n';
  std::cout << "  5) Check state:     midi transport status" << '\n';
  std::cout << "Tips:" << '\n';
  std::cout << "  - DAWs can act as master clock (Ardour, REAPER, Bitwig, Renoise)." << '\n';
  std::cout << "  - If clock drops out, exTracker can fall back to internal transport." << '\n';
  std::cout << "  - Tune timeout with: midi transport timeout <ms>" << '\n';
  std::cout << "  - List sources: midi clock sources [name]" << '\n';
  std::cout << "  - Auto-connect helper: midi clock autoconnect [name] [index]" << '\n';
  std::cout << "  - Quick diagnostics: midi clock diagnose [name]" << '\n';
  std::cout << "  - Compact status: midi clock quick [name]" << '\n';
  std::cout << "  - Live probe: midi clock diagnose live [name]" << '\n';
}

void handleMidiClockQuick(const MidiCommandContext& context,
                          std::istringstream& midiInputStream) {
  std::string needle;
  std::getline(midiInputStream, needle);
  needle = trimLeadingSpaces(needle);
  if (needle.empty()) {
    needle = kDefaultClockSourceNeedle;
  }

  int targetClient = -1;
  int targetPort = -1;
  bool hasTargetEndpoint = context.parseHintEndpoint(context.midiEndpointHint(), targetClient, targetPort);

  bool hasClockSnapshot = false;
  bool clockFresh = false;
  long long clockAgeMs = -1;
  {
    std::lock_guard<std::mutex> lock(context.stateMutex);
    hasClockSnapshot = context.hasMidiClockTimestamp;
    if (hasClockSnapshot) {
      auto now = std::chrono::steady_clock::now();
      clockAgeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - context.lastMidiClockTimestamp).count();
      clockFresh = (now - context.lastMidiClockTimestamp) <= context.midiClockTimeout;
    }
  }

  std::vector<MidiPortEntry> matches;
  bool listedSources = readMatchingClockSources(context, needle, matches);

  std::cout << "MIDI clock quick:" << '\n';
  std::cout << "  running: " << (context.midiInputRunning() ? "yes" : "no") << '\n';
  if (hasTargetEndpoint) {
    std::cout << "  endpoint: " << targetClient << ":" << targetPort << '\n';
  } else {
    std::cout << "  endpoint: unavailable" << '\n';
  }

  if (!hasClockSnapshot) {
    std::cout << "  clock: none" << '\n';
  } else {
    std::cout << "  clock: " << (clockFresh ? "fresh" : "stale");
    if (clockAgeMs >= 0) {
      std::cout << " (" << clockAgeMs << " ms)";
    }
    std::cout << '\n';
  }

  std::cout << "  source filter: '" << needle << "'" << '\n';
  if (!listedSources) {
    std::cout << "  source matches: n/a (aconnect failed)" << '\n';
  } else {
    std::cout << "  source matches: " << matches.size();
    if (!matches.empty()) {
      const auto& first = matches.front();
      std::cout << " (first " << first.client << ":" << first.port << ")";
    }
    std::cout << '\n';
  }
}

void handleMidiClockSources(const MidiCommandContext& context,
                            std::istringstream& midiInputStream) {
  std::string needle;
  std::getline(midiInputStream, needle);
  needle = trimLeadingSpaces(needle);
  if (needle.empty()) {
    needle = kDefaultClockSourceNeedle;
  }

  std::vector<MidiPortEntry> matches;
  if (!readMatchingClockSources(context, needle, matches)) {
    std::cout << "Failed to run aconnect -l. Ensure ALSA tools are installed." << '\n';
  } else if (matches.empty()) {
    std::cout << "No MIDI source matching '" << needle << "' found." << '\n';
  } else {
    std::cout << "Matching MIDI sources for '" << needle << "':" << '\n';
    for (std::size_t i = 0; i < matches.size(); ++i) {
      const auto& source = matches[i];
      std::cout << "  [" << i << "] " << source.client << ":" << source.port
                << " '" << source.clientName << " / " << source.portName << "'" << '\n';
    }
  }
}

}  // namespace

void handleMidiClockCommand(std::istringstream& midiInputStream,
                            const MidiCommandContext& context) {
  const auto& toLower = context.toLower;
  const auto& midiInputRunning = context.midiInputRunning;

  std::string mode;
  midiInputStream >> mode;

  auto hasTrailingArgs = [&]() {
    midiInputStream >> std::ws;
    return midiInputStream.peek() != EOF;
  };

  if (mode == "help") {
    if (hasTrailingArgs()) {
      std::cout << kClockUsage << '\n';
      return;
    }
    printMidiClockHelp();
  } else if (mode == "quick") {
    handleMidiClockQuick(context, midiInputStream);
  } else if (mode == "sources") {
    handleMidiClockSources(context, midiInputStream);
  } else if (mode == "diagnose") {
    std::string rest;
    std::getline(midiInputStream, rest);
    rest = trimLeadingSpaces(rest);
    runMidiClockDiagnose(context, parseClockDiagnoseArgs(rest, toLower));
  } else if (mode == "autoconnect") {
    if (!midiInputRunning()) {
      std::cout << "MIDI input is not running. Start it with: midi on" << '\n';
    } else {
      std::string rest;
      std::getline(midiInputStream, rest);
      rest = trimLeadingSpaces(rest);
      runMidiClockAutoconnect(context, parseClockAutoconnectArgs(rest));
    }
  } else {
    std::cout << kClockUsage << '\n';
  }
}

}  // namespace extracker::midi_cli_internal
