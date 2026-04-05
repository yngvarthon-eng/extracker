#include "extracker/pattern_cli.hpp"

#include <iostream>

#include "pattern_cli_internal.hpp"

namespace extracker {

void handlePatternCommand(PatternCommandContext context,
                          std::istringstream& patternInput) {
  std::string subcommand;
  patternInput >> subcommand;

  if (pattern_cli_internal::handlePatternBasicSubcommand(context, subcommand, patternInput)) {
    return;
  }

  if (pattern_cli_internal::handlePatternBulkSubcommand(context, subcommand, patternInput)) {
    return;
  }

  std::cout << "Usage: pattern <print|play|template|transpose|velocity|gate|effect|copy|paste|humanize|randomize|undo|redo> ..." << '\n';
}

}  // namespace extracker
