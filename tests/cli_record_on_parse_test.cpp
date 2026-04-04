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
      "printf 'record on 1x\n"
      "record note 60\n"
      "record on 2\n"
      "record channel status extra\n"
      "record note dry 60\n"
      "quit\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record on parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record on parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawOnUsage = output.find("Usage: record on [channel]") != std::string::npos;
  const bool sawNotEnabled = output.find("Record is not enabled. Use: record on [channel]") != std::string::npos;
  const bool sawOnEnabled = output.find("Record enabled on channel 2") != std::string::npos;
  const bool sawChannelUsage = output.find("Usage: record channel <0..7|status>") != std::string::npos;
  const bool sawDryRun = output.find("Record dry-run: row") != std::string::npos;

  if (!sawOnUsage || !sawNotEnabled || !sawOnEnabled || !sawChannelUsage || !sawDryRun) {
    std::cerr << "Missing expected record on parsing output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
