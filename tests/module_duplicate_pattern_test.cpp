#include <cstdint>
#include <iostream>

#include "extracker/module.hpp"

int main() {
  extracker::Module module(16, 4);

  auto& source = module.currentEditor();
  source.insertNote(3, 1, 62, static_cast<std::uint8_t>(2), 24, static_cast<std::uint8_t>(96), true, 0x0A, 0x0F);
  module.setCurrentPatternSwing(static_cast<std::uint8_t>(68));

  if (!module.duplicateCurrentPattern()) {
    std::cerr << "Failed to duplicate current pattern" << '\n';
    return 1;
  }

  if (module.patternCount() != 2 || module.currentPattern() != 1) {
    std::cerr << "Unexpected pattern state after duplicate" << '\n';
    return 1;
  }

  const auto& duplicated = module.currentEditor();
  if (!duplicated.hasNoteAt(3, 1) || duplicated.noteAt(3, 1) != 62 ||
      duplicated.instrumentAt(3, 1) != 2 || duplicated.gateTicksAt(3, 1) != 24 ||
      duplicated.velocityAt(3, 1) != 96 || !duplicated.retriggerAt(3, 1) ||
      duplicated.effectCommandAt(3, 1) != 0x0A || duplicated.effectValueAt(3, 1) != 0x0F) {
    std::cerr << "Duplicated pattern did not preserve source step values" << '\n';
    return 1;
  }

  if (module.currentPatternSwing() != 68) {
    std::cerr << "Duplicated pattern did not preserve swing" << '\n';
    return 1;
  }

  module.currentEditor().insertNote(4, 1, 65, static_cast<std::uint8_t>(3));
  if (!module.switchToPattern(0)) {
    std::cerr << "Failed to switch back to source pattern" << '\n';
    return 1;
  }

  if (module.currentEditor().hasNoteAt(4, 1)) {
    std::cerr << "Source pattern was mutated by edits in duplicate" << '\n';
    return 1;
  }

  if (module.songLength() < 2 || module.songEntryAt(0) != 0 || module.songEntryAt(1) != 1) {
    std::cerr << "Song order was not updated for duplicated pattern" << '\n';
    return 1;
  }

  return 0;
}
