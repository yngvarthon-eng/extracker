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
      "record cursor +5\\n"
      "record cursor status\\n"
      "record cursor -3\\n"
      "record cursor status\\n"
      "record jump 4\\n"
      "record cursor next\\n"
      "record cursor status\\n"
      "record cursor prev\\n"
      "record cursor status\\n"
      "record cursor start\\n"
      "record cursor status\\n"
      "record cursor end\\n"
      "record cursor status\\n"
      "record cursor -999\\n"
      "record cursor status\\n"
      "record cursor +999\\n"
      "record cursor status\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record cursor relative command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record cursor relative command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSet10 = output.find("Record cursor set to row 10") != std::string::npos;
  const bool sawPlus5 = output.find("Record cursor set to row 15") != std::string::npos;
  const bool sawStatus15 = output.find("Record cursor row: 15") != std::string::npos;
  const bool sawMinus3 = output.find("Record cursor set to row 12") != std::string::npos;
  const bool sawStatus12 = output.find("Record cursor row: 12") != std::string::npos;
  const bool sawJumpSet = output.find("Record jump 4 -> 4") != std::string::npos;
  const bool sawNext = output.find("Record cursor set to row 16") != std::string::npos;
  const bool sawStatus16 = output.find("Record cursor row: 16") != std::string::npos;
  const bool sawPrev = output.find("Record cursor set to row 12") != std::string::npos;
  const bool sawStart = output.find("Record cursor set to row 0") != std::string::npos;
  const bool sawStatus0 = output.find("Record cursor row: 0") != std::string::npos;
  const bool sawClampLow = output.find("Record cursor set to row 0") != std::string::npos;
  const bool sawEnd = output.find("Record cursor set to row 63") != std::string::npos;
  const bool sawClampHigh = output.find("Record cursor set to row 63") != std::string::npos;
  const bool sawStatus63 = output.find("Record cursor row: 63") != std::string::npos;

  if (!sawSet10 || !sawPlus5 || !sawStatus15 || !sawMinus3 || !sawStatus12 || !sawJumpSet || !sawNext || !sawStatus16 || !sawPrev || !sawStart || !sawStatus0 || !sawClampLow || !sawEnd || !sawClampHigh || !sawStatus63) {
    std::cerr << "Missing expected relative record cursor output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
