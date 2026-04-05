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
      "note set 0 0 60 4 98 12 34\\n"
      "note set 0 1 127 2 110 1 2\\n"
      "pattern transpose dry preview verbose 2 0 0\\n"
      "pattern transpose preview verbose 2 0 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern transpose preview verbose command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern transpose preview verbose command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string usage = "Usage: pattern transpose [dry [preview [verbose]]] <semitones> [from] [to] [ch]";
  const bool sawDrySummary =
      output.find("Transposed 2 step(s) by 2 semitones in rows 0..0 (all channels, 1 clamped) [dry-run]") !=
      std::string::npos;
  const bool sawPreviewHeader = output.find("Transpose preview:") != std::string::npos;
  const bool sawVerboseLine1 = output.find("row 0 ch 0 60 -> 62 i4 v98 fx12:34") != std::string::npos;
  const bool sawVerboseLine2 =
      output.find("row 0 ch 1 127 -> 127 [clamped] i2 v110 fx1:2") != std::string::npos;
  const bool sawGuardUsage = output.find(usage) != std::string::npos;

  if (!sawDrySummary || !sawPreviewHeader || !sawVerboseLine1 || !sawVerboseLine2 || !sawGuardUsage) {
    std::cerr << "Missing expected pattern transpose preview verbose output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
