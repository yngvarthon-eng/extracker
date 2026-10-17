#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "extracker/plugin_host.hpp"

namespace extracker {

void handleInstrumentCommand(PluginHost& plugins, std::istringstream& input);

// What an instrument slot plays, for messages: the plugin id, or
// `sample slot N "name"` for a sample instrument; empty for an empty slot.
std::string describeInstrumentSlot(PluginHost& plugins, int instrument);

// Sample instruments that play sample-bank slot `sampleSlot`, lowest first.
std::vector<int> instrumentsPlayingSample(PluginHost& plugins, int sampleSlot);

// Prints "Replaced <before> on instrument N" when an assignment changed what
// a previously occupied slot plays.
void reportInstrumentReplacement(PluginHost& plugins, int instrument, const std::string& before);

}  // namespace extracker
