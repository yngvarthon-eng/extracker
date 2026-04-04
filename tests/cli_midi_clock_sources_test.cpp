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
      "printf 'midi clock sources\n"
      "midi clock sources exTracker\n"
      "midi clock sources NonExistent\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi clock sources command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi clock sources command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool hasNoSourcesOutput = output.find("No MIDI source matching") != std::string::npos;
  const bool hasFallbackHint = output.find("exTracker Virtual Clock") != std::string::npos ||
                                output.find("No MIDI source") != std::string::npos;
  const bool hasMatchingSourcesFormat = output.find("Matching MIDI sources") != std::string::npos ||
                                        (hasNoSourcesOutput);

  if (!hasMatchingSourcesFormat || !hasFallbackHint) {
    std::cerr << "Missing expected MIDI clock sources output markers" << '\n';
    std::cerr << "  hasMatchingSourcesFormat: " << hasMatchingSourcesFormat << '\n';
    std::cerr << "  hasFallbackHint: " << hasFallbackHint << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
