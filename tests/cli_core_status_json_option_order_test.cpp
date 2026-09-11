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
      "printf 'status --json --pretty --minimal\\n"
      "status --json --minimal --pretty\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core status json option-order command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core status json option-order command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool hasUsageError = containsAny(output, {"Usage: status"});
  const std::size_t prettyTransportCount = countOccurrences(output, "  \"transport\": {");
  const std::size_t patternNoteCount = countOccurrences(output, "\"patternNoteCount\": ");
  const std::size_t audioStatusCount = countOccurrences(output, "\"audioStatus\":");

  if (hasUsageError || prettyTransportCount < 2 || patternNoteCount < 2 || audioStatusCount != 0) {
    std::cerr << "Missing expected status json option-order output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
