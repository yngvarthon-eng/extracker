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
      "printf 'song append 1\\n"
      "song mv 2 up\\n"
      "song mv 1 down\\n"
      "song mv 1\\n"
      "song mv 1 down extra\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song move alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song move alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawMoveUp = containsAny(output, {"Moved song entry 2 up"});
  const bool sawMoveDown = containsAny(output, {"Moved song entry 1 down"});
  const bool sawAliasUsage = containsAny(output, {"song mv <entry> <up|down>: alias for song move"});

  if (!sawMoveUp || !sawMoveDown || !sawAliasUsage) {
    std::cerr << "Missing expected song move alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
