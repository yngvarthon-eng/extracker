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
      "printf 'record cursor 10\\n"
      "record jump 3\\n"
      "record cursor next 2\\n"
      "record cursor status\\n"
      "record cursor prev 3\\n"
      "record cursor status\\n"
      "record cursor +\\n"
      "record cursor status\\n"
      "record cursor - 2\\n"
      "record cursor status\\n"
      "record cursor next 0\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record cursor step command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record cursor step command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSet10 = output.find("Record cursor set to row 10") != std::string::npos;
  const bool sawJumpSet = output.find("Record jump 3 -> 3") != std::string::npos;
  const bool sawNext2 = output.find("Record cursor set to row 16") != std::string::npos;
  const bool sawStatus16 = output.find("Record cursor row: 16") != std::string::npos;
  const bool sawPrev3 = output.find("Record cursor set to row 7") != std::string::npos;
  const bool sawStatus7 = output.find("Record cursor row: 7") != std::string::npos;
  const bool sawPlus = output.find("Record cursor set to row 10") != std::string::npos;
  const bool sawMinus2 = output.find("Record cursor set to row 4") != std::string::npos;
  const bool sawStatus4 = output.find("Record cursor row: 4") != std::string::npos;
  const bool sawInvalidUsage = output.find("Usage: record cursor <0..63|+n|-n|+ [count]|- [count]|start|end|next [count]|prev [count]|status>") != std::string::npos;

  if (!sawSet10 || !sawJumpSet || !sawNext2 || !sawStatus16 || !sawPrev3 ||
      !sawStatus7 || !sawPlus || !sawMinus2 || !sawStatus4 || !sawInvalidUsage) {
    std::cerr << "Missing expected record cursor step output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
