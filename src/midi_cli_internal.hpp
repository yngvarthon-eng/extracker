#pragma once

#include <chrono>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "extracker/midi_cli.hpp"

namespace extracker::midi_cli_internal {

// Default port name filter used when the user does not pass an explicit name.
constexpr const char* kDefaultClockSourceNeedle = "exTracker Virtual Clock";

// Removes leading whitespace from `value` and returns the remainder.
std::string trimLeadingSpaces(std::string value);

// Joins tokens with single spaces.
std::string joinTokens(const std::vector<std::string>& tokens);

// Runs `aconnect -l` and collects every port whose client or port name contains
// `needle` (case-insensitive). Returns false when the listing command failed.
bool readMatchingClockSources(const MidiCommandContext& context,
                              const std::string& needle,
                              std::vector<MidiPortEntry>& matches);

struct ClockDiagnoseArgs {
  bool liveProbe = false;
  std::string needle = kDefaultClockSourceNeedle;
};

struct ClockAutoconnectArgs {
  int selectedIndex = 0;
  bool selectedIndexExplicit = false;
  bool malformedIndexToken = false;
  std::string needle = kDefaultClockSourceNeedle;
};

ClockDiagnoseArgs parseClockDiagnoseArgs(
    const std::string& rest,
    const std::function<std::string(std::string)>& toLower);

ClockAutoconnectArgs parseClockAutoconnectArgs(const std::string& rest);

void runMidiClockDiagnose(const MidiCommandContext& context,
                          const ClockDiagnoseArgs& args);

void runMidiClockAutoconnect(const MidiCommandContext& context,
                             const ClockAutoconnectArgs& args);

void handleMidiMapCommand(std::istringstream& midiInputStream,
                          const MidiCommandContext& context);

void handleMidiQuickCommand(std::istringstream& midiInputStream,
                            const MidiCommandContext& context);

void handleMidiTransportCommand(std::istringstream& midiInputStream,
                                const MidiCommandContext& context);

void handleMidiClockCommand(std::istringstream& midiInputStream,
                            const MidiCommandContext& context);

}  // namespace extracker::midi_cli_internal
