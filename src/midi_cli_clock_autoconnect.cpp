#include "midi_cli_internal.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace extracker::midi_cli_internal {

namespace {

constexpr char kAutoconnectUsage[] = "Usage: midi clock autoconnect [name] [index]";

void connectSelectedClockSource(const MidiCommandContext& context,
                                const ClockAutoconnectArgs& args,
                                const std::vector<MidiPortEntry>& matches) {
  int targetClient = -1;
  int targetPort = -1;
  if (!context.parseHintEndpoint(context.midiEndpointHint(), targetClient, targetPort)) {
    std::cout << "Could not parse exTracker MIDI target endpoint." << '\n';
    std::cout << context.midiEndpointHint() << '\n';
    return;
  }

  const auto& source = matches[static_cast<std::size_t>(args.selectedIndex)];
  std::ostringstream command;
  command << "aconnect " << source.client << ":" << source.port
          << " " << targetClient << ":" << targetPort;
  int status = context.executeSystemCommand(command.str());
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

}  // namespace

void runMidiClockAutoconnect(const MidiCommandContext& context,
                             const ClockAutoconnectArgs& args) {
  if (args.malformedIndexToken) {
    std::cout << kAutoconnectUsage << '\n';
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
  } else if (args.selectedIndex < 0 ||
             static_cast<std::size_t>(args.selectedIndex) >= matches.size()) {
    std::cout << "Selected index " << args.selectedIndex << " is out of range for "
              << matches.size() << " match(es)." << '\n';
    std::cout << "Use: midi clock sources " << args.needle << '\n';
  } else {
    connectSelectedClockSource(context, args, matches);
  }
}

}  // namespace extracker::midi_cli_internal
