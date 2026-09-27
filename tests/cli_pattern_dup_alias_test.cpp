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

  // dup with no index (duplicate current), dup with explicit index, malformed extra token
  const std::string command =
      "printf 'pattern dup\\n"
      "pattern dup 1\\n"
      "pattern dup 1 extra\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern dup alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern dup alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  // First dup (no index) should produce the "Duplicated current pattern" message
  const bool sawDupCurrent = containsAny(output, {"Duplicated current pattern"});
  // Extra-token form should produce the alias usage guard
  const bool sawAliasUsage = containsAny(output, {"pattern dup [index]: alias for pattern duplicate"});

  if (!sawDupCurrent || !sawAliasUsage) {
    std::cerr << "Missing expected pattern dup alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
