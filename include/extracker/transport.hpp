#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace extracker {

class Transport {
public:
  Transport();
  ~Transport();

  void setTempoBpm(double bpm);
  void setTicksPerBeat(std::uint32_t ticksPerBeat);
  void setTicksPerRow(std::uint32_t ticksPerRow);
  void setPatternRows(std::uint32_t patternRows);

  double tempoBpm() const;
  std::uint32_t ticksPerBeat() const;
  std::uint32_t ticksPerRow() const;
  std::uint32_t patternRows() const;

  bool play();
  void stop();
  bool isPlaying() const;

  std::uint64_t tickCount() const;
  std::uint64_t rowAdvanceCount() const;
  std::uint32_t currentRow() const;
  void resetTickCount();
  void jumpToRow(std::uint32_t row);
  void advanceExternalTick();

private:
  void runClock();

  std::atomic<bool> playing_;
  std::atomic<std::uint64_t> tickCount_;
  std::atomic<std::uint64_t> rowAdvanceCount_;
  std::atomic<std::uint32_t> currentRow_;
  std::atomic<double> tempoBpm_;
  std::atomic<std::uint32_t> ticksPerBeat_;
  std::atomic<std::uint32_t> ticksPerRow_;
  std::atomic<std::uint32_t> patternRows_;
  std::thread clockThread_;
};

}  // namespace extracker
