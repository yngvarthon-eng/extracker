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
      "printf 'status\\n"
      "note set 2 0 60 1\\n"
      "note set 10 3 64 2\\n"
      "status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core status pattern stats command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core status pattern stats command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawEmptyRows = output.find("Pattern active rows: (empty)") != std::string::npos;
  const bool sawNoteCount = output.find("Pattern note count: 2") != std::string::npos;
  const bool sawRowSpan = output.find("Pattern active rows: 2..10") != std::string::npos;

  if (!sawEmptyRows || !sawNoteCount || !sawRowSpan) {
    std::cerr << "Missing expected pattern status markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
