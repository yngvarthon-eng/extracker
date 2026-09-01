#pragma once

#include <sstream>
#include "extracker/audio_engine.hpp"
#include "extracker/plugin_host.hpp"

namespace extracker {

void handleReverbCommand(AudioEngine& audio, PluginHost& plugins, std::istringstream& input);

} // namespace extracker
