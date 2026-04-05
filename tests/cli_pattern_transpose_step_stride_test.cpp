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
      "note set 1 0 61 1\\n"
      "note set 2 0 62 1\\n"
      "note set 3 0 63 1\\n"
      "note set 4 0 64 1\\n"
      "pattern transpose 12 0 4 step 2\\n"
      "pattern print 0 4\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern transpose stride command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern transpose stride command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSummary =
      output.find("Transposed 3 step(s) by 12 semitones in rows 0..4 (all channels, 0 clamped) [step 2]") !=
      std::string::npos;
  const bool sawRow0 = output.find("Row 0: [72:i1:v100:f0:0]") != std::string::npos;
  const bool sawRow1 = output.find("Row 1: [61:i1:v100:f0:0]") != std::string::npos;
  const bool sawRow2 = output.find("Row 2: [74:i1:v100:f0:0]") != std::string::npos;
  const bool sawRow3 = output.find("Row 3: [63:i1:v100:f0:0]") != std::string::npos;
  const bool sawRow4 = output.find("Row 4: [76:i1:v100:f0:0]") != std::string::npos;

  if (!sawSummary || !sawRow0 || !sawRow1 || !sawRow2 || !sawRow3 || !sawRow4) {
    std::cerr << "Missing expected pattern transpose stride output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
