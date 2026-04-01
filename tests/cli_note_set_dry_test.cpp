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
      "printf 'note set dry 30 2 75 4 96 12 55\\n"
      "pattern print 30 30\\n"
      "note set 30 2 75 4 96 12 55\\n"
      "pattern print 30 30\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note set dry command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note set dry command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDryRun = output.find("Note set dry-run: row 30, channel 2, note 75, instr 4, vel 96, fx 12:55") != std::string::npos;
  const bool sawBeforeEmpty = output.find("Row 30: [--] [--] [--] [--] [--] [--] [--] [--]") != std::string::npos;
  const bool sawWrite = output.find("Note set at row 30, channel 2") != std::string::npos;
  const bool sawAfterWrite = output.find("Row 30: [--] [--] [75:i4:v96:f12:55]") != std::string::npos;

  if (!sawDryRun || !sawBeforeEmpty || !sawWrite || !sawAfterWrite) {
    std::cerr << "Missing expected note set dry-run output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
