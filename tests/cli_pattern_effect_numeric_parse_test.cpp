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
      "printf 'pattern effect\\n"
      "pattern effect 1x 2\\n"
      "pattern effect 1 2x\\n"
      "pattern effect dry preview verbose\\n"
      "pattern effect preview 1 2\\n"
      "pattern effect dry verbose 1 2\\n"
      "pattern effect 1 2 4x\\n"
      "pattern effect 1 2 8 4 0 1\\n"
      "pattern effect 1 2 8 4 0\\n"
      "pattern effect 1 2 0 1 99\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern effect numeric parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern effect numeric parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string usage = "Usage: pattern effect [dry [preview [verbose]]] <fx> <fxval> [from] [to] [ch] [step <n>]";
  const bool sawUsage = output.find(usage) != std::string::npos;
  const bool sawValidFill =
      output.find("Filled effect on 0 step(s) with 1:2 in rows 4..8 (channel 0, 0 input clamped)") !=
      std::string::npos;
  const bool sawChannelRange = output.find("Channel out of range: 99 (valid 0..7)") != std::string::npos;

  if (!sawUsage || !sawValidFill || !sawChannelRange) {
    std::cerr << "Missing expected pattern effect numeric parse output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
