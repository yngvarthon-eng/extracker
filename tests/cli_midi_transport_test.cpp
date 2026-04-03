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
      "printf 'midi transport status\\n"
      "midi transport timeout 2750\\n"
      "midi transport timeout status\\n"
      "midi transport lock off\\n"
      "midi transport lock status\\n"
      "midi transport reset\\n"
      "midi transport status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi transport command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi transport command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawInitialStatus = output.find("MIDI transport sync: off") != std::string::npos;
  const bool sawTimeoutSet = output.find("MIDI clock timeout set to 2750 ms") != std::string::npos;
  const bool sawTimeoutStatus = output.find("MIDI clock timeout ms: 2750") != std::string::npos;
  const bool sawLockOff = output.find("MIDI fallback tempo lock disabled") != std::string::npos;
  const bool sawLockStatus = output.find("MIDI fallback tempo lock: off") != std::string::npos;
  const bool sawReset = output.find("MIDI transport state reset") != std::string::npos;

  if (!sawInitialStatus || !sawTimeoutSet || !sawTimeoutStatus ||
      !sawLockOff || !sawLockStatus || !sawReset) {
    std::cerr << "Missing expected MIDI transport output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
