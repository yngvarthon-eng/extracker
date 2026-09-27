#include "midi_cli_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace extracker::midi_cli_internal {

namespace {

constexpr char kMidiMapUsage[] = "Usage: midi map <channel> <instr|clear>";

// Returns true when the token parses as a complete integer with no trailing junk.
bool parseCompleteInt(const std::string& token, int& value) {
  std::istringstream parse(token);
  parse >> value;
  return static_cast<bool>(parse) && parse.eof();
}

void printMidiChannelMap(const std::array<int, 16>& midiChannelMap) {
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
}

}  // namespace

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
      std::cout << kMidiMapUsage << '\n';
      return;
    }
    printMidiChannelMap(midiChannelMap);
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
    if (!parseCompleteInt(firstArg, channel)) {
      std::cout << kMidiMapUsage << '\n';
    } else if (channel < 0 || channel > 15) {
      std::cout << "MIDI channel must be in range 0..15" << '\n';
    } else {
      std::string secondArg;
      midiInputStream >> secondArg;
      if (secondArg.empty()) {
        std::cout << kMidiMapUsage << '\n';
      } else if (secondArg == "clear") {
        if (hasTrailingArgs()) {
          std::cout << kMidiMapUsage << '\n';
        } else {
          midiChannelMap[static_cast<std::size_t>(channel)] = -1;
          std::cout << "Cleared MIDI mapping for channel " << channel << '\n';
        }
      } else {
        int instrument = -1;
        if (!parseCompleteInt(secondArg, instrument) || hasTrailingArgs()) {
          std::cout << kMidiMapUsage << '\n';
        } else {
          midiChannelMap[static_cast<std::size_t>(channel)] =
              std::clamp(instrument, 0, 255);
          std::cout << "Mapped MIDI channel " << channel << " to instrument "
                    << midiChannelMap[static_cast<std::size_t>(channel)] << '\n';
        }
      }
    }
  }
}

void handleMidiQuickCommand(std::istringstream& midiInputStream,
                            const MidiCommandContext& context) {
  auto& midiInput = context.midiInput;
  auto& midiThruEnabled = context.midiThruEnabled;
  auto& midiInstrument = context.midiInstrument;
  auto& midiLearnEnabled = context.midiLearnEnabled;

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
    bool hasTargetEndpoint =
        context.parseHintEndpoint(midiInput.endpointHint(), targetClient, targetPort);

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
      clockNeedle = kDefaultClockSourceNeedle;
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
      clockNeedle = kDefaultClockSourceNeedle;
    }
    printMidiQuickCompactSummary(clockNeedle);
  } else {
    std::cout << "Usage: midi quick [all|compact [name]]" << '\n';
  }
}

}  // namespace extracker::midi_cli_internal
