#include "midi_cli_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace extracker::midi_cli_internal {

std::string trimLeadingSpaces(std::string value) {
  if (!value.empty()) {
    const auto first = std::find_if(value.begin(), value.end(), [](unsigned char ch) {
      return !std::isspace(ch);
    });
    value.erase(value.begin(), first);
  }
  return value;
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
    if (indexStream >> parsedIndex && indexStream.eof()) {
      args.selectedIndex = parsedIndex;
      args.selectedIndexExplicit = true;
      tokens.pop_back();
    } else {
      // Treat token as malformed index only when it starts like an integer
      // but contains trailing junk (e.g. "1foo"). Names like "clock2" remain valid.
      std::istringstream partialIndexStream(tokens.back());
      if (tokens.size() > 1 && (partialIndexStream >> parsedIndex)) {
        args.malformedIndexToken = true;
      }
    }
  }

  if (!tokens.empty()) {
    args.needle = joinTokens(tokens);
  }

  return args;
}

}  // namespace extracker::midi_cli_internal
