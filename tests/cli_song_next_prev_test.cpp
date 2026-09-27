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
      "printf 'pattern insert after\\n"
      "song goto 1\\n"
      "song n\\n"
      "song next\\n"
      "song b\\n"
      "song prev\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song next/prev command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song next/prev command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawNext = containsAny(output, {"Moved to song entry 2 (pattern 2)"});
  const bool sawAtLast = containsAny(output, {"Already at last song entry"});
  const bool sawPrev = containsAny(output, {"Moved to song entry 1 (pattern 1)"});
  const bool sawAtFirst = containsAny(output, {"Already at first song entry"});

  if (!sawNext || !sawAtLast || !sawPrev || !sawAtFirst) {
    std::cerr << "Missing expected song next/prev output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
