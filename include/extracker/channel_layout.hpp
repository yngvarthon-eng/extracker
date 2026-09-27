#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "extracker/channel_manager.hpp"
#include "extracker/pattern_editor.hpp"

namespace extracker {

class Module;

// Structural channel edits applied to every pattern and to the channel state
// together. They keep each pattern's channel count: insert and duplicate need
// the last channel to be empty (it is dropped), delete appends an empty channel.
struct ChannelEditResult {
  bool ok = false;
  std::string error;
  // For every channel after the edit, the channel it came from before the edit,
  // or -1 for a new empty channel. Use it to carry other per-channel state along.
  std::vector<int> sources;

  // Where a channel moved to, or -1 if it was deleted (duplicates map to the original).
  int newIndexOf(int oldChannel) const;
};

ChannelEditResult insertChannel(Module& module, ChannelManager& channels, std::size_t at);
ChannelEditResult deleteChannel(Module& module, ChannelManager& channels, std::size_t channel);
ChannelEditResult moveChannel(Module& module, ChannelManager& channels, std::size_t from, std::size_t to);
// The copy lands right after the original.
ChannelEditResult duplicateChannel(Module& module, ChannelManager& channels, std::size_t channel);

// True if the channel holds a note or effect in any pattern.
bool channelHasContent(const Module& module, std::size_t channel);

// Undo/redo for channel edits: snapshots every pattern plus the channel state.
// Undo and redo refuse to run once the patterns have changed since the edit, so
// they can never throw away later note edits.
class ChannelLayoutHistory {
public:
  static constexpr std::size_t kMaxDepth = 16;

  // Runs edit() (one of the functions above) and records it if it succeeds.
  template <typename Edit>
  ChannelEditResult apply(Module& module, ChannelManager& channels, Edit edit) {
    Snapshot before = capture(module, channels);
    ChannelEditResult result = edit();
    if (result.ok) {
      push(undo_, Entry{std::move(before), patternHash(module)});
      redo_.clear();
    }
    return result;
  }

  bool undo(Module& module, ChannelManager& channels, std::string& error);
  bool redo(Module& module, ChannelManager& channels, std::string& error);
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  void clear();

private:
  struct Snapshot {
    std::vector<PatternEditor> patterns;
    ChannelManager channels;
  };
  struct Entry {
    Snapshot snapshot;               // state to go back to
    std::uint64_t expectedHash = 0;  // pattern hash the module must have now
  };

  static Snapshot capture(const Module& module, const ChannelManager& channels);
  static std::uint64_t patternHash(const Module& module);
  static void push(std::vector<Entry>& stack, Entry entry);
  static bool step(std::vector<Entry>& from, std::vector<Entry>& to, Module& module, ChannelManager& channels,
                   std::string& error);

  std::vector<Entry> undo_;
  std::vector<Entry> redo_;
};

}  // namespace extracker
