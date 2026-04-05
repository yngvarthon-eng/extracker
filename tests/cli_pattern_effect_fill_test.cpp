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
      "note set 0 1 62 2 110 4 5\\n"
      "pattern effect 10 20 0 0\\n"
      "pattern print 0 0\\n"
      "pattern effect dry preview verbose 255 300 0 0\\n"
      "pattern print 0 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern effect fill command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern effect fill command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawFill =
      output.find("Filled effect on 2 step(s) with 10:20 in rows 0..0 (all channels, 0 input clamped)") !=
      std::string::npos;
  const bool sawRowFilled = output.find("Row 0: [60:i1:v100:f10:20] [62:i2:v110:f10:20]") != std::string::npos;
  const bool sawDrySummary =
      output.find("Filled effect on 2 step(s) with 255:255 in rows 0..0 (all channels, 1 input clamped) [dry-run]") !=
      std::string::npos;
  const bool sawPreviewHeader = output.find("Effect preview:") != std::string::npos;
  const bool sawVerboseLine1 = output.find("row 0 ch 0 fx10:20 -> fx255:255 n60 i1 v100") != std::string::npos;
  const bool sawVerboseLine2 = output.find("row 0 ch 1 fx10:20 -> fx255:255 n62 i2 v110") != std::string::npos;
  const bool sawRowUnchangedAfterDry = output.find("Row 0: [60:i1:v100:f10:20] [62:i2:v110:f10:20]") != std::string::npos;

  if (!sawFill || !sawRowFilled || !sawDrySummary || !sawPreviewHeader ||
      !sawVerboseLine1 || !sawVerboseLine2 || !sawRowUnchangedAfterDry) {
    std::cerr << "Missing expected pattern effect fill output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
