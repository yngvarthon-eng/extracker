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
      "printf 'midi clock autoconnect\n"
      "midi clock autoconnect clock 1foo\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi clock autoconnect command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi clock autoconnect command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string notRunning = "MIDI input is not running. Start it with: midi on";
  const std::size_t first = output.find(notRunning);
  const std::size_t second = (first == std::string::npos) ? std::string::npos : output.find(notRunning, first + 1);

  if (first == std::string::npos || second == std::string::npos) {
    std::cerr << "Expected not-running autoconnect guard output for both commands" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
