#include "extracker/transport.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace extracker {

Transport::Transport()
    : playing_(false),
      tickCount_(0),
  rowAdvanceCount_(0),
  currentRow_(0),
      tempoBpm_(125.0),
  ticksPerBeat_(6),
  ticksPerRow_(6),
  patternRows_(64) {}

Transport::~Transport() {
  stop();
}

void Transport::setTempoBpm(double bpm) {
  if (bpm > 0.0) {
    tempoBpm_.store(bpm);
  }
}

void Transport::setTicksPerBeat(std::uint32_t ticksPerBeat) {
  if (ticksPerBeat > 0) {
    ticksPerBeat_.store(ticksPerBeat);
  }
}

void Transport::setTicksPerRow(std::uint32_t ticksPerRow) {
  if (ticksPerRow > 0) {
    ticksPerRow_.store(ticksPerRow);
  }
}

void Transport::setPatternRows(std::uint32_t patternRows) {
  if (patternRows > 0) {
    patternRows_.store(patternRows);
  }
}

double Transport::tempoBpm() const {
  return tempoBpm_.load();
}

std::uint32_t Transport::ticksPerBeat() const {
  return ticksPerBeat_.load();
}

std::uint32_t Transport::ticksPerRow() const {
  return ticksPerRow_.load();
}

std::uint32_t Transport::patternRows() const {
  return patternRows_.load();
}

bool Transport::play() {
  if (playing_.load()) {
    return false;
  }

  if (clockThread_.joinable()) {
    clockThread_.join();
  }

  bool expected = false;
  if (!playing_.compare_exchange_strong(expected, true)) {
    return false;
  }

  clockThread_ = std::thread([this]() { runClock(); });
  return true;
}

void Transport::stop() {
  playing_.store(false);
  if (clockThread_.joinable()) {
    clockThread_.join();
  }
}

bool Transport::isPlaying() const {
  return playing_.load();
}

std::uint64_t Transport::tickCount() const {
  return tickCount_.load();
}

std::uint64_t Transport::rowAdvanceCount() const {
  return rowAdvanceCount_.load();
}

std::uint32_t Transport::currentRow() const {
  return currentRow_.load();
}

void Transport::resetTickCount() {
  tickCount_.store(0);
  rowAdvanceCount_.store(0);
  currentRow_.store(0);
}

void Transport::jumpToRow(std::uint32_t row) {
  std::uint32_t rows = std::max<std::uint32_t>(patternRows_.load(), 1);
  std::uint32_t targetRow = row % rows;

  std::uint64_t ticksPerRow = std::max<std::uint32_t>(ticksPerRow_.load(), 1);
  tickCount_.store(static_cast<std::uint64_t>(targetRow) * ticksPerRow);
  rowAdvanceCount_.store(targetRow);
  currentRow_.store(targetRow);
}

void Transport::advanceExternalTick() {
  std::uint64_t ticks = tickCount_.fetch_add(1) + 1;
  std::uint32_t ticksPerRow = std::max<std::uint32_t>(ticksPerRow_.load(), 1);
  if (ticks % ticksPerRow == 0) {
    std::uint64_t rows = rowAdvanceCount_.fetch_add(1) + 1;
    std::uint32_t patternRows = std::max<std::uint32_t>(patternRows_.load(), 1);
    currentRow_.store(static_cast<std::uint32_t>(rows % patternRows));
  }
}

void Transport::runClock() {
  while (playing_.load()) {
    double bpm = std::max(tempoBpm_.load(), 1.0);
    std::uint32_t tpb = std::max<std::uint32_t>(ticksPerBeat_.load(), 1);
    double tickSeconds = 60.0 / (bpm * static_cast<double>(tpb));

    std::this_thread::sleep_for(std::chrono::duration<double>(tickSeconds));

    if (!playing_.load()) {
      break;
    }

    std::uint64_t ticks = tickCount_.fetch_add(1) + 1;
    std::uint32_t ticksPerRow = std::max<std::uint32_t>(ticksPerRow_.load(), 1);
    if (ticks % ticksPerRow == 0) {
      std::uint64_t rows = rowAdvanceCount_.fetch_add(1) + 1;
      std::uint32_t patternRows = std::max<std::uint32_t>(patternRows_.load(), 1);
      currentRow_.store(static_cast<std::uint32_t>(rows % patternRows));
    }
  }
}

}  // namespace extracker
