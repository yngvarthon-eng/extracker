#include "midi_cli_internal.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace extracker::midi_cli_internal {

namespace {

constexpr int kMaxDiagnoseSourceListings = 4;
constexpr int kLiveProbeSleepSeconds = 1;

void runMidiClockLiveProbe(const MidiCommandContext& context) {
  bool beforeHasClock = false;
  std::chrono::steady_clock::time_point beforeClockTimestamp{};
  {
    std::lock_guard<std::mutex> lock(context.stateMutex);
    beforeHasClock = context.hasMidiClockTimestamp;
    beforeClockTimestamp = context.lastMidiClockTimestamp;
  }

  std::this_thread::sleep_for(std::chrono::seconds(kLiveProbeSleepSeconds));

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

void printClockSourceMatches(const std::vector<MidiPortEntry>& matches) {
  for (std::size_t i = 0; i < matches.size() && i < static_cast<std::size_t>(kMaxDiagnoseSourceListings); ++i) {
    const auto& source = matches[i];
    std::cout << "    [" << i << "] " << source.client << ":" << source.port
              << " '" << source.clientName << " / " << source.portName << "'" << '\n';
  }
}

void printClockDiagnoseSuggestion(const ClockDiagnoseArgs& args,
                                  const std::vector<MidiPortEntry>& matches,
                                  bool hasTargetEndpoint,
                                  bool hasClockSnapshot,
                                  bool clockFresh) {
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
}

void printClockDiagnoseHeader(const MidiCommandContext& context,
                              const ClockDiagnoseArgs& args) {
  std::cout << "MIDI clock diagnostics:" << '\n';
  if (args.liveProbe) {
    std::cout << "  Mode: live health probe (1 second)" << '\n';
  }
  std::cout << "  MIDI input running: " << (context.midiInputRunning() ? "yes" : "no") << '\n';
  if (!context.midiLastError().empty()) {
    std::cout << "  MIDI last error: " << context.midiLastError() << '\n';
  }
}

void printClockDiagnoseEndpoint(const MidiCommandContext& context) {
  int targetClient = -1;
  int targetPort = -1;
  bool hasTargetEndpoint = context.parseHintEndpoint(context.midiEndpointHint(), targetClient, targetPort);
  if (hasTargetEndpoint) {
    std::cout << "  exTracker endpoint: " << targetClient << ":" << targetPort << '\n';
  } else {
    std::cout << "  exTracker endpoint: unavailable (start with: midi on)" << '\n';
  }
}

void printClockDiagnoseState(const MidiCommandContext& context) {
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
}

}  // namespace

void runMidiClockDiagnose(const MidiCommandContext& context,
                          const ClockDiagnoseArgs& args) {
  printClockDiagnoseHeader(context, args);
  printClockDiagnoseEndpoint(context);
  printClockDiagnoseState(context);

  int targetClient = -1;
  int targetPort = -1;
  const bool hasTargetEndpoint =
      context.parseHintEndpoint(context.midiEndpointHint(), targetClient, targetPort);

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

  std::vector<MidiPortEntry> matches;
  if (!readMatchingClockSources(context, args.needle, matches)) {
    std::cout << "  ALSA port listing: failed (is aconnect installed?)" << '\n';
  } else {
    std::cout << "  Source filter: '" << args.needle << "'" << '\n';
    std::cout << "  Matching sources: " << matches.size() << '\n';
    printClockSourceMatches(matches);
    printClockDiagnoseSuggestion(args, matches, hasTargetEndpoint, hasClockSnapshot, clockFresh);
  }

  if (args.liveProbe) {
    runMidiClockLiveProbe(context);
  }
}

}  // namespace extracker::midi_cli_internal
