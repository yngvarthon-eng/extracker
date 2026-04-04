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
      "printf 'midi clock quick\n"
      "midi clock quick\tClock\n"
      "quit\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi clock quick command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi clock quick command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawHeader = output.find("MIDI clock quick:") != std::string::npos;
  const bool sawRunning = output.find("running: no") != std::string::npos;
  const bool sawEndpoint = output.find("endpoint:") != std::string::npos;
  const bool sawSourceMatches = output.find("source matches:") != std::string::npos;
  std::size_t headerCount = 0;
  std::size_t pos = output.find("MIDI clock quick:");
  while (pos != std::string::npos) {
    ++headerCount;
    pos = output.find("MIDI clock quick:", pos + 1);
  }

  if (!sawHeader || !sawRunning || !sawEndpoint || !sawSourceMatches || headerCount < 2) {
    std::cerr << "Missing expected MIDI clock quick output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
