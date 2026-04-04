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

  // 1. Set loop range, check status shows "Play range: 4..8"
  // 2. pattern play with loop off → "(loop off)"
  // 3. loop on, pattern play → "(loop on)"
  const std::string command =
      "printf 'loop range 4 8\\n"
      "status\\n"
      "loop off\\n"
      "pattern play 2 6\\n"
      "loop on\\n"
      "pattern play 2 6\\n"
      "quit\\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI loop range status command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI loop range status command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawRangeSet     = output.find("Loop/play range set to 4..8") != std::string::npos;
  const bool sawStatusRange  = output.find("Play range: 4..8") != std::string::npos;
  const bool sawLoopOff      = output.find("(loop off)") != std::string::npos;
  const bool sawLoopOn       = output.find("(loop on)") != std::string::npos;

  if (!sawRangeSet || !sawStatusRange || !sawLoopOff || !sawLoopOn) {
    std::cerr << "Missing expected loop range / pattern play loop output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
