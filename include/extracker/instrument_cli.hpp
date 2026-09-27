#pragma once

#include <sstream>

#include "extracker/plugin_host.hpp"

namespace extracker {

void handleInstrumentCommand(PluginHost& plugins, std::istringstream& input);

}  // namespace extracker
