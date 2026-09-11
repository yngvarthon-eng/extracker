#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "extracker/pattern_editor.hpp"

namespace extracker {

class Module {
public:
  Module(std::size_t rows = 64, std::size_t channels = 16);

  void reset(std::size_t rows, std::size_t channels, std::size_t patternCount = 1);

  // Pattern management
  std::size_t patternCount() const;
  std::size_t currentPattern() const;
  bool switchToPattern(std::size_t patternIndex);
  
  // Insert pattern after the current pattern (or at end if current >= patterns)
  bool insertPatternAfter();
  
  // Insert pattern before the current pattern
  bool insertPatternBefore();
  
  // Remove the current pattern (must have at least one pattern remaining)
  bool removeCurrentPattern();

  // Duplicate current pattern and switch to the duplicate
  bool duplicateCurrentPattern();

  // Duplicate a specific pattern index and switch to the duplicate
  bool duplicatePattern(std::size_t sourcePatternIndex);
  
  // Get the current pattern editor
  PatternEditor& currentEditor();
  const PatternEditor& currentEditor() const;
  
  // Get a specific pattern editor
  PatternEditor& patternEditor(std::size_t index);
  const PatternEditor& patternEditor(std::size_t index) const;

  // Song order management
  std::size_t songLength() const;
  std::size_t songEntryAt(std::size_t orderIndex) const;
  bool setSongEntry(std::size_t orderIndex, std::size_t patternIndex);
  bool insertSongEntry(std::size_t orderIndex, std::size_t patternIndex);
  bool appendSongEntry(std::size_t patternIndex);
  bool removeSongEntry(std::size_t orderIndex);
  bool moveSongEntryUp(std::size_t orderIndex);
  bool moveSongEntryDown(std::size_t orderIndex);
  std::size_t firstSongEntryForPattern(std::size_t patternIndex) const;
  bool setSongOrder(const std::vector<std::size_t>& order);
  const std::vector<std::size_t>& songOrder() const;

  // Per-pattern groove/swing percentage (50 = straight timing).
  std::uint8_t patternSwing(std::size_t patternIndex) const;
  std::uint8_t currentPatternSwing() const;
  bool setPatternSwing(std::size_t patternIndex, std::uint8_t swingPercent);
  bool setCurrentPatternSwing(std::uint8_t swingPercent);
  void setInheritSwingOnInsert(bool enabled);
  bool inheritSwingOnInsert() const;
  void setRowEditAllChannels(bool enabled);
  bool rowEditAllChannels() const;
  
  // Status information
  std::string status() const;

  // Song/module metadata
  void setMessage(std::string message);
  const std::string& message() const;

private:
  std::vector<std::unique_ptr<PatternEditor>> patterns_;
  std::vector<std::uint8_t> patternSwingPercent_;
  std::vector<std::size_t> songOrder_;
  std::size_t currentPatternIndex_;
  std::size_t rows_;
  std::size_t channels_;
  bool inheritSwingOnInsert_ = false;
  bool rowEditAllChannels_ = false;
  std::string message_;
};

}  // namespace extracker
