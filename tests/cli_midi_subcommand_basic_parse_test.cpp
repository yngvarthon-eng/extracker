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
      "printf 'midi instrument\n"
      "midi thru\n"
      "midi thru nope\n"
      "midi learn\n"
      "midi learn nope\n"
      "midi map\n"
      "midi map nope\n"
      "midi map clear\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi subcommand basic parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi subcommand basic parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawInstrumentUsage = output.find("Usage: midi instrument <index>") != std::string::npos;
  const bool sawThruUsage = output.find("Usage: midi thru <on|off>") != std::string::npos;
  const bool sawLearnUsage = output.find("Usage: midi learn <on|off|status>") != std::string::npos;
  const bool sawMapStatus = output.find("MIDI channel map: (empty)") != std::string::npos;
  const bool sawMapUsage = output.find("Usage: midi map <channel> <instr|clear>") != std::string::npos;
  const bool sawMapClearUsage = output.find("Usage: midi map clear all") != std::string::npos;

  if (!sawInstrumentUsage || !sawThruUsage || !sawLearnUsage || !sawMapStatus || !sawMapUsage || !sawMapClearUsage) {
    std::cerr << "Missing expected MIDI subcommand usage output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
