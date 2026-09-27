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
      "song goto 2\\n"
      "song n wrap\\n"
      "song b wrap\\n"
      "song next nope\\n"
      "song n nope\\n"
      "song b nope\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song next/prev wrap command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song next/prev wrap command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawWrappedNext = containsAny(output, {"Moved to song entry 1 (pattern 1)"});
  const bool sawWrappedPrev = containsAny(output, {"Moved to song entry 2 (pattern 2)"});
  const bool sawUsage = containsAny(output, {"song next [wrap]: move playback focus by one song entry"});
  const bool sawNextAliasUsage = containsAny(output, {"song n [wrap]: alias for song next"});
  const bool sawPrevAliasUsage = containsAny(output, {"song b [wrap]: alias for song prev"});

  if (!sawWrappedNext || !sawWrappedPrev || !sawUsage || !sawNextAliasUsage || !sawPrevAliasUsage) {
    std::cerr << "Missing expected song next/prev wrap output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
