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
      "printf 'record quantize off\n"
      "record on 0\n"
      "record cursor 10\n"
      "record note 64\n"
      "status\n"
      "record undo\n"
      "status\n"
      "record redo\n"
      "status\n"
      "quit\n' | " + appPath;

  std::array<char, 4096> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI core status undo/redo flags command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI core status undo/redo flags command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawUndoYes = output.find("Record undo available: yes") != std::string::npos;
  const bool sawRedoYes = output.find("Record redo available: yes") != std::string::npos;
  const bool sawUndoAction = output.find("Undid record write at row") != std::string::npos;
  const bool sawRedoAction = output.find("Redid record write at row") != std::string::npos;

  if (!sawUndoYes || !sawRedoYes || !sawUndoAction || !sawRedoAction) {
    std::cerr << "Missing expected status undo/redo availability output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
