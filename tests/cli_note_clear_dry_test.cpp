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
      "printf 'note set 31 1 72 3 100 2 9\\n"
      "pattern print 31 31\\n"
      "note clear dry 31 1\\n"
      "pattern print 31 31\\n"
      "note clear 31 1\\n"
      "pattern print 31 31\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note clear dry command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note clear dry command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawInitialNote = output.find("Row 31: [--] [72:i3:v100:f2:9]") != std::string::npos;
  const bool sawDryRun = output.find("Note clear dry-run: row 31, channel 1") != std::string::npos;
  const bool sawStillPresent = output.find("Row 31: [--] [72:i3:v100:f2:9]") != std::string::npos;
  const bool sawClear = output.find("Cleared row 31, channel 1") != std::string::npos;
  const bool sawClearedRow = output.find("Row 31: [--] [--] [--] [--] [--] [--] [--] [--]") != std::string::npos;

  if (!sawInitialNote || !sawDryRun || !sawStillPresent || !sawClear || !sawClearedRow) {
    std::cerr << "Missing expected note clear dry-run output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
