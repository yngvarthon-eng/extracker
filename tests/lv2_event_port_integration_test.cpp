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
    std::cerr << "LV2 event test plugin binary not found" << '\n';
    return 1;
  }

  const auto tmpRoot = std::filesystem::temp_directory_path() / "extracker_lv2_event_test";
  const auto lv2Bundle = tmpRoot / "event.lv2";
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
    pluginTtl << "           [ a lv2:OutputPort , lv2:ControlPort ; lv2:index 3 ] ,\n";
    pluginTtl << "           [ a lv2:InputPort , lv2:EventPort ; lv2:index 4 ] .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH for event integration test" << '\n';
    return 1;
  }

  extracker::PluginHost plugins;
  if (plugins.rescanExternalPlugins() == 0) {
    std::cerr << "LV2 adapter did not discover event test plugin" << '\n';
    return 1;
  }

  const std::string pluginId = "lv2:urn:extracker:test:runtime";
  if (!plugins.loadPlugin(pluginId)) {
    std::cerr << "Plugin host failed to load event test plugin" << '\n';
    return 1;
  }

  if (!plugins.assignInstrument(8, pluginId)) {
    std::cerr << "Plugin host failed to assign event test plugin" << '\n';
    return 1;
  }

  if (plugins.getInstrumentParameter(8, "lv2_event_input_count") != 1.0) {
    std::cerr << "LV2 event input count was not discovered" << '\n';
    return 1;
  }

  if (!plugins.setInstrumentParameter(8, "lv2_control_in_0", 1.0)) {
    std::cerr << "Failed to set LV2 gain control input for event test" << '\n';
    return 1;
  }

  std::vector<double> monoBuffer(256, 0.0);
  (void)plugins.renderInterleaved(monoBuffer, 48000);

  const std::size_t noteOnBefore = plugins.noteOnEventCount();
  const std::size_t noteOffBefore = plugins.noteOffEventCount();

  if (!plugins.triggerNoteOn(8, 60, 100, true)) {
    std::cerr << "Event test plugin did not accept note-on" << '\n';
    return 1;
  }

  std::fill(monoBuffer.begin(), monoBuffer.end(), 0.0);
  (void)plugins.renderInterleaved(monoBuffer, 48000);
  const double meterAfterNoteOn = plugins.getInstrumentParameter(8, "lv2_control_out_0");
  const double runtimeActiveAfterNoteOn = plugins.getInstrumentParameter(8, "lv2_runtime_active");

  if (plugins.noteOnEventCount() != noteOnBefore + 1) {
    std::cerr << "Plugin host note-on event count did not advance" << '\n';
    return 1;
  }

  if (runtimeActiveAfterNoteOn < 0.5) {
    std::cerr << "LV2 runtime did not stay active after note-on render" << '\n';
    return 1;
  }

  if (!(meterAfterNoteOn > 0.0)) {
    std::cerr << "LV2 control output meter did not update after note-on render" << '\n';
    return 1;
  }

  if (!plugins.triggerNoteOff(8, 60)) {
    std::cerr << "Event test plugin did not accept note-off" << '\n';
    return 1;
  }

  std::fill(monoBuffer.begin(), monoBuffer.end(), 0.0);
  (void)plugins.renderInterleaved(monoBuffer, 48000);
  const double runtimeActiveAfterNoteOff = plugins.getInstrumentParameter(8, "lv2_runtime_active");

  if (plugins.noteOffEventCount() != noteOffBefore + 1) {
    std::cerr << "Plugin host note-off event count did not advance" << '\n';
    return 1;
  }

  if (runtimeActiveAfterNoteOff < 0.5) {
    std::cerr << "LV2 runtime did not stay active after note-off render" << '\n';
    return 1;
  }

  std::filesystem::remove_all(tmpRoot, fsError);
  return 0;
}
