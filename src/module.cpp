#include "extracker/module.hpp"

#include <algorithm>
#include <sstream>

namespace extracker {

Module::Module(std::size_t rows, std::size_t channels)
    : currentPatternIndex_(0), rows_(rows), channels_(channels) {
  reset(rows, channels, 1);
}

void Module::reset(std::size_t rows, std::size_t channels, std::size_t patternCount) {
  rows_ = rows;
  channels_ = channels;
  currentPatternIndex_ = 0;
  patterns_.clear();
  songOrder_.clear();

  const std::size_t safePatternCount = std::max<std::size_t>(patternCount, 1);
  patterns_.reserve(safePatternCount);
  for (std::size_t i = 0; i < safePatternCount; ++i) {
    patterns_.push_back(std::make_unique<PatternEditor>(rows_, channels_));
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

  auto newPattern = std::make_unique<PatternEditor>(rows_, channels_);
  patterns_.insert(patterns_.begin() + static_cast<long>(insertPos), std::move(newPattern));

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

  auto newPattern = std::make_unique<PatternEditor>(rows_, channels_);
  patterns_.insert(patterns_.begin() + static_cast<long>(insertPos), std::move(newPattern));

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

std::string Module::status() const {
  std::ostringstream oss;
  oss << "Module: " << patterns_.size() << " pattern" << (patterns_.size() != 1 ? "s" : "");
  oss << ", current pattern: " << (currentPatternIndex_ + 1);
  oss << ", song length: " << songOrder_.size();
  return oss.str();
}

}  // namespace extracker
