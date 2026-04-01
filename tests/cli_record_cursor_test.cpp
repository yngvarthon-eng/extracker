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
      "printf 'record quantize off\\n"
      "record on 0\\n"
      "record cursor 12\\n"
      "record cursor status\\n"
      "record note 60 1 100\\n"
      "pattern print 12 12\\n"
      "record cursor 999\\n"
      "record cursor status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record cursor command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record cursor command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawCursorSet = output.find("Record cursor set to row 12") != std::string::npos;
  const bool sawCursorStatus = output.find("Record cursor row: 12") != std::string::npos;
  const bool sawRecordAtCursor = output.find("Recorded note at row 12, channel 0") != std::string::npos;
  const bool sawPatternRow = output.find("Row 12: [60:i1:v100:f0:0]") != std::string::npos;
  const bool sawCursorClampSet = output.find("Record cursor set to row 63") != std::string::npos;
  const bool sawCursorClampStatus = output.find("Record cursor row: 63") != std::string::npos;

  if (!sawCursorSet || !sawCursorStatus || !sawRecordAtCursor || !sawPatternRow || !sawCursorClampSet || !sawCursorClampStatus) {
    std::cerr << "Missing expected record cursor output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
