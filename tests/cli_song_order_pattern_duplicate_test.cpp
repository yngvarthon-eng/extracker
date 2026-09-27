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

  const std::string command =
      "printf 'note set 3 1 62 2\\n"
      "pattern duplicate\\n"
      "song status\\n"
      "pattern switch 1\\n"
      "pattern switch 2\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song duplicate-pattern command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song duplicate-pattern command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDuplicate = containsAny(output, {"Duplicated current pattern to pattern 2"});
  const bool sawSongOrder = containsAny(output, {"Order: 1 2"});
  const bool sawSwitchOne = containsAny(output, {"Switched to pattern 1"});
  const bool sawSwitchTwo = containsAny(output, {"Switched to pattern 2"});

  if (!sawDuplicate || !sawSongOrder || !sawSwitchOne || !sawSwitchTwo) {
    std::cerr << "Missing expected duplicate-pattern output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
