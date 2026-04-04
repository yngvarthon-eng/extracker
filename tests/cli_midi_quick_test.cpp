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
      "printf 'midi quick\n"
      "midi quick nope\n"
      "midi transport on\n"
      "midi quick all\n"
      "midi quick all\tClock\n"
      "midi quick compact\n"
      "midi quick compact Clock\n"
      "quit\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi quick command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi quick command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawHeader = output.find("MIDI quick:") != std::string::npos;
  const bool sawQuickUsage = output.find("Usage: midi quick [all|compact [name]]") != std::string::npos;
  const bool sawAllHeader = output.find("MIDI quick all:") != std::string::npos;
  const bool sawAllClockFilter = output.find("source filter: 'Clock'") != std::string::npos;
  const bool sawCompactHeader = output.find("MIDI quick compact:") != std::string::npos;
  const bool sawCompactClockFilter = output.find("filter='Clock'") != std::string::npos;
  const bool sawTransportQuickHeader = output.find("MIDI transport quick:") != std::string::npos;
  const bool sawClockQuickHeader = output.find("MIDI clock quick:") != std::string::npos;
  const bool sawRunning = output.find("running: no") != std::string::npos;
  const bool sawTransportOn = output.find("transport sync: on") != std::string::npos;
  const bool sawClock = output.find("clock: none") != std::string::npos;
  const bool sawEndpoint = output.find("endpoint:") != std::string::npos;

  if (!sawHeader || !sawQuickUsage || !sawAllHeader || !sawAllClockFilter || !sawCompactHeader ||
      !sawTransportQuickHeader || !sawClockQuickHeader ||
      !sawRunning || !sawTransportOn || !sawClock || !sawEndpoint || !sawCompactClockFilter) {
    std::cerr << "Missing expected MIDI quick output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
