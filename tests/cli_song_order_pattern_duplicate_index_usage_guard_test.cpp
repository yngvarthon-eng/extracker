#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int countOccurrences(const std::string& haystack, const std::string& needle) {
  int count = 0;
  std::size_t pos = 0;
  while (true) {
    pos = haystack.find(needle, pos);
    if (pos == std::string::npos) {
      break;
    }
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  const std::string command =
      "printf 'pattern duplicate 0\\n"
      "pattern duplicate -1\\n"
      "pattern duplicate 999\\n"
      "pattern duplicate nope\\n"
      "pattern duplicate 1 extra\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI duplicate-index usage-guard command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI duplicate-index usage-guard command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const int invalidIndexCount = countOccurrences(output, "Invalid pattern index");
  const int usageCount = countOccurrences(output, "pattern duplicate [index]: duplicate current or selected pattern (1-indexed)");

  if (invalidIndexCount != 4 || usageCount != 1) {
    std::cerr << "Missing expected duplicate-index usage-guard output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
