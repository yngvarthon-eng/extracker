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
      "printf 'plugin assign 1x builtin.sine\n"
      "plugin list extra\n"
      "plugin load builtin.sine\n"
      "plugin assign 1 builtin.sine\n"
      "plugin status extra\n"
      "plugin get 1x 0\n"
      "plugin info builtin.sine extra\n"
      "sine 2z\n"
      "quit\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI plugin numeric parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI plugin numeric parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawAssignUsage = output.find("Usage: plugin assign <instrument> <id>") != std::string::npos;
  const bool sawListUsage = output.find("Usage: plugin list") != std::string::npos;
  const bool sawAssignOk = output.find("Assigned builtin.sine to instrument 1") != std::string::npos;
  const bool sawStatusUsage = output.find("Usage: plugin status") != std::string::npos;
  const bool sawGetUsage = output.find("Usage: plugin get <instrument> <control-port-index|symbol>") != std::string::npos;
  const bool sawInfoUsage = output.find("Usage: plugin info <id>") != std::string::npos;
  const bool sawSineUsage = output.find("Usage: sine <instrument>") != std::string::npos;

  if (!sawAssignUsage || !sawListUsage || !sawAssignOk || !sawStatusUsage ||
      !sawGetUsage || !sawInfoUsage || !sawSineUsage) {
    std::cerr << "Missing expected plugin numeric parsing output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
