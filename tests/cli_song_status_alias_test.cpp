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
      "printf 'song st\\n"
      "song st extra\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song status alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song status alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawStatus = containsAny(output, {"Song: "});
  const bool sawOrder = containsAny(output, {"Order:"});
  const bool sawAliasUsage = containsAny(output, {"song st: alias for song status"});

  if (!sawStatus || !sawOrder || !sawAliasUsage) {
    std::cerr << "Missing expected song status alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
