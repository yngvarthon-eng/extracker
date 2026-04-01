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
      "printf 'midi instrument 7\\n"
      "record quantize off\\n"
      "record on 0\\n"
      "record cursor 20\\n"
      "record note 64 vel 93\\n"
      "pattern print 20 20\\n"
      "record cursor 21\\n"
      "record note 65 vel 88 12 34\\n"
      "pattern print 21 21\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record note vel command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record note vel command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawFirstRow = output.find("Row 20: [64:i7:v93:f0:0]") != std::string::npos;
  const bool sawSecondRow = output.find("Row 21: [65:i7:v88:f12:34]") != std::string::npos;

  if (!sawFirstRow || !sawSecondRow) {
    std::cerr << "Missing expected record note vel output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
