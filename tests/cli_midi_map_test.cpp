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
      "printf 'midi map status\\n"
      "midi map status extra\\n"
      "midi map -1 1\n"
      "midi map 16 1\n"
      "midi map 2 6\\n"
      "midi map 6 -2\n"
      "midi map 2foo 1\n"
      "midi map 2 6foo\n"
      "midi map 3 999\n"
      "midi map status\\n"
      "midi map 2 6 extra\\n"
      "midi map 2 clear\\n"
      "midi map 2 clear extra\\n"
      "midi map status\\n"
      "midi map 4 9\\n"
      "midi map 5 10\\n"
      "midi map clear all extra\\n"
      "midi map clear all\\n"
      "midi map status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi map command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi map command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawInitialEmpty = output.find("MIDI channel map: (empty)") != std::string::npos;
    const std::string rangeError = "MIDI channel must be in range 0..15";
    const std::size_t firstRangeError = output.find(rangeError);
    const std::size_t secondRangeError =
      (firstRangeError == std::string::npos) ? std::string::npos : output.find(rangeError, firstRangeError + 1);
    const bool sawRangeErrorTwice =
      firstRangeError != std::string::npos && secondRangeError != std::string::npos;
  const bool sawSetMapping = output.find("Mapped MIDI channel 2 to instrument 6") != std::string::npos;
      const bool sawClampMapping = output.find("Mapped MIDI channel 3 to instrument 255") != std::string::npos;
    const bool sawClampLowMapping = output.find("Mapped MIDI channel 6 to instrument 0") != std::string::npos;
    const std::string mapUsage = "Usage: midi map <channel> <instr|clear>";
    const bool sawMapUsage = output.find(mapUsage) != std::string::npos;
    std::size_t mapUsageCount = 0;
    for (std::size_t pos = output.find(mapUsage); pos != std::string::npos;
         pos = output.find(mapUsage, pos + mapUsage.size())) {
      ++mapUsageCount;
    }
  const bool sawStatusMapped = output.find("MIDI channel map: ch2->6") != std::string::npos;
      const bool sawStatusClampMapped = output.find("ch3->255") != std::string::npos;
    const bool sawStatusClampLowMapped = output.find("ch6->0") != std::string::npos;
  const bool sawChannelClear = output.find("Cleared MIDI mapping for channel 2") != std::string::npos;
  const bool sawMapClearUsage = output.find("Usage: midi map clear all") != std::string::npos;
  const bool sawGlobalClear = output.find("Cleared all MIDI channel mappings") != std::string::npos;

      if (!sawInitialEmpty || !sawRangeErrorTwice || !sawSetMapping || !sawClampMapping ||
      !sawClampLowMapping || !sawMapUsage || !sawStatusMapped || !sawStatusClampMapped ||
          !sawStatusClampLowMapped || mapUsageCount < 5 ||
      !sawChannelClear || !sawMapClearUsage || !sawGlobalClear) {
    std::cerr << "Missing expected MIDI map output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
