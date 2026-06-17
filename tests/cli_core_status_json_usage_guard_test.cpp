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
      "printf 'status nope\\n"
      "status --json extra\\n"
      "status --pretty\\n"
      "quit\\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core status json usage guard command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core status json usage guard command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string usage = "Usage: status [--json [--minimal] [--pretty]]";
  const std::size_t first = output.find(usage);
  const std::size_t second = first == std::string::npos ? std::string::npos : output.find(usage, first + 1);
  const std::size_t third = second == std::string::npos ? std::string::npos : output.find(usage, second + 1);

  if (first == std::string::npos || second == std::string::npos || third == std::string::npos) {
    std::cerr << "Missing expected status json usage output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
