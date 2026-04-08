#include "extracker/module_cli.hpp"

#include <iostream>
#include <sstream>

#include "extracker/cli_parse_utils.hpp"
#include "extracker/module.hpp"

namespace extracker {

bool handleModuleCommand(const std::string& command,
                         const std::vector<std::string>& tokens,
                         ModuleCommandContext context) {
  auto& module = context.module;
  auto* songModeEnabled = context.songModeEnabled;
  auto* songPlaybackPosition = context.songPlaybackPosition;

  if (command == "pattern") {
    if (tokens.empty()) {
      std::cout << "Module: " << module.patternCount() << " pattern(s), current: " << (module.currentPattern() + 1) << '\n';
      return true;
    }

    const std::string& subcommand = tokens[0];

    if (subcommand == "list" || subcommand == "status") {
      std::cout << "Patterns: " << module.patternCount() << ", current: " << (module.currentPattern() + 1) << '\n';
      return true;
    }

    if (subcommand == "switch") {
      if (tokens.size() < 2) {
        std::cout << "pattern switch <index>: switch to pattern (1-indexed)" << '\n';
        return true;
      }
      int index = 0;
      if (!extracker::cli::parseStrictIntToken(tokens[1], index) || index <= 0 || index > static_cast<int>(module.patternCount())) {
        std::cout << "Invalid pattern index" << '\n';
        return true;
      }
      if (module.switchToPattern(static_cast<std::size_t>(index - 1))) {
        std::cout << "Switched to pattern " << index << '\n';
      }
      return true;
    }

    if (subcommand == "insert") {
      if (tokens.size() < 2) {
        std::cout << "pattern insert <before|after>: insert a new pattern" << '\n';
        return true;
      }
      
      const std::string& position = tokens[1];
      bool success = false;
      if (position == "before") {
        success = module.insertPatternBefore();
        if (success) {
          std::cout << "Inserted pattern before current (now at pattern " << (module.currentPattern() + 1) << ")" << '\n';
        }
      } else if (position == "after") {
        success = module.insertPatternAfter();
        if (success) {
          std::cout << "Inserted pattern after current (now at pattern " << (module.currentPattern() + 1) << ")" << '\n';
        }
      } else {
        std::cout << "Position must be 'before' or 'after'" << '\n';
        return true;
      }
      
      if (!success) {
        std::cout << "Insert failed" << '\n';
      }
      return true;
    }

    if (subcommand == "remove") {
      if (module.removeCurrentPattern()) {
        std::cout << "Removed current pattern (now at pattern " << (module.currentPattern() + 1) << ")" << '\n';
      } else {
        std::cout << "Cannot remove: must have at least one pattern" << '\n';
      }
      return true;
    }

    return false;
  }

  if (command == "song") {
    if (tokens.empty() || tokens[0] == "status" || tokens[0] == "list") {
      std::cout << "Song: " << module.songLength() << " entry(s), current pattern: "
                << (module.currentPattern() + 1) << '\n';
      std::cout << "Order:";
      for (std::size_t i = 0; i < module.songLength(); ++i) {
        std::cout << " " << (module.songEntryAt(i) + 1);
      }
      std::cout << '\n';
      if (songModeEnabled != nullptr && songPlaybackPosition != nullptr) {
        std::size_t safePos = std::min(songPlaybackPosition->load(), module.songLength() > 0 ? module.songLength() - 1 : 0);
        std::cout << "Playback mode: " << (songModeEnabled->load() ? "song" : "pattern") << '\n';
        if (module.songLength() > 0) {
          std::cout << "Song position: " << (safePos + 1)
                    << " (pattern " << (module.songEntryAt(safePos) + 1) << ")" << '\n';
        }
      }
      return true;
    }

    const std::string& subcommand = tokens[0];
    if (subcommand == "play") {
      if (tokens.size() < 2) {
        std::cout << "song play <pattern|song|status>: set or show playback mode" << '\n';
        return true;
      }
      if (songModeEnabled == nullptr || songPlaybackPosition == nullptr) {
        std::cout << "Song playback mode is unavailable" << '\n';
        return true;
      }
      const std::string& mode = tokens[1];
      if (mode == "status") {
        std::size_t safePos = std::min(songPlaybackPosition->load(), module.songLength() > 0 ? module.songLength() - 1 : 0);
        std::cout << "Playback mode: " << (songModeEnabled->load() ? "song" : "pattern") << '\n';
        if (module.songLength() > 0) {
          std::cout << "Song position: " << (safePos + 1)
                    << " (pattern " << (module.songEntryAt(safePos) + 1) << ")" << '\n';
        }
      } else if (mode == "song") {
        songModeEnabled->store(true);
        std::size_t pos = module.firstSongEntryForPattern(module.currentPattern());
        if (module.songLength() > 0) {
          pos = std::min(pos, module.songLength() - 1);
        } else {
          pos = 0;
        }
        songPlaybackPosition->store(pos);
        std::cout << "Playback mode set to song" << '\n';
      } else if (mode == "pattern") {
        songModeEnabled->store(false);
        std::cout << "Playback mode set to pattern" << '\n';
      } else {
        std::cout << "Mode must be 'pattern', 'song', or 'status'" << '\n';
      }
      return true;
    }

    if (subcommand == "goto") {
      if (tokens.size() < 2) {
        std::cout << "song goto <entry>: jump to song entry (1-indexed)" << '\n';
        return true;
      }
      int entryIndex = 0;
      if (!extracker::cli::parseStrictIntToken(tokens[1], entryIndex) ||
          entryIndex <= 0 ||
          entryIndex > static_cast<int>(module.songLength())) {
        std::cout << "Invalid song entry index" << '\n';
        return true;
      }
      std::size_t targetPos = static_cast<std::size_t>(entryIndex - 1);
      std::size_t targetPattern = module.songEntryAt(targetPos);
      if (module.switchToPattern(targetPattern)) {
        if (songPlaybackPosition != nullptr) {
          songPlaybackPosition->store(targetPos);
        }
        std::cout << "Jumped to song entry " << entryIndex
                  << " (pattern " << (targetPattern + 1) << ")" << '\n';
      }
      return true;
    }

    if (subcommand == "set") {
      if (tokens.size() < 3) {
        std::cout << "song set <entry> <pattern>: set song entry (1-indexed)" << '\n';
        return true;
      }
      int entryIndex = 0;
      int patternIndex = 0;
      if (!extracker::cli::parseStrictIntToken(tokens[1], entryIndex) ||
          !extracker::cli::parseStrictIntToken(tokens[2], patternIndex) ||
          entryIndex <= 0 || patternIndex <= 0 ||
          entryIndex > static_cast<int>(module.songLength()) ||
          patternIndex > static_cast<int>(module.patternCount())) {
        std::cout << "Invalid song entry or pattern index" << '\n';
        return true;
      }
      if (module.setSongEntry(static_cast<std::size_t>(entryIndex - 1),
                              static_cast<std::size_t>(patternIndex - 1))) {
        std::cout << "Set song entry " << entryIndex << " to pattern " << patternIndex << '\n';
      }
      return true;
    }

    if (subcommand == "insert") {
      if (tokens.size() < 3) {
        std::cout << "song insert <entry> <pattern>: insert before entry (1-indexed)" << '\n';
        return true;
      }
      int entryIndex = 0;
      int patternIndex = 0;
      if (!extracker::cli::parseStrictIntToken(tokens[1], entryIndex) ||
          !extracker::cli::parseStrictIntToken(tokens[2], patternIndex) ||
          entryIndex <= 0 || patternIndex <= 0 ||
          entryIndex > static_cast<int>(module.songLength() + 1) ||
          patternIndex > static_cast<int>(module.patternCount())) {
        std::cout << "Invalid song entry or pattern index" << '\n';
        return true;
      }
      if (module.insertSongEntry(static_cast<std::size_t>(entryIndex - 1),
                                 static_cast<std::size_t>(patternIndex - 1))) {
        std::cout << "Inserted pattern " << patternIndex << " at song entry " << entryIndex << '\n';
      }
      return true;
    }

    if (subcommand == "append") {
      if (tokens.size() < 2) {
        std::cout << "song append <pattern>: append pattern to song order (1-indexed)" << '\n';
        return true;
      }
      int patternIndex = 0;
      if (!extracker::cli::parseStrictIntToken(tokens[1], patternIndex) ||
          patternIndex <= 0 ||
          patternIndex > static_cast<int>(module.patternCount())) {
        std::cout << "Invalid pattern index" << '\n';
        return true;
      }
      if (module.appendSongEntry(static_cast<std::size_t>(patternIndex - 1))) {
        std::cout << "Appended pattern " << patternIndex << " to song order" << '\n';
      }
      return true;
    }

    if (subcommand == "remove") {
      if (tokens.size() < 2) {
        std::cout << "song remove <entry>: remove song entry (1-indexed)" << '\n';
        return true;
      }
      int entryIndex = 0;
      if (!extracker::cli::parseStrictIntToken(tokens[1], entryIndex) ||
          entryIndex <= 0 ||
          entryIndex > static_cast<int>(module.songLength())) {
        std::cout << "Invalid song entry index" << '\n';
        return true;
      }
      if (module.removeSongEntry(static_cast<std::size_t>(entryIndex - 1))) {
        std::cout << "Removed song entry " << entryIndex << '\n';
      } else {
        std::cout << "Cannot remove: song order must contain at least one entry" << '\n';
      }
      return true;
    }

    if (subcommand == "move") {
      if (tokens.size() < 3) {
        std::cout << "song move <entry> <up|down>: move song entry (1-indexed)" << '\n';
        return true;
      }
      int entryIndex = 0;
      if (!extracker::cli::parseStrictIntToken(tokens[1], entryIndex) ||
          entryIndex <= 0 ||
          entryIndex > static_cast<int>(module.songLength())) {
        std::cout << "Invalid song entry index" << '\n';
        return true;
      }
      const std::string& direction = tokens[2];
      if (direction == "up") {
        if (module.moveSongEntryUp(static_cast<std::size_t>(entryIndex - 1))) {
          std::cout << "Moved song entry " << entryIndex << " up" << '\n';
        } else {
          std::cout << "Cannot move up" << '\n';
        }
      } else if (direction == "down") {
        if (module.moveSongEntryDown(static_cast<std::size_t>(entryIndex - 1))) {
          std::cout << "Moved song entry " << entryIndex << " down" << '\n';
        } else {
          std::cout << "Cannot move down" << '\n';
        }
      } else {
        std::cout << "Direction must be 'up' or 'down'" << '\n';
      }
      return true;
    }

    return false;
  }

  return false;
}

}  // namespace extracker
