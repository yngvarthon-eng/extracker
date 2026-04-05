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
      "printf 'note clear x 0\n"
      "note vel 1 x 90\n"
      "note gate dry x 0 10\n"
      "note fx 3 0 y 5\n"
      "note set 1 0 z 1\n"
      "quit\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note strict numeric parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note strict numeric parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawClearUsage = output.find("Usage: note clear [dry] <row> <ch>") != std::string::npos;
  const bool sawVelUsage = output.find("Usage: note vel [dry] <row> <ch> <vel>") != std::string::npos;
  const bool sawGateUsage = output.find("Usage: note gate [dry] <row> <ch> <ticks>") != std::string::npos;
  const bool sawFxUsage = output.find("Usage: note fx [dry] <row> <ch> <fx> <fxval>") != std::string::npos;
  const bool sawSetUsage = output.find("Usage: note set [dry] <row> <ch> <midi> <instr> [vel] [fx] [fxval]") != std::string::npos;

  if (!sawClearUsage || !sawVelUsage || !sawGateUsage || !sawFxUsage || !sawSetUsage) {
    std::cerr << "Missing expected note strict numeric parse usage markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
