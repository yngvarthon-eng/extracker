#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  const std::string command =
      "printf 'loop range 2 6\\n"
      "status\\n"
      "loop clear\\n"
      "status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI loop clear command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI loop clear command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawRangeSet = output.find("Loop/play range set to 2..6") != std::string::npos;
  const bool sawRangeClear = output.find("Loop/play range cleared") != std::string::npos;
  const bool sawStatusRange = output.find("Play range: 2..6") != std::string::npos;
  const bool sawFullPattern = output.find("Play range: (full pattern)") != std::string::npos;

  if (!sawRangeSet || !sawRangeClear || !sawStatusRange || !sawFullPattern) {
    std::cerr << "Missing expected loop clear output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
