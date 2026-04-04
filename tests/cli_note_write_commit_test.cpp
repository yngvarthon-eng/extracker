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

  // Write velocity, gate ticks and effect to row 10 channel 0 (non-dry-run)
  const std::string command =
      "printf 'note vel 10 0 90\\n"
      "note gate 10 0 48\\n"
      "note fx 10 0 1 50\\n"
      "quit\\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note write command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note write command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawVelSet  = output.find("Velocity set at row 10, channel 0") != std::string::npos;
  const bool sawGateSet = output.find("Gate ticks set at row 10, channel 0") != std::string::npos;
  const bool sawFxSet   = output.find("Effect set at row 10, channel 0") != std::string::npos;

  if (!sawVelSet || !sawGateSet || !sawFxSet) {
    std::cerr << "Missing expected note write confirmation output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
