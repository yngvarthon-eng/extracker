#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace extracker {

// Number of instrument slots (00-FF), the range of a pattern step's 8-bit
// instrument field. Every per-instrument table (plugins, filters, effects,
// pitch, depth, reverb sends) is sized by this one constant.
inline constexpr std::size_t kInstrumentSlotCount = 256;

// Per-instrument mono buffers for one audio block, allocated once and reused.
// Only instruments that are touched in a block get zeroed, rendered and mixed,
// so the cost scales with the instruments in use rather than the slot count.
// Not thread-safe: each audio thread owns its own instance.
class InstrumentMixBuffers {
public:
  // Starts a new block of `frames` frames. Allocates only when the block size
  // grows past what was allocated before.
  void beginBlock(std::size_t frames) {
    for (const std::uint8_t i : active_) {
      touched_[i] = false;
    }
    active_.clear();
    if (active_.capacity() < kInstrumentSlotCount) {
      active_.reserve(kInstrumentSlotCount);
    }
    frames_ = frames;
  }

  // The buffer for `instrument`, zeroed on first use in this block.
  std::vector<double>& touch(std::size_t instrument) {
    std::vector<double>& buffer = buffers_[instrument];
    if (!touched_[instrument]) {
      touched_[instrument] = true;
      active_.push_back(static_cast<std::uint8_t>(instrument));
      buffer.resize(frames_);
      std::fill(buffer.begin(), buffer.end(), 0.0);
    }
    return buffer;
  }

  bool isTouched(std::size_t instrument) const { return touched_[instrument]; }
  const std::vector<double>& buffer(std::size_t instrument) const { return buffers_[instrument]; }
  // Instruments touched this block, in first-touch order.
  const std::vector<std::uint8_t>& active() const { return active_; }
  std::size_t frames() const { return frames_; }

private:
  std::array<std::vector<double>, kInstrumentSlotCount> buffers_;
  std::array<bool, kInstrumentSlotCount> touched_{};
  std::vector<std::uint8_t> active_;
  std::size_t frames_ = 0;
};

}  // namespace extracker
