#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "extracker/plugin_host.hpp"

namespace {

void writeWord(std::ofstream& out, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
  }
}

bool writeRampWav(const std::filesystem::path& path, std::uint32_t frameCount) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }

  constexpr std::uint16_t channelCount = 1;
  constexpr std::uint32_t sampleRate = 44100;
  constexpr std::uint16_t bitsPerSample = 16;
  constexpr std::uint16_t bytesPerSample = bitsPerSample / 8;
  const std::uint32_t dataSize = frameCount * channelCount * bytesPerSample;
  const std::uint32_t riffSize = 36 + dataSize;
  const std::uint32_t byteRate = sampleRate * channelCount * bytesPerSample;
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
    const std::int16_t sample = static_cast<std::int16_t>(500 + (i * 200));
    writeWord(out, static_cast<std::uint16_t>(sample), 2);
  }

  return static_cast<bool>(out);
}

bool approxEqual(float a, float b, float epsilon = 0.0001f) {
  return std::fabs(a - b) <= epsilon;
}

}  // namespace

int main() {
  const std::filesystem::path wavPath =
      std::filesystem::current_path() / "sample_edit_workflow_test.wav";
  std::filesystem::remove(wavPath);

  if (!writeRampWav(wavPath, 64)) {
    std::cerr << "Failed to write test WAV" << '\n';
    return 1;
  }

  extracker::PluginHost plugins;
  if (!plugins.loadSampleToSlot(0, wavPath.string())) {
    std::filesystem::remove(wavPath);
    std::cerr << "Failed to load sample into slot 0" << '\n';
    return 1;
  }

  const std::size_t initialFrames = plugins.sampleFrameCountForSlot(0);
  const std::size_t initialSourceFrames = plugins.sampleSourceFrameCountForSlot(0);
  if (initialFrames != 64 || initialSourceFrames != 64) {
    std::filesystem::remove(wavPath);
    std::cerr << "Unexpected initial frame counts: current=" << initialFrames
              << " source=" << initialSourceFrames << '\n';
    return 1;
  }

  const auto originalWaveform = plugins.sampleWaveformForSlot(0, initialFrames);
  if (originalWaveform.size() != initialFrames) {
    std::filesystem::remove(wavPath);
    std::cerr << "Unexpected original waveform size" << '\n';
    return 1;
  }

  if (!plugins.trimSampleSlot(0, 8, 24)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Trim operation failed" << '\n';
    return 1;
  }

  const std::size_t trimmedFrames = plugins.sampleFrameCountForSlot(0);
  const std::size_t sourceFramesAfterTrim = plugins.sampleSourceFrameCountForSlot(0);
  if (trimmedFrames != 16 || sourceFramesAfterTrim != 64) {
    std::filesystem::remove(wavPath);
    std::cerr << "Unexpected post-trim frame counts: current=" << trimmedFrames
              << " source=" << sourceFramesAfterTrim << '\n';
    return 1;
  }

  const auto trimmedWaveform = plugins.sampleWaveformForSlot(0, trimmedFrames);
  if (trimmedWaveform.size() != trimmedFrames) {
    std::filesystem::remove(wavPath);
    std::cerr << "Unexpected trimmed waveform size" << '\n';
    return 1;
  }

  if (!approxEqual(trimmedWaveform.front(), originalWaveform[8]) ||
      !approxEqual(trimmedWaveform.back(), originalWaveform[23])) {
    std::filesystem::remove(wavPath);
    std::cerr << "Trimmed waveform boundaries do not match source selection" << '\n';
    return 1;
  }

  if (!plugins.normalizeSampleSlot(0, 0, trimmedFrames)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Normalize operation failed" << '\n';
    return 1;
  }

  const auto normalizedWaveform = plugins.sampleWaveformForSlot(0, trimmedFrames);
  const float maxAbs = *std::max_element(
      normalizedWaveform.begin(),
      normalizedWaveform.end(),
      [](float lhs, float rhs) { return std::fabs(lhs) < std::fabs(rhs); });
  if (std::fabs(std::fabs(maxAbs) - 1.0f) > 0.001f) {
    std::filesystem::remove(wavPath);
    std::cerr << "Normalize did not scale selection to unity peak" << '\n';
    return 1;
  }

  if (!plugins.fadeInSampleSlot(0, 0, trimmedFrames)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Fade-in operation failed" << '\n';
    return 1;
  }

  const auto fadeInWaveform = plugins.sampleWaveformForSlot(0, trimmedFrames);
  if (fadeInWaveform.empty() || std::fabs(fadeInWaveform.front()) > 0.0001f) {
    std::filesystem::remove(wavPath);
    std::cerr << "Fade-in did not pull first sample to zero" << '\n';
    return 1;
  }

  if (!plugins.restoreSampleSlotSource(0)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Restore-source operation failed" << '\n';
    return 1;
  }

  const std::size_t restoredFrames = plugins.sampleFrameCountForSlot(0);
  const std::size_t restoredSourceFrames = plugins.sampleSourceFrameCountForSlot(0);
  if (restoredFrames != 64 || restoredSourceFrames != 64) {
    std::filesystem::remove(wavPath);
    std::cerr << "Unexpected post-restore frame counts: current=" << restoredFrames
              << " source=" << restoredSourceFrames << '\n';
    return 1;
  }

  const auto restoredWaveform = plugins.sampleWaveformForSlot(0, restoredFrames);
  if (restoredWaveform.size() != restoredFrames ||
      !approxEqual(restoredWaveform.front(), originalWaveform.front())) {
    std::filesystem::remove(wavPath);
    std::cerr << "Restore-source did not restore original waveform" << '\n';
    return 1;
  }

  if (!plugins.fadeOutSampleSlot(0, 0, restoredFrames)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Fade-out operation failed" << '\n';
    return 1;
  }

  const auto fadeOutWaveform = plugins.sampleWaveformForSlot(0, restoredFrames);
  if (fadeOutWaveform.empty() || std::fabs(fadeOutWaveform.back()) > 0.0001f) {
    std::filesystem::remove(wavPath);
    std::cerr << "Fade-out did not pull last sample to zero" << '\n';
    return 1;
  }

  if (plugins.trimSampleSlot(0, 10, 10)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Invalid trim range unexpectedly succeeded" << '\n';
    return 1;
  }

  // Reverse: restore the pristine ramp, then reverse the whole sample and
  // confirm the endpoints swap.
  if (!plugins.restoreSampleSlotSource(0)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Restore before reverse failed" << '\n';
    return 1;
  }

  const std::size_t reverseFrames = plugins.sampleFrameCountForSlot(0);
  if (!plugins.reverseSampleSlot(0, 0, reverseFrames)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Reverse operation failed" << '\n';
    return 1;
  }

  const auto reversedWaveform = plugins.sampleWaveformForSlot(0, reverseFrames);
  if (reversedWaveform.size() != reverseFrames ||
      !approxEqual(reversedWaveform.front(), originalWaveform.back()) ||
      !approxEqual(reversedWaveform.back(), originalWaveform.front())) {
    std::filesystem::remove(wavPath);
    std::cerr << "Reverse did not swap sample endpoints" << '\n';
    return 1;
  }

  // Reversing twice restores the original order.
  if (!plugins.reverseSampleSlot(0, 0, reverseFrames)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Second reverse operation failed" << '\n';
    return 1;
  }
  const auto doubleReversed = plugins.sampleWaveformForSlot(0, reverseFrames);
  if (!approxEqual(doubleReversed.front(), originalWaveform.front()) ||
      !approxEqual(doubleReversed.back(), originalWaveform.back())) {
    std::filesystem::remove(wavPath);
    std::cerr << "Double reverse did not restore original order" << '\n';
    return 1;
  }

  // Invalid (empty) range is rejected.
  if (plugins.reverseSampleSlot(0, 10, 10)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Invalid reverse range unexpectedly succeeded" << '\n';
    return 1;
  }

  // Resample: restore pristine 64-frame ramp at 44100, halve the rate, then
  // double it back. Pitch-preserving resample changes frame count + stored rate.
  if (!plugins.restoreSampleSlotSource(0)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Restore before resample failed" << '\n';
    return 1;
  }

  if (!plugins.resampleSampleSlot(0, 22050)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Downsample operation failed" << '\n';
    return 1;
  }
  const std::size_t downFrames = plugins.sampleFrameCountForSlot(0);
  if (plugins.sampleRateForSlot(0) != 22050 ||
      downFrames < 31 || downFrames > 33) {  // ~64 * 22050/44100 = 32
    std::filesystem::remove(wavPath);
    std::cerr << "Unexpected downsample result: rate=" << plugins.sampleRateForSlot(0)
              << " frames=" << downFrames << '\n';
    return 1;
  }

  if (!plugins.resampleSampleSlot(0, 44100)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Upsample operation failed" << '\n';
    return 1;
  }
  const std::size_t upFrames = plugins.sampleFrameCountForSlot(0);
  if (plugins.sampleRateForSlot(0) != 44100 ||
      upFrames < 63 || upFrames > 65) {  // ~32 * 44100/22050 = 64
    std::filesystem::remove(wavPath);
    std::cerr << "Unexpected upsample result: rate=" << plugins.sampleRateForSlot(0)
              << " frames=" << upFrames << '\n';
    return 1;
  }

  // Resample guards: same-rate and out-of-range rate are rejected.
  if (plugins.resampleSampleSlot(0, 44100)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Same-rate resample unexpectedly succeeded" << '\n';
    return 1;
  }
  if (plugins.resampleSampleSlot(0, 500)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Out-of-range resample unexpectedly succeeded" << '\n';
    return 1;
  }

  // Bit depth: restore source, quantize to 2 bits over the whole sample. With
  // levels = 2^(2-1) = 2, every value must land on a multiple of 0.5.
  if (!plugins.restoreSampleSlotSource(0)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Restore before bitdepth failed" << '\n';
    return 1;
  }
  const std::size_t bitFrames = plugins.sampleFrameCountForSlot(0);
  if (!plugins.bitDepthSampleSlot(0, 2, 0, bitFrames)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Bitdepth operation failed" << '\n';
    return 1;
  }
  const auto crushed = plugins.sampleWaveformForSlot(0, bitFrames);
  for (float v : crushed) {
    const float steps = v / 0.5f;
    if (!approxEqual(steps, std::round(steps))) {
      std::filesystem::remove(wavPath);
      std::cerr << "Bitdepth value not on quantization grid: " << v << '\n';
      return 1;
    }
  }

  // Bit depth guards: invalid bit counts and empty range are rejected.
  if (plugins.bitDepthSampleSlot(0, 0, 0, bitFrames) ||
      plugins.bitDepthSampleSlot(0, 33, 0, bitFrames)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Invalid bit count unexpectedly succeeded" << '\n';
    return 1;
  }
  if (plugins.bitDepthSampleSlot(0, 8, 10, 10)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Invalid bitdepth range unexpectedly succeeded" << '\n';
    return 1;
  }

  // Loop crossfade: restore pristine ramp, set a forward loop over [16,48),
  // then crossfade. The equal-power blend pulls the last loop-tail frame (47)
  // toward the pre-loopStart frame (15), making the wrap seamless.
  if (!plugins.restoreSampleSlotSource(0)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Restore before loop-crossfade failed" << '\n';
    return 1;
  }
  plugins.setSampleSlotParameter(0, "loop_mode", 1.0);   // forward
  plugins.setSampleSlotParameter(0, "loop_start", 16.0);
  plugins.setSampleSlotParameter(0, "loop_end", 48.0);

  const auto preXfade = plugins.sampleWaveformForSlot(0, plugins.sampleFrameCountForSlot(0));
  const float origTail = preXfade[47];     // ramp value at loop_end-1
  const float preStartVal = preXfade[15];  // ramp value at loop_start-1

  if (!plugins.crossfadeLoopSampleSlot(0, 8)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Loop-crossfade operation failed" << '\n';
    return 1;
  }
  const auto postXfade = plugins.sampleWaveformForSlot(0, plugins.sampleFrameCountForSlot(0));
  if (approxEqual(postXfade[47], origTail)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Loop-crossfade did not modify the loop tail" << '\n';
    return 1;
  }
  if (!approxEqual(postXfade[47], preStartVal, 0.01f)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Loop-crossfade tail end did not match pre-loopStart frame" << '\n';
    return 1;
  }

  // Guards: no pre-roll (loop_start=0) and no loop (mode off) are rejected.
  plugins.restoreSampleSlotSource(0);
  plugins.setSampleSlotParameter(0, "loop_mode", 1.0);
  plugins.setSampleSlotParameter(0, "loop_start", 0.0);
  plugins.setSampleSlotParameter(0, "loop_end", 48.0);
  if (plugins.crossfadeLoopSampleSlot(0, 8)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Loop-crossfade with zero loop start unexpectedly succeeded" << '\n';
    return 1;
  }
  plugins.setSampleSlotParameter(0, "loop_mode", 0.0);   // off
  plugins.setSampleSlotParameter(0, "loop_start", 16.0);
  if (plugins.crossfadeLoopSampleSlot(0, 8)) {
    std::filesystem::remove(wavPath);
    std::cerr << "Loop-crossfade with loop off unexpectedly succeeded" << '\n';
    return 1;
  }

  std::filesystem::remove(wavPath);
  return 0;
}
