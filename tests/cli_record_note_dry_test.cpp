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
      "record cursor 26\\n"
      "record note dry 71 vel 90 fx 4 44\\n"
      "pattern print 26 26\\n"
      "record note 71 vel 90 fx 4 44\\n"
      "pattern print 26 26\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record note dry command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record note dry command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDryRun = output.find("Record dry-run: row 26, channel 0, note 71, instr 0, vel 90, fx 4:44") != std::string::npos;
  const bool sawBeforeEmpty = output.find("Row 26: [--] [--] [--] [--] [--] [--] [--] [--]") != std::string::npos;
  const bool sawWrite = output.find("Recorded note at row 26, channel 0") != std::string::npos;
  const bool sawAfterWrite = output.find("Row 26: [71:i0:v90:f4:44]") != std::string::npos;

  if (!sawDryRun || !sawBeforeEmpty || !sawWrite || !sawAfterWrite) {
    std::cerr << "Missing expected record note dry-run output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
