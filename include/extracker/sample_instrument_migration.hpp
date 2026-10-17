#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "extracker/instrument_mix.hpp"

namespace extracker {

class Module;
class PluginHost;

// A note plays its instrument and nothing else. Songs written before that rule
// could reach a sample two other ways, and are converted on load:
//  - a step's sample column naming a loaded sample-bank slot (it overrode the
//    instrument), and
//  - an instrument slot with no plugin (or an empty, unlinked builtin.sample)
//    whose number matched a loaded sample-bank slot (it borrowed that sample).
// Both become a sample instrument linked to the bank slot, so the song sounds
// the same and each note's instrument number alone decides its sound.

using InstrumentUseMap = std::array<bool, kInstrumentSlotCount>;

// Instrument numbers used by any note in any pattern of `module`.
InstrumentUseMap instrumentsUsedByNotes(const Module& module);

// The instrument that plays sample-bank slot `sampleSlot`: an existing sample
// instrument linked to it, else a new one in `preferredSlot` (if it has no
// plugin and is not in `reserved`), else in the lowest such free slot.
// Returns -1 if the bank slot is empty or no slot is free. Sets `created` when
// a new instrument was assigned.
int ensureSampleInstrument(PluginHost& plugins,
                           std::uint16_t sampleSlot,
                           const InstrumentUseMap& reserved,
                           int preferredSlot,
                           bool* created = nullptr);

struct SampleReferenceMigration {
  std::size_t notesRemapped = 0;            // notes whose instrument number changed
  std::size_t sampleColumnsCleared = 0;     // steps whose sample column was reset
  std::vector<std::pair<int, int>> linked;  // (instrument, sample slot) created or linked
  std::vector<int> unplaced;                // sample slots that found no free instrument

  bool changed() const { return sampleColumnsCleared > 0 || !linked.empty(); }
  // One line for the user, e.g. "Converted 12 note(s) to sample instruments: I03=S03, I10=S07".
  std::string summary() const;
};

// Converts every pattern of `module` (see above). Call after a song and its
// instruments/samples are fully loaded.
SampleReferenceMigration migrateSampleReferences(Module& module, PluginHost& plugins);

}  // namespace extracker
