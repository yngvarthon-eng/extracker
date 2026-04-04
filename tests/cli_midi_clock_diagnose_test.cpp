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
      "printf 'midi clock diagnose\n"
      "midi clock diagnose live\n"
      "midi clock diagnose exTracker\n"
      "midi clock diagnose live exTracker\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi clock diagnose command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi clock diagnose command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDiagnosticsHeader = output.find("MIDI clock diagnostics:") != std::string::npos;
  const bool sawLiveProbe = output.find("Mode: live health probe") != std::string::npos;
  const bool sawSourceFilter = output.find("Source filter:") != std::string::npos;
  const bool sawMidiInputRunning = output.find("MIDI input running:") != std::string::npos;

  if (!sawDiagnosticsHeader || !sawLiveProbe || !sawSourceFilter || !sawMidiInputRunning) {
    std::cerr << "Missing expected MIDI clock diagnose output markers" << '\n';
    std::cerr << "  sawDiagnosticsHeader: " << sawDiagnosticsHeader << '\n';
    std::cerr << "  sawLiveProbe: " << sawLiveProbe << '\n';
    std::cerr << "  sawSourceFilter: " << sawSourceFilter << '\n';
    std::cerr << "  sawMidiInputRunning: " << sawMidiInputRunning << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
