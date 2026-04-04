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
      "printf 'plugin load\\n"
      "plugin load no.such.plugin\\n"
      "plugin assign 0 no.such.plugin\\n"
      "quit\\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI plugin load/assign failure command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI plugin load/assign failure command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawLoadUsage   = output.find("Usage: plugin load <id>") != std::string::npos;
  const bool sawLoadFail    = output.find("Failed to load plugin: no.such.plugin") != std::string::npos;
  const bool sawAssignFail  = output.find("Failed to assign plugin; ensure it is loaded and instrument index is valid") != std::string::npos;

  if (!sawLoadUsage || !sawLoadFail || !sawAssignFail) {
    std::cerr << "Missing expected plugin load/assign failure output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
