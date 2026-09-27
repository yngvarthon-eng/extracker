#include "extracker/pattern_clipboard.hpp"

#include <algorithm>
#include <sstream>

namespace extracker {

namespace {

// Resets the note-bound fields to what PatternEditor::clearStep leaves behind.
void clearNoteFields(PatternClipboard::Step& step) {
  const PatternClipboard::Step empty;
  step.hasNote = false;
  step.note = empty.note;
  step.instrument = empty.instrument;
  step.sample = empty.sample;
  step.gateTicks = empty.gateTicks;
  step.velocity = empty.velocity;
  step.retrigger = empty.retrigger;
}

bool takesColumn(PatternClipboard::PasteMode mode, bool sourceHas, bool targetHas) {
  switch (mode) {
    case PatternClipboard::PasteMode::Overwrite:
    case PatternClipboard::PasteMode::Insert:
      return true;
    case PatternClipboard::PasteMode::Merge:
      return sourceHas;
    case PatternClipboard::PasteMode::Mix:
      return sourceHas && !targetHas;
  }
  return false;
}

// False when a source cell can never change a target: Merge and Mix skip
// source cells that are empty in every pasted column.
bool sourceCanWrite(const PatternClipboard::Step& source, const PatternClipboard::PasteOptions& options) {
  using Clip = PatternClipboard;
  if (options.mode == Clip::PasteMode::Overwrite || options.mode == Clip::PasteMode::Insert) {
    return (options.columns & Clip::kAllColumns) != 0;
  }
  const unsigned noteBound = Clip::kNoteColumn | Clip::kInstrumentColumn | Clip::kVolumeColumn;
  return ((options.columns & noteBound) != 0 && source.hasNote) ||
         ((options.columns & Clip::kEffectColumn) != 0 && source.hasEffect());
}

}  // namespace

bool PatternClipboard::Step::operator==(const Step& other) const {
  return hasNote == other.hasNote && note == other.note && instrument == other.instrument &&
         sample == other.sample && gateTicks == other.gateTicks && velocity == other.velocity &&
         retrigger == other.retrigger && effectCommand == other.effectCommand &&
         effectValue == other.effectValue;
}

bool PatternClipboard::copy(const PatternEditor& editor,
                            int fromRow,
                            int toRow,
                            int fromChannel,
                            int toChannel,
                            int rowStep) {
  clear();
  fromRow = std::max(fromRow, 0);
  fromChannel = std::max(fromChannel, 0);
  toRow = std::min(toRow, static_cast<int>(editor.rows()) - 1);
  toChannel = std::min(toChannel, static_cast<int>(editor.channels()) - 1);
  if (rowStep < 1 || toRow < fromRow || toChannel < fromChannel) {
    return false;
  }

  rows_ = ((toRow - fromRow) / rowStep) + 1;
  channels_ = toChannel - fromChannel + 1;
  sourceFromRow_ = fromRow;
  sourceFromChannel_ = fromChannel;
  sourceRowStep_ = rowStep;
  steps_.reserve(static_cast<std::size_t>(rows_ * channels_));

  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < channels_; ++c) {
      steps_.push_back(readStep(editor, fromRow + (r * rowStep), fromChannel + c));
    }
  }
  return true;
}

bool PatternClipboard::composeStep(const Step& target,
                                   const Step& source,
                                   PasteMode mode,
                                   unsigned columns,
                                   Step& result) {
  result = target;
  bool written = false;

  if ((columns & kNoteColumn) != 0 && takesColumn(mode, source.hasNote, target.hasNote)) {
    written = true;
    if (source.hasNote) {
      result.hasNote = true;
      result.note = source.note;
      result.gateTicks = source.gateTicks;
      result.retrigger = source.retrigger;
    } else {
      clearNoteFields(result);
    }
  }

  // Instrument and volume belong to a note; they only land where the result has one.
  const bool noteBound = source.hasNote && result.hasNote;
  if ((columns & kInstrumentColumn) != 0 && noteBound && takesColumn(mode, source.hasNote, target.hasNote)) {
    written = true;
    result.instrument = source.instrument;
    result.sample = source.sample;
  }
  if ((columns & kVolumeColumn) != 0 && noteBound && takesColumn(mode, source.hasNote, target.hasNote)) {
    written = true;
    result.velocity = source.velocity;
  }

  if ((columns & kEffectColumn) != 0 && takesColumn(mode, source.hasEffect(), target.hasEffect())) {
    written = true;
    result.effectCommand = source.effectCommand;
    result.effectValue = source.effectValue;
  }

  return written;
}

PatternClipboard::PastePlan PatternClipboard::plan(const PatternEditor& editor,
                                                   int destRow,
                                                   int destChannel,
                                                   const PasteOptions& options) const {
  PastePlan result;
  if (empty()) {
    return result;
  }

  const int totalRows = static_cast<int>(editor.rows());
  const int totalChannels = static_cast<int>(editor.channels());
  const int rowStep = std::max(options.rowStep, 1);
  const int blockSpan = ((rows_ - 1) * rowStep) + 1;
  const bool insert = options.mode == PasteMode::Insert;
  const bool flood = options.flood && !insert;

  // Target cells as they look before the block lands. Insert pushes rows at and
  // below destRow down by the block span; everything else pastes over the pattern as is.
  auto baseStep = [&](int row, int channel) {
    if (insert && row >= destRow && channel >= destChannel && channel < destChannel + channels_) {
      if (row < destRow + blockSpan) {
        return Step{};
      }
      return readStep(editor, row - blockSpan, channel);
    }
    return readStep(editor, row, channel);
  };

  // Block cell landing on each target cell (flood repeats it every blockSpan rows).
  std::vector<int> blockIndexByCell(static_cast<std::size_t>(totalRows * totalChannels), -1);
  const int repeatSpan = rows_ * rowStep;
  for (int start = destRow; ; start += repeatSpan) {
    for (int r = 0; r < rows_; ++r) {
      for (int c = 0; c < channels_; ++c) {
        if (!sourceCanWrite(at(r, c), options)) {
          continue;
        }
        const int row = start + (r * rowStep);
        const int channel = destChannel + c;
        if (row < 0 || row >= totalRows || channel < 0 || channel >= totalChannels) {
          if (!flood || start == destRow) {
            ++result.skippedOutOfBounds;
          }
          continue;
        }
        blockIndexByCell[static_cast<std::size_t>(row * totalChannels + channel)] = r * channels_ + c;
      }
    }
    if (!flood || start + repeatSpan >= totalRows || repeatSpan <= 0) {
      break;
    }
  }

  for (int row = 0; row < totalRows; ++row) {
    for (int channel = 0; channel < totalChannels; ++channel) {
      const int blockIndex = blockIndexByCell[static_cast<std::size_t>(row * totalChannels + channel)];
      const Step before = readStep(editor, row, channel);
      const Step base = baseStep(row, channel);

      CellChange change;
      change.row = row;
      change.channel = channel;
      change.before = before;
      if (blockIndex >= 0) {
        change.blockRow = blockIndex / channels_;
        change.blockChannel = blockIndex % channels_;
        if (!composeStep(base, steps_[static_cast<std::size_t>(blockIndex)], options.mode, options.columns,
                         change.after)) {
          change.after = base;
          if (change.after == before) {
            continue;
          }
          change.blockRow = -1;
          change.blockChannel = -1;
        }
      } else {
        change.after = base;
        if (change.after == before) {
          continue;  // untouched cell (or an Insert-moved cell that did not change)
        }
      }
      result.changes.push_back(change);
    }
  }
  return result;
}

void PatternClipboard::apply(PatternEditor& editor, const PastePlan& plan) {
  for (const CellChange& change : plan.changes) {
    writeStep(editor, change.row, change.channel, change.after);
  }
}

PatternClipboard::PastePlan PatternClipboard::paste(PatternEditor& editor,
                                                    int destRow,
                                                    int destChannel,
                                                    const PasteOptions& options) const {
  PastePlan result = plan(editor, destRow, destChannel, options);
  apply(editor, result);
  return result;
}

PatternClipboard::PastePlan PatternClipboard::paste(PatternEditor& editor, int destRow, int destChannel) const {
  return paste(editor, destRow, destChannel, PasteOptions{});
}

PatternClipboard::Step PatternClipboard::readStep(const PatternEditor& editor, int row, int channel) {
  Step step;
  step.hasNote = editor.hasNoteAt(row, channel);
  step.effectCommand = editor.effectCommandAt(row, channel);
  step.effectValue = editor.effectValueAt(row, channel);
  if (step.hasNote) {
    step.note = editor.noteAt(row, channel);
    step.instrument = editor.instrumentAt(row, channel);
    step.sample = editor.sampleAt(row, channel);
    step.gateTicks = editor.gateTicksAt(row, channel);
    step.velocity = editor.velocityAt(row, channel);
    step.retrigger = editor.retriggerAt(row, channel);
  }
  return step;
}

void PatternClipboard::writeStep(PatternEditor& editor, int row, int channel, const Step& step) {
  editor.clearStep(row, channel);
  if (step.hasNote && step.note == PatternEditor::kNoteOff) {
    // insertNote rejects negative notes, so note-offs need the dedicated path.
    editor.insertNoteOff(row, channel);
    editor.setInstrument(row, channel, step.instrument);
    editor.setGateTicks(row, channel, step.gateTicks);
    editor.setVelocity(row, channel, step.velocity);
    editor.setRetrigger(row, channel, step.retrigger);
  } else if (step.hasNote) {
    editor.insertNote(row,
                      channel,
                      step.note,
                      step.instrument,
                      step.gateTicks,
                      step.velocity,
                      step.retrigger);
  }
  if (step.hasNote) {
    editor.setSample(row, channel, step.sample);
  }
  if (step.hasEffect()) {
    editor.setEffect(row, channel, step.effectCommand, step.effectValue);
  }
}

void PatternClipboard::clear() {
  rows_ = 0;
  channels_ = 0;
  sourceFromRow_ = 0;
  sourceFromChannel_ = 0;
  sourceRowStep_ = 1;
  steps_.clear();
}

const PatternClipboard::Step& PatternClipboard::at(int row, int channel) const {
  return steps_[static_cast<std::size_t>(row * channels_ + channel)];
}

bool PatternClipboard::sameContent(const PatternClipboard& other) const {
  return rows_ == other.rows_ && channels_ == other.channels_ && steps_ == other.steps_;
}

std::string PatternClipboard::describe() const {
  if (empty()) {
    return "(empty)";
  }
  static const char* kNoteNames[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
  constexpr int kPreviewNotes = 4;

  int notes = 0;
  int effects = 0;
  std::ostringstream preview;
  for (const Step& step : steps_) {
    if (step.hasEffect()) {
      ++effects;
    }
    if (!step.hasNote) {
      continue;
    }
    if (notes < kPreviewNotes) {
      if (step.note == PatternEditor::kNoteOff) {
        preview << " ^^^";
      } else if (step.note >= 0 && step.note <= 127) {
        preview << ' ' << kNoteNames[step.note % 12] << step.note / 12;
      }
    } else if (notes == kPreviewNotes) {
      preview << " ...";
    }
    ++notes;
  }

  const int lastRow = sourceFromRow_ + (rows_ - 1) * sourceRowStep_;
  const int lastChannel = sourceFromChannel_ + channels_ - 1;
  std::ostringstream out;
  out << rows_ << 'x' << channels_ << "  rows " << sourceFromRow_ << '-' << lastRow;
  if (sourceRowStep_ > 1) {
    out << " step " << sourceRowStep_;
  }
  out << " ch " << sourceFromChannel_;
  if (lastChannel != sourceFromChannel_) {
    out << '-' << lastChannel;
  }
  out << "  " << notes << (notes == 1 ? " note" : " notes");
  if (effects > 0) {
    out << ' ' << effects << " fx";
  }
  out << preview.str();
  return out.str();
}

void ClipboardHistory::push(PatternClipboard clipboard) {
  if (clipboard.empty()) {
    return;
  }
  const auto duplicate = std::find_if(entries_.begin(), entries_.end(), [&](const PatternClipboard& entry) {
    return entry.sameContent(clipboard);
  });
  if (duplicate != entries_.end()) {
    entries_.erase(duplicate);
  }
  entries_.push_front(std::move(clipboard));
  if (entries_.size() > kMaxEntries) {
    entries_.pop_back();
  }
  active_ = 0;
}

bool ClipboardHistory::select(std::size_t index) {
  if (index >= entries_.size()) {
    return false;
  }
  active_ = index;
  return true;
}

void ClipboardHistory::clear() {
  entries_.clear();
  active_ = 0;
}

const PatternClipboard& ClipboardHistory::active() const {
  static const PatternClipboard kEmpty;
  return entries_.empty() ? kEmpty : entries_[active_];
}

}  // namespace extracker
