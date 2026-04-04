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
      "printf 'record jump 3/2\\n"
      "record jump status\\n"
      "record jump 8\\n"
      "record jump status\\n"
      "record jump 0\\n"
      "record jump 999\\n"
      "record jump nope\\n"
      "record jump 3/0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI jump command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI jump command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawRatioConversion = output.find("Record jump 3/2 -> 2") != std::string::npos;
  const bool sawRatioStatus = output.find("Record jump: 2") != std::string::npos;
  const bool sawIntegerConversion = output.find("Record jump 8 -> 8") != std::string::npos;
  const bool sawIntegerStatus = output.find("Record jump: 8") != std::string::npos;
  const bool sawJumpUsage = output.find("Usage: record jump <1..") != std::string::npos;

  if (!sawRatioConversion || !sawRatioStatus || !sawIntegerConversion || !sawIntegerStatus || !sawJumpUsage) {
    std::cerr << "Missing expected record jump output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
