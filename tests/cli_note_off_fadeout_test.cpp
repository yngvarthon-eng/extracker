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
      "printf 'note set 18 2 64 3 95 1 2\n"
      "note off dry 18 2 7\n"
      "note off 18 2 7\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note off fadeout command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note off fadeout command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSet = output.find("Note set at row 18, channel 2") != std::string::npos;
  const bool sawDry = output.find("Note off dry-run: row 18, channel 2, fadeout ticks 7") != std::string::npos;
  const bool sawWrite = output.find("Note off set at row 18, channel 2, fadeout ticks 7") != std::string::npos;

  if (!sawSet || !sawDry || !sawWrite) {
    std::cerr << "Missing expected note off fadeout output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
