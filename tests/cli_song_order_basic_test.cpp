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
      "printf 'song status\n"
      "song append 1\n"
      "song move 2 up\n"
      "song remove 1\n"
      "song status\n"
      "quit\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI song basic command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI song basic command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawStatus = output.find("Song: ") != std::string::npos;
  const bool sawOrder = output.find("Order:") != std::string::npos;
  const bool sawAppend = output.find("Appended pattern 1 to song order") != std::string::npos;
  const bool sawMove = output.find("Moved song entry 2 up") != std::string::npos;
  const bool sawRemove = output.find("Removed song entry 1") != std::string::npos;

  if (!sawStatus || !sawOrder || !sawAppend || !sawMove || !sawRemove) {
    std::cerr << "Missing expected song basic output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
