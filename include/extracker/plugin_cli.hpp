#pragma once

#include <sstream>

#include "extracker/plugin_host.hpp"

namespace extracker {

void handlePluginCommand(PluginHost& plugins, std::istringstream& pluginInput);
void handleSineCommand(PluginHost& plugins, std::istringstream& sineInput);
void handleHelpCommand();

}  // namespace extracker
