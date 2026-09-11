#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#include "test_string_utils.hpp"

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  // sw with no index (usage guard), sw with valid index
  const std::string command =
      "printf 'pattern sw\\n"
      "pattern sw 1\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern sw alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern sw alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  // No-index form should show the alias usage message
  const bool sawAliasUsage = containsAny(output, {"pattern sw <index>: alias for pattern switch"});
  // Valid index form should produce the switch confirmation
  const bool sawSwitched = containsAny(output, {"Switched to pattern 1"});

  if (!sawAliasUsage || !sawSwitched) {
    std::cerr << "Missing expected pattern sw alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
