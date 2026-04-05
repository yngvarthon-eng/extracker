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
      "printf 'pattern template blank\\n"
      "note set 0 0 60 1 100 2 3\\n"
      "note set 1 0 61 1 101 2 4\\n"
      "note set 2 0 62 1 102 2 5\\n"
      "note set 3 0 63 1 103 2 6\\n"
      "pattern copy 0 3 step 2\\n"
      "pattern paste 8 step 2\\n"
      "pattern print 8 11\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern copy/paste step stride command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern copy/paste step stride command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawCopy = output.find("Copied rows 0..3 channels 0..7 (2x8) [step 2]") != std::string::npos;
  const bool sawPaste = output.find("Pasted 2 step(s) at row 8 (channel offset 0, 0 skipped) [step 2]") != std::string::npos;
  const bool sawRow8 = output.find("Row 8: [60:i1:v100:f2:3]") != std::string::npos;
  const bool sawRow9 = output.find("Row 9: [--] [--] [--] [--] [--] [--] [--] [--]") != std::string::npos;
  const bool sawRow10 = output.find("Row 10: [62:i1:v102:f2:5]") != std::string::npos;
  const bool sawRow11 = output.find("Row 11: [--] [--] [--] [--] [--] [--] [--] [--]") != std::string::npos;

  if (!sawCopy || !sawPaste || !sawRow8 || !sawRow9 || !sawRow10 || !sawRow11) {
    std::cerr << "Missing expected pattern copy/paste step stride output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
