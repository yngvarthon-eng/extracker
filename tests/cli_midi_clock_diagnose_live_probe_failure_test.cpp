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
      "export PATH=/nonexistent; "
      "printf 'midi clock diagnose live\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI midi clock diagnose live probe failure command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI midi clock diagnose live probe failure command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawLiveHeader = output.find("Mode: live health probe (1 second)") != std::string::npos;
  const bool sawListingFailure =
      output.find("ALSA port listing: failed (is aconnect installed?)") != std::string::npos;
  const bool sawLiveProbeResult = output.find("Live probe result:") != std::string::npos;

  if (!sawLiveHeader || !sawListingFailure || !sawLiveProbeResult) {
    std::cerr << "Missing expected live-probe-on-listing-failure output markers" << '\n';
    std::cerr << "  sawLiveHeader: " << sawLiveHeader << '\n';
    std::cerr << "  sawListingFailure: " << sawListingFailure << '\n';
    std::cerr << "  sawLiveProbeResult: " << sawLiveProbeResult << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
