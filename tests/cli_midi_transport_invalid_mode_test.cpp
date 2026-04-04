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
      "printf 'midi transport timeout nope\n"
      "midi transport timeout -1\n"
      "midi transport timeout 0\n"
      "midi transport lock nope\n"
      "midi transport wobble\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi transport invalid-mode command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi transport invalid-mode command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string timeoutUsage = "Usage: midi transport timeout <100..10000|status>";
  const bool sawTimeoutUsage = output.find(timeoutUsage) != std::string::npos;
  std::size_t timeoutUsageCount = 0;
  for (std::size_t pos = output.find(timeoutUsage); pos != std::string::npos;
       pos = output.find(timeoutUsage, pos + timeoutUsage.size())) {
    ++timeoutUsageCount;
  }
  const bool sawLockUsage =
      output.find("Usage: midi transport lock <on|off|status>") != std::string::npos;
  const bool sawTransportUsage =
      output.find("Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>") != std::string::npos;

  if (!sawTimeoutUsage || timeoutUsageCount < 3 || !sawLockUsage || !sawTransportUsage) {
    std::cerr << "Missing expected transport invalid-mode usage output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
