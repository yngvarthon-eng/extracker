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
      "midi transport quick\\n"
      "midi transport quick extra\\n"
      "midi transport on extra\\n"
      "midi transport toggle\\n"
      "midi transport status\\n"
      "midi transport status extra\\n"
      "midi transport toggle\\n"
      "midi transport status\\n"
      "midi transport timeout 2750ms\\n"
      "midi transport timeout 50\\n"
      "midi transport timeout 100\\n"
      "midi transport timeout 10000\\n"
      "midi transport timeout 10001\\n"
      "midi transport timeout 2750\\n"
      "midi transport timeout 2750 extra\\n"
      "midi transport timeout\\n"
      "midi transport timeout status extra\\n"
      "midi transport lock off\\n"
      "midi transport lock\\n"
      "midi transport lock on extra\\n"
      "midi transport reset\\n"
      "midi transport reset extra\\n"
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
  const bool sawQuickHeader = output.find("MIDI transport quick:") != std::string::npos;
  const bool sawQuickSource = output.find("source: internal") != std::string::npos;
  const bool sawToggleOn = output.find("MIDI transport sync enabled") != std::string::npos;
  const bool sawToggleOff = output.find("MIDI transport sync disabled") != std::string::npos;
  const bool sawStatusOn = output.find("MIDI transport sync: on") != std::string::npos;
  const std::string timeoutUsage = "Usage: midi transport timeout <100..10000|status>";
  const bool sawMalformedTimeoutUsage = output.find(timeoutUsage) != std::string::npos;
  std::size_t timeoutUsageCount = 0;
  for (std::size_t pos = output.find(timeoutUsage); pos != std::string::npos;
       pos = output.find(timeoutUsage, pos + timeoutUsage.size())) {
    ++timeoutUsageCount;
  }
    const bool sawTransportUsage =
      output.find("Usage: midi transport <on|off|toggle|status|quick|timeout|lock|reset>") != std::string::npos;
  const bool sawTimeoutSetMin = output.find("MIDI clock timeout set to 100 ms") != std::string::npos;
  const bool sawTimeoutSetMax = output.find("MIDI clock timeout set to 10000 ms") != std::string::npos;
  const bool sawTimeoutSet = output.find("MIDI clock timeout set to 2750 ms") != std::string::npos;
  const bool sawTimeoutStatus = output.find("MIDI clock timeout ms: 2750") != std::string::npos;
  const bool sawLockOff = output.find("MIDI fallback tempo lock disabled") != std::string::npos;
    const bool sawLockUsage =
      output.find("Usage: midi transport lock <on|off|status>") != std::string::npos;
  const bool sawLockStatus = output.find("MIDI fallback tempo lock: off") != std::string::npos;
  const bool sawReset = output.find("MIDI transport state reset") != std::string::npos;

      if (!sawInitialStatus || !sawQuickHeader || !sawQuickSource ||
        !sawToggleOn || !sawToggleOff || !sawStatusOn ||
      !sawMalformedTimeoutUsage || !sawTransportUsage || !sawTimeoutSetMin || !sawTimeoutSetMax ||
      timeoutUsageCount < 5 ||
      !sawTimeoutSet || !sawTimeoutStatus ||
      !sawLockOff || !sawLockUsage || !sawLockStatus || !sawReset) {
    std::cerr << "Missing expected MIDI transport output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
