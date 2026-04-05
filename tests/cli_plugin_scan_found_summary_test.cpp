#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef EXTRACKER_LV2_TEST_PLUGIN_PATH
#define EXTRACKER_LV2_TEST_PLUGIN_PATH ""
#endif

int main() {
  const std::string appPath = "./extracker";
  const std::string pluginPath = EXTRACKER_LV2_TEST_PLUGIN_PATH;
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }
  if (pluginPath.empty() || !std::filesystem::exists(pluginPath)) {
    std::cerr << "LV2 CLI scan-summary test plugin binary not found" << '\n';
    return 1;
  }

  const auto tmpRoot = std::filesystem::temp_directory_path() / "extracker_cli_plugin_scan_found_summary_test";
  const auto lv2Bundle = tmpRoot / "runtime.lv2";
  const auto manifestPath = lv2Bundle / "manifest.ttl";

  std::error_code fsError;
  std::filesystem::remove_all(tmpRoot, fsError);
  std::filesystem::create_directories(lv2Bundle, fsError);
  if (fsError) {
    std::cerr << "Failed to prepare temporary LV2 bundle directory" << '\n';
    return 1;
  }

  {
    std::ofstream manifest(manifestPath);
    manifest << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    manifest << "<urn:extracker:test:runtime> a lv2:Plugin ; lv2:binary <file://"
             << pluginPath << "> .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH for CLI scan-summary test" << '\n';
    return 1;
  }

  const std::string command = "printf 'plugin scan\nquit\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI plugin scan-summary command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  std::filesystem::remove_all(tmpRoot, fsError);

  if (status != 0) {
    std::cerr << "CLI plugin scan-summary command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawFoundSummary = output.find("Scan found 1 new plugin(s)") != std::string::npos;
  const bool sawAvailableSummary = output.find("plugin(s) available") != std::string::npos;

  if (!sawFoundSummary || !sawAvailableSummary) {
    std::cerr << "Missing expected plugin scan found-summary output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
