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
      "printf 'loop range 4 8\n"
      "reset\n"
      "status\n"
      "quit\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core reset state command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core reset state command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawRangeSet = output.find("Loop/play range set to 4..8") != std::string::npos;
  const bool sawReset = output.find("Playback state reset") != std::string::npos;
  const bool sawFullPattern = output.find("Play range: (full pattern)") != std::string::npos;
  const bool sawStaleRange = output.find("Play range: 4..8") != std::string::npos;

  if (!sawRangeSet || !sawReset || !sawFullPattern || sawStaleRange) {
    std::cerr << "Missing expected reset state markers or stale play range persisted" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
