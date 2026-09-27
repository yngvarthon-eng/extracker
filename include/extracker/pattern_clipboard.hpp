#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "extracker/pattern_editor.hpp"

namespace extracker {

// Rectangular block of pattern steps, shared by the CLI and the GUI.
// Steps are stored row-major: index = row * channels + channel.
class PatternClipboard {
public:
  static constexpr std::uint16_t kNoSample = 0xFFFF;

  struct Step {
    bool hasNote = false;
    int note = -1;  // PatternEditor::kNoteOff when hasNote is set for a note-off
    std::uint8_t instrument = 0;
    std::uint16_t sample = kNoSample;
    std::uint32_t gateTicks = 0;
    std::uint8_t velocity = PatternEditor::kDefaultVelocity;
    bool retrigger = false;
    std::uint8_t effectCommand = 0;
    std::uint8_t effectValue = 0;

    bool hasEffect() const { return effectCommand != 0 || effectValue != 0; }
    bool isEmpty() const { return !hasNote && !hasEffect(); }
    bool operator==(const Step& other) const;
    bool operator!=(const Step& other) const { return !(*this == other); }
  };

  enum class PasteMode {
    Overwrite,  // block replaces the target, empty source cells clear it
    Merge,      // non-empty source cells replace the target, empty ones leave it
    Mix,        // only fill target cells that are empty
    Insert,     // push the target rows down to make room, then overwrite
  };

  // Column mask: which parts of a step a paste touches.
  enum Column : unsigned {
    kNoteColumn = 1u << 0,        // note, note-off, gate, retrigger
    kInstrumentColumn = 1u << 1,  // instrument and sample
    kVolumeColumn = 1u << 2,      // velocity
    kEffectColumn = 1u << 3,      // effect command and value
    kAllColumns = kNoteColumn | kInstrumentColumn | kVolumeColumn | kEffectColumn,
  };

  struct PasteOptions {
    PasteMode mode = PasteMode::Overwrite;
    unsigned columns = kAllColumns;
    int rowStep = 1;     // spacing between pasted source rows
    bool flood = false;  // repeat the block down to the end of the pattern (not with Insert)
  };

  struct CellChange {
    int row = 0;
    int channel = 0;
    int blockRow = -1;  // source cell inside the clipboard, -1 for cells moved by Insert
    int blockChannel = -1;
    Step before;
    Step after;
  };

  struct PastePlan {
    std::vector<CellChange> changes;  // every cell the paste writes, in row-major order
    int skippedOutOfBounds = 0;       // cells that would be written but fall outside the pattern
  };

  // Copies rows fromRow..toRow (every rowStep-th row) and channels
  // fromChannel..toChannel. Bounds are inclusive and clamped to the editor.
  // Returns false (leaving the clipboard empty) if nothing is left to copy.
  bool copy(const PatternEditor& editor,
            int fromRow,
            int toRow,
            int fromChannel,
            int toChannel,
            int rowStep = 1);

  // Works out what pasting with the block's top-left at (destRow, destChannel)
  // would do, without touching the editor.
  PastePlan plan(const PatternEditor& editor, int destRow, int destChannel, const PasteOptions& options) const;
  static void apply(PatternEditor& editor, const PastePlan& plan);
  PastePlan paste(PatternEditor& editor, int destRow, int destChannel, const PasteOptions& options) const;
  PastePlan paste(PatternEditor& editor, int destRow, int destChannel) const;

  static Step readStep(const PatternEditor& editor, int row, int channel);
  // Makes the cell exactly equal to step.
  static void writeStep(PatternEditor& editor, int row, int channel, const Step& step);
  // Result of pasting source over target, or false if the cell is not written.
  static bool composeStep(const Step& target, const Step& source, PasteMode mode, unsigned columns, Step& result);

  void clear();
  bool empty() const { return steps_.empty(); }
  int rows() const { return rows_; }
  int channels() const { return channels_; }
  int sourceFromRow() const { return sourceFromRow_; }
  int sourceFromChannel() const { return sourceFromChannel_; }
  int sourceRowStep() const { return sourceRowStep_; }
  const Step& at(int row, int channel) const;

  // Same block content (source position is ignored).
  bool sameContent(const PatternClipboard& other) const;
  // One-line summary, e.g. "4x2  rows 0-3 ch 1-2  3 notes 1 fx  C-4 D#4 E-4 ...".
  std::string describe() const;

private:
  int rows_ = 0;
  int channels_ = 0;
  int sourceFromRow_ = 0;
  int sourceFromChannel_ = 0;
  int sourceRowStep_ = 1;
  std::vector<Step> steps_;
};

// Recent copies, newest first. Copying adds slot 1; any slot can be made the
// active one that paste uses.
class ClipboardHistory {
public:
  static constexpr std::size_t kMaxEntries = 9;

  // Adds a copy as slot 1 and makes it active. Copying the same block content
  // again reuses the existing slot instead of adding a duplicate.
  void push(PatternClipboard clipboard);
  bool select(std::size_t index);  // 0 = newest
  void clear();

  bool empty() const { return entries_.empty(); }
  std::size_t size() const { return entries_.size(); }
  const PatternClipboard& entry(std::size_t index) const { return entries_[index]; }
  std::size_t activeIndex() const { return active_; }
  // The clipboard paste uses; an empty clipboard when there is no history.
  const PatternClipboard& active() const;

private:
  std::deque<PatternClipboard> entries_;
  std::size_t active_ = 0;
};

}  // namespace extracker
