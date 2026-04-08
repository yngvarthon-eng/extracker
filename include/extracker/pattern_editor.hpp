#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace extracker {

class PatternEditor {
public:
  struct Step {
    bool hasNote = false;
    int note = -1;
    std::uint8_t instrument = 0;
    std::uint16_t sample = 0xFFFF;  // kInvalidSampleSlot; 0xFFFF means no sample, use instrument
    std::uint32_t gateTicks = 0;
    std::uint8_t velocity = 100;
    bool retrigger = false;
    std::uint8_t effectCommand = 0;
    std::uint8_t effectValue = 0;
  };

  PatternEditor(std::size_t rows = 64, std::size_t channels = 8);

  std::string status() const;
  void insertNote(
      int row,
      int channel,
      int note,
      std::uint8_t instrument = 0,
      std::uint32_t gateTicks = 0,
      std::uint8_t velocity = 100,
      bool retrigger = false,
      std::uint8_t effectCommand = 0,
      std::uint8_t effectValue = 0);
  void setInstrument(int row, int channel, std::uint8_t instrument);
  void setSample(int row, int channel, std::uint16_t sample);
  void setGateTicks(int row, int channel, std::uint32_t gateTicks);
  void setVelocity(int row, int channel, std::uint8_t velocity);
  void setRetrigger(int row, int channel, bool retrigger);
  void setEffect(int row, int channel, std::uint8_t effectCommand, std::uint8_t effectValue);
  void clearStep(int row, int channel);

  bool hasNoteAt(int row, int channel) const;
  int noteAt(int row, int channel) const;
  std::uint8_t instrumentAt(int row, int channel) const;
  std::uint16_t sampleAt(int row, int channel) const;
  std::uint32_t gateTicksAt(int row, int channel) const;
  std::uint8_t velocityAt(int row, int channel) const;
  bool retriggerAt(int row, int channel) const;
  std::uint8_t effectCommandAt(int row, int channel) const;
  std::uint8_t effectValueAt(int row, int channel) const;

  std::size_t rows() const;
  std::size_t channels() const;
  void resizeRows(std::size_t newRows);
  void resizeChannels(std::size_t newChannels);

private:
  bool isValidCell(int row, int channel) const;
  Step& cell(int row, int channel);
  const Step& cell(int row, int channel) const;

  std::size_t rows_;
  std::size_t channels_;
  std::vector<Step> steps_;
};

}  // namespace extracker
