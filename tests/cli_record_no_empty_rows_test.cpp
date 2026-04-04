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

  // Fill all 64 rows of channel 0 with notes, then attempt record note → no empty rows
  const std::string command =
      "( seq 0 63 | while IFS= read -r r; do printf 'note set %s 0 60 0\\n' \"$r\"; done; "
      "printf 'record on 0\\nrecord note 64\\nquit\\n' ) | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record no empty rows command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record no empty rows command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawNoEmpty = output.find("No empty rows available in channel 0") != std::string::npos;

  if (!sawNoEmpty) {
    std::cerr << "Missing expected 'No empty rows available' output marker" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
