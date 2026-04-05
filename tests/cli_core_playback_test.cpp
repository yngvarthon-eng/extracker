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
      "printf 'play\\n"
      "play\\n"
      "stop\\n"
      "reset\\n"
      "status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core playback command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core playback command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawPlaybackStarted  = output.find("Playback started") != std::string::npos;
  const bool sawAlreadyRunning   = output.find("Playback already running") != std::string::npos;
  const bool sawPlaybackStopped  = output.find("Playback stopped") != std::string::npos;
  const bool sawPlaybackReset    = output.find("Playback state reset") != std::string::npos;
  const bool sawStatusPlaying    = output.find("Transport playing:") != std::string::npos;
  const bool sawStatusTempo      = output.find("Transport tempo:") != std::string::npos;
  const bool sawStatusLoop       = output.find("Loop enabled:") != std::string::npos;
  const bool sawStatusPlayRange  = output.find("Play range:") != std::string::npos;
  const bool sawRecordChannelLine = output.find("Record channel:") != std::string::npos;
  const bool sawRecordCursorLine  = output.find("Record cursor row:") != std::string::npos;

  if (!sawPlaybackStarted || !sawAlreadyRunning || !sawPlaybackStopped || !sawPlaybackReset ||
      !sawStatusPlaying || !sawStatusTempo || !sawStatusLoop || !sawStatusPlayRange ||
      sawRecordChannelLine || sawRecordCursorLine) {
    std::cerr << "Missing expected core playback output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
