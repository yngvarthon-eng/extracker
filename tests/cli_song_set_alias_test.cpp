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
      "song se 1 2\\n"
      "song se 1 2 extra\\n"
      "song se 1\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song set alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song set alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSet = containsAny(output, {"Set song entry 1 to pattern 2"});
  const bool sawAliasUsage = containsAny(output, {"song se <entry> <pattern>: alias for song set"});

  if (!sawSet || !sawAliasUsage) {
    std::cerr << "Missing expected song set alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
