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

  // Use definitely invalid paths under /tmp to trigger save/load failure messages.
  const std::string invalidSavePath = "/tmp/extracker_missing_parent_dir/never_written.xtp";
  const std::string invalidLoadPath = "/tmp/extracker_missing_file/does_not_exist.xtp";

  const std::string command =
      "printf 'save " + invalidSavePath + "\\n"
      "load " + invalidLoadPath + "\\n"
      "quit\\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI save/load failure command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI save/load failure command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSaveFailure =
      output.find("Failed to save module to " + invalidSavePath) != std::string::npos;
  const bool sawLoadFailure =
      output.find("Failed to load module from " + invalidLoadPath) != std::string::npos;

  if (!sawSaveFailure || !sawLoadFailure) {
    std::cerr << "Missing expected save/load failure output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
