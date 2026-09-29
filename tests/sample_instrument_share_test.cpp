// A sample instrument plays the sample-bank slot it is linked to, sharing the
// data instead of copying it: bank edits reach every linked instrument,
// clearing the bank slot silences them (they stay linked, and play again once
// the slot is reloaded), and loading a WAV straight into a linked instrument
// never overwrites the bank sample.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "extracker/plugin_host.hpp"

namespace {

void writeWord(std::ofstream& out, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
  }
}

bool writeTestWav(const std::filesystem::path& path, std::uint32_t frameCount) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  constexpr std::uint16_t channelCount = 1;
  constexpr std::uint32_t sampleRate = 44100;
  constexpr std::uint16_t bitsPerSample = 16;
  const std::uint32_t dataSize = frameCount * channelCount * (bitsPerSample / 8);
  out.write("RIFF", 4);
  writeWord(out, 36 + dataSize, 4);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeWord(out, 16, 4);
  writeWord(out, 1, 2);
  writeWord(out, channelCount, 2);
  writeWord(out, sampleRate, 4);
  writeWord(out, sampleRate * channelCount * (bitsPerSample / 8), 4);
  writeWord(out, channelCount * (bitsPerSample / 8), 2);
  writeWord(out, bitsPerSample, 2);
  out.write("data", 4);
  writeWord(out, dataSize, 4);
  for (std::uint32_t i = 0; i < frameCount; ++i) {
    const std::int16_t sample = (i % 8 < 4) ? 8000 : -8000;
    writeWord(out, static_cast<std::uint16_t>(sample), 2);
  }
  return static_cast<bool>(out);
}

std::uintmax_t savedSize(extracker::PluginHost& plugins, std::uint8_t instrument,
                         const std::filesystem::path& path) {
  std::filesystem::remove(path);
  if (!plugins.saveSampleFromInstrument(instrument, path.string())) {
    return 0;
  }
  return std::filesystem::file_size(path);
}

double renderEnergy(extracker::PluginHost& plugins, std::uint8_t instrument) {
  extracker::InstrumentMixBuffers mix;
  mix.beginBlock(64);
  plugins.renderPerInstrument(mix, 44100);
  if (!mix.isTouched(instrument)) {
    return 0.0;
  }
  double energy = 0.0;
  for (const double v : mix.buffer(instrument)) {
    energy += v * v;
  }
  return energy;
}

int fail(const std::string& message, const std::filesystem::path& dir) {
  std::cerr << message << '\n';
  std::filesystem::remove_all(dir);
  return 1;
}

}  // namespace

int main() {
  const auto dir = std::filesystem::current_path() / "sample_instrument_share_test_files";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto longWav = dir / "long.wav";
  const auto otherWav = dir / "other.wav";
  if (!writeTestWav(longWav, 256) || !writeTestWav(otherWav, 32)) {
    return fail("Failed to write test WAVs", dir);
  }

  extracker::PluginHost plugins;
  constexpr std::uint16_t kBank = 5;
  constexpr std::uint8_t kInstrA = 40;
  constexpr std::uint8_t kInstrB = 200;
  if (!plugins.loadSampleToSlot(kBank, longWav.string()) ||
      !plugins.assignSampleSlotToInstrument(kBank, kInstrA) ||
      !plugins.assignSampleSlotToInstrument(kBank, kInstrB)) {
    return fail("Failed to load the bank sample and link two instruments", dir);
  }
  if (plugins.sampleSlotForInstrument(kInstrA) != kBank || plugins.sampleSlotForInstrument(kInstrB) != kBank) {
    return fail("Instruments do not report their linked bank slot", dir);
  }

  // Bank edits reach both linked instruments.
  const auto fullSize = savedSize(plugins, kInstrA, dir / "a_full.wav");
  if (fullSize == 0 || !plugins.trimSampleSlot(kBank, 0, 64)) {
    return fail("Could not save or trim the sample", dir);
  }
  const auto trimmedA = savedSize(plugins, kInstrA, dir / "a_trim.wav");
  const auto trimmedB = savedSize(plugins, kInstrB, dir / "b_trim.wav");
  if (trimmedA == 0 || trimmedA >= fullSize || trimmedA != trimmedB) {
    return fail("Trimming the bank slot did not reach the linked instruments (full=" +
                std::to_string(fullSize) + " a=" + std::to_string(trimmedA) +
                " b=" + std::to_string(trimmedB) + ")", dir);
  }
  if (!plugins.setSampleSlotParameter(kBank, "sample_root", 48.0) ||
      plugins.getInstrumentParameter(kInstrB, "sample_root") != 48.0) {
    return fail("Bank sample properties are not shared with linked instruments", dir);
  }

  // Each instrument keeps its own voices: A plays, B stays silent.
  plugins.triggerNoteOn(kInstrA, 60, 127, true);
  extracker::InstrumentMixBuffers mix;
  mix.beginBlock(32);
  plugins.renderPerInstrument(mix, 44100);
  double energyB = 0.0;
  for (const double v : mix.buffer(kInstrB)) {
    energyB += v * v;
  }
  if (!mix.isTouched(kInstrA) || energyB != 0.0) {
    return fail("Voices leaked between instruments sharing one sample", dir);
  }
  plugins.allNotesOff();

  // Loading a WAV straight into a linked instrument detaches it first.
  const auto bankFramesBefore = plugins.sampleFrameCountForSlot(kBank);
  if (!plugins.loadSampleToInstrument(kInstrA, otherWav.string())) {
    return fail("loadSampleToInstrument failed", dir);
  }
  if (plugins.sampleFrameCountForSlot(kBank) != bankFramesBefore) {
    return fail("Loading into a linked instrument overwrote the bank sample", dir);
  }
  if (plugins.sampleSlotForInstrument(kInstrA) != -1) {
    return fail("Instrument stayed linked after loading its own sample", dir);
  }

  // Clearing the bank slot silences the instruments still linked to it.
  if (!plugins.clearSampleSlot(kBank)) {
    return fail("clearSampleSlot failed", dir);
  }
  if (plugins.sampleSlotForInstrument(kInstrB) != kBank) {
    return fail("Instrument lost its link when the bank slot was cleared", dir);
  }
  plugins.triggerNoteOn(kInstrB, 60, 127, true);
  if (renderEnergy(plugins, kInstrB) != 0.0) {
    return fail("Instrument kept playing a sample that was cleared from the bank", dir);
  }
  plugins.allNotesOff();

  // Loading a sample into the slot again brings the linked instrument back.
  if (!plugins.loadSampleToSlot(kBank, longWav.string())) {
    return fail("Reloading the bank slot failed", dir);
  }
  plugins.triggerNoteOn(kInstrB, 60, 127, true);
  if (renderEnergy(plugins, kInstrB) <= 0.0) {
    return fail("Linked instrument stayed silent after its bank slot was reloaded", dir);
  }

  std::filesystem::remove_all(dir);
  return 0;
}
