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
      "printf 'note set 3 0 60 2\\n"
      "pattern insert after\\n"
      "note set 6 0 72 4\\n"
      "pattern duplicate 1\\n"
      "song status\\n"
      "pattern switch 2\\n"
      "pattern switch 3\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI indexed duplicate-pattern command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI indexed duplicate-pattern command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDuplicate = containsAny(output, {"Duplicated pattern 1 to pattern 2"});
  const bool sawSongOrder = containsAny(output, {"Order: 1 2 3"});
  const bool sawSwitchTwo = containsAny(output, {"Switched to pattern 2"});
  const bool sawSwitchThree = containsAny(output, {"Switched to pattern 3"});

  if (!sawDuplicate || !sawSongOrder || !sawSwitchTwo || !sawSwitchThree) {
    std::cerr << "Missing expected indexed duplicate-pattern output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
