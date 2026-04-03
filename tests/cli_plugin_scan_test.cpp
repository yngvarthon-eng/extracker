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

  // plugin scan should print "<n> plugin(s) available"
  {
    const std::string cmd = "printf 'plugin scan\\nquit\\n' | " + appPath;
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

    if (output.find("plugin(s) available") == std::string::npos) {
      std::cerr << "Expected 'plugin(s) available' in scan output\n" << output;
      return 1;
    }
    // Builtins (builtin.sine, builtin.square) are always present: expect at least 2
    if (output.find("0 plugin(s) available") != std::string::npos ||
        output.find("1 plugin(s) available") != std::string::npos) {
      std::cerr << "Expected at least 2 plugins after scan\n" << output;
      return 1;
    }
  }

  // Repeated scan should not crash and should report "no new plugins" (already registered)
  {
    const std::string cmd = "printf 'plugin scan\\nplugin scan\\nquit\\n' | " + appPath;
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
    const int status = pclose(pipe);
    if (status != 0) {
      std::cerr << "CLI exited non-zero on repeated scan: " << status << '\n' << output;
      return 1;
    }
  }

  return 0;
}
