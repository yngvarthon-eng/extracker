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

    if (subcommand == "list" || subcommand == "status" || subcommand == "ls") {
      std::cout << "Patterns: " << module.patternCount() << ", current: " << (module.currentPattern() + 1) << '\n';
      return true;
    }

    if (subcommand == "switch" || subcommand == "sw") {
      const bool aliasMode = (subcommand == "sw");
      if (tokens.size() < 2) {
        if (aliasMode) {
          std::cout << "pattern sw <index>: alias for pattern switch" << '\n';
        } else {
          std::cout << "pattern switch <index>: switch to pattern (1-indexed)" << '\n';
        }
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

    if (subcommand == "insert" || subcommand == "in") {
      const bool aliasMode = (subcommand == "in");
      if (tokens.size() < 2) {
        if (aliasMode) {
          std::cout << "pattern in <before|after>: alias for pattern insert" << '\n';
        } else {
          std::cout << "pattern insert <before|after>: insert a new pattern" << '\n';
        }
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

    if (subcommand == "insert-swing") {
      if (tokens.size() == 1 || tokens[1] == "status") {
        std::cout << "Pattern insert swing inheritance: "
                  << (module.inheritSwingOnInsert() ? "on" : "off") << '\n';
        return true;
      }

      if (tokens[1] == "on") {
        module.setInheritSwingOnInsert(true);
        std::cout << "Pattern insert swing inheritance enabled" << '\n';
        return true;
      }

      if (tokens[1] == "off") {
        module.setInheritSwingOnInsert(false);
        std::cout << "Pattern insert swing inheritance disabled" << '\n';
        return true;
      }

      std::cout << "Usage: pattern insert-swing <on|off|status>" << '\n';
      return true;
    }

    if (subcommand == "remove" || subcommand == "del") {
      if (module.removeCurrentPattern()) {
        std::cout << "Removed current pattern (now at pattern " << (module.currentPattern() + 1) << ")" << '\n';
      } else {
        std::cout << "Cannot remove: must have at least one pattern" << '\n';
      }
      return true;
    }

    if (subcommand == "duplicate" || subcommand == "dup") {
      const bool aliasMode = (subcommand == "dup");
      if (tokens.size() > 2) {
        if (aliasMode) {
          std::cout << "pattern dup [index]: alias for pattern duplicate" << '\n';
        } else {
          std::cout << "pattern duplicate [index]: duplicate current or selected pattern (1-indexed)" << '\n';
        }
        return true;
      }

      std::size_t sourcePattern = module.currentPattern();
      bool usedExplicitSource = false;
      if (tokens.size() == 2) {
        int index = 0;
        if (!extracker::cli::parseStrictIntToken(tokens[1], index) ||
            index <= 0 ||
            index > static_cast<int>(module.patternCount())) {
          std::cout << "Invalid pattern index" << '\n';
          return true;
        }
        sourcePattern = static_cast<std::size_t>(index - 1);
        usedExplicitSource = true;
      }

      if (module.duplicatePattern(sourcePattern)) {
        if (usedExplicitSource) {
          std::cout << "Duplicated pattern " << (sourcePattern + 1)
                    << " to pattern " << (module.currentPattern() + 1) << '\n';
        } else {
          std::cout << "Duplicated current pattern to pattern " << (module.currentPattern() + 1) << '\n';
        }
      } else {
        std::cout << "Cannot duplicate current pattern" << '\n';
      }
      return true;
    }

    return false;
  }

  if (command == "song") {
    if (tokens.empty()) {
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
    if (subcommand == "status" || subcommand == "list" || subcommand == "st" || subcommand == "ls") {
      if (subcommand == "st" && tokens.size() != 1) {
        std::cout << "song st: alias for song status" << '\n';
        return true;
      }
      if (subcommand == "ls" && tokens.size() != 1) {
        std::cout << "song ls: alias for song list" << '\n';
        return true;
      }

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
    if (subcommand == "pos" || subcommand == "p" || subcommand == "gp") {
      const bool aliasP = (subcommand == "p");
      const bool aliasGp = (subcommand == "gp");
      if (tokens.size() != 1) {
        if (aliasP) {
          std::cout << "song p: alias for song pos" << '\n';
        } else if (aliasGp) {
          std::cout << "song gp: alias for song pos" << '\n';
        } else {
          std::cout << "song pos: show compact song position/mode status" << '\n';
        }
        return true;
      }

      std::size_t songLen = module.songLength();
      std::size_t pos = 0;
      bool songMode = false;

      if (songModeEnabled != nullptr && songPlaybackPosition != nullptr) {
        songMode = songModeEnabled->load();
        if (songLen > 0) {
          pos = std::min(songPlaybackPosition->load(), songLen - 1);
        }
      } else if (songLen > 0) {
        pos = std::min(module.firstSongEntryForPattern(module.currentPattern()), songLen - 1);
      }

      if (songLen == 0) {
        std::cout << "Song pos: 0/0, pattern 0, mode " << (songMode ? "song" : "pattern") << '\n';
        return true;
      }

      std::cout << "Song pos: " << (pos + 1)
                << "/" << songLen
                << ", pattern " << (module.songEntryAt(pos) + 1)
                << ", mode " << (songMode ? "song" : "pattern") << '\n';
      return true;
    }

    if (subcommand == "play" || subcommand == "pl") {
      const bool aliasMode = (subcommand == "pl");
      if ((aliasMode && tokens.size() != 2) || (!aliasMode && tokens.size() < 2)) {
        if (aliasMode) {
          std::cout << "song pl <pattern|song|status>: alias for song play" << '\n';
        } else {
          std::cout << "song play <pattern|song|status>: set or show playback mode" << '\n';
        }
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

    if (subcommand == "goto" || subcommand == "g") {
      const bool aliasMode = (subcommand == "g");
      if (tokens.size() < 2 || tokens.size() > 2) {
        if (aliasMode) {
          std::cout << "song g <entry>: alias for song goto" << '\n';
        } else {
          std::cout << "song goto <entry>: jump to song entry (1-indexed)" << '\n';
        }
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

    if (subcommand == "first" || subcommand == "last" || subcommand == "f" || subcommand == "l") {
      const bool firstDirection = (subcommand == "first" || subcommand == "f");
      const bool aliasMode = (subcommand == "f" || subcommand == "l");
      const char* canonical = firstDirection ? "first" : "last";
      if (tokens.size() != 1) {
        if (aliasMode) {
          std::cout << "song " << subcommand << ": alias for song " << canonical << '\n';
        } else {
          std::cout << "song " << subcommand << ": jump to "
                    << (firstDirection ? "first" : "last")
                    << " song entry" << '\n';
        }
        return true;
      }
      if (module.songLength() == 0) {
        std::cout << "Song is empty" << '\n';
        return true;
      }

      const std::size_t targetPos = firstDirection ? 0 : (module.songLength() - 1);
      const std::size_t targetPattern = module.songEntryAt(targetPos);
      if (module.switchToPattern(targetPattern)) {
        if (songPlaybackPosition != nullptr) {
          songPlaybackPosition->store(targetPos);
        }
        std::cout << "Moved to song entry " << (targetPos + 1)
                  << " (pattern " << (targetPattern + 1) << ")" << '\n';
      }
      return true;
    }

    if (subcommand == "next" || subcommand == "prev" || subcommand == "n" || subcommand == "b") {
      const bool nextDirection = (subcommand == "next" || subcommand == "n");
      const bool aliasMode = (subcommand == "n" || subcommand == "b");
      const char* canonical = nextDirection ? "next" : "prev";
      bool wrap = false;
      if (tokens.size() == 2) {
        if (tokens[1] == "wrap") {
          wrap = true;
        } else {
          if (aliasMode) {
            std::cout << "song " << subcommand << " [wrap]: alias for song " << canonical << '\n';
          } else {
            std::cout << "song " << subcommand << " [wrap]: move playback focus by one song entry" << '\n';
          }
          return true;
        }
      } else if (tokens.size() != 1) {
        if (aliasMode) {
          std::cout << "song " << subcommand << " [wrap]: alias for song " << canonical << '\n';
        } else {
          std::cout << "song " << subcommand << " [wrap]: move playback focus by one song entry" << '\n';
        }
        return true;
      }
      if (module.songLength() == 0) {
        std::cout << "Song is empty" << '\n';
        return true;
      }

      std::size_t currentEntry = module.firstSongEntryForPattern(module.currentPattern());
      if (songPlaybackPosition != nullptr) {
        currentEntry = std::min(songPlaybackPosition->load(), module.songLength() - 1);
      }

      if (nextDirection) {
        if (currentEntry + 1 >= module.songLength()) {
          if (!wrap) {
            std::cout << "Already at last song entry" << '\n';
            return true;
          }
          currentEntry = 0;
        } else {
          ++currentEntry;
        }
      } else {
        if (currentEntry == 0) {
          if (!wrap) {
            std::cout << "Already at first song entry" << '\n';
            return true;
          }
          currentEntry = module.songLength() - 1;
        } else {
          --currentEntry;
        }
      }

      std::size_t targetPattern = module.songEntryAt(currentEntry);
      if (module.switchToPattern(targetPattern)) {
        if (songPlaybackPosition != nullptr) {
          songPlaybackPosition->store(currentEntry);
        }
        std::cout << "Moved to song entry " << (currentEntry + 1)
                  << " (pattern " << (targetPattern + 1) << ")" << '\n';
      }
      return true;
    }

    if (subcommand == "set" || subcommand == "se") {
      const bool aliasMode = (subcommand == "se");
      if ((aliasMode && tokens.size() != 3) || (!aliasMode && tokens.size() < 3)) {
        if (aliasMode) {
          std::cout << "song se <entry> <pattern>: alias for song set" << '\n';
        } else {
          std::cout << "song set <entry> <pattern>: set song entry (1-indexed)" << '\n';
        }
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

    if (subcommand == "insert" || subcommand == "si") {
      const bool aliasMode = (subcommand == "si");
      if ((aliasMode && tokens.size() != 3) || (!aliasMode && tokens.size() < 3)) {
        if (aliasMode) {
          std::cout << "song si <entry> <pattern>: alias for song insert" << '\n';
        } else {
          std::cout << "song insert <entry> <pattern>: insert before entry (1-indexed)" << '\n';
        }
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

    if (subcommand == "append" || subcommand == "ap") {
      const bool aliasMode = (subcommand == "ap");
      if ((aliasMode && tokens.size() != 2) || (!aliasMode && tokens.size() < 2)) {
        if (aliasMode) {
          std::cout << "song ap <pattern>: alias for song append" << '\n';
        } else {
          std::cout << "song append <pattern>: append pattern to song order (1-indexed)" << '\n';
        }
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

    if (subcommand == "remove" || subcommand == "rm") {
      const bool aliasMode = (subcommand == "rm");
      if ((aliasMode && tokens.size() != 2) || (!aliasMode && tokens.size() < 2)) {
        if (aliasMode) {
          std::cout << "song rm <entry>: alias for song remove" << '\n';
        } else {
          std::cout << "song remove <entry>: remove song entry (1-indexed)" << '\n';
        }
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

    if (subcommand == "move" || subcommand == "mv") {
      const bool aliasMode = (subcommand == "mv");
      if ((aliasMode && tokens.size() != 3) || (!aliasMode && tokens.size() < 3)) {
        if (aliasMode) {
          std::cout << "song mv <entry> <up|down>: alias for song move" << '\n';
        } else {
          std::cout << "song move <entry> <up|down>: move song entry (1-indexed)" << '\n';
        }
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
