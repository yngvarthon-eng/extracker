#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#include "test_string_utils.hpp"

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  const std::string command =
      "printf 'note set 0 0 60 1 100\\n"
      "pattern duplicate 1\\n"
      "pattern switch 2\\n"
      "pattern transpose 12 0 0\\n"
      "pattern print 0 0\\n"
      "pattern undo\\n"
      "pattern print 0 0\\n"
      "pattern redo\\n"
      "pattern print 0 0\\n"
      "song status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 8192> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI indexed duplicate undo/redo command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI indexed duplicate undo/redo command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawDuplicate = containsAny(output, {"Duplicated pattern 1 to pattern 2"});
  const bool sawTransposed = containsAny(output, {"Row 0: [72:i1:v100:f0:0]"});
  const bool sawUndo = containsAny(output, {"Bulk pattern undo applied"});
  const bool sawOriginal = containsAny(output, {"Row 0: [60:i1:v100:f0:0]"});
  const bool sawRedo = containsAny(output, {"Bulk pattern redo applied"});
  const bool sawSongOrder = containsAny(output, {"Order: 1 2"});

  if (!sawDuplicate || !sawTransposed || !sawUndo || !sawOriginal || !sawRedo || !sawSongOrder) {
    std::cerr << "Missing expected indexed duplicate undo/redo output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
