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

  // rec with no channel (arms on default channel), then record off to disarm
  const std::string command =
      "printf 'rec\\n"
      "record off\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI rec alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI rec alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  // rec should produce the "Record enabled" confirmation
  const bool sawEnabled = containsAny(output, {"Record enabled"});

  if (!sawEnabled) {
    std::cerr << "Missing expected rec alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
