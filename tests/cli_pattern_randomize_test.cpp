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
      "note set 0 1 62 1 110 4 5\\n"
      "pattern randomize dry preview verbose 0 42 0 0\\n"
      "pattern randomize 100 42 0 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern randomize command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern randomize command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDry =
      output.find("Randomized 0 step(s) at 0% probability in rows 0..0 (all channels, 0 input clamped, seed 42) [dry-run]") !=
      std::string::npos;
  const bool sawDryPreview = output.find("Randomize preview:") != std::string::npos;
  const bool sawCommit =
      output.find("Randomized 2 step(s) at 100% probability in rows 0..0 (all channels, 0 input clamped, seed 42)") !=
      std::string::npos;

  if (!sawDry || !sawDryPreview || !sawCommit) {
    std::cerr << "Missing expected pattern randomize output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
