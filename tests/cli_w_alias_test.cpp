#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <cstdlib>

#include "test_string_utils.hpp"

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  // w with no filename should trigger the save usage/default path logic
  const std::string command =
      "printf 'w\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI w alias command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI w alias command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  // save with no args produces either a save confirmation or a usage message
  const bool sawSave = containsAny(output, {"Saved", "save", "Usage"});

  if (!sawSave) {
    std::cerr << "Missing expected w alias output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
