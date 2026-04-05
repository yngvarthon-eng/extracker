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
      "note set 0 0 60 1 100\\n"
      "note set 0 1 62 1 100\\n"
      "note gate 0 0 8\\n"
      "note gate 0 1 5\\n"
      "pattern gate 50 0 0\\n"
      "pattern print 0 0\\n"
      "pattern gate dry preview verbose -100 0 0\\n"
      "pattern print 0 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern gate scale command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern gate scale command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawScale = output.find("Scaled gate on 2 step(s) by 50% in rows 0..0 (all channels, 0 clamped)") != std::string::npos;
  const bool sawRowScaled = output.find("Row 0: [60:i1:v100:f0:0] [62:i1:v100:f0:0]") != std::string::npos;
  const bool sawDrySummary =
      output.find("Scaled gate on 2 step(s) by -100% in rows 0..0 (all channels, 2 clamped) [dry-run]") !=
      std::string::npos;
  const bool sawPreviewHeader = output.find("Gate preview:") != std::string::npos;
    const bool sawVerboseLine1 = output.find("row 0 ch 0 g4 -> g0 [clamped] n60 i1 v100 fx0:0") != std::string::npos;
    const bool sawVerboseLine2 = output.find("row 0 ch 1 g3 -> g0 [clamped] n62 i1 v100 fx0:0") != std::string::npos;
  const bool sawRowUnchangedAfterDry = output.find("Row 0: [60:i1:v100:f0:0] [62:i1:v100:f0:0]") != std::string::npos;

  if (!sawScale || !sawRowScaled || !sawDrySummary || !sawPreviewHeader ||
      !sawVerboseLine1 || !sawVerboseLine2 || !sawRowUnchangedAfterDry) {
    std::cerr << "Missing expected pattern gate scale output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
