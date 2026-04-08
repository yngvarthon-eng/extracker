#include "extracker/default_command_bindings.hpp"

namespace extracker {
namespace {

void assignTransportAndSessionBindings(
    CommandBindings& bindings,
    const DefaultCommandBindingCallbacks& callbacks) {
  bindings.help = callbacks.onHelp;
  bindings.record = callbacks.onRecord;
  bindings.midi = callbacks.onMidi;
  bindings.core = callbacks.onCore;
}

void assignInstrumentAndPatternBindings(
    CommandBindings& bindings,
    const DefaultCommandBindingCallbacks& callbacks) {
  bindings.plugin = callbacks.onPlugin;
  bindings.sample = callbacks.onSample;
  bindings.sine = callbacks.onSine;
  bindings.note = callbacks.onNote;
  bindings.pattern = callbacks.onPattern;
}

}  // namespace

CommandBindings createDefaultCommandBindings(
    const DefaultCommandBindingCallbacks& callbacks) {
  CommandBindings bindings;
  assignTransportAndSessionBindings(bindings, callbacks);
  assignInstrumentAndPatternBindings(bindings, callbacks);
  return bindings;
}

}  // namespace extracker
