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
      "note set 0 1 62 1 110\\n"
      "note gate 0 0 8\\n"
      "note gate 0 1 6\\n"
      "pattern humanize dry preview verbose 0 0 42 0 0\\n"
      "pattern humanize 0 0 42 0 0\\n"
      "pattern print 0 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern humanize command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern humanize command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDry =
      output.find("Humanized 2 step(s) with vel +/-0 and gate +/-0% in rows 0..0 (all channels, 0 clamped, seed 42) [dry-run]") !=
      std::string::npos;
  const bool sawPreview = output.find("Humanize preview:") != std::string::npos;
  const bool sawCommit =
      output.find("Humanized 2 step(s) with vel +/-0 and gate +/-0% in rows 0..0 (all channels, 0 clamped, seed 42)") !=
      std::string::npos;
  const bool sawRow = output.find("Row 0: [60:i1:v100:f0:0] [62:i1:v110:f0:0]") != std::string::npos;

  if (!sawDry || !sawPreview || !sawCommit || !sawRow) {
    std::cerr << "Missing expected pattern humanize output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
