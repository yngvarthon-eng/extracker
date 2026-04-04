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
      "printf 'pattern print 2x\n"
      "pattern print 4\n"
      "pattern play 8y\n"
      "pattern play 6 3\n"
      "pattern template house extra\n"
      "quit\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern numeric parse command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern numeric parse command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawPrintUsage = output.find("Usage: pattern print [from] [to]") != std::string::npos;
  const bool sawPrintRows = output.find("Row 4:") != std::string::npos;
  const bool sawPlayUsage = output.find("Usage: pattern play [from] [to]") != std::string::npos;
  const bool sawPlaySet = output.find("Playing range 3..6") != std::string::npos;
  const bool sawTemplateUsage = output.find("Usage: pattern template <blank|house|electro>") != std::string::npos;

  if (!sawPrintUsage || !sawPrintRows || !sawPlayUsage || !sawPlaySet || !sawTemplateUsage) {
    std::cerr << "Missing expected pattern numeric parsing output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
