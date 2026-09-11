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

  // r with no filename should trigger load usage message
  const std::string command =
      "printf 'r\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI r alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI r alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  // No-arg load produces its usage message
  const bool sawUsage = containsAny(output, {"Usage: load <file>"});

  if (!sawUsage) {
    std::cerr << "Missing expected r alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
