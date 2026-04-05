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

  const std::string command = "printf 'plugin list\nquit\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI plugin list command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI plugin list command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawHeader = output.find("Discovered plugins:") != std::string::npos;
  const bool sawSine = output.find("  builtin.sine") != std::string::npos;
  const bool sawSquare = output.find("  builtin.square") != std::string::npos;

  if (!sawHeader || !sawSine || !sawSquare) {
    std::cerr << "Missing expected plugin list output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
