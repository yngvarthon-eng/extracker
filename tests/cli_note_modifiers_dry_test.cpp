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
      "printf 'note set 33 3 70 2 100 1 2\\n"
      "pattern print 33 33\\n"
      "note vel dry 33 3 45\\n"
      "note gate dry 33 3 8\\n"
      "note fx dry 33 3 12 77\\n"
      "pattern print 33 33\\n"
      "note vel 33 3 45\\n"
      "note gate 33 3 8\\n"
      "note fx 33 3 12 77\\n"
      "pattern print 33 33\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note modifiers dry command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note modifiers dry command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawInitialRow = output.find("Row 33: [--] [--] [--] [70:i2:v100:f1:2]") != std::string::npos;
  const bool sawDryVel = output.find("Note vel dry-run: row 33, channel 3, vel 45") != std::string::npos;
  const bool sawDryGate = output.find("Note gate dry-run: row 33, channel 3, ticks 8") != std::string::npos;
  const bool sawDryFx = output.find("Note fx dry-run: row 33, channel 3, fx 12:77") != std::string::npos;
  const bool sawStillInitial = output.find("Row 33: [--] [--] [--] [70:i2:v100:f1:2]") != std::string::npos;
  const bool sawUpdatedRow = output.find("Row 33: [--] [--] [--] [70:i2:v45:f12:77]") != std::string::npos;

  if (!sawInitialRow || !sawDryVel || !sawDryGate || !sawDryFx || !sawStillInitial || !sawUpdatedRow) {
    std::cerr << "Missing expected note modifier dry-run output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
