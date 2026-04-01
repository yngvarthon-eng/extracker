#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace extracker {

using CommandHandler = std::function<void(std::istringstream&)>;
using CommandRegistry = std::unordered_map<std::string, CommandHandler>;

struct CommandBindings {
  CommandHandler help;
  CommandHandler plugin;
  CommandHandler sine;
  CommandHandler note;
  CommandHandler pattern;
  CommandHandler record;
  CommandHandler midi;
  std::function<void(const std::string&, std::istringstream&)> core;
};

void registerCommandHandlers(CommandRegistry& commandRegistry,
                             const CommandBindings& commandBindings);

}  // namespace extracker
