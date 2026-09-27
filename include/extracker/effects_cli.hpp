#pragma once

#include <sstream>
#include "extracker/audio_engine.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sequencer.hpp"

namespace extracker {

void handleEffectsCommand(AudioEngine& audio, PluginHost& plugins, Sequencer& sequencer,
                           std::istringstream& input);

}  // namespace extracker
