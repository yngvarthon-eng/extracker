#include "extracker/midi_cli.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace extracker {
namespace {

std::string trimLeadingSpaces(std::string value) {
  if (!value.empty()) {
    const auto first = std::find_if(value.begin(), value.end(), [](unsigned char ch) {
      return !std::isspace(ch);
    });
    value.erase(value.begin(), first);
  }
  return value;
}

bool readMatchingClockSources(const MidiCommandContext& context,
                              const std::string& needle,
                              std::vector<MidiPortEntry>& matches) {
  std::string listing;
  if (!context.readCommandOutput("aconnect -l", listing)) {
    return false;
  }

  auto ports = context.parseAconnectPorts(listing);
  std::string needleLower = context.toLower(needle);
  matches.clear();
  for (const auto& port : ports) {
    std::string clientLower = context.toLower(port.clientName);
    std::string portLower = context.toLower(port.portName);
    if (clientLower.find(needleLower) != std::string::npos ||
        portLower.find(needleLower) != std::string::npos) {
      matches.push_back(port);
    }
  }
  return true;
}

std::string joinTokens(const std::vector<std::string>& tokens) {
  std::ostringstream joined;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i > 0) {
      joined << " ";
    }
    joined << tokens[i];
  }
  return joined.str();
}

struct ClockDiagnoseArgs {
  bool liveProbe = false;
  std::string needle = "exTracker Virtual Clock";
};

ClockDiagnoseArgs parseClockDiagnoseArgs(
    const std::string& rest,
    const std::function<std::string(std::string)>& toLower) {
  ClockDiagnoseArgs args;
  if (rest.empty()) {
    return args;
  }

  std::vector<std::string> tokens;
  std::istringstream tokenStream(rest);
  std::string token;
  while (tokenStream >> token) {
    tokens.push_back(token);
  }

  if (!tokens.empty() && toLower(tokens.front()) == "live") {
    args.liveProbe = true;
    tokens.erase(tokens.begin());
  }

  if (!tokens.empty()) {
    args.needle = joinTokens(tokens);
  }

  return args;
}

struct ClockAutoconnectArgs {
  int selectedIndex = 0;
  bool selectedIndexExplicit = false;
  bool malformedIndexToken = false;
  std::string needle = "exTracker Virtual Clock";
};

ClockAutoconnectArgs parseClockAutoconnectArgs(const std::string& rest) {
  ClockAutoconnectArgs args;
  if (rest.empty()) {
    return args;
  }

  std::vector<std::string> tokens;
  std::istringstream tokenStream(rest);
  std::string token;
  while (tokenStream >> token) {
    tokens.push_back(token);
  }

  if (!tokens.empty()) {
    int parsedIndex = -1;
    std::istringstream indexStream(tokens.back());
    if (indexStream >> parsedIndex && indexStream.eof() && parsedIndex >= 0) {
      args.selectedIndex = parsedIndex;
      args.selectedIndexExplicit = true;
      tokens.pop_back();
    } else {
      // Treat token as malformed index only when it starts like an integer
      // but contains trailing junk (e.g. "1foo"). Names like "clock2" remain valid.
      std::istringstream partialIndexStream(tokens.back());
      if (partialIndexStream >> parsedIndex) {
        args.malformedIndexToken = true;
      }
    }
  }

  if (!tokens.empty()) {
    args.needle = joinTokens(tokens);
  }

  return args;
}

void runMidiClockLiveProbe(const MidiCommandContext& context) {
  bool beforeHasClock = false;
  std::chrono::steady_clock::time_point beforeClockTimestamp{};
  {
    std::lock_guard<std::mutex> lock(context.stateMutex);
    beforeHasClock = context.hasMidiClockTimestamp;
    beforeClockTimestamp = context.lastMidiClockTimestamp;
  }

  std::this_thread::sleep_for(std::chrono::seconds(1));

  bool afterHasClock = false;
  std::chrono::steady_clock::time_point afterClockTimestamp{};
  bool freshClock = false;
  long long clockAgeMs = -1;
  {
    std::lock_guard<std::mutex> lock(context.stateMutex);
    auto now = std::chrono::steady_clock::now();
    afterHasClock = context.hasMidiClockTimestamp;
    afterClockTimestamp = context.lastMidiClockTimestamp;
    if (afterHasClock) {
      clockAgeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - context.lastMidiClockTimestamp).count();
      freshClock = (now - context.lastMidiClockTimestamp) <= context.midiClockTimeout;
    }
  }

  bool tickAdvanced = false;
  if (afterHasClock) {
    tickAdvanced = !beforeHasClock || (afterClockTimestamp > beforeClockTimestamp);
  }

  std::cout << "  Live probe result:" << '\n';
  std::cout << "    Clock tick advanced: " << (tickAdvanced ? "yes" : "no") << '\n';
  std::cout << "    Clock freshness: " << (freshClock ? "fresh" : "stale") << '\n';
  if (clockAgeMs >= 0) {
    std::cout << "    Last clock age ms: " << clockAgeMs << '\n';
  } else {
    std::cout << "    Last clock age ms: n/a" << '\n';
  }
}

void handleMidiMapCommand(std::istringstream& midiInputStream,
                          const MidiCommandContext& context) {
  auto& midiChannelMap = context.midiChannelMap;

  auto hasTrailingArgs = [&]() {
    midiInputStream >> std::ws;
    return midiInputStream.peek() != EOF;
  };

  std::string firstArg;
  midiInputStream >> firstArg;
  if (firstArg.empty() || firstArg == "status") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi map <channel> <instr|clear>" << '\n';
      return;
    }
    std::cout << "MIDI channel map:";
    bool hasMapping = false;
    for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
      if (midiChannelMap[ch] >= 0) {
        hasMapping = true;
        std::cout << " ch" << ch << "->" << midiChannelMap[ch];
      }
    }
    if (!hasMapping) {
      std::cout << " (empty)";
    }
    std::cout << '\n';
  } else if (firstArg == "clear") {
    std::string clearScope;
    midiInputStream >> clearScope;
    if (clearScope != "all" || hasTrailingArgs()) {
      std::cout << "Usage: midi map clear all" << '\n';
    } else {
      midiChannelMap.fill(-1);
      std::cout << "Cleared all MIDI channel mappings" << '\n';
    }
  } else {
    int channel = -1;
    std::istringstream channelParse(firstArg);
    channelParse >> channel;
    if (!channelParse || !channelParse.eof()) {
      std::cout << "Usage: midi map <channel> <instr|clear>" << '\n';
    } else if (channel < 0 || channel > 15) {
      std::cout << "MIDI channel must be in range 0..15" << '\n';
    } else {
      std::string secondArg;
      midiInputStream >> secondArg;
      if (secondArg.empty()) {
        std::cout << "Usage: midi map <channel> <instr|clear>" << '\n';
      } else if (secondArg == "clear") {
        if (hasTrailingArgs()) {
          std::cout << "Usage: midi map <channel> <instr|clear>" << '\n';
        } else {
          midiChannelMap[static_cast<std::size_t>(channel)] = -1;
          std::cout << "Cleared MIDI mapping for channel " << channel << '\n';
        }
      } else {
        int instrument = -1;
        std::istringstream instrumentParse(secondArg);
        instrumentParse >> instrument;
        if (!instrumentParse || !instrumentParse.eof() || hasTrailingArgs()) {
          std::cout << "Usage: midi map <channel> <instr|clear>" << '\n';
        } else {
          midiChannelMap[static_cast<std::size_t>(channel)] = std::clamp(instrument, 0, 255);
          std::cout << "Mapped MIDI channel " << channel << " to instrument "
                    << midiChannelMap[static_cast<std::size_t>(channel)] << '\n';
        }
      }
    }
  }
}

void handleMidiTransportCommand(std::istringstream& midiInputStream,
                                const MidiCommandContext& context) {
  auto& midiTransportSyncEnabled = context.midiTransportSyncEnabled;
  auto& midiTransportRunning = context.midiTransportRunning;
  auto& stateMutex = context.stateMutex;
  const auto& midiClockAlive = context.midiClockAlive;
  auto& midiClockTimeout = context.midiClockTimeout;
  auto& midiFallbackLockTempo = context.midiFallbackLockTempo;
  const auto& transportSource = context.transportSource;
  auto& hasMidiClockTimestamp = context.hasMidiClockTimestamp;
  auto& midiClockEstimatedBpm = context.midiClockEstimatedBpm;

  std::string mode;
  midiInputStream >> mode;

  auto hasTrailingArgs = [&]() {
    midiInputStream >> std::ws;
    return midiInputStream.peek() != EOF;
  };

  if (mode == "on") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>" << '\n';
      return;
    }
    midiTransportSyncEnabled = true;
    std::cout << "MIDI transport sync enabled" << '\n';
  } else if (mode == "off") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>" << '\n';
      return;
    }
    midiTransportSyncEnabled = false;
    midiTransportRunning = false;
    std::cout << "MIDI transport sync disabled" << '\n';
  } else if (mode == "toggle") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>" << '\n';
      return;
    }
    midiTransportSyncEnabled = !midiTransportSyncEnabled;
    if (!midiTransportSyncEnabled) {
      midiTransportRunning = false;
      std::cout << "MIDI transport sync disabled" << '\n';
    } else {
      std::cout << "MIDI transport sync enabled" << '\n';
    }
  } else if (mode == "status") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>" << '\n';
      return;
    }
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      midiClockAlive();
    }
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
  } else if (mode == "quick") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>" << '\n';
      return;
    }
    bool hasClockSnapshot = false;
    bool clockFresh = false;
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      hasClockSnapshot = hasMidiClockTimestamp;
      if (hasClockSnapshot) {
        auto now = std::chrono::steady_clock::now();
        clockFresh = (now - context.lastMidiClockTimestamp) <= midiClockTimeout;
      }
    }

    std::cout << "MIDI transport quick:" << '\n';
    std::cout << "  sync: " << (midiTransportSyncEnabled ? "on" : "off") << '\n';
    std::cout << "  running: " << (midiTransportRunning ? "yes" : "no") << '\n';
    std::cout << "  source: " << transportSource() << '\n';
    if (!hasClockSnapshot) {
      std::cout << "  clock: none" << '\n';
    } else {
      std::cout << "  clock: " << (clockFresh ? "fresh" : "stale") << '\n';
    }
    std::cout << "  timeout ms: " << midiClockTimeout.count() << '\n';
    std::cout << "  fallback lock: " << (midiFallbackLockTempo ? "on" : "off") << '\n';
  } else if (mode == "timeout") {
    std::string timeoutArg;
    midiInputStream >> timeoutArg;
    if (timeoutArg.empty() || timeoutArg == "status") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi transport timeout <100..10000|status>" << '\n';
        return;
      }
      std::cout << "MIDI clock timeout ms: " << midiClockTimeout.count() << '\n';
    } else {
      int timeoutMs = -1;
      std::istringstream parse(timeoutArg);
      parse >> timeoutMs;
      if (!parse || !parse.eof() || timeoutMs < 100 || timeoutMs > 10000 || hasTrailingArgs()) {
        std::cout << "Usage: midi transport timeout <100..10000|status>" << '\n';
      } else {
        {
          std::lock_guard<std::mutex> lock(stateMutex);
          midiClockTimeout = std::chrono::milliseconds(timeoutMs);
        }
        std::cout << "MIDI clock timeout set to " << midiClockTimeout.count() << " ms" << '\n';
      }
    }
  } else if (mode == "lock") {
    std::string lockMode;
    midiInputStream >> lockMode;
    if (lockMode == "on") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi transport lock <on|off|status>" << '\n';
        return;
      }
      midiFallbackLockTempo = true;
      std::cout << "MIDI fallback tempo lock enabled" << '\n';
    } else if (lockMode == "off") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi transport lock <on|off|status>" << '\n';
        return;
      }
      midiFallbackLockTempo = false;
      std::cout << "MIDI fallback tempo lock disabled" << '\n';
    } else if (lockMode.empty() || lockMode == "status") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi transport lock <on|off|status>" << '\n';
        return;
      }
      std::cout << "MIDI fallback tempo lock: " << (midiFallbackLockTempo ? "on" : "off") << '\n';
    } else {
      std::cout << "Usage: midi transport lock <on|off|status>" << '\n';
    }
  } else if (mode == "reset") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>" << '\n';
      return;
    }
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      midiTransportRunning = false;
      hasMidiClockTimestamp = false;
      midiClockEstimatedBpm = 0.0;
    }
    std::cout << "MIDI transport state reset" << '\n';
  } else {
    std::cout << "Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>" << '\n';
  }
}

void handleMidiClockCommand(std::istringstream& midiInputStream,
                            const MidiCommandContext& context) {
  const auto& toLower = context.toLower;
  const auto& parseHintEndpoint = context.parseHintEndpoint;
  const auto& midiInputRunning = context.midiInputRunning;
  const auto& midiLastError = context.midiLastError;
  const auto& midiEndpointHint = context.midiEndpointHint;
  const auto& executeSystemCommand = context.executeSystemCommand;

  std::string mode;
  midiInputStream >> mode;

  auto hasTrailingArgs = [&]() {
    midiInputStream >> std::ws;
    return midiInputStream.peek() != EOF;
  };

  if (mode == "help") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi clock <help|quick [name]|sources [name]|autoconnect [name] [index]|diagnose [name]|diagnose live [name]>" << '\n';
      return;
    }
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
  } else if (mode == "quick") {
    std::string needle;
    std::getline(midiInputStream, needle);
    needle = trimLeadingSpaces(needle);
    if (needle.empty()) {
      needle = "exTracker Virtual Clock";
    }

    int targetClient = -1;
    int targetPort = -1;
    bool hasTargetEndpoint = parseHintEndpoint(midiEndpointHint(), targetClient, targetPort);

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
    std::cout << "  running: " << (midiInputRunning() ? "yes" : "no") << '\n';
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
  } else if (mode == "sources") {
    std::string needle;
    std::getline(midiInputStream, needle);
    needle = trimLeadingSpaces(needle);
    if (needle.empty()) {
      needle = "exTracker Virtual Clock";
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
  } else if (mode == "diagnose") {
    std::string rest;
    std::getline(midiInputStream, rest);
    rest = trimLeadingSpaces(rest);
    ClockDiagnoseArgs args = parseClockDiagnoseArgs(rest, toLower);

    std::cout << "MIDI clock diagnostics:" << '\n';
    if (args.liveProbe) {
      std::cout << "  Mode: live health probe (1 second)" << '\n';
    }
    std::cout << "  MIDI input running: " << (midiInputRunning() ? "yes" : "no") << '\n';
    if (!midiLastError().empty()) {
      std::cout << "  MIDI last error: " << midiLastError() << '\n';
    }

    int targetClient = -1;
    int targetPort = -1;
    bool hasTargetEndpoint = parseHintEndpoint(midiEndpointHint(), targetClient, targetPort);
    if (hasTargetEndpoint) {
      std::cout << "  exTracker endpoint: " << targetClient << ":" << targetPort << '\n';
    } else {
      std::cout << "  exTracker endpoint: unavailable (start with: midi on)" << '\n';
    }

    bool hasClockSnapshot = false;
    bool clockFresh = false;
    long long clockAgeMs = -1;
    double clockBpmSnapshot = 0.0;
    {
      std::lock_guard<std::mutex> lock(context.stateMutex);
      hasClockSnapshot = context.hasMidiClockTimestamp;
      if (hasClockSnapshot) {
        auto now = std::chrono::steady_clock::now();
        clockAgeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - context.lastMidiClockTimestamp).count();
        clockFresh = (now - context.lastMidiClockTimestamp) <= context.midiClockTimeout;
      }
      clockBpmSnapshot = context.midiClockEstimatedBpm;
    }

    if (!hasClockSnapshot) {
      std::cout << "  Clock state: none" << '\n';
    } else if (clockFresh) {
      std::cout << "  Clock state: fresh" << '\n';
    } else {
      std::cout << "  Clock state: stale" << '\n';
    }
    if (clockAgeMs >= 0) {
      std::cout << "  Last clock age ms: " << clockAgeMs << '\n';
    }
    if (clockBpmSnapshot > 0.0) {
      std::cout << "  Clock BPM snapshot: " << clockBpmSnapshot << '\n';
    }

    std::vector<MidiPortEntry> matches;
    if (!readMatchingClockSources(context, args.needle, matches)) {
      std::cout << "  ALSA port listing: failed (is aconnect installed?)" << '\n';
    } else {
      std::cout << "  Source filter: '" << args.needle << "'" << '\n';
      std::cout << "  Matching sources: " << matches.size() << '\n';
      for (std::size_t i = 0; i < matches.size() && i < 4; ++i) {
        const auto& source = matches[i];
        std::cout << "    [" << i << "] " << source.client << ":" << source.port
                  << " '" << source.clientName << " / " << source.portName << "'" << '\n';
      }

      if (matches.empty()) {
        std::cout << "  Suggestion: start source script first:" << '\n';
        std::cout << "    python3 tools/virtual_midi_clock.py --bpm 125" << '\n';
      } else if (matches.size() > 1) {
        std::cout << "  Multiple matching sources found; choose an explicit index:" << '\n';
        std::cout << "    midi clock autoconnect " << args.needle << " <index>" << '\n';
      } else if (!hasTargetEndpoint) {
        std::cout << "  Suggestion: run 'midi on' and then 'midi clock autoconnect'" << '\n';
      } else {
        std::cout << "  Suggestion: run 'midi clock autoconnect " << args.needle << " 0'" << '\n';
      }

      if (hasClockSnapshot && !clockFresh) {
        std::cout << "  Suggestion: run 'midi transport reset' if clock source changed." << '\n';
      }

      if (args.liveProbe) {
        runMidiClockLiveProbe(context);
      }
    }
  } else if (mode == "autoconnect") {
    if (!midiInputRunning()) {
      std::cout << "MIDI input is not running. Start it with: midi on" << '\n';
    } else {
      std::string rest;
      std::getline(midiInputStream, rest);
      rest = trimLeadingSpaces(rest);
      ClockAutoconnectArgs args = parseClockAutoconnectArgs(rest);

      if (args.malformedIndexToken) {
        std::cout << "Usage: midi clock autoconnect [name] [index]" << '\n';
        return;
      }

      std::vector<MidiPortEntry> matches;
      if (!readMatchingClockSources(context, args.needle, matches)) {
        std::cout << "Failed to run aconnect -l. Ensure ALSA tools are installed." << '\n';
      } else if (matches.empty()) {
        std::cout << "No MIDI source matching '" << args.needle << "' found." << '\n';
        std::cout << "Tip: run script first: python3 tools/virtual_midi_clock.py --bpm 125" << '\n';
      } else if (matches.size() > 1 && !args.selectedIndexExplicit) {
        std::cout << "Multiple MIDI sources matched '" << args.needle << "'." << '\n';
        std::cout << "Specify an explicit index to avoid accidental connection:" << '\n';
        std::cout << "  midi clock autoconnect " << args.needle << " <index>" << '\n';
        std::cout << "Use: midi clock sources " << args.needle << '\n';
      } else if (args.selectedIndex < 0 || static_cast<std::size_t>(args.selectedIndex) >= matches.size()) {
        std::cout << "Selected index " << args.selectedIndex << " is out of range for "
                  << matches.size() << " match(es)." << '\n';
        std::cout << "Use: midi clock sources " << args.needle << '\n';
      } else {
        int targetClient = -1;
        int targetPort = -1;
        if (!parseHintEndpoint(midiEndpointHint(), targetClient, targetPort)) {
          std::cout << "Could not parse exTracker MIDI target endpoint." << '\n';
          std::cout << midiEndpointHint() << '\n';
        } else {
          const auto& source = matches[static_cast<std::size_t>(args.selectedIndex)];
          std::ostringstream command;
          command << "aconnect " << source.client << ":" << source.port
                  << " " << targetClient << ":" << targetPort;
          int status = executeSystemCommand(command.str());
          if (status == 0) {
            std::cout << "Connected MIDI clock source '" << source.clientName << " / "
                      << source.portName << "' -> "
                      << targetClient << ":" << targetPort << '\n';
            if (matches.size() > 1) {
              std::cout << "Matched " << matches.size() << " source(s); selected index "
                        << args.selectedIndex << "." << '\n';
            }
          } else {
            std::cout << "Failed to connect with command: " << command.str() << '\n';
          }
        }
      }
    }
  } else {
    std::cout << "Usage: midi clock <help|quick [name]|sources [name]|autoconnect [name] [index]|diagnose [name]|diagnose live [name]>" << '\n';
  }
}

}  // namespace

void handleMidiEventLocked(const MidiEvent& event,
                           MidiEventContext context) {
  if (event.type == MidiEvent::Type::Start) {
    if (context.midiTransportSyncEnabled) {
      context.transport.stop();
      context.transport.resetTickCount();
      context.sequencer.reset();
      context.audio.allNotesOff();
      context.midiTransportRunning = true;
    }
    return;
  }

  if (event.type == MidiEvent::Type::Continue) {
    if (context.midiTransportSyncEnabled) {
      context.transport.stop();
      context.midiTransportRunning = true;
    }
    return;
  }

  if (event.type == MidiEvent::Type::Stop) {
    if (context.midiTransportSyncEnabled) {
      context.midiTransportRunning = false;
      context.audio.allNotesOff();
    }
    return;
  }

  if (event.type == MidiEvent::Type::Clock) {
    auto now = std::chrono::steady_clock::now();
    if (context.hasMidiClockTimestamp) {
      double seconds = std::chrono::duration<double>(now - context.lastMidiClockTimestamp).count();
      if (seconds > 0.0) {
        // MIDI clock runs at 24 pulses per quarter note.
        double instantBpm = 60.0 / (seconds * 24.0);
        if (instantBpm > 20.0 && instantBpm < 400.0) {
          if (context.midiClockEstimatedBpm <= 0.0) {
            context.midiClockEstimatedBpm = instantBpm;
          } else {
            context.midiClockEstimatedBpm =
                (context.midiClockEstimatedBpm * 0.85) + (instantBpm * 0.15);
          }
        }
      }
    }
    context.lastMidiClockTimestamp = now;
    context.hasMidiClockTimestamp = true;

    if (context.midiTransportSyncEnabled) {
      if (!context.midiTransportRunning) {
        // Some sources emit clock without START; treat first clock as external follow trigger.
        context.midiTransportRunning = true;
        if (context.transport.isPlaying()) {
          context.transport.stop();
        }
      }
      context.transport.advanceExternalTick();
    }
    return;
  }

  int targetInstrument = context.midiInstrument;
  if (event.channel < context.midiChannelMap.size() && context.midiChannelMap[event.channel] >= 0) {
    targetInstrument = context.midiChannelMap[event.channel];
  } else if (context.midiLearnEnabled && event.type == MidiEvent::Type::NoteOn) {
    targetInstrument = static_cast<int>(event.channel) % static_cast<int>(PluginHost::kMaxInstrumentSlots);
    if (event.channel < context.midiChannelMap.size()) {
      context.midiChannelMap[event.channel] = targetInstrument;
    }

    if (!context.plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(targetInstrument))) {
      context.plugins.loadPlugin("builtin.sine");
      context.plugins.assignInstrument(static_cast<std::uint8_t>(targetInstrument), "builtin.sine");
    }
    std::cout << "[midi learn] channel " << static_cast<int>(event.channel)
              << " mapped to instrument " << targetInstrument << '\n';
  }

  if (event.type == MidiEvent::Type::NoteOn) {
    if (context.midiThruEnabled) {
      if (!context.plugins.triggerNoteOn(static_cast<std::uint8_t>(targetInstrument), event.note,
                                         event.velocity, true)) {
        context.audio.noteOn(
            event.note,
            context.midiNoteToFrequencyHz(event.note),
            static_cast<double>(event.velocity) / 127.0,
            true,
            static_cast<std::uint8_t>(targetInstrument));
      }
    }

    if (context.recordEnabled) {
      int targetRow = context.chooseRecordRow(context.recordChannel);
      if (targetRow >= 0) {
        context.applyRecordWrite(
            targetRow,
            context.recordChannel,
            event.note,
            static_cast<std::uint8_t>(std::clamp(targetInstrument, 0, 255)),
            0,
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(event.velocity), 1, 127)),
            false,
            0,
            0);
      }
    }
  } else if (event.type == MidiEvent::Type::NoteOff) {
    if (context.midiThruEnabled) {
      if (!context.plugins.triggerNoteOff(static_cast<std::uint8_t>(targetInstrument), event.note)) {
        context.audio.noteOff(event.note, static_cast<std::uint8_t>(targetInstrument));
      }
    }
  }
}

void handleMidiCommand(std::istringstream& midiInputStream,
                       const MidiCommandContext& context) {
  auto& midiInput = context.midiInput;
  const auto& onMidiEvent = context.onMidiEvent;
  auto& midiThruEnabled = context.midiThruEnabled;
  auto& midiInstrument = context.midiInstrument;
  auto& midiLearnEnabled = context.midiLearnEnabled;
  auto& midiChannelMap = context.midiChannelMap;

  auto hasTrailingArgs = [&]() {
    midiInputStream >> std::ws;
    return midiInputStream.peek() != EOF;
  };

  std::string subcommand;
  midiInputStream >> subcommand;
  if (subcommand == "on") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi <on|off|status|quick|thru|instrument|learn|map|transport|clock> ..." << '\n';
      return;
    }
    if (midiInput.isRunning()) {
      std::cout << "MIDI input already running" << '\n';
    } else if (midiInput.start(onMidiEvent)) {
      std::cout << "MIDI input started (" << midiInput.backendName() << ")" << '\n';
      std::cout << midiInput.endpointHint() << '\n';
    } else {
      std::cout << "Failed to start MIDI input: " << midiInput.lastError() << '\n';
    }
  } else if (subcommand == "off") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi <on|off|status|quick|thru|instrument|learn|map|transport|clock> ..." << '\n';
      return;
    }
    midiInput.stop();
    std::cout << "MIDI input stopped" << '\n';
  } else if (subcommand == "status") {
    if (hasTrailingArgs()) {
      std::cout << "Usage: midi <on|off|status|quick|thru|instrument|learn|map|transport|clock> ..." << '\n';
      return;
    }
    std::cout << "MIDI backend: " << midiInput.backendName() << '\n';
    std::cout << "MIDI running: " << (midiInput.isRunning() ? "yes" : "no") << '\n';
    if (!midiInput.lastError().empty()) {
      std::cout << "MIDI last error: " << midiInput.lastError() << '\n';
    }
    std::cout << midiInput.endpointHint() << '\n';
  } else if (subcommand == "quick") {
    auto computeClockState = [&]() {
      bool hasClockSnapshot = false;
      bool clockFresh = false;
      {
        std::lock_guard<std::mutex> lock(context.stateMutex);
        hasClockSnapshot = context.hasMidiClockTimestamp;
        if (hasClockSnapshot) {
          auto now = std::chrono::steady_clock::now();
          clockFresh = (now - context.lastMidiClockTimestamp) <= context.midiClockTimeout;
        }
      }
      return std::make_pair(hasClockSnapshot, clockFresh);
    };

    auto printMidiQuickSummary = [&]() {
      const auto [hasClockSnapshot, clockFresh] = computeClockState();
      std::cout << "MIDI quick:" << '\n';
      std::cout << "  running: " << (midiInput.isRunning() ? "yes" : "no") << '\n';
      std::cout << "  thru: " << (midiThruEnabled ? "on" : "off") << '\n';
      std::cout << "  instrument: " << midiInstrument << '\n';
      std::cout << "  learn: " << (midiLearnEnabled ? "on" : "off") << '\n';
      std::cout << "  transport sync: " << (context.midiTransportSyncEnabled ? "on" : "off") << '\n';
      std::cout << "  transport running: " << (context.midiTransportRunning ? "yes" : "no") << '\n';
      if (!hasClockSnapshot) {
        std::cout << "  clock: none" << '\n';
      } else {
        std::cout << "  clock: " << (clockFresh ? "fresh" : "stale") << '\n';
      }
      std::cout << "  endpoint: " << midiInput.endpointHint() << '\n';
    };

    auto printMidiQuickCompactSummary = [&](const std::string& needle) {
      const auto [hasClockSnapshot, clockFresh] = computeClockState();
      std::vector<MidiPortEntry> matches;
      bool listedSources = readMatchingClockSources(context, needle, matches);

      int targetClient = -1;
      int targetPort = -1;
      bool hasTargetEndpoint = context.parseHintEndpoint(midiInput.endpointHint(), targetClient, targetPort);

      std::cout << "MIDI quick compact:" << '\n';
      std::cout << "  midi: running=" << (midiInput.isRunning() ? "yes" : "no")
                << " thru=" << (midiThruEnabled ? "on" : "off")
                << " instrument=" << midiInstrument
                << " learn=" << (midiLearnEnabled ? "on" : "off") << '\n';
      std::cout << "  transport: sync=" << (context.midiTransportSyncEnabled ? "on" : "off")
                << " running=" << (context.midiTransportRunning ? "yes" : "no")
                << " source=" << context.transportSource()
                << " clock=" << (!hasClockSnapshot ? "none" : (clockFresh ? "fresh" : "stale"))
                << " timeout_ms=" << context.midiClockTimeout.count()
                << " fallback_lock=" << (context.midiFallbackLockTempo ? "on" : "off") << '\n';
      std::cout << "  clock: endpoint=";
      if (hasTargetEndpoint) {
        std::cout << targetClient << ":" << targetPort;
      } else {
        std::cout << "unavailable";
      }
      std::cout << " filter='" << needle << "' matches=";
      if (!listedSources) {
        std::cout << "n/a";
      } else {
        std::cout << matches.size();
      }
      std::cout << '\n';
    };

    std::string quickMode;
    midiInputStream >> quickMode;
    if (quickMode.empty()) {
      printMidiQuickSummary();
    } else if (quickMode == "all") {
      std::string clockNeedle;
      std::getline(midiInputStream, clockNeedle);
      clockNeedle = trimLeadingSpaces(clockNeedle);
      if (clockNeedle.empty()) {
        clockNeedle = "exTracker Virtual Clock";
      }

      std::cout << "MIDI quick all:" << '\n';
      printMidiQuickSummary();
      std::istringstream transportQuickInput("quick");
      handleMidiTransportCommand(transportQuickInput, context);

      std::istringstream clockQuickInput("quick " + clockNeedle);
      handleMidiClockCommand(clockQuickInput, context);
    } else if (quickMode == "compact") {
      std::string clockNeedle;
      std::getline(midiInputStream, clockNeedle);
      clockNeedle = trimLeadingSpaces(clockNeedle);
      if (clockNeedle.empty()) {
        clockNeedle = "exTracker Virtual Clock";
      }
      printMidiQuickCompactSummary(clockNeedle);
    } else {
      std::cout << "Usage: midi quick [all|compact [name]]" << '\n';
    }
  } else if (subcommand == "thru") {
    std::string mode;
    midiInputStream >> mode;
    if (mode == "on") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi thru <on|off>" << '\n';
        return;
      }
      midiThruEnabled = true;
      std::cout << "MIDI thru enabled" << '\n';
    } else if (mode == "off") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi thru <on|off>" << '\n';
        return;
      }
      midiThruEnabled = false;
      std::cout << "MIDI thru disabled" << '\n';
    } else {
      std::cout << "Usage: midi thru <on|off>" << '\n';
    }
  } else if (subcommand == "instrument") {
    int instrument = -1;
    std::string instrumentArg;
    midiInputStream >> instrumentArg;
    if (instrumentArg.empty()) {
      std::cout << "Usage: midi instrument <index>" << '\n';
      return;
    }

    std::istringstream instrumentParse(instrumentArg);
    instrumentParse >> instrument;
    if (!instrumentParse || !instrumentParse.eof() || hasTrailingArgs()) {
      std::cout << "Usage: midi instrument <index>" << '\n';
    } else {
      midiInstrument = std::clamp(instrument, 0, 255);
      std::cout << "MIDI instrument set to " << midiInstrument << '\n';
    }
  } else if (subcommand == "learn") {
    std::string mode;
    midiInputStream >> mode;
    if (mode == "on") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi learn <on|off|status>" << '\n';
        return;
      }
      midiLearnEnabled = true;
      std::cout << "MIDI learn enabled" << '\n';
    } else if (mode == "off") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi learn <on|off|status>" << '\n';
        return;
      }
      midiLearnEnabled = false;
      std::cout << "MIDI learn disabled" << '\n';
    } else if (mode == "status") {
      if (hasTrailingArgs()) {
        std::cout << "Usage: midi learn <on|off|status>" << '\n';
        return;
      }
      std::cout << "MIDI learn: " << (midiLearnEnabled ? "on" : "off") << '\n';
      std::cout << "Channel map:" << '\n';
      for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
        if (midiChannelMap[ch] >= 0) {
          std::cout << "  ch " << ch << " -> instr " << midiChannelMap[ch] << '\n';
        }
      }
    } else {
      std::cout << "Usage: midi learn <on|off|status>" << '\n';
    }
  } else if (subcommand == "map") {
    handleMidiMapCommand(midiInputStream, context);
  } else if (subcommand == "transport") {
    handleMidiTransportCommand(midiInputStream, context);
  } else if (subcommand == "clock") {
    handleMidiClockCommand(midiInputStream, context);
  } else {
    std::cout << "Usage: midi <on|off|status|quick|thru|instrument|learn|map|transport|clock> ..." << '\n';
  }
}

}  // namespace extracker
