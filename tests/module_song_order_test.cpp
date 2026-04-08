#include <iostream>
#include <vector>

#include "extracker/module.hpp"

int main() {
  extracker::Module module(16, 4);

  if (module.patternCount() != 1 || module.songLength() != 1) {
    std::cerr << "Initial module/song size mismatch" << '\n';
    return 1;
  }

  if (!module.insertPatternAfter()) {
    std::cerr << "Failed to insert pattern after" << '\n';
    return 1;
  }
  if (!module.insertPatternAfter()) {
    std::cerr << "Failed to insert second pattern" << '\n';
    return 1;
  }

  if (module.patternCount() != 3 || module.songLength() != 3) {
    std::cerr << "Pattern/song size after insertion mismatch" << '\n';
    return 1;
  }

  if (!module.setSongOrder({0, 2, 1, 2})) {
    std::cerr << "Failed to set custom song order" << '\n';
    return 1;
  }

  if (module.songLength() != 4 || module.songEntryAt(1) != 2 || module.songEntryAt(2) != 1) {
    std::cerr << "Song order content mismatch" << '\n';
    return 1;
  }

  if (!module.moveSongEntryUp(2)) {
    std::cerr << "Failed to move song entry up" << '\n';
    return 1;
  }
  if (module.songEntryAt(1) != 1 || module.songEntryAt(2) != 2) {
    std::cerr << "Song order move-up result mismatch" << '\n';
    return 1;
  }

  if (!module.moveSongEntryDown(1)) {
    std::cerr << "Failed to move song entry down" << '\n';
    return 1;
  }
  if (module.songEntryAt(1) != 2 || module.songEntryAt(2) != 1) {
    std::cerr << "Song order move-down result mismatch" << '\n';
    return 1;
  }

  if (!module.removeSongEntry(0)) {
    std::cerr << "Failed to remove song entry" << '\n';
    return 1;
  }
  if (module.songLength() != 3) {
    std::cerr << "Song length after remove mismatch" << '\n';
    return 1;
  }

  if (!module.switchToPattern(1)) {
    std::cerr << "Failed to switch to pattern 2" << '\n';
    return 1;
  }
  if (!module.removeCurrentPattern()) {
    std::cerr << "Failed to remove current pattern" << '\n';
    return 1;
  }

  if (module.patternCount() != 2) {
    std::cerr << "Pattern count after remove mismatch" << '\n';
    return 1;
  }

  for (std::size_t i = 0; i < module.songLength(); ++i) {
    if (module.songEntryAt(i) >= module.patternCount()) {
      std::cerr << "Song entry references removed pattern index" << '\n';
      return 1;
    }
  }

  if (module.songLength() == 0) {
    std::cerr << "Song order unexpectedly empty" << '\n';
    return 1;
  }

  return 0;
}
