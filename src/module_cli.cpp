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

  return false;
}

}  // namespace extracker
