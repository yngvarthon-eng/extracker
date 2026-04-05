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
      "printf 'record on 2 extra\n"
      "record note 60\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record on extra-token command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record on extra-token command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawOnUsage = output.find("Usage: record on [channel]") != std::string::npos;
  const bool sawNotEnabled = output.find("Record is not enabled. Use: record on [channel]") != std::string::npos;

  if (!sawOnUsage || !sawNotEnabled) {
    std::cerr << "Missing expected record on extra-token output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
