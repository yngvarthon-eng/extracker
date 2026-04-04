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
      "printf 'note vel 5\\n"
      "note gate 5\\n"
      "note fx 5\\n"
      "note nope\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI note usage guard command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI note usage guard command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawVelUsage  = output.find("Usage: note vel [dry] <row> <ch> <vel>") != std::string::npos;
  const bool sawGateUsage = output.find("Usage: note gate [dry] <row> <ch> <ticks>") != std::string::npos;
  const bool sawFxUsage   = output.find("Usage: note fx [dry] <row> <ch> <fx> <fxval>") != std::string::npos;
  const bool sawTopUsage  = output.find("Usage: note <set|clear|vel|gate|fx> ...") != std::string::npos;

  if (!sawVelUsage || !sawGateUsage || !sawFxUsage || !sawTopUsage) {
    std::cerr << "Missing expected note usage guard output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
