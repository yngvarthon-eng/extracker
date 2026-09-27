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
      "printf 'note set 3 1 62 2\\n"
      "status --json --minimal --pretty\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core status json pretty minimal command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core status json pretty minimal command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawJsonObjectOpen = output.find("{\n") != std::string::npos;
  const bool sawIndentedTransport = output.find("  \"transport\": {") != std::string::npos;
  const bool sawPatternNoteCount = output.find("\"patternNoteCount\": 1") != std::string::npos;
  const bool sawAudioStatus = output.find("\"audioStatus\":") != std::string::npos;
  const bool sawCompactTransport = output.find("\"transport\":{") != std::string::npos;

  if (!sawJsonObjectOpen || !sawIndentedTransport || !sawPatternNoteCount ||
      sawAudioStatus || sawCompactTransport) {
    std::cerr << "Missing expected status json pretty minimal output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
