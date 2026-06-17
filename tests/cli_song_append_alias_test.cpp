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
      "printf 'song ap 1\\n"
      "song ap 1 extra\\n"
      "song ap\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song append alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song append alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawAppend = containsAny(output, {"Appended pattern 1 to song order"});
  const bool sawAliasUsage = containsAny(output, {"song ap <pattern>: alias for song append"});

  if (!sawAppend || !sawAliasUsage) {
    std::cerr << "Missing expected song append alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
