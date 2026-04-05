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
      "printf 'plugin load builtin.sine\n"
      "plugin assign 9 builtin.sine\n"
      "plugin set 9 gain 0.5\n"
      "plugin get 9 gain\n"
      "quit\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI plugin no-lv2-meta command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI plugin no-lv2-meta command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSetNoMeta = output.find("Failed to set plugin control; assigned plugin has no LV2 control metadata") != std::string::npos;
  const bool sawGetNoMeta = output.find("Failed to get plugin control; assigned plugin has no LV2 control metadata") != std::string::npos;

  if (!sawSetNoMeta || !sawGetNoMeta) {
    std::cerr << "Missing expected plugin no-LV2-metadata output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
