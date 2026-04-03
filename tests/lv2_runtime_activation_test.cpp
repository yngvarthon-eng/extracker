#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "extracker/plugin_host.hpp"

#ifndef EXTRACKER_LV2_TEST_PLUGIN_PATH
#define EXTRACKER_LV2_TEST_PLUGIN_PATH ""
#endif

int main() {
  const std::string pluginPath = EXTRACKER_LV2_TEST_PLUGIN_PATH;
  if (pluginPath.empty() || !std::filesystem::exists(pluginPath)) {
    std::cerr << "LV2 runtime test plugin binary not found" << '\n';
    return 1;
  }

  const auto tmpRoot = std::filesystem::temp_directory_path() / "extracker_lv2_runtime_test";
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
    pluginTtl << "<urn:extracker:test:runtime> a lv2:Plugin ;\n";
    pluginTtl << "  lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:index 0 ] ,\n";
    pluginTtl << "           [ a lv2:OutputPort , lv2:AudioPort ; lv2:index 1 ] ,\n";
    pluginTtl << "           [ a lv2:InputPort , lv2:ControlPort ; lv2:index 2 ] ,\n";
    pluginTtl << "           [ a lv2:OutputPort , lv2:ControlPort ; lv2:index 3 ] .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH for runtime activation test" << '\n';
    return 1;
  }

  extracker::PluginHost plugins;
  if (plugins.rescanExternalPlugins() == 0) {
    std::cerr << "LV2 adapter did not discover runtime test plugin" << '\n';
    return 1;
  }

  const std::string pluginId = "lv2:urn:extracker:test:runtime";
  if (!plugins.loadPlugin(pluginId)) {
    std::cerr << "Plugin host failed to load runtime test plugin" << '\n';
    return 1;
  }

  if (!plugins.assignInstrument(7, pluginId)) {
    std::cerr << "Plugin host failed to assign runtime test plugin" << '\n';
    return 1;
  }

  if (plugins.getInstrumentParameter(7, "lv2_control_input_count") != 1.0 ||
      plugins.getInstrumentParameter(7, "lv2_control_output_count") != 1.0) {
    std::cerr << "LV2 control port discovery counts are incorrect" << '\n';
    return 1;
  }

  if (!plugins.setInstrumentParameter(7, "lv2_control_in_0", 0.5)) {
    std::cerr << "Failed to set LV2 control input parameter" << '\n';
    return 1;
  }

  if (!plugins.triggerNoteOn(7, 60, 120, true)) {
    std::cerr << "Runtime test plugin did not accept note-on" << '\n';
    return 1;
  }

  std::vector<double> monoBuffer(128, 0.0);
  (void)plugins.renderInterleaved(monoBuffer, 48000);

  const double runtimeActive = plugins.getInstrumentParameter(7, "lv2_runtime_active");
  if (runtimeActive < 0.5) {
    std::cerr << "LV2 runtime did not activate during render" << '\n';
    return 1;
  }

  const double meterValue = plugins.getInstrumentParameter(7, "lv2_control_out_0");
  if (meterValue <= 0.0) {
    std::cerr << "LV2 control output parameter did not update during render" << '\n';
    return 1;
  }

  bool sawAudio = false;
  for (double sample : monoBuffer) {
    if (sample > 0.0) {
      sawAudio = true;
      break;
    }
  }

  if (!sawAudio) {
    std::cerr << "LV2 runtime render did not produce expected output" << '\n';
    return 1;
  }

  std::filesystem::remove_all(tmpRoot, fsError);
  return 0;
}
