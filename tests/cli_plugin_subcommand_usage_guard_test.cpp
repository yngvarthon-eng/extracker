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
      "printf 'plugin scan extra\n"
      "plugin load builtin.sine extra\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI plugin subcommand usage guard command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI plugin subcommand usage guard command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawScanUsage = output.find("Usage: plugin scan") != std::string::npos;
  const bool sawLoadUsage = output.find("Usage: plugin load <id>") != std::string::npos;

  if (!sawScanUsage || !sawLoadUsage) {
    std::cerr << "Missing expected plugin subcommand usage guard markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
