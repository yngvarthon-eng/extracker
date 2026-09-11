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
      "printf 'note set 5 1 60 2\\n"
      "status --json\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core status json command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core status json command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawJsonOpen = output.find("{") != std::string::npos;
  const bool sawPatternNoteCount = output.find("\"patternNoteCount\":1") != std::string::npos;
  const bool sawPatternActiveFrom = output.find("\"patternActiveRowFrom\":5") != std::string::npos;
  const bool sawPatternActiveTo = output.find("\"patternActiveRowTo\":5") != std::string::npos;
  const bool sawTransport = output.find("\"transport\":{") != std::string::npos;
  const bool sawMidi = output.find("\"midi\":{") != std::string::npos;

  if (!sawJsonOpen || !sawPatternNoteCount || !sawPatternActiveFrom ||
      !sawPatternActiveTo || !sawTransport || !sawMidi) {
    std::cerr << "Missing expected status json output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
