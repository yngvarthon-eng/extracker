// Older songs played a sample by using its bank number as the instrument number
// (an empty instrument slot N borrowed sample-bank slot N). Converting the
// song turns instrument N into a sample instrument for bank N, so the note keeps
// its number and its sound.
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "extracker/audio_engine.hpp"
#include "extracker/module.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sample_instrument_migration.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

namespace {

void writeWord(std::ofstream& out, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
  }
}

bool writeTestWav(const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }

  constexpr std::uint16_t channelCount = 1;
  constexpr std::uint32_t sampleRate = 44100;
  constexpr std::uint16_t bitsPerSample = 16;
  constexpr std::uint16_t bytesPerSample = bitsPerSample / 8;
  constexpr std::uint32_t frameCount = 64;
  constexpr std::uint32_t dataSize = frameCount * channelCount * bytesPerSample;
  constexpr std::uint32_t riffSize = 36 + dataSize;
  constexpr std::uint32_t byteRate = sampleRate * channelCount * bytesPerSample;
  constexpr std::uint16_t blockAlign = channelCount * bytesPerSample;

  out.write("RIFF", 4);
  writeWord(out, riffSize, 4);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeWord(out, 16, 4);
  writeWord(out, 1, 2);
  writeWord(out, channelCount, 2);
  writeWord(out, sampleRate, 4);
  writeWord(out, byteRate, 4);
  writeWord(out, blockAlign, 2);
  writeWord(out, bitsPerSample, 2);
  out.write("data", 4);
  writeWord(out, dataSize, 4);

  for (std::uint32_t i = 0; i < frameCount; ++i) {
    const std::int16_t sample = (i % 8 < 4) ? 9000 : -9000;
    writeWord(out, static_cast<std::uint16_t>(sample), 2);
  }

  return static_cast<bool>(out);
}

}  // namespace

int main() {
  constexpr std::uint16_t legacySampleSlot = 20;
  const std::filesystem::path wavPath =
      std::filesystem::current_path() / "sample_legacy_instrument_fallback_test.wav";
  std::filesystem::remove(wavPath);

  if (!writeTestWav(wavPath)) {
    std::cerr << "Failed to write test WAV" << '\n';
    return 1;
  }

  extracker::PluginHost plugins;
  if (!plugins.loadSampleToSlot(legacySampleSlot, wavPath.string())) {
    std::filesystem::remove(wavPath);
    std::cerr << "Failed to load sample into slot " << legacySampleSlot << '\n';
    return 1;
  }

  extracker::Module module;
  module.reset(4, 1, 1);
  extracker::PatternEditor& pattern = module.currentEditor();
  pattern.insertNote(0, 0, 60, static_cast<std::uint8_t>(legacySampleSlot), 0, 110, true);

  // Without conversion the note has no instrument to play.
  extracker::Transport transport;
  transport.setPatternRows(4);
  transport.resetTickCount();
  extracker::AudioEngine audio;
  if (plugins.triggerNoteOn(static_cast<std::uint8_t>(legacySampleSlot), 60, 110, true)) {
    std::filesystem::remove(wavPath);
    std::cerr << "An empty instrument slot still borrowed the sample with the same number" << '\n';
    return 1;
  }

  extracker::migrateSampleReferences(module, plugins);
  std::filesystem::remove(wavPath);

  if (pattern.instrumentAt(0, 0) != legacySampleSlot ||
      plugins.pluginForInstrument(static_cast<std::uint8_t>(legacySampleSlot)) != "builtin.sample" ||
      plugins.sampleSlotForInstrument(static_cast<std::uint8_t>(legacySampleSlot)) != legacySampleSlot) {
    std::cerr << "Expected instrument " << legacySampleSlot << " linked to sample " << legacySampleSlot << '\n';
    return 1;
  }

  extracker::Sequencer sequencer;
  sequencer.update(pattern, transport, audio, plugins);
  if (plugins.activeVoiceCountForInstrument(static_cast<std::uint8_t>(legacySampleSlot)) == 0) {
    std::cerr << "Expected the converted sample instrument to play" << '\n';
    return 1;
  }

  return 0;
}
