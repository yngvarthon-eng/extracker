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
      "printf 'record channel status\\n"
      "record channel 3\\n"
      "record channel status\\n"
      "record on\\n"
      "status\\n"
      "record channel 99\\n"
      "record channel status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record channel command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record channel command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawInitial = output.find("Record channel: 0") != std::string::npos;
  const bool sawSetToThree = output.find("Record channel set to 3") != std::string::npos;
  const bool sawStatusThree = output.find("Record channel: 3") != std::string::npos;
  const bool sawArmedOnThree = output.find("Record enabled on channel 3") != std::string::npos;
  const bool sawClampSet = output.find("Record channel set to 7") != std::string::npos;
  const bool sawStatusSeven = output.find("Record channel: 7") != std::string::npos;

  if (!sawInitial || !sawSetToThree || !sawStatusThree || !sawArmedOnThree || !sawClampSet || !sawStatusSeven) {
    std::cerr << "Missing expected record channel output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
