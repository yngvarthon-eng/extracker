#include "extracker/module.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace extracker {

Module::Module(std::size_t rows, std::size_t channels)
    : currentPatternIndex_(0), rows_(rows), channels_(channels) {
  reset(rows, channels, 1);
}

void Module::reset(std::size_t rows, std::size_t channels, std::size_t patternCount) {
  rows_ = rows;
  channels_ = channels;
  currentPatternIndex_ = 0;
  inheritSwingOnInsert_ = false;
  rowEditAllChannels_ = false;
  message_.clear();
  patterns_.clear();
  patternSwingPercent_.clear();
  songOrder_.clear();

  const std::size_t safePatternCount = std::max<std::size_t>(patternCount, 1);
  patterns_.reserve(safePatternCount);
  for (std::size_t i = 0; i < safePatternCount; ++i) {
    patterns_.push_back(std::make_unique<PatternEditor>(rows_, channels_));
    patternSwingPercent_.push_back(50);
    songOrder_.push_back(i);
  }
}

std::size_t Module::patternCount() const {
  return patterns_.size();
}

std::size_t Module::currentPattern() const {
  return currentPatternIndex_;
}

bool Module::switchToPattern(std::size_t patternIndex) {
  if (patternIndex >= patterns_.size()) {
    return false;
  }
  currentPatternIndex_ = patternIndex;
  return true;
}

bool Module::insertPatternAfter() {
  const std::size_t previousPatternIndex = currentPatternIndex_;
  std::size_t insertPos = currentPatternIndex_ + 1;
  if (insertPos > patterns_.size()) {
    insertPos = patterns_.size();
  }
  const std::uint8_t swingForInsertedPattern = inheritSwingOnInsert_ ? currentPatternSwing() : 50;

  auto newPattern = std::make_unique<PatternEditor>(rows_, channels_);
  patterns_.insert(patterns_.begin() + static_cast<long>(insertPos), std::move(newPattern));
  patternSwingPercent_.insert(patternSwingPercent_.begin() + static_cast<long>(insertPos), swingForInsertedPattern);

  for (std::size_t& entry : songOrder_) {
    if (entry >= insertPos) {
      ++entry;
    }
  }

  // Switch to the newly inserted pattern
  currentPatternIndex_ = insertPos;
  const std::size_t anchorSongEntry = firstSongEntryForPattern(previousPatternIndex);
  songOrder_.insert(songOrder_.begin() + static_cast<long>(std::min(anchorSongEntry + 1, songOrder_.size())), currentPatternIndex_);
  return true;
}

bool Module::insertPatternBefore() {
  const std::size_t previousPatternIndex = currentPatternIndex_;
  std::size_t insertPos = currentPatternIndex_;
  const std::uint8_t swingForInsertedPattern = inheritSwingOnInsert_ ? currentPatternSwing() : 50;

  auto newPattern = std::make_unique<PatternEditor>(rows_, channels_);
  patterns_.insert(patterns_.begin() + static_cast<long>(insertPos), std::move(newPattern));
  patternSwingPercent_.insert(patternSwingPercent_.begin() + static_cast<long>(insertPos), swingForInsertedPattern);

  for (std::size_t& entry : songOrder_) {
    if (entry >= insertPos) {
      ++entry;
    }
  }

  // Stay on the current index (which now points to the new pattern)
  // currentPatternIndex_ stays the same since all indices shifted down
  const std::size_t anchorSongEntry = firstSongEntryForPattern(previousPatternIndex + 1);
  songOrder_.insert(songOrder_.begin() + static_cast<long>(std::min(anchorSongEntry, songOrder_.size())), currentPatternIndex_);
  return true;
}

bool Module::removeCurrentPattern() {
  if (patterns_.size() <= 1) {
    // Must have at least one pattern
    return false;
  }
  
  const std::size_t removedPattern = currentPatternIndex_;
  patterns_.erase(patterns_.begin() + static_cast<long>(currentPatternIndex_));
  if (removedPattern < patternSwingPercent_.size()) {
    patternSwingPercent_.erase(patternSwingPercent_.begin() + static_cast<long>(removedPattern));
  }

  std::vector<std::size_t> nextSongOrder;
  nextSongOrder.reserve(songOrder_.size());
  for (std::size_t entry : songOrder_) {
    if (entry == removedPattern) {
      continue;
    }
    if (entry > removedPattern) {
      --entry;
    }
    nextSongOrder.push_back(entry);
  }

  // Adjust current index if we removed the last pattern
  if (currentPatternIndex_ >= patterns_.size()) {
    currentPatternIndex_ = patterns_.size() - 1;
  }

  if (nextSongOrder.empty()) {
    nextSongOrder.push_back(currentPatternIndex_);
  }
  songOrder_ = std::move(nextSongOrder);

  return true;
}

bool Module::duplicateCurrentPattern() {
  return duplicatePattern(currentPatternIndex_);
}

bool Module::duplicatePattern(std::size_t sourcePattern) {
  if (patterns_.empty() || sourcePattern >= patterns_.size()) {
    return false;
  }

  const std::size_t insertPos = sourcePattern + 1;

  auto duplicated = std::make_unique<PatternEditor>(*patterns_[sourcePattern]);
  patterns_.insert(patterns_.begin() + static_cast<long>(insertPos), std::move(duplicated));

  std::uint8_t swing = patternSwing(sourcePattern);
  patternSwingPercent_.insert(patternSwingPercent_.begin() + static_cast<long>(insertPos), swing);

  for (std::size_t& entry : songOrder_) {
    if (entry >= insertPos) {
      ++entry;
    }
  }

  currentPatternIndex_ = insertPos;
  const std::size_t anchorSongEntry = firstSongEntryForPattern(sourcePattern);
  const std::size_t songInsertPos = std::min(anchorSongEntry + 1, songOrder_.size());
  songOrder_.insert(songOrder_.begin() + static_cast<long>(songInsertPos), currentPatternIndex_);

  return true;
}

PatternEditor& Module::currentEditor() {
  if (currentPatternIndex_ >= patterns_.size()) {
    currentPatternIndex_ = 0;  // Safety fallback
  }
  return *patterns_[currentPatternIndex_];
}

const PatternEditor& Module::currentEditor() const {
  // For const, we don't modify. Just use 0 if out of range.
  std::size_t safeIndex = (currentPatternIndex_ >= patterns_.size()) ? 0 : currentPatternIndex_;
  return *patterns_[safeIndex];
}

PatternEditor& Module::patternEditor(std::size_t index) {
  if (index >= patterns_.size()) {
    return *patterns_[0];  // Fallback to first pattern
  }
  return *patterns_[index];
}

const PatternEditor& Module::patternEditor(std::size_t index) const {
  if (index >= patterns_.size()) {
    return *patterns_[0];  // Fallback to first pattern
  }
  return *patterns_[index];
}

std::size_t Module::songLength() const {
  return songOrder_.size();
}

std::size_t Module::songEntryAt(std::size_t orderIndex) const {
  if (songOrder_.empty()) {
    return 0;
  }
  if (orderIndex >= songOrder_.size()) {
    return songOrder_.front();
  }
  return songOrder_[orderIndex];
}

bool Module::setSongEntry(std::size_t orderIndex, std::size_t patternIndex) {
  if (orderIndex >= songOrder_.size() || patternIndex >= patterns_.size()) {
    return false;
  }
  songOrder_[orderIndex] = patternIndex;
  return true;
}

bool Module::insertSongEntry(std::size_t orderIndex, std::size_t patternIndex) {
  if (patternIndex >= patterns_.size()) {
    return false;
  }
  const std::size_t insertPos = std::min(orderIndex, songOrder_.size());
  songOrder_.insert(songOrder_.begin() + static_cast<long>(insertPos), patternIndex);
  return true;
}

bool Module::appendSongEntry(std::size_t patternIndex) {
  if (patternIndex >= patterns_.size()) {
    return false;
  }
  songOrder_.push_back(patternIndex);
  return true;
}

bool Module::removeSongEntry(std::size_t orderIndex) {
  if (songOrder_.size() <= 1 || orderIndex >= songOrder_.size()) {
    return false;
  }
  songOrder_.erase(songOrder_.begin() + static_cast<long>(orderIndex));
  return true;
}

bool Module::moveSongEntryUp(std::size_t orderIndex) {
  if (orderIndex == 0 || orderIndex >= songOrder_.size()) {
    return false;
  }
  std::swap(songOrder_[orderIndex - 1], songOrder_[orderIndex]);
  return true;
}

bool Module::moveSongEntryDown(std::size_t orderIndex) {
  if (orderIndex + 1 >= songOrder_.size()) {
    return false;
  }
  std::swap(songOrder_[orderIndex], songOrder_[orderIndex + 1]);
  return true;
}

std::size_t Module::firstSongEntryForPattern(std::size_t patternIndex) const {
  for (std::size_t i = 0; i < songOrder_.size(); ++i) {
    if (songOrder_[i] == patternIndex) {
      return i;
    }
  }
  return 0;
}

bool Module::setSongOrder(const std::vector<std::size_t>& order) {
  if (order.empty()) {
    return false;
  }
  for (std::size_t entry : order) {
    if (entry >= patterns_.size()) {
      return false;
    }
  }
  songOrder_ = order;
  return true;
}

const std::vector<std::size_t>& Module::songOrder() const {
  return songOrder_;
}

std::uint8_t Module::patternSwing(std::size_t patternIndex) const {
  if (patternSwingPercent_.empty()) {
    return 50;
  }
  if (patternIndex >= patternSwingPercent_.size()) {
    return patternSwingPercent_.front();
  }
  return static_cast<std::uint8_t>(std::clamp<int>(patternSwingPercent_[patternIndex], 50, 75));
}

std::uint8_t Module::currentPatternSwing() const {
  return patternSwing(currentPatternIndex_);
}

bool Module::setPatternSwing(std::size_t patternIndex, std::uint8_t swingPercent) {
  if (patternIndex >= patternSwingPercent_.size()) {
    return false;
  }
  patternSwingPercent_[patternIndex] = static_cast<std::uint8_t>(std::clamp<int>(swingPercent, 50, 75));
  return true;
}

bool Module::setCurrentPatternSwing(std::uint8_t swingPercent) {
  return setPatternSwing(currentPatternIndex_, swingPercent);
}

void Module::setInheritSwingOnInsert(bool enabled) {
  inheritSwingOnInsert_ = enabled;
}

bool Module::inheritSwingOnInsert() const {
  return inheritSwingOnInsert_;
}

void Module::setRowEditAllChannels(bool enabled) {
  rowEditAllChannels_ = enabled;
}

bool Module::rowEditAllChannels() const {
  return rowEditAllChannels_;
}

std::string Module::status() const {
  std::ostringstream oss;
  oss << "Module: " << patterns_.size() << " pattern" << (patterns_.size() != 1 ? "s" : "");
  oss << ", current pattern: " << (currentPatternIndex_ + 1);
  oss << ", song length: " << songOrder_.size();
  if (!message_.empty()) {
    oss << ", message: '" << message_ << "'";
  }
  return oss.str();
}

void Module::setMessage(std::string message) {
  message_ = std::move(message);
}

const std::string& Module::message() const {
  return message_;
}

}  // namespace extracker
