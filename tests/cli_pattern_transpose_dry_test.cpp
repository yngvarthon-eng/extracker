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
      "pattern transpose dry 12 0 0\\n"
      "pattern print 0 0\\n"
      "pattern transpose 12 0 0\\n"
      "pattern print 0 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern transpose dry command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern transpose dry command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDry =
      output.find("Transposed 1 step(s) by 12 semitones in rows 0..0 (all channels, 0 clamped) [dry-run]") !=
      std::string::npos;
  const bool sawCommitted =
      output.find("Transposed 1 step(s) by 12 semitones in rows 0..0 (all channels, 0 clamped)") !=
      std::string::npos;
  const bool sawRowBefore = output.find("Row 0: [60:i1:v100:f0:0]") != std::string::npos;
  const bool sawRowAfter = output.find("Row 0: [72:i1:v100:f0:0]") != std::string::npos;

  if (!sawDry || !sawCommitted || !sawRowBefore || !sawRowAfter) {
    std::cerr << "Missing expected pattern transpose dry output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
