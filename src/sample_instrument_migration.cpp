#include "extracker/sample_instrument_migration.hpp"

#include <algorithm>
#include <sstream>

#include "extracker/module.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"

namespace extracker {

namespace {

constexpr std::uint16_t kNoSample = 0xFFFF;

bool bankSlotLoaded(const PluginHost& plugins, std::uint16_t sampleSlot) {
  return sampleSlot < PluginHost::kMaxSampleSlots && !plugins.samplePathForSlot(sampleSlot).empty();
}

// Calls fn(editor, row, channel) for every note (not note-off) in the module.
template <typename Fn>
void forEachNote(Module& module, Fn fn) {
  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    PatternEditor& editor = module.patternEditor(p);
    for (std::size_t r = 0; r < editor.rows(); ++r) {
      for (std::size_t c = 0; c < editor.channels(); ++c) {
        const int row = static_cast<int>(r);
        const int channel = static_cast<int>(c);
        if (editor.hasNoteAt(row, channel) && editor.noteAt(row, channel) >= 0) {
          fn(editor, row, channel);
        }
      }
    }
  }
}

// Instruments print as 2 hex digits and sample slots as 3, as in the GUI.
std::string hexDigits(int value, int width) {
  static const char* digits = "0123456789ABCDEF";
  std::string out(static_cast<std::size_t>(width), '0');
  for (int i = width - 1; i >= 0; --i, value >>= 4) {
    out[static_cast<std::size_t>(i)] = digits[value & 0xF];
  }
  return out;
}

}  // namespace

InstrumentUseMap instrumentsUsedByNotes(const Module& module) {
  InstrumentUseMap used{};
  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    const PatternEditor& editor = module.patternEditor(p);
    for (std::size_t r = 0; r < editor.rows(); ++r) {
      for (std::size_t c = 0; c < editor.channels(); ++c) {
        const int row = static_cast<int>(r);
        const int channel = static_cast<int>(c);
        if (editor.hasNoteAt(row, channel) && editor.noteAt(row, channel) >= 0) {
          used[editor.instrumentAt(row, channel)] = true;
        }
      }
    }
  }
  return used;
}

int ensureSampleInstrument(PluginHost& plugins,
                           std::uint16_t sampleSlot,
                           const InstrumentUseMap& reserved,
                           int preferredSlot,
                           bool* created) {
  if (created) {
    *created = false;
  }
  if (!bankSlotLoaded(plugins, sampleSlot)) {
    return -1;
  }
  if (const int existing = plugins.instrumentForSampleSlot(sampleSlot); existing >= 0) {
    return existing;
  }

  const auto isFree = [&](int slot) {
    return slot >= 0 && slot < static_cast<int>(kInstrumentSlotCount) &&
           !reserved[static_cast<std::size_t>(slot)] &&
           plugins.pluginForInstrument(static_cast<std::uint8_t>(slot)).empty();
  };
  int target = isFree(preferredSlot) ? preferredSlot : -1;
  for (int slot = 0; target < 0 && slot < static_cast<int>(kInstrumentSlotCount); ++slot) {
    if (isFree(slot)) {
      target = slot;
    }
  }
  if (target < 0 || !plugins.assignSampleSlotToInstrument(sampleSlot, static_cast<std::uint8_t>(target))) {
    return -1;
  }
  if (created) {
    *created = true;
  }
  return target;
}

SampleReferenceMigration migrateSampleReferences(Module& module, PluginHost& plugins) {
  SampleReferenceMigration report;

  // A sample column naming an empty bank slot never overrode anything: the
  // note already played its instrument. Clear those first so the instrument
  // rule below sees them.
  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    PatternEditor& editor = module.patternEditor(p);
    for (std::size_t r = 0; r < editor.rows(); ++r) {
      for (std::size_t c = 0; c < editor.channels(); ++c) {
        const int row = static_cast<int>(r);
        const int channel = static_cast<int>(c);
        const std::uint16_t sample = editor.sampleAt(row, channel);
        const bool isNote = editor.hasNoteAt(row, channel) && editor.noteAt(row, channel) >= 0;
        if (sample != kNoSample && (!isNote || !bankSlotLoaded(plugins, sample))) {
          editor.setSample(row, channel, kNoSample);
          report.sampleColumnsCleared += 1;
        }
      }
    }
  }

  // Instruments played by notes without a sample column.
  InstrumentUseMap plainUse{};
  forEachNote(module, [&](PatternEditor& editor, int row, int channel) {
    if (editor.sampleAt(row, channel) == kNoSample) {
      plainUse[editor.instrumentAt(row, channel)] = true;
    }
  });

  // Same-number fallback: an empty instrument slot N (or an empty, unlinked
  // builtin.sample) borrowed bank slot N. Link N to bank N; numbers stay put.
  for (std::size_t n = 0; n < kInstrumentSlotCount; ++n) {
    const auto instrument = static_cast<std::uint8_t>(n);
    const auto bankSlot = static_cast<std::uint16_t>(n);
    if (!plainUse[n] || !bankSlotLoaded(plugins, bankSlot)) {
      continue;
    }
    const std::string plugin = plugins.pluginForInstrument(instrument);
    const bool borrowed = plugin.empty() ||
        (plugin == "builtin.sample" && plugins.sampleSlotForInstrument(instrument) < 0 &&
         plugins.sampleFrameCountForInstrument(instrument) == 0);
    if (borrowed && plugins.assignSampleSlotToInstrument(bankSlot, instrument)) {
      report.linked.emplace_back(static_cast<int>(n), static_cast<int>(n));
    }
  }

  // Sample columns naming a loaded bank slot: the note becomes a note on the
  // sample instrument for that slot. Slots already used by notes are never
  // taken over, so no other note changes sound.
  InstrumentUseMap reserved = instrumentsUsedByNotes(module);
  forEachNote(module, [&](PatternEditor& editor, int row, int channel) {
    const std::uint16_t sample = editor.sampleAt(row, channel);
    if (sample == kNoSample) {
      return;
    }
    bool created = false;
    const int target = ensureSampleInstrument(plugins, sample, reserved, sample, &created);
    if (target < 0) {
      if (std::find(report.unplaced.begin(), report.unplaced.end(), sample) == report.unplaced.end()) {
        report.unplaced.push_back(sample);
      }
      return;
    }
    if (created) {
      report.linked.emplace_back(target, static_cast<int>(sample));
    }
    reserved[static_cast<std::size_t>(target)] = true;
    if (editor.instrumentAt(row, channel) != static_cast<std::uint8_t>(target)) {
      editor.setInstrument(row, channel, static_cast<std::uint8_t>(target));
      report.notesRemapped += 1;
    }
    editor.setSample(row, channel, kNoSample);
    report.sampleColumnsCleared += 1;
  });

  return report;
}

std::string SampleReferenceMigration::summary() const {
  if (!changed() && unplaced.empty()) {
    return "";
  }
  std::ostringstream out;
  out << "Converted song to sample instruments:";
  if (!linked.empty()) {
    out << " ";
    for (std::size_t i = 0; i < linked.size(); ++i) {
      out << (i ? ", " : "") << "I" << hexDigits(linked[i].first, 2) << "=S" << hexDigits(linked[i].second, 3);
    }
    out << ";";
  }
  out << " " << notesRemapped << " note(s) renumbered";
  if (!unplaced.empty()) {
    out << "; no free instrument slot for sample(s)";
    for (const int s : unplaced) {
      out << " S" << hexDigits(s, 3);
    }
  }
  return out.str();
}

}  // namespace extracker
