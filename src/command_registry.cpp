#include "extracker/command_registry.hpp"

#include <array>

namespace extracker {

void registerCommandHandlers(CommandRegistry& commandRegistry,
                             const CommandBindings& commandBindings) {
  commandRegistry["help"] = commandBindings.help;
  commandRegistry["plugin"] = commandBindings.plugin;
  commandRegistry["sine"] = commandBindings.sine;
  commandRegistry["note"] = commandBindings.note;
  commandRegistry["pattern"] = commandBindings.pattern;
  commandRegistry["record"] = commandBindings.record;
  commandRegistry["midi"] = commandBindings.midi;

  const std::array<std::string, 8> coreCommands = {
      "play", "stop", "tempo", "loop", "status", "reset", "save", "load"};
  for (const auto& coreCommand : coreCommands) {
    commandRegistry[coreCommand] = [commandBindings, coreCommand](std::istringstream& input) {
      commandBindings.core(coreCommand, input);
    };
  }
}

}  // namespace extracker
