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
      "printf 'midi instrument 12foo\\n"
      "midi instrument 64 extra\\n"
      "midi instrument 64\\n"
      "midi instrument -2\\n"
      "midi instrument 999\\n"
      "midi thru on extra\\n"
      "midi thru on\\n"
      "midi thru off\\n"
      "midi learn status extra\\n"
      "midi learn on extra\\n"
      "midi learn on\\n"
      "midi learn status\\n"
      "midi learn off\\n"
      "midi learn status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi instrument command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi instrument command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawUsage = output.find("Usage: midi instrument <index>") != std::string::npos;
  const bool sawSet64 = output.find("MIDI instrument set to 64") != std::string::npos;
  const bool sawClampLow = output.find("MIDI instrument set to 0") != std::string::npos;
  const bool sawClampHigh = output.find("MIDI instrument set to 255") != std::string::npos;
  const bool sawThruUsage = output.find("Usage: midi thru <on|off>") != std::string::npos;
  const bool sawThruEnabled = output.find("MIDI thru enabled") != std::string::npos;
  const bool sawThruDisabled = output.find("MIDI thru disabled") != std::string::npos;
  const bool sawLearnUsage = output.find("Usage: midi learn <on|off|status>") != std::string::npos;
  const bool sawLearnEnabled = output.find("MIDI learn enabled") != std::string::npos;
  const bool sawLearnDisabled = output.find("MIDI learn disabled") != std::string::npos;
  const bool sawLearnStatus = output.find("MIDI learn: on") != std::string::npos;
  const bool sawLearnStatusOff = output.find("MIDI learn: off") != std::string::npos;

  if (!sawUsage || !sawSet64 || !sawClampLow || !sawClampHigh ||
      !sawThruUsage || !sawThruEnabled || !sawThruDisabled || !sawLearnUsage ||
      !sawLearnEnabled || !sawLearnDisabled || !sawLearnStatus || !sawLearnStatusOff) {
    std::cerr << "Missing expected MIDI instrument output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
