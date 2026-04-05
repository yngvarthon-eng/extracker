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
      "pattern transpose 12 0 0\\n"
      "pattern print 0 0\\n"
      "pattern undo\\n"
      "pattern print 0 0\\n"
      "pattern redo\\n"
      "pattern print 0 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern bulk undo/redo command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern bulk undo/redo command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawTransposed = output.find("Row 0: [72:i1:v100:f0:0]") != std::string::npos;
  const bool sawUndo = output.find("Bulk pattern undo applied") != std::string::npos;
  const bool sawOriginal = output.find("Row 0: [60:i1:v100:f0:0]") != std::string::npos;
  const bool sawRedo = output.find("Bulk pattern redo applied") != std::string::npos;

  if (!sawTransposed || !sawUndo || !sawOriginal || !sawRedo) {
    std::cerr << "Missing expected pattern bulk undo/redo output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
