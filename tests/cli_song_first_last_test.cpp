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
      "pattern insert after\\n"
      "song append 2\\n"
      "song append 3\\n"
      "song goto 2\\n"
      "song f\\n"
      "song l\\n"
      "song first extra\\n"
      "song last extra\\n"
      "song f extra\\n"
      "song l extra\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song first/last command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song first/last command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawFirstMove = containsAny(output, {"Moved to song entry 1 (pattern 1)"});
  const bool sawLastMove = containsAny(output, {"Moved to song entry 5 (pattern 3)"});
  const bool sawFirstUsage = containsAny(output, {"song first: jump to first song entry"});
  const bool sawLastUsage = containsAny(output, {"song last: jump to last song entry"});
  const bool sawFirstAliasUsage = containsAny(output, {"song f: alias for song first"});
  const bool sawLastAliasUsage = containsAny(output, {"song l: alias for song last"});

  if (!sawFirstMove || !sawLastMove || !sawFirstUsage || !sawLastUsage ||
      !sawFirstAliasUsage || !sawLastAliasUsage) {
    std::cerr << "Missing expected song first/last output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
