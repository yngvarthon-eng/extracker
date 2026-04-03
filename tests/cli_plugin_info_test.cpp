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

  // plugin info without an id shows usage hint
  {
    const std::string cmd = "printf 'plugin info\\nquit\\n' | " + appPath;
    std::array<char, 1024> buf{};
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      std::cerr << "popen failed\n";
      return 1;
    }
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
      output += buf.data();
    }
    pclose(pipe);
    if (output.find("Usage") == std::string::npos) {
      std::cerr << "Expected 'Usage' hint when plugin info has no id\n" << output;
      return 1;
    }
  }

  // plugin info for a non-existent id reports "Unknown plugin"
  {
    const std::string cmd =
        "printf 'plugin info lv2:urn:extracker:no-such-plugin\\nquit\\n' | " + appPath;
    std::array<char, 1024> buf{};
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      std::cerr << "popen failed\n";
      return 1;
    }
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
      output += buf.data();
    }
    pclose(pipe);
    if (output.find("Unknown plugin") == std::string::npos) {
      std::cerr << "Expected 'Unknown plugin' for non-existent plugin id\n" << output;
      return 1;
    }
  }

  // plugin info for builtin.sine: builtins have no port info → "Unknown plugin"
  {
    const std::string cmd = "printf 'plugin info builtin.sine\\nquit\\n' | " + appPath;
    std::array<char, 1024> buf{};
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      std::cerr << "popen failed\n";
      return 1;
    }
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
      output += buf.data();
    }
    pclose(pipe);
    if (output.find("Unknown plugin") == std::string::npos) {
      std::cerr << "Expected 'Unknown plugin' for builtin.sine (builtins have no port info)\n"
                << output;
      return 1;
    }
  }

  return 0;
}
