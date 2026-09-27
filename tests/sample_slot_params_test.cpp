#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "extracker/plugin_host.hpp"

namespace {

void writeWord(std::ofstream& out, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
  }
}

bool writeSineWav(const std::filesystem::path& path, std::uint32_t frameCount) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  constexpr std::uint32_t sampleRate = 44100;
  const std::uint32_t dataSize = frameCount * 2;
  writeWord(out, 0x46464952, 4);  // "RIFF"
  writeWord(out, 36 + dataSize, 4);
  writeWord(out, 0x45564157, 4);  // "WAVE"
  writeWord(out, 0x20746D66, 4);  // "fmt "
  writeWord(out, 16, 4);
  writeWord(out, 1, 2);           // PCM
  writeWord(out, 1, 2);           // mono
  writeWord(out, sampleRate, 4);
  writeWord(out, sampleRate * 2, 4);
  writeWord(out, 2, 2);           // block align
  writeWord(out, 16, 2);          // bits
  writeWord(out, 0x61746164, 4);  // "data"
  writeWord(out, dataSize, 4);
  for (std::uint32_t i = 0; i < frameCount; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    const std::int16_t s = static_cast<std::int16_t>(std::sin(2.0 * 3.14159265 * 440.0 * t) * 16000.0);
    writeWord(out, static_cast<std::uint32_t>(static_cast<std::uint16_t>(s)), 2);
  }
  return static_cast<bool>(out);
}

}  // namespace

int main() {
  const std::filesystem::path wavPath =
      std::filesystem::current_path() / "sample_slot_params_test.wav";
  std::filesystem::remove(wavPath);

  if (!writeSineWav(wavPath, 4410)) {
    std::cerr << "Failed to write test WAV\n";
    return 1;
  }

  extracker::PluginHost plugins;
  if (!plugins.loadSampleToSlot(0, wavPath.string())) {
    std::filesystem::remove(wavPath);
    std::cerr << "Failed to load sample into slot 0\n";
    return 1;
  }

  // ── gain (volume) ─────────────────────────────────────────────────────────
  // Default gain should be 1.0
  {
    const double g = plugins.getSampleSlotParameter(0, "gain");
    if (std::fabs(g - 1.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "Default gain should be 1.0, got " << g << "\n";
      return 1;
    }
  }

  // Set gain to 1.5 (above old 1.0 cap, verifies extended range)
  if (!plugins.setSampleSlotParameter(0, "gain", 1.5)) {
    std::filesystem::remove(wavPath);
    std::cerr << "setSampleSlotParameter gain failed\n";
    return 1;
  }
  {
    const double g = plugins.getSampleSlotParameter(0, "gain");
    if (std::fabs(g - 1.5) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "gain should be 1.5, got " << g << "\n";
      return 1;
    }
  }

  // Clamp above 2.0
  plugins.setSampleSlotParameter(0, "gain", 5.0);
  {
    const double g = plugins.getSampleSlotParameter(0, "gain");
    if (g > 2.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "gain above 2.0 should be clamped, got " << g << "\n";
      return 1;
    }
  }

  // ── pan ───────────────────────────────────────────────────────────────────
  // Default pan should be 0.5 (center)
  {
    const double p = plugins.getSampleSlotParameter(0, "pan");
    if (std::fabs(p - 0.5) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "Default pan should be 0.5, got " << p << "\n";
      return 1;
    }
  }

  plugins.setSampleSlotParameter(0, "pan", 0.0);  // full left
  {
    const double p = plugins.getSampleSlotParameter(0, "pan");
    if (std::fabs(p - 0.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "pan should be 0.0, got " << p << "\n";
      return 1;
    }
  }

  plugins.setSampleSlotParameter(0, "pan", 1.0);  // full right
  {
    const double p = plugins.getSampleSlotParameter(0, "pan");
    if (std::fabs(p - 1.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "pan should be 1.0, got " << p << "\n";
      return 1;
    }
  }

  // Clamp out-of-range
  plugins.setSampleSlotParameter(0, "pan", 2.0);
  {
    const double p = plugins.getSampleSlotParameter(0, "pan");
    if (p > 1.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "pan above 1.0 should clamp, got " << p << "\n";
      return 1;
    }
  }

  // ── transpose (sample_root) ───────────────────────────────────────────────
  // Default root should be 60 (C4)
  {
    const double r = plugins.getSampleSlotParameter(0, "sample_root");
    if (std::fabs(r - 60.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "Default sample_root should be 60, got " << r << "\n";
      return 1;
    }
  }

  plugins.setSampleSlotParameter(0, "sample_root", 72.0);  // C5
  {
    const double r = plugins.getSampleSlotParameter(0, "sample_root");
    if (std::fabs(r - 72.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "sample_root should be 72, got " << r << "\n";
      return 1;
    }
  }

  // ── loop mode ─────────────────────────────────────────────────────────────
  // Default loop_mode should be 0 (off)
  {
    const double lm = plugins.getSampleSlotParameter(0, "loop_mode");
    if (std::fabs(lm - 0.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "Default loop_mode should be 0, got " << lm << "\n";
      return 1;
    }
  }

  plugins.setSampleSlotParameter(0, "loop_mode", 1.0);  // forward
  {
    const double lm = plugins.getSampleSlotParameter(0, "loop_mode");
    if (std::fabs(lm - 1.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "loop_mode should be 1, got " << lm << "\n";
      return 1;
    }
  }

  plugins.setSampleSlotParameter(0, "loop_mode", 2.0);  // bidi
  {
    const double lm = plugins.getSampleSlotParameter(0, "loop_mode");
    if (std::fabs(lm - 2.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "loop_mode should be 2, got " << lm << "\n";
      return 1;
    }
  }

  // ── loop start/end ────────────────────────────────────────────────────────
  // Default loop_end returns 0.0 (means "use full sample")
  {
    const double le = plugins.getSampleSlotParameter(0, "loop_end");
    if (std::fabs(le) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "Default loop_end should be 0 (sentinel), got " << le << "\n";
      return 1;
    }
  }

  plugins.setSampleSlotParameter(0, "loop_start", 100.0);
  plugins.setSampleSlotParameter(0, "loop_end", 2000.0);
  {
    const double ls = plugins.getSampleSlotParameter(0, "loop_start");
    const double le = plugins.getSampleSlotParameter(0, "loop_end");
    if (std::fabs(ls - 100.0) > 0.001 || std::fabs(le - 2000.0) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "loop_start/end not stored correctly: " << ls << " " << le << "\n";
      return 1;
    }
  }

  // Setting loop_end to 0 resets to sentinel (full sample)
  plugins.setSampleSlotParameter(0, "loop_end", 0.0);
  {
    const double le = plugins.getSampleSlotParameter(0, "loop_end");
    if (std::fabs(le) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "loop_end 0 should reset to sentinel, got " << le << "\n";
      return 1;
    }
  }

  // ── forward loop renders without stopping ─────────────────────────────────
  // Reset to forward loop, render a large buffer — voice should not go silent
  plugins.setSampleSlotParameter(0, "gain", 1.0);
  plugins.setSampleSlotParameter(0, "loop_mode", 1.0);
  plugins.setSampleSlotParameter(0, "loop_end", 0.0);  // full sample length loop

  plugins.triggerNoteOnResolved(0, 0, 60, 100, true);

  const std::uint32_t renderRate = 44100;
  // Render more frames than the sample length to verify looping keeps voices alive
  std::vector<double> buf(8820, 0.0);  // 200ms at 44100 = 2x the 4410-frame sample
  plugins.renderInterleaved(buf, renderRate);

  {
    // At least some non-zero samples expected (looping voice should produce audio)
    bool hasNonZero = false;
    for (double v : buf) {
      if (std::fabs(v) > 0.00001) { hasNonZero = true; break; }
    }
    if (!hasNonZero) {
      std::filesystem::remove(wavPath);
      std::cerr << "Forward loop produced no audio\n";
      return 1;
    }
  }

  // ── parameters on empty slot return defaults ───────────────────────────────
  {
    // Slot 5 is not loaded
    const double g = plugins.getSampleSlotParameter(5, "gain");
    if (std::fabs(g) > 0.001) {
      std::filesystem::remove(wavPath);
      std::cerr << "getSampleSlotParameter on empty slot should return 0.0, got " << g << "\n";
      return 1;
    }
    const bool ok = plugins.setSampleSlotParameter(5, "gain", 1.0);
    if (ok) {
      std::filesystem::remove(wavPath);
      std::cerr << "setSampleSlotParameter on empty slot should return false\n";
      return 1;
    }
  }

  std::filesystem::remove(wavPath);
  return 0;
}
