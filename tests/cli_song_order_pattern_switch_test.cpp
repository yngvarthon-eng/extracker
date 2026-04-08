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
      "printf 'pattern insert after\n"
      "song set 1 2\n"
      "song insert 2 1\n"
      "song status\n"
      "quit\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song pattern-switch command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song pattern-switch command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawInsertPattern = output.find("Inserted pattern after current") != std::string::npos;
  const bool sawSet = output.find("Set song entry 1 to pattern 2") != std::string::npos;
  const bool sawInsertSong = output.find("Inserted pattern 1 at song entry 2") != std::string::npos;
  const bool sawOrder = output.find("Order:") != std::string::npos;

  if (!sawInsertPattern || !sawSet || !sawInsertSong || !sawOrder) {
    std::cerr << "Missing expected song pattern-switch output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
