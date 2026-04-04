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
      "printf 'midi status\n"
      "midi status extra\n"
      "midi off extra\n"
      "midi on extra\n"
      "midi off\n"
      "quit\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi basic parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi basic parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawStatus = output.find("MIDI backend:") != std::string::npos;
  const bool sawStopped = output.find("MIDI input stopped") != std::string::npos;
  const bool sawUsage =
      output.find("Usage: midi <on|off|status|quick|thru|instrument|learn|map|transport|clock> ...") !=
      std::string::npos;

  if (!sawStatus || !sawStopped || !sawUsage) {
    std::cerr << "Missing expected MIDI basic parse output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
