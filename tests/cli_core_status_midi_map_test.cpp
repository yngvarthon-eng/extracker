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
      "printf 'midi map 2 6\n"
      "status\n"
      "quit\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core status MIDI map command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core status MIDI map command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawMapSet = output.find("Mapped MIDI channel 2 to instrument 6") != std::string::npos;
  const bool sawStatusMap = output.find("MIDI channel map: ch2->6") != std::string::npos;

  if (!sawMapSet || !sawStatusMap) {
    std::cerr << "Missing expected core status MIDI map output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
