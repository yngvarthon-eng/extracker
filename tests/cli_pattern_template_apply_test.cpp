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
      "printf 'pattern template blank\\n"
      "pattern template house\\n"
      "pattern template electro\\n"
      "pattern template nope\\n"
      "pattern nope\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern template command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern template command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawBlank    = output.find("Applied pattern template: blank") != std::string::npos;
  const bool sawHouse    = output.find("Applied pattern template: house") != std::string::npos;
  const bool sawElectro  = output.find("Applied pattern template: electro") != std::string::npos;
  const bool sawUnknown  = output.find("Unknown pattern template: nope") != std::string::npos;
  const bool sawTopUsage = output.find("Usage: pattern <print|play|template> ...") != std::string::npos;

  if (!sawBlank || !sawHouse || !sawElectro || !sawUnknown || !sawTopUsage) {
    std::cerr << "Missing expected pattern template output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
