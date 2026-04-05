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
      "printf 'note set 1 0 60 1 90 2\n"
      "note set 1 0 60 1 90 2 nope\n"
      "note set 1 0x 60 1\n"
      "note set 1 0 60 1 90\n"
      "note set 1 0 60 1 90 2 3\n"
      "quit\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note set optional parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note set optional parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string usage = "Usage: note set [dry] <row> <ch> <midi> <instr> [vel] [fx] [fxval]";
  const bool sawUsage = output.find(usage) != std::string::npos;

  std::size_t writeCount = 0;
  std::size_t pos = 0;
  const std::string writeMarker = "Note set at row 1, channel 0";
  while ((pos = output.find(writeMarker, pos)) != std::string::npos) {
    ++writeCount;
    pos += writeMarker.size();
  }

  if (!sawUsage || writeCount < 2) {
    std::cerr << "Missing expected note set optional parsing markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
