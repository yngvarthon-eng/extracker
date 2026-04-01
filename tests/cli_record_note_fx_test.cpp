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
      "printf 'midi instrument 9\\n"
      "record quantize off\\n"
      "record on 0\\n"
      "record cursor 22\\n"
      "record note 67 fx 12 45\\n"
      "pattern print 22 22\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record note fx command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record note fx command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawPattern = output.find("Row 22: [67:i9:v100:f12:45]") != std::string::npos;
  if (!sawPattern) {
    std::cerr << "Missing expected record note fx output marker" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
