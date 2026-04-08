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
      "printf 'pattern insert after\n"
      "song set 1 2\n"
      "song play status\n"
      "song play song\n"
      "song goto 1\n"
      "song play pattern\n"
      "song status\n"
      "quit\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song play mode command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song play mode command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawPlayStatus = output.find("Playback mode:") != std::string::npos;
  const bool sawSongMode = output.find("Playback mode set to song") != std::string::npos;
  const bool sawGoto = output.find("Jumped to song entry 1 (pattern 2)") != std::string::npos;
  const bool sawPatternMode = output.find("Playback mode set to pattern") != std::string::npos;

  if (!sawPlayStatus || !sawSongMode || !sawGoto || !sawPatternMode) {
    std::cerr << "Missing expected song play mode output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
