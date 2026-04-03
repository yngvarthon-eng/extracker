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

  const std::string basePath = "/tmp/xt_cli_default_ext_test";
  const std::string expectedPath = basePath + ".xtp";

  const std::string command =
      "printf 'record on 0\\n"
      "record note 64 0 110\\n"
      "save " + basePath + "\\n"
      "load " + basePath + "\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI save/load default ext command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI save/load default ext command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSave = output.find("Module saved to " + expectedPath) != std::string::npos;
  const bool sawLoad = output.find("Module loaded from " + expectedPath) != std::string::npos;
  const bool savedFileExists = std::filesystem::exists(expectedPath);

  if (!sawSave || !sawLoad || !savedFileExists) {
    std::cerr << "Missing expected CLI output markers for default extension save/load" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  std::error_code removeError;
  std::filesystem::remove(expectedPath, removeError);

  return 0;
}
