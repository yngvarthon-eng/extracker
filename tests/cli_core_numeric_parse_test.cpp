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
      "printf 'tempo 120foo\n"
      "tempo 130\n"
      "loop range 4 5x\n"
      "loop range 8 4\n"
      "quit\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core numeric parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core numeric parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawTempoUsage = output.find("Usage: tempo <positive_bpm>") != std::string::npos;
  const bool sawTempoSet = output.find("Tempo set to 130 BPM") != std::string::npos;
  const bool sawLoopRangeUsage = output.find("Usage: loop range <from> <to>") != std::string::npos;
  const bool sawLoopRangeSet = output.find("Loop/play range set to 4..8") != std::string::npos;

  if (!sawTempoUsage || !sawTempoSet || !sawLoopRangeUsage || !sawLoopRangeSet) {
    std::cerr << "Missing expected core numeric parsing output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
