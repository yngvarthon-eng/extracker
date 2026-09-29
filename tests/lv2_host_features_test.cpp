// An LV2 instrument that requires options/urid:map/worker:schedule and a
// bundle path (like ZynAddSubFX) runs for real, and its worker job gets a
// response. The bundle's TTL uses layouts real plugins ship: the plugin typed
// only "a lv2:InstrumentPlugin" (LSP samplers), a port-group statement before
// it, and "lv2:port [" continuing the statement at column 0 (x42 matrixmixer).
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "extracker/plugin_host.hpp"

#ifndef EXTRACKER_LV2_TEST_PLUGIN_PATH
#define EXTRACKER_LV2_TEST_PLUGIN_PATH ""
#endif

int main() {
  const std::string pluginPath = EXTRACKER_LV2_TEST_PLUGIN_PATH;
  if (pluginPath.empty() || !std::filesystem::exists(pluginPath)) {
    std::cerr << "LV2 test plugin binary not found\n";
    return 1;
  }

  const auto tmpRoot = std::filesystem::temp_directory_path() / "extracker_lv2_host_features_test";
  const auto bundle = tmpRoot / "features.lv2";
  std::error_code fsError;
  std::filesystem::remove_all(tmpRoot, fsError);
  std::filesystem::create_directories(bundle, fsError);

  {
    std::ofstream manifest(bundle / "manifest.ttl");
    manifest << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    manifest << "<urn:extracker:test:features> a lv2:Plugin ; lv2:binary <file://" << pluginPath << "> .\n";
  }
  {
    std::ofstream ttl(bundle / "features.ttl");
    ttl << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    ttl << "@prefix pg:  <http://lv2plug.in/ns/ext/port-groups#> .\n";
    ttl << "@prefix tst: <urn:extracker:test:> .\n\n";
    ttl << "tst:mono_out\n";
    ttl << "\ta pg:MonoGroup, pg:OutputGroup ;\n";
    ttl << "\tlv2:symbol \"mono_out\" .\n\n";
    ttl << "tst:features\n";
    ttl << "\ta lv2:InstrumentPlugin ;  # instrument class only\n";
    ttl << "\tlv2:requiredFeature <http://lv2plug.in/ns/ext/options#options> ;\n";
    ttl << "\n";
    ttl << "lv2:port [\n";
    ttl << "\ta lv2:OutputPort, lv2:AudioPort ;\n";
    ttl << "\tlv2:index 0 ;\n";
    ttl << "\tlv2:symbol \"out\" ;\n";
    ttl << "] .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH\n";
    return 1;
  }

  extracker::PluginHost plugins;
  plugins.rescanExternalPlugins();
  const std::string id = "lv2:urn:extracker:test:features";
  extracker::PluginPortInfo info;
  if (!plugins.getPluginPortInfo(id, info) || info.audioOut != 0) {
    std::cerr << "Audio output port not parsed (audioOut=" << info.audioOut << ")\n";
    return 1;
  }
  if (!plugins.assignInstrument(4, id)) {
    std::cerr << "Failed to assign the features test plugin\n";
    return 1;
  }

  // LV2 instruments render while a note is held.
  plugins.triggerNoteOn(4, 60, 100, true);
  double lastLevel = 0.0;
  for (int block = 0; block < 3; ++block) {
    extracker::InstrumentMixBuffers mix;
    mix.beginBlock(256);
    plugins.renderPerInstrument(mix, 44100);
    lastLevel = mix.isTouched(4) ? mix.buffer(4)[128] : 0.0;
  }
  if (plugins.getInstrumentParameter(4, "lv2_runtime_active") < 0.5) {
    std::cerr << "Plugin did not instantiate: host features or bundle path missing\n";
    return 1;
  }
  if (std::abs(lastLevel - 0.5) > 1e-6) {
    std::cerr << "Worker response never reached the plugin (level " << lastLevel << ")\n";
    return 1;
  }

  std::filesystem::remove_all(tmpRoot, fsError);
  return 0;
}
