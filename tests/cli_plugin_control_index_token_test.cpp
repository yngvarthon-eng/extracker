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
    std::cerr << "LV2 CLI control-index-token test plugin binary not found" << '\n';
    return 1;
  }

  const auto tmpRoot = std::filesystem::temp_directory_path() / "extracker_cli_plugin_control_index_token_test";
  const auto lv2Bundle = tmpRoot / "runtime.lv2";
  const auto manifestPath = lv2Bundle / "manifest.ttl";
  const auto pluginTtlPath = lv2Bundle / "plugin.ttl";

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

  {
    std::ofstream pluginTtl(pluginTtlPath);
    pluginTtl << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    pluginTtl << "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n";
    pluginTtl << "<urn:extracker:test:runtime> a lv2:Plugin ;\n";
    pluginTtl << "  lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:index 0 ] ,\n";
    pluginTtl << "           [ a lv2:OutputPort , lv2:AudioPort ; lv2:index 1 ] ,\n";
    pluginTtl << "           [ a lv2:InputPort , lv2:ControlPort ; lv2:index 2 ; lv2:symbol \"gain\" ; rdfs:label \"Gain\" ; lv2:minimum 0.0 ; lv2:maximum 1.0 ; lv2:default 0.25 ] ,\n";
    pluginTtl << "           [ a lv2:OutputPort , lv2:ControlPort ; lv2:index 3 ; lv2:symbol \"meter\" ; rdfs:label \"Meter\" ] .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH for CLI control-index-token test" << '\n';
    return 1;
  }

  const std::string command =
      "printf 'plugin scan\n"
      "plugin load lv2:urn:extracker:test:runtime\n"
      "plugin assign 9 lv2:urn:extracker:test:runtime\n"
      "plugin set 9 2 0.4\n"
      "plugin get 9 2\n"
      "plugin get 9 3\n"
      "quit\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI control-index-token command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  std::filesystem::remove_all(tmpRoot, fsError);

  if (status != 0) {
    std::cerr << "CLI control-index-token command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSetByIndex = output.find("Set instrument 9 control port 2 [gain] \"Gain\" to 0.4") != std::string::npos;
  const bool sawGetInputByIndex = output.find("Instrument 9 control port 2 [gain] \"Gain\" = 0.4 (input)") != std::string::npos;
  const bool sawGetOutputByIndex = output.find("Instrument 9 control port 3 [meter] \"Meter\" = ") != std::string::npos &&
                                   output.find("(output)") != std::string::npos;

  if (!sawSetByIndex || !sawGetInputByIndex || !sawGetOutputByIndex) {
    std::cerr << "Missing expected control-index-token output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
