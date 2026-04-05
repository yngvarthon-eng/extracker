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
      "printf 'record cursor prev nope\n"
      "record cursor + 0\n"
      "record cursor - 0\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record cursor invalid step-variant command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record cursor invalid step-variant command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string usage = "Usage: record cursor <0..63|+n|-n|+ [count]|- [count]|start|end|next [count]|prev [count]|status>";
  std::size_t count = 0;
  std::size_t pos = output.find(usage);
  while (pos != std::string::npos) {
    ++count;
    pos = output.find(usage, pos + usage.size());
  }

  if (count < 3) {
    std::cerr << "Expected usage output for each invalid cursor step variant" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
