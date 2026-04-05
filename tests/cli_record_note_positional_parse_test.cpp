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
      "printf 'record quantize off\n"
      "record on 0\n"
      "record cursor 40\n"
      "record note 70 3\n"
      "pattern print 40 40\n"
      "record cursor 41\n"
      "record note 71 4 88\n"
      "pattern print 41 41\n"
      "record cursor 42\n"
      "record note 72 5 77 9 11\n"
      "pattern print 42 42\n"
      "record note 73 6x\n"
      "quit\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record note positional parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record note positional parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawInstrOnly = output.find("Row 40: [70:i3:v100:f0:0]") != std::string::npos;
  const bool sawInstrVel = output.find("Row 41: [71:i4:v88:f0:0]") != std::string::npos;
  const bool sawInstrVelFx = output.find("Row 42: [72:i5:v77:f9:11]") != std::string::npos;
  const bool sawUsage = output.find("Usage: record note [dry] <midi> [instr] [vel] [fx] [fxval]") != std::string::npos;

  if (!sawInstrOnly || !sawInstrVel || !sawInstrVelFx || !sawUsage) {
    std::cerr << "Missing expected record note positional parse output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
