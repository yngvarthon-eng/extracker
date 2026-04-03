#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "extracker/plugin_host.hpp"

int main() {
  const auto tmpRoot = std::filesystem::temp_directory_path() / "extracker_lv2_port_map_test";
  const auto lv2Bundle = tmpRoot / "mapped.lv2";
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
    manifest << "<urn:extracker:test:mapped> a lv2:Plugin ; lv2:binary <instrument.so> .\n";
  }

  {
    std::ofstream pluginTtl(pluginTtlPath);
    pluginTtl << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    pluginTtl << "<urn:extracker:test:mapped> a lv2:Plugin ;\n";
    pluginTtl << "  lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:index 0 ] ,\n";
    pluginTtl << "           [ a lv2:OutputPort , lv2:AudioPort ; lv2:index 1 ] .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH for LV2 port map scaffold test" << '\n';
    return 1;
  }

  extracker::PluginHost plugins;
  if (plugins.rescanExternalPlugins() == 0) {
    std::cerr << "LV2 adapter did not discover mapped scaffold plugin" << '\n';
    return 1;
  }

  const std::string pluginId = "lv2:urn:extracker:test:mapped";
  if (!plugins.loadPlugin(pluginId)) {
    std::cerr << "Plugin host failed to load mapped scaffold plugin" << '\n';
    return 1;
  }

  if (!plugins.assignInstrument(6, pluginId)) {
    std::cerr << "Plugin host failed to assign mapped scaffold plugin" << '\n';
    return 1;
  }

  const double mapReady = plugins.getInstrumentParameter(6, "lv2_port_map_ready");
  const double inputPort = plugins.getInstrumentParameter(6, "lv2_audio_input_port");
  const double outputPort = plugins.getInstrumentParameter(6, "lv2_audio_output_port");

  if (mapReady < 0.5 || inputPort != 0.0 || outputPort != 1.0) {
    std::cerr << "LV2 port map scaffold parameters did not match expected values" << '\n';
    return 1;
  }

  std::filesystem::remove_all(tmpRoot, fsError);
  return 0;
}
