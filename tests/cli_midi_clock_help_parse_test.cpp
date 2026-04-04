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
      "printf 'midi clock help extra\n"
      "midi clock help\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi clock help parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi clock help parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawUsage =
      output.find("Usage: midi clock <help|quick [name]|sources [name]|autoconnect [name] [index]|diagnose [name]|diagnose live [name]>") !=
      std::string::npos;
  const bool sawHelpHeader = output.find("External MIDI clock setup (ALSA):") != std::string::npos;
  const bool sawQuickHint = output.find("Compact status: midi clock quick [name]") != std::string::npos;

  if (!sawUsage || !sawHelpHeader || !sawQuickHint) {
    std::cerr << "Missing expected MIDI clock help output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
