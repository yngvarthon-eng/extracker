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
      "printf 'midi instrument 2\\n"
      "record quantize off\\n"
      "record on 0\\n"
      "record cursor 24\\n"
      "record note 68 instr 11 vel 77 fx 9 33\\n"
      "pattern print 24 24\\n"
      "record cursor 25\\n"
      "record note 69 fx 5 66 vel 88 instr 12\\n"
      "pattern print 25 25\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record note keyword command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record note keyword command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawRow24 = output.find("Row 24: [68:i11:v77:f9:33]") != std::string::npos;
  const bool sawRow25 = output.find("Row 25: [69:i12:v88:f5:66]") != std::string::npos;

  if (!sawRow24 || !sawRow25) {
    std::cerr << "Missing expected record note keyword output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
