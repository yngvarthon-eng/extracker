#include "extracker/pattern_editor.hpp"

#include <algorithm>

namespace {

constexpr int kMinMidiNote = 0;
constexpr int kMaxMidiNote = 127;
constexpr std::uint8_t kDefaultVelocity = 100;

}  // namespace

namespace extracker {

PatternEditor::PatternEditor(std::size_t rows, std::size_t channels)
    : rows_(std::max<std::size_t>(rows, 1)),
      channels_(std::max<std::size_t>(channels, 1)),
      steps_(rows_ * channels_) {}

std::string PatternEditor::status() const {
  return "PatternEditor: " + std::to_string(rows_) + "x" + std::to_string(channels_) + " grid";
}

void PatternEditor::insertNote(
    int row,
    int channel,
    int note,
    std::uint8_t instrument,
    std::uint32_t gateTicks,
    std::uint8_t velocity,
  bool retrigger,
  std::uint8_t effectCommand,
  std::uint8_t effectValue) {
  if (!isValidCell(row, channel)) {
    return;
  }

  if (note < kMinMidiNote || note > kMaxMidiNote) {
    return;
  }

  Step& step = cell(row, channel);
  step.hasNote = true;
  step.note = note;
  step.instrument = instrument;
  step.gateTicks = gateTicks;
  step.velocity = std::max<std::uint8_t>(velocity, 1);
  step.retrigger = retrigger;
  step.effectCommand = effectCommand;
  step.effectValue = effectValue;
}

void PatternEditor::setInstrument(int row, int channel, std::uint8_t instrument) {
  if (!isValidCell(row, channel)) {
    return;
  }

  Step& step = cell(row, channel);
  step.instrument = instrument;
}

void PatternEditor::setSample(int row, int channel, std::uint16_t sample) {
  if (!isValidCell(row, channel)) {
    return;
  }

  Step& step = cell(row, channel);
  step.sample = sample;
}

void PatternEditor::setGateTicks(int row, int channel, std::uint32_t gateTicks) {
  if (!isValidCell(row, channel)) {
    return;
  }

  Step& step = cell(row, channel);
  step.gateTicks = gateTicks;
}

void PatternEditor::setVelocity(int row, int channel, std::uint8_t velocity) {
  if (!isValidCell(row, channel)) {
    return;
  }

  Step& step = cell(row, channel);
  step.velocity = std::max<std::uint8_t>(velocity, 1);
}

void PatternEditor::setRetrigger(int row, int channel, bool retrigger) {
  if (!isValidCell(row, channel)) {
    return;
  }

  Step& step = cell(row, channel);
  step.retrigger = retrigger;
}

void PatternEditor::setEffect(int row, int channel, std::uint8_t effectCommand, std::uint8_t effectValue) {
  if (!isValidCell(row, channel)) {
    return;
  }

  Step& step = cell(row, channel);
  step.effectCommand = effectCommand;
  step.effectValue = effectValue;
}

void PatternEditor::clearStep(int row, int channel) {
  if (!isValidCell(row, channel)) {
    return;
  }

  Step& step = cell(row, channel);
  step.hasNote = false;
  step.note = -1;
  step.instrument = 0;
  step.sample = 0xFFFF;
  step.gateTicks = 0;
  step.velocity = kDefaultVelocity;
  step.retrigger = false;
  step.effectCommand = 0;
  step.effectValue = 0;
}

bool PatternEditor::hasNoteAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return false;
  }

  return cell(row, channel).hasNote;
}

int PatternEditor::noteAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return -1;
  }

  const Step& step = cell(row, channel);
  return step.hasNote ? step.note : -1;
}

std::uint8_t PatternEditor::instrumentAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return 0;
  }

  const Step& step = cell(row, channel);
  return step.hasNote ? step.instrument : 0;
}

std::uint16_t PatternEditor::sampleAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return 0xFFFF;
  }

  const Step& step = cell(row, channel);
  return step.hasNote ? step.sample : 0xFFFF;
}

std::uint32_t PatternEditor::gateTicksAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return 0;
  }

  const Step& step = cell(row, channel);
  return step.hasNote ? step.gateTicks : 0;
}

std::uint8_t PatternEditor::velocityAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return kDefaultVelocity;
  }

  const Step& step = cell(row, channel);
  return step.hasNote ? step.velocity : kDefaultVelocity;
}

bool PatternEditor::retriggerAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return false;
  }

  const Step& step = cell(row, channel);
  return step.hasNote && step.retrigger;
}

std::uint8_t PatternEditor::effectCommandAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return 0;
  }

  const Step& step = cell(row, channel);
  return step.effectCommand;
}

std::uint8_t PatternEditor::effectValueAt(int row, int channel) const {
  if (!isValidCell(row, channel)) {
    return 0;
  }

  const Step& step = cell(row, channel);
  return step.effectValue;
}

std::size_t PatternEditor::rows() const {
  return rows_;
}

std::size_t PatternEditor::channels() const {
  return channels_;
}

void PatternEditor::resizeRows(std::size_t newRows) {
  std::size_t targetRows = std::max<std::size_t>(newRows, 1);
  if (targetRows == rows_) {
    return;
  }

  std::vector<Step> resized(targetRows * channels_);
  std::size_t rowsToCopy = std::min(rows_, targetRows);
  for (std::size_t row = 0; row < rowsToCopy; ++row) {
    for (std::size_t channel = 0; channel < channels_; ++channel) {
      resized[row * channels_ + channel] = steps_[row * channels_ + channel];
    }
  }

  steps_.swap(resized);
  rows_ = targetRows;
}

void PatternEditor::resizeChannels(std::size_t newChannels) {
  std::size_t targetChannels = std::max<std::size_t>(newChannels, 1);
  if (targetChannels == channels_) {
    return;
  }

  std::vector<Step> resized(rows_ * targetChannels);
  std::size_t channelsToCopy = std::min(channels_, targetChannels);
  for (std::size_t row = 0; row < rows_; ++row) {
    for (std::size_t channel = 0; channel < channelsToCopy; ++channel) {
      resized[row * targetChannels + channel] = steps_[row * channels_ + channel];
    }
  }

  steps_.swap(resized);
  channels_ = targetChannels;
}

bool PatternEditor::isValidCell(int row, int channel) const {
  return row >= 0 && channel >= 0 &&
         static_cast<std::size_t>(row) < rows_ &&
         static_cast<std::size_t>(channel) < channels_;
}

PatternEditor::Step& PatternEditor::cell(int row, int channel) {
  return steps_[static_cast<std::size_t>(row) * channels_ + static_cast<std::size_t>(channel)];
}

const PatternEditor::Step& PatternEditor::cell(int row, int channel) const {
  return steps_[static_cast<std::size_t>(row) * channels_ + static_cast<std::size_t>(channel)];
}

}  // namespace extracker
