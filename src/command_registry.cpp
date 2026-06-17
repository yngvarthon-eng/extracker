#include "extracker/command_registry.hpp"

#include <array>

namespace extracker {

void registerCommandHandlers(CommandRegistry& commandRegistry,
                             const CommandBindings& commandBindings) {
  commandRegistry["help"] = commandBindings.help;
  commandRegistry["h"] = commandBindings.help;
  commandRegistry["plugin"] = commandBindings.plugin;
  commandRegistry["sample"] = commandBindings.sample;
  commandRegistry["sine"] = commandBindings.sine;
  commandRegistry["note"] = commandBindings.note;
  commandRegistry["pattern"] = commandBindings.pattern;
  commandRegistry["record"] = commandBindings.record;
  commandRegistry["midi"] = commandBindings.midi;

  const std::array<std::string, 9> coreCommands = {
      "play", "stop", "tempo", "loop", "status", "reset", "save", "load", "message"};
  for (const auto& coreCommand : coreCommands) {
    commandRegistry[coreCommand] = [commandBindings, coreCommand](std::istringstream& input) {
      commandBindings.core(coreCommand, input);
    };
  }

  // bpm is an ergonomic alias for tempo
  commandRegistry["bpm"] = [commandBindings](std::istringstream& input) {
    commandBindings.core("tempo", input);
  };

  // st is an ergonomic alias for status
  commandRegistry["st"] = [commandBindings](std::istringstream& input) {
    commandBindings.core("status", input);
  };

  // w (write) is an ergonomic alias for save
  commandRegistry["w"] = [commandBindings](std::istringstream& input) {
    commandBindings.core("save", input);
  };

  // r (read) is an ergonomic alias for load
  commandRegistry["r"] = [commandBindings](std::istringstream& input) {
    commandBindings.core("load", input);
  };

  // rec is an ergonomic alias for record on [channel]
  commandRegistry["rec"] = [commandBindings](std::istringstream& input) {
    std::string rest;
    std::getline(input, rest);
    std::istringstream forwarded(std::string("on") + (rest.empty() ? "" : " ") + rest);
    commandBindings.record(forwarded);
  };

  // p is an ergonomic alias for play
  commandRegistry["p"] = [commandBindings](std::istringstream& input) {
    commandBindings.core("play", input);
  };

  // s is an ergonomic alias for stop
  commandRegistry["s"] = [commandBindings](std::istringstream& input) {
    commandBindings.core("stop", input);
  };
}

}  // namespace extracker
