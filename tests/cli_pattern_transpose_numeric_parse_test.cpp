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
      "printf 'pattern transpose\\n"
      "pattern transpose 2x\\n"
      "pattern transpose 2 4x\\n"
      "pattern transpose 2 4 8 1x\\n"
      "pattern transpose 2 8 4 0\\n"
      "pattern transpose 2 0 3 step 0\\n"
      "pattern transpose 2 0 1 99\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern transpose numeric parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern transpose numeric parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string usage = "Usage: pattern transpose [dry [preview [verbose]]] <semitones> [from] [to] [ch] [step <n>]";
  const bool sawUsage = output.find(usage) != std::string::npos;
  const bool sawValidTranspose =
      output.find("Transposed 0 step(s) by 2 semitones in rows 4..8 (channel 0, 0 clamped)") !=
      std::string::npos;
  const bool sawStepRange = output.find("Step must be >= 1") != std::string::npos;
  const bool sawChannelRange = output.find("Channel out of range: 99 (valid 0..7)") != std::string::npos;

  if (!sawUsage || !sawValidTranspose || !sawStepRange || !sawChannelRange) {
    std::cerr << "Missing expected pattern transpose numeric parse output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
