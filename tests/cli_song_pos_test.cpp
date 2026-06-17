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
      "song play song\\n"
      "song goto 2\\n"
      "song gp\\n"
      "song play pattern\\n"
      "song p\\n"
      "song pos extra\\n"
      "song p extra\\n"
      "song gp extra\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song pos command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song pos command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSongMode = containsAny(output, {"Song pos: 2/2, pattern 2, mode song"});
  const bool sawPatternMode = containsAny(output, {"Song pos: 2/2, pattern 2, mode pattern"});
  const bool sawUsage = containsAny(output, {"song pos: show compact song position/mode status"});
  const bool sawAliasUsage = containsAny(output, {"song p: alias for song pos"});
  const bool sawGpAliasUsage = containsAny(output, {"song gp: alias for song pos"});

  if (!sawSongMode || !sawPatternMode || !sawUsage || !sawAliasUsage || !sawGpAliasUsage) {
    std::cerr << "Missing expected song pos output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
