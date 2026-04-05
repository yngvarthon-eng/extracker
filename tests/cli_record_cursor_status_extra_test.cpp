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
      "printf 'record cursor status extra\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record cursor status extra command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record cursor status extra command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawUsage = output.find("Usage: record cursor <0..63|+n|-n|+ [count]|- [count]|start|end|next [count]|prev [count]|status>") != std::string::npos;

  if (!sawUsage) {
    std::cerr << "Missing expected record cursor status-extra usage marker" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
