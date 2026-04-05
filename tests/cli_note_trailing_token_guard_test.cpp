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
      "printf 'note set 1 0 60 1 extra\\n"
      "note clear 1 0 extra\\n"
      "note vel 1 0 96 extra\\n"
      "note gate 1 0 24 extra\\n"
      "note fx 1 0 1 64 extra\\n"
      "quit\\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note trailing-token guard command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note trailing-token guard command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSetUsage = output.find("Usage: note set [dry] <row> <ch> <midi> <instr> [vel] [fx] [fxval]") != std::string::npos;
  const bool sawClearUsage = output.find("Usage: note clear [dry] <row> <ch>") != std::string::npos;
  const bool sawVelUsage = output.find("Usage: note vel [dry] <row> <ch> <vel>") != std::string::npos;
  const bool sawGateUsage = output.find("Usage: note gate [dry] <row> <ch> <ticks>") != std::string::npos;
  const bool sawFxUsage = output.find("Usage: note fx [dry] <row> <ch> <fx> <fxval>") != std::string::npos;

  if (!sawSetUsage || !sawClearUsage || !sawVelUsage || !sawGateUsage || !sawFxUsage) {
    std::cerr << "Missing expected note trailing-token usage guard markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
