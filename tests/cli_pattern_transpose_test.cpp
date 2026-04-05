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
      "printf 'pattern template blank\\n"
      "note set 0 0 60 1\\n"
      "note set 1 1 127 2\\n"
      "note set 2 0 0 3\\n"
      "pattern transpose 2 0 2\\n"
      "pattern print 0 2\\n"
      "pattern transpose -5 0 2 1\\n"
      "pattern print 1 1\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern transpose command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern transpose command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawTransposeAll =
      output.find("Transposed 3 step(s) by 2 semitones in rows 0..2 (all channels, 1 clamped)") !=
      std::string::npos;
  const bool sawTransposeChannel =
      output.find("Transposed 1 step(s) by -5 semitones in rows 0..2 (channel 1, 0 clamped)") !=
      std::string::npos;
  const bool sawRow0Raised = output.find("Row 0: [62:i1:v100:f0:0]") != std::string::npos;
  const bool sawRow2Raised = output.find("Row 2: [2:i3:v100:f0:0]") != std::string::npos;
  const bool sawRow1ChannelLowered = output.find("Row 1: [--] [122:i2:v100:f0:0]") != std::string::npos;

  if (!sawTransposeAll || !sawTransposeChannel || !sawRow0Raised || !sawRow2Raised ||
      !sawRow1ChannelLowered) {
    std::cerr << "Missing expected pattern transpose output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
