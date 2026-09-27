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

  // in with no arg (usage guard), in after (valid), in before (valid)
  const std::string command =
      "printf 'pattern in\\n"
      "pattern in after\\n"
      "pattern in before\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern in alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern in alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  // No-arg form should show alias usage message
  const bool sawAliasUsage = containsAny(output, {"pattern in <before|after>: alias for pattern insert"});
  // Valid form should produce insert confirmation
  const bool sawInserted = containsAny(output, {"Inserted pattern"});

  if (!sawAliasUsage || !sawInserted) {
    std::cerr << "Missing expected pattern in alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
