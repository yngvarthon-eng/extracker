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

  // bpm with a valid value, then an invalid value (usage guard)
  const std::string command =
      "printf 'bpm 140\\n"
      "bpm -1\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI bpm alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI bpm alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  // Valid bpm should produce the tempo confirmation
  const bool sawTempoSet = containsAny(output, {"Tempo set to"});
  // Invalid value should show usage guard
  const bool sawUsage = containsAny(output, {"Usage: tempo <positive_bpm>"});

  if (!sawTempoSet || !sawUsage) {
    std::cerr << "Missing expected bpm alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
