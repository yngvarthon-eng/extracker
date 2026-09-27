#include "midi_cli_internal.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace extracker::midi_cli_internal {

namespace {

constexpr const char* kTransportUsage =
    "Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>";
constexpr const char* kTransportTimeoutUsage =
    "Usage: midi transport timeout <100..10000|status>";
constexpr const char* kTransportLockUsage = "Usage: midi transport lock <on|off|status>";

}  // namespace

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
      std::cout << kTransportUsage << '\n';
      return;
    }
    midiTransportSyncEnabled = true;
    std::cout << "MIDI transport sync enabled" << '\n';
  } else if (mode == "off") {
    if (hasTrailingArgs()) {
      std::cout << kTransportUsage << '\n';
      return;
    }
    midiTransportSyncEnabled = false;
    midiTransportRunning = false;
    std::cout << "MIDI transport sync disabled" << '\n';
  } else if (mode == "toggle") {
    if (hasTrailingArgs()) {
      std::cout << kTransportUsage << '\n';
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
      std::cout << kTransportUsage << '\n';
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
      std::cout << kTransportUsage << '\n';
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
        std::cout << kTransportTimeoutUsage << '\n';
        return;
      }
      std::cout << "MIDI clock timeout ms: " << midiClockTimeout.count() << '\n';
    } else {
      int timeoutMs = -1;
      std::istringstream parse(timeoutArg);
      parse >> timeoutMs;
      if (!parse || !parse.eof() || timeoutMs < 100 || timeoutMs > 10000 || hasTrailingArgs()) {
        std::cout << kTransportTimeoutUsage << '\n';
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
        std::cout << kTransportLockUsage << '\n';
        return;
      }
      midiFallbackLockTempo = true;
      std::cout << "MIDI fallback tempo lock enabled" << '\n';
    } else if (lockMode == "off") {
      if (hasTrailingArgs()) {
        std::cout << kTransportLockUsage << '\n';
        return;
      }
      midiFallbackLockTempo = false;
      std::cout << "MIDI fallback tempo lock disabled" << '\n';
    } else if (lockMode.empty() || lockMode == "status") {
      if (hasTrailingArgs()) {
        std::cout << kTransportLockUsage << '\n';
        return;
      }
      std::cout << "MIDI fallback tempo lock: " << (midiFallbackLockTempo ? "on" : "off") << '\n';
    } else {
      std::cout << kTransportLockUsage << '\n';
    }
  } else if (mode == "reset") {
    if (hasTrailingArgs()) {
      std::cout << kTransportUsage << '\n';
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
    std::cout << kTransportUsage << '\n';
  }
}

}  // namespace extracker::midi_cli_internal
