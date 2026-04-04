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
      "printf 'tempo 140\\n"
      "tempo 0\\n"
      "tempo -10\\n"
      "tempo nope\\n"
      "tempo\\n"
      "save\\n"
      "load\\n"
      "quit\\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI tempo/save/load guard command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI tempo/save/load guard command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawTempoSet   = output.find("Tempo set to") != std::string::npos &&
                              output.find("BPM") != std::string::npos;
  const bool sawTempoUsage = output.find("Usage: tempo <positive_bpm>") != std::string::npos;
  const bool sawSaveUsage  = output.find("Usage: save <file>") != std::string::npos;
  const bool sawLoadUsage  = output.find("Usage: load <file>") != std::string::npos;

  if (!sawTempoSet || !sawTempoUsage || !sawSaveUsage || !sawLoadUsage) {
    std::cerr << "Missing expected tempo/save/load output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
