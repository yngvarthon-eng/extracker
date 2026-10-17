// Discovery of LV2 bundles written in layouts other than "<uri> a lv2:Plugin"
// on one line: prefixed subjects on their own line (LSP), nested [ ] blocks
// inside ports, and class definitions that merely mention lv2:Plugin
// (midifilter.lv2's "lv2:MIDIPlugin rdfs:subClassOf lv2:Plugin").
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "extracker/plugin_host.hpp"

int main() {
  const auto tmpRoot =
      std::filesystem::temp_directory_path() / "extracker_lv2_turtle_layout_test";
  const auto lv2Bundle = tmpRoot / "layout.lv2";

  std::error_code fsError;
  std::filesystem::remove_all(tmpRoot, fsError);
  std::filesystem::create_directories(lv2Bundle, fsError);
  if (fsError) {
    std::cerr << "Failed to prepare temp bundle dir\n";
    return 1;
  }

  {
    std::ofstream manifest(lv2Bundle / "manifest.ttl");
    manifest << "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n";
    manifest << "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n";
    manifest << "@prefix tst:  <urn:extracker:test:> .\n\n";
    manifest << "lv2:MIDIPlugin\n";
    manifest << "\ta rdfs:Class ;\n";
    manifest << "\trdfs:subClassOf lv2:Plugin .\n\n";
    manifest << "tst:comp\n";
    manifest << "\ta lv2:Plugin ;\n";
    manifest << "\tlv2:binary <none.so> ;\n";
    manifest << "\trdfs:seeAlso <comp.ttl> .\n";
  }

  {
    std::ofstream ttl(lv2Bundle / "comp.ttl");
    ttl << "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .\n";
    ttl << "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .\n";
    ttl << "@prefix units: <http://lv2plug.in/ns/extensions/units#> .\n";
    ttl << "@prefix tst:   <urn:extracker:test:> .\n\n";
    ttl << "tst:comp\n";
    ttl << "\ta lv2:Plugin, lv2:CompressorPlugin ;\n";
    ttl << "\tlv2:port [\n";
    ttl << "\t\ta lv2:InputPort, lv2:AudioPort ;\n";
    ttl << "\t\tlv2:index 0 ;\n";
    ttl << "\t] , [\n";
    ttl << "\t\ta lv2:OutputPort, lv2:AudioPort ;\n";
    ttl << "\t\tlv2:index 1 ;\n";
    ttl << "\t] , [\n";
    ttl << "\t\ta lv2:InputPort, lv2:ControlPort ;\n";
    ttl << "\t\tlv2:index 2 ;\n";
    ttl << "\t\tlv2:symbol \"g_in\" ;\n";
    ttl << "\t\tlv2:name \"Input gain\" ;\n";
    ttl << "\t\tunits:unit [\n";
    ttl << "\t\t\ta units:Unit ;\n";
    ttl << "\t\t\trdfs:label \"gain\" ;\n";
    ttl << "\t\t\tunits:render \"%.8f [G]\" ;\n";
    ttl << "\t\t] ;\n";
    ttl << "\t\tlv2:minimum 0.0 ;\n";
    ttl << "\t\tlv2:maximum 10.0 ;\n";
    ttl << "\t\tlv2:default 1.0 ;\n";
    ttl << "\t] , [\n";
    ttl << "\t\ta lv2:InputPort, lv2:ControlPort ;\n";
    ttl << "\t\tlv2:index 3 ;\n";
    ttl << "\t\tlv2:symbol \"mode\" ;\n";
    ttl << "\t\tlv2:name \"Mode\" ;\n";
    ttl << "\t\tlv2:scalePoint [ rdfs:label \"Soft\" ; rdf:value 0 ] ,\n";
    ttl << "\t\t\t[ rdfs:label \"Hard\" ; rdf:value 1 ] ;\n";
    ttl << "\t\tlv2:minimum 0 ;\n";
    ttl << "\t\tlv2:maximum 1 ;\n";
    ttl << "\t\tlv2:default 1 ;\n";
    ttl << "\t] , [\n";
    ttl << "\t\ta lv2:OutputPort, lv2:ControlPort ;\n";
    ttl << "\t\tlv2:index 4 ;\n";
    ttl << "\t\tlv2:symbol \"meter\" ;\n";
    ttl << "\t] .\n";
  }

  if (setenv("LV2_PATH", tmpRoot.string().c_str(), 1) != 0) {
    std::cerr << "Failed to set LV2_PATH\n";
    return 1;
  }

  extracker::PluginHost plugins;
  plugins.rescanExternalPlugins();

  const auto available = plugins.discoverAvailablePlugins();
  const auto has = [&](const std::string& id) {
    return std::find(available.begin(), available.end(), id) != available.end();
  };
  if (!has("lv2:urn:extracker:test:comp")) {
    std::cerr << "Prefixed split-line plugin was not discovered\n";
    return 1;
  }
  if (has("lv2:http://lv2plug.in/ns/lv2core#MIDIPlugin")) {
    std::cerr << "Class definition (subClassOf lv2:Plugin) was registered as a plugin\n";
    return 1;
  }

  extracker::PluginPortInfo info;
  if (!plugins.getPluginPortInfo("lv2:urn:extracker:test:comp", info)) {
    std::cerr << "getPluginPortInfo returned false\n";
    return 1;
  }
  if (info.audioIn != 0 || info.audioOut != 1) {
    std::cerr << "Audio ports wrong: in=" << info.audioIn << " out=" << info.audioOut << '\n';
    return 1;
  }
  if (info.controlInCount != 2 || info.controlInMeta.size() != 2) {
    std::cerr << "Expected 2 control inputs, got " << info.controlInCount << '\n';
    return 1;
  }
  if (info.controlOutCount != 1) {
    std::cerr << "Expected 1 control output, got " << info.controlOutCount << '\n';
    return 1;
  }

  const auto& gain = info.controlInMeta[0];
  if (gain.index != 2 || gain.symbol != "g_in" || gain.label != "Input gain" ||
      !gain.hasMax || std::abs(gain.maxVal - 10.0f) > 0.001f) {
    std::cerr << "Port 2 meta wrong after nested units block (index=" << gain.index
              << " symbol=" << gain.symbol << " label=" << gain.label
              << " max=" << gain.maxVal << ")\n";
    return 1;
  }
  const auto& mode = info.controlInMeta[1];
  if (mode.index != 3 || mode.label != "Mode" || !mode.hasDefault ||
      std::abs(mode.defaultVal - 1.0f) > 0.001f) {
    std::cerr << "Port 3 meta wrong after scale points (index=" << mode.index
              << " label=" << mode.label << " default=" << mode.defaultVal << ")\n";
    return 1;
  }

  std::filesystem::remove_all(tmpRoot, fsError);
  return 0;
}
