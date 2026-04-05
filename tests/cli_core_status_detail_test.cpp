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
      "printf 'record on 3\\n"
      "record cursor 12\\n"
      "status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core status detail command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core status detail command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawRecordEnabled = output.find("Record enabled: yes") != std::string::npos;
  const bool sawRecordChannel = output.find("Record channel: 3") != std::string::npos;
  const bool sawRecordCursor = output.find("Record cursor row: 12") != std::string::npos;
  const bool sawRecordUndo = output.find("Record undo available: no") != std::string::npos;
  const bool sawRecordRedo = output.find("Record redo available: no") != std::string::npos;
  const bool sawTransportSource = output.find("Transport source: internal") != std::string::npos;
  const bool sawClockActive = output.find("MIDI clock active: no") != std::string::npos;
  const bool sawClockBpmNa = output.find("MIDI clock BPM (estimated): n/a") != std::string::npos;

  if (!sawRecordEnabled || !sawRecordChannel || !sawRecordCursor ||
      !sawRecordUndo || !sawRecordRedo || !sawTransportSource ||
      !sawClockActive || !sawClockBpmNa) {
    std::cerr << "Missing expected core status detail output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
