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

  // Add a second pattern so removal is possible, then use 'pattern del'
  const std::string command =
      "printf 'pattern duplicate\\n"
      "pattern del\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern del alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern del alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  // del should produce the "Removed current pattern" confirmation
  const bool sawRemoved = containsAny(output, {"Removed current pattern"});

  if (!sawRemoved) {
    std::cerr << "Missing expected pattern del alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
