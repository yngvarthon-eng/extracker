#include <cstdint>
#include <iostream>

#include "extracker/module.hpp"

int main() {
  extracker::Module module(16, 4);

  auto& first = module.currentEditor();
  first.insertNote(3, 0, 60, static_cast<std::uint8_t>(2));
  module.setCurrentPatternSwing(static_cast<std::uint8_t>(70));

  if (!module.insertPatternAfter()) {
    std::cerr << "Failed to insert second pattern" << '\n';
    return 1;
  }

  auto& second = module.currentEditor();
  second.insertNote(6, 0, 72, static_cast<std::uint8_t>(4));

  if (!module.duplicatePattern(0)) {
    std::cerr << "Failed to duplicate pattern by index" << '\n';
    return 1;
  }

  if (module.patternCount() != 3 || module.currentPattern() != 1) {
    std::cerr << "Unexpected state after duplicate-by-index" << '\n';
    return 1;
  }

  const auto& duplicate = module.currentEditor();
  if (!duplicate.hasNoteAt(3, 0) || duplicate.noteAt(3, 0) != 60 || duplicate.instrumentAt(3, 0) != 2) {
    std::cerr << "Duplicate did not preserve source pattern note content" << '\n';
    return 1;
  }

  if (module.currentPatternSwing() != 70) {
    std::cerr << "Duplicate did not preserve source swing" << '\n';
    return 1;
  }

  if (!module.switchToPattern(2)) {
    std::cerr << "Failed to switch to shifted original second pattern" << '\n';
    return 1;
  }

  if (!module.currentEditor().hasNoteAt(6, 0) || module.currentEditor().noteAt(6, 0) != 72) {
    std::cerr << "Shifted original second pattern content was not preserved" << '\n';
    return 1;
  }

  if (module.songLength() != 3 || module.songEntryAt(0) != 0 || module.songEntryAt(1) != 1 || module.songEntryAt(2) != 2) {
    std::cerr << "Song order was not updated correctly for duplicate-by-index" << '\n';
    return 1;
  }

  return 0;
}
