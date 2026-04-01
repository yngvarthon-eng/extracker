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
      "printf 'midi instrument 5\\n"
      "record quantize off\\n"
      "record on 0\\n"
      "record cursor 14\\n"
      "record note 62\\n"
      "pattern print 14 14\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record note default instrument command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record note default instrument command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawMidiInstrSet = output.find("MIDI instrument set to 5") != std::string::npos;
  const bool sawRecordWrite = output.find("Recorded note at row 14, channel 0") != std::string::npos;
  const bool sawPatternInstr = output.find("Row 14: [62:i5:v100:f0:0]") != std::string::npos;

  if (!sawMidiInstrSet || !sawRecordWrite || !sawPatternInstr) {
    std::cerr << "Missing expected record note default instrument output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
