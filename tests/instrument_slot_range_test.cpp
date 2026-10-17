// Instrument slots cover the full 8-bit range (00-FF): a plugin in the top
// slot plays, renders into its own mix buffer, and keeps per-instrument
// filter/pitch/depth settings. Unassigned slots are not rendered at all.
#include <cmath>
#include <iostream>

#include "extracker/plugin_host.hpp"

int main() {
  extracker::PluginHost plugins;
  if (extracker::PluginHost::kMaxInstrumentSlots != 256) {
    std::cerr << "Expected 256 instrument slots, got " << extracker::PluginHost::kMaxInstrumentSlots << '\n';
    return 1;
  }

  constexpr std::uint8_t kTop = 255;
  if (!plugins.assignInstrument(kTop, "builtin.sine")) {
    std::cerr << "Failed to assign builtin.sine to instrument 255\n";
    return 1;
  }
  if (plugins.pluginForInstrument(kTop) != "builtin.sine") {
    std::cerr << "Instrument 255 does not report builtin.sine\n";
    return 1;
  }

  plugins.setInstrumentPitch(kTop, 3.0f);
  plugins.setInstrumentDepth(kTop, 0.25f);
  plugins.setInstrumentFilter(kTop, extracker::BiquadType::LowPass, 0.5f, 0.1f);
  if (std::abs(plugins.getInstrumentPitch(kTop) - 3.0f) > 0.001f ||
      std::abs(plugins.getInstrumentDepth(kTop) - 0.25f) > 0.001f ||
      !plugins.getInstrumentFilterParams(kTop).isActive()) {
    std::cerr << "Per-instrument settings on instrument 255 were not kept\n";
    return 1;
  }

  if (!plugins.triggerNoteOn(kTop, 60, 100, true)) {
    std::cerr << "triggerNoteOn on instrument 255 failed\n";
    return 1;
  }

  extracker::InstrumentMixBuffers mix;
  mix.beginBlock(512);
  if (!plugins.renderPerInstrument(mix, 44100)) {
    std::cerr << "renderPerInstrument reported nothing rendered\n";
    return 1;
  }
  if (!mix.isTouched(kTop)) {
    std::cerr << "Instrument 255 was not rendered into its buffer\n";
    return 1;
  }
  double energy = 0.0;
  for (const double v : mix.buffer(kTop)) {
    energy += v * v;
  }
  if (energy <= 0.0) {
    std::cerr << "Instrument 255 rendered silence\n";
    return 1;
  }

  // Only assigned instruments are touched: the default host has none besides
  // the one assigned above.
  for (const std::uint8_t instrument : mix.active()) {
    if (plugins.pluginForInstrument(instrument).empty()) {
      std::cerr << "Unassigned instrument " << static_cast<int>(instrument) << " was rendered\n";
      return 1;
    }
  }

  // A second block reuses the buffers and starts from silence.
  plugins.allNotesOff();
  mix.beginBlock(512);
  if (!mix.active().empty() || mix.isTouched(kTop)) {
    std::cerr << "beginBlock did not reset the touched set\n";
    return 1;
  }
  const std::vector<double>& again = mix.touch(kTop);
  for (const double v : again) {
    if (v != 0.0) {
      std::cerr << "Reused buffer was not zeroed\n";
      return 1;
    }
  }
  return 0;
}
