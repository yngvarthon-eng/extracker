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
      "printf 'plugin status\n"
      "plugin load builtin.sine\n"
      "plugin assign 2 builtin.sine\n"
      "plugin status\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI plugin status command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI plugin status command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawHeader = output.find("Instrument assignments:") != std::string::npos;
  const bool sawBuiltin0 = output.find("  0: builtin.sine") != std::string::npos;
  const bool sawBuiltin1 = output.find("  1: builtin.square") != std::string::npos;
  const bool sawAssign = output.find("Assigned builtin.sine to instrument 2") != std::string::npos;
  const bool sawMapped = output.find("  2: builtin.sine") != std::string::npos;

  if (!sawHeader || !sawBuiltin0 || !sawBuiltin1 || !sawAssign || !sawMapped) {
    std::cerr << "Missing expected plugin status output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
