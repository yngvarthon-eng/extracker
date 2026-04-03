#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "extracker/plugin_host.hpp"

static bool approx(float a, float b, float eps = 0.001f) {
  return std::abs(a - b) < eps;
}

int main() {
  const auto tmpRoot =
      std::filesystem::temp_directory_path() / "extracker_lv2_port_range_test";
  const auto lv2Bundle = tmpRoot / "portrange.lv2";

  std::error_code fsError;
  std::filesystem::remove_all(tmpRoot, fsError);
  std::filesystem::create_directories(lv2Bundle, fsError);
  if (fsError) {
    std::cerr << "Failed to prepare temp bundle dir\n";
    return 1;
  }

  {
    std::ofstream manifest(lv2Bundle / "manifest.ttl");
    manifest << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    manifest << "<urn:extracker:test:portrange> a lv2:Plugin ; lv2:binary <none.so> .\n";
  }

  {
    std::ofstream ttl(lv2Bundle / "plugin.ttl");
    ttl << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    ttl << "<urn:extracker:test:portrange> a lv2:Plugin ;\n";
    ttl << "  lv2:port\n";
    ttl << "    [ a lv2:InputPort , lv2:AudioPort ; lv2:index 0 ] ,\n";
    ttl << "    [ a lv2:OutputPort , lv2:AudioPort ; lv2:index 1 ] ,\n";
    ttl << "    [ a lv2:InputPort , lv2:ControlPort ; lv2:index 2 ; lv2:minimum 0.0 ; lv2:maximum 1.0 ; lv2:default 0.5 ] ,\n";
    ttl << "    [ a lv2:InputPort , lv2:ControlPort ; lv2:index 3 ; lv2:minimum 20.0 ; lv2:maximum 20000.0 ; lv2:default 440.0 ] .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH\n";
    return 1;
  }

  extracker::PluginHost plugins;
  if (plugins.rescanExternalPlugins() == 0) {
    std::cerr << "Adapter did not discover portrange plugin\n";
    return 1;
  }

  extracker::PluginPortInfo info;
  if (!plugins.getPluginPortInfo("lv2:urn:extracker:test:portrange", info)) {
    std::cerr << "getPluginPortInfo returned false\n";
    return 1;
  }

  if (info.controlInCount != 2) {
    std::cerr << "Expected 2 control inputs, got " << info.controlInCount << '\n';
    return 1;
  }

  if (info.controlInMeta.size() != 2) {
    std::cerr << "Expected 2 controlInMeta entries, got " << info.controlInMeta.size() << '\n';
    return 1;
  }

  const auto& m0 = info.controlInMeta[0];
  if (m0.index != 2 || !m0.hasMin || !m0.hasMax || !m0.hasDefault) {
    std::cerr << "Port 2 meta missing fields (index=" << m0.index
              << " hasMin=" << m0.hasMin << " hasMax=" << m0.hasMax
              << " hasDefault=" << m0.hasDefault << ")\n";
    return 1;
  }
  if (!approx(m0.minVal, 0.0f) || !approx(m0.maxVal, 1.0f) || !approx(m0.defaultVal, 0.5f)) {
    std::cerr << "Port 2 values wrong: min=" << m0.minVal
              << " max=" << m0.maxVal << " default=" << m0.defaultVal << '\n';
    return 1;
  }

  const auto& m1 = info.controlInMeta[1];
  if (m1.index != 3 || !m1.hasMin || !m1.hasMax || !m1.hasDefault) {
    std::cerr << "Port 3 meta missing fields (index=" << m1.index
              << " hasMin=" << m1.hasMin << " hasMax=" << m1.hasMax
              << " hasDefault=" << m1.hasDefault << ")\n";
    return 1;
  }
  if (!approx(m1.minVal, 20.0f) || !approx(m1.maxVal, 20000.0f) || !approx(m1.defaultVal, 440.0f)) {
    std::cerr << "Port 3 values wrong: min=" << m1.minVal
              << " max=" << m1.maxVal << " default=" << m1.defaultVal << '\n';
    return 1;
  }

  std::filesystem::remove_all(tmpRoot, fsError);
  return 0;
}
