#pragma once

#include "extracker/command_registry.hpp"

namespace extracker {

struct DefaultCommandBindingCallbacks {
  CommandHandler onHelp;
  CommandHandler onPlugin;
  CommandHandler onInstrument;
  CommandHandler onSample;
  CommandHandler onSine;
  CommandHandler onNote;
  CommandHandler onPattern;
  CommandHandler onRecord;
  CommandHandler onMidi;
  CommandHandler onChannel;
  std::function<void(const std::string&, std::istringstream&)> onCore;
};

CommandBindings createDefaultCommandBindings(
    const DefaultCommandBindingCallbacks& callbacks);

}  // namespace extracker
