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

  // Instrument slot 9 has no plugin assigned; set and get must report failure
  const std::string command =
      "printf 'plugin set 9 0 1.0\\n"
      "plugin get 9 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI plugin set/get no-assignment command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI plugin set/get no-assignment command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSetFail = output.find("Failed to set plugin control; instrument 9 has no assigned plugin") != std::string::npos;
  const bool sawGetFail = output.find("Failed to get plugin control; instrument 9 has no assigned plugin") != std::string::npos;

  if (!sawSetFail || !sawGetFail) {
    std::cerr << "Missing expected plugin set/get no-assignment failure output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
