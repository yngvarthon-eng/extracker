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
      "printf 'record undo\\n"
      "record redo\\n"
      "record on 0\\n"
      "record note 65 0 100\\n"
      "record undo\\n"
      "save /tmp/xt_cli_roundtrip_test\\n"
      "load /tmp/xt_cli_roundtrip_test\\n"
      "record redo\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI integration command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawUndoEmpty = output.find("No record action to undo") != std::string::npos;
  const bool sawRedoEmpty = output.find("No record action to redo") != std::string::npos;
  const bool sawUndo = output.find("Undid record write") != std::string::npos;
  const bool sawLoad = output.find("Module loaded from /tmp/xt_cli_roundtrip_test.xtp") != std::string::npos;
  const bool sawRedo = output.find("Redid record write") != std::string::npos;

  if (!sawUndoEmpty || !sawRedoEmpty || !sawUndo || !sawLoad || !sawRedo) {
    std::cerr << "Missing expected CLI output markers for undo/save-load/redo flow" << '\n';
    std::cerr << "  sawUndoEmpty=" << sawUndoEmpty << " sawRedoEmpty=" << sawRedoEmpty << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
