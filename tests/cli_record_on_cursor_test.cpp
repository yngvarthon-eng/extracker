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
      "record cursor 12\\n"
      "record on 0\\n"
      "record note 61 1 100\\n"
      "pattern print 12 12\\n"
      "record quantize on\\n"
      "record cursor 30\\n"
      "record on 0\\n"
      "record cursor status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record-on cursor command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record-on cursor command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawQuantizeOffArm = output.find("Record enabled on channel 0 from row 12") != std::string::npos;
  const bool sawWriteAtPreservedCursor = output.find("Recorded note at row 12, channel 0") != std::string::npos;
  const bool sawPatternAt12 = output.find("Row 12: [61:i1:v100:f0:0]") != std::string::npos;
  const bool sawQuantizeOnArm = output.find("Record enabled on channel 0 from row 0") != std::string::npos;
  const bool sawCursorReset = output.find("Record cursor row: 0") != std::string::npos;

  if (!sawQuantizeOffArm || !sawWriteAtPreservedCursor || !sawPatternAt12 || !sawQuantizeOnArm || !sawCursorReset) {
    std::cerr << "Missing expected record-on cursor behavior markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
