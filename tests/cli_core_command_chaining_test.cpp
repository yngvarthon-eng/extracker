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
      "printf 'tempo 141; loop on; loop off\n"
      "tempo 142; quit; tempo 160\n"
      "status\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI command chaining command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI command chaining command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawTempo141 = output.find("Tempo set to 141 BPM") != std::string::npos;
  const bool sawTempo142 = output.find("Tempo set to 142 BPM") != std::string::npos;
  const bool sawLoopEnabled = output.find("Loop enabled") != std::string::npos;
  const bool sawLoopDisabled = output.find("Loop disabled") != std::string::npos;
  const bool sawTempo160 = output.find("Tempo set to 160 BPM") != std::string::npos;

  if (!sawTempo141 || !sawTempo142 || !sawLoopEnabled || !sawLoopDisabled || sawTempo160) {
    std::cerr << "Missing expected command chaining output markers or quit did not stop chain" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
