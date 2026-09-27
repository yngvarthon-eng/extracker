#include "extracker/channel_layout.hpp"

#include <algorithm>

#include "extracker/module.hpp"
#include "extracker/pattern_clipboard.hpp"

namespace extracker {

namespace {

bool columnHasContent(const PatternEditor& editor, std::size_t channel) {
  if (channel >= editor.channels()) {
    return false;
  }
  const int ch = static_cast<int>(channel);
  for (int row = 0; row < static_cast<int>(editor.rows()); ++row) {
    if (editor.hasNoteAt(row, ch) || editor.effectCommandAt(row, ch) != 0 || editor.effectValueAt(row, ch) != 0) {
      return true;
    }
  }
  return false;
}

// Rewrites the pattern so channel i holds old channel sources[i] (or empty).
void remapPattern(PatternEditor& editor, const std::vector<int>& sources) {
  const PatternEditor original = editor;
  const int channels = static_cast<int>(editor.channels());
  for (int row = 0; row < static_cast<int>(editor.rows()); ++row) {
    for (int ch = 0; ch < channels; ++ch) {
      const int source = static_cast<std::size_t>(ch) < sources.size() ? sources[static_cast<std::size_t>(ch)] : ch;
      if (source >= 0 && source < channels) {
        PatternClipboard::writeStep(editor, row, ch, PatternClipboard::readStep(original, row, source));
      } else {
        editor.clearStep(row, ch);
      }
    }
  }
}

std::size_t layoutWidth(const Module& module) {
  return module.currentEditor().channels();
}

ChannelEditResult fail(std::string error) {
  ChannelEditResult result;
  result.error = std::move(error);
  return result;
}

// Applies a count-preserving remap to every pattern and the channel state.
// makeSources builds the remap for a given channel count.
template <typename MakeSources>
ChannelEditResult applyRemap(Module& module, ChannelManager& channels, MakeSources makeSources) {
  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    PatternEditor& editor = module.patternEditor(p);
    remapPattern(editor, makeSources(editor.channels()));
  }

  ChannelEditResult result;
  result.ok = true;
  result.sources = makeSources(layoutWidth(module));
  channels.remap(result.sources);
  return result;
}

std::vector<int> identity(std::size_t width) {
  std::vector<int> sources(width);
  for (std::size_t i = 0; i < width; ++i) {
    sources[i] = static_cast<int>(i);
  }
  return sources;
}

// Error text when making room would push content off the end of some pattern.
std::string lastChannelBlocker(const Module& module, std::size_t firstShifted) {
  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    const PatternEditor& editor = module.patternEditor(p);
    const std::size_t width = editor.channels();
    if (firstShifted < width && columnHasContent(editor, width - 1)) {
      return "channel " + std::to_string(width - 1) + " is not empty in pattern " + std::to_string(p) +
             "; clear it or add a channel first";
    }
  }
  return {};
}

}  // namespace

int ChannelEditResult::newIndexOf(int oldChannel) const {
  for (std::size_t i = 0; i < sources.size(); ++i) {
    if (sources[i] == oldChannel) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool channelHasContent(const Module& module, std::size_t channel) {
  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    if (columnHasContent(module.patternEditor(p), channel)) {
      return true;
    }
  }
  return false;
}

ChannelEditResult insertChannel(Module& module, ChannelManager& channels, std::size_t at) {
  const std::size_t width = layoutWidth(module);
  if (at >= width) {
    return fail("channel " + std::to_string(at) + " is out of range (0-" + std::to_string(width - 1) + ")");
  }
  if (std::string blocker = lastChannelBlocker(module, at); !blocker.empty()) {
    return fail(blocker);
  }
  return applyRemap(module, channels, [at](std::size_t w) {
    std::vector<int> sources = identity(w);
    for (std::size_t i = at; i < w; ++i) {
      sources[i] = i == at ? -1 : static_cast<int>(i - 1);
    }
    return sources;
  });
}

ChannelEditResult deleteChannel(Module& module, ChannelManager& channels, std::size_t channel) {
  const std::size_t width = layoutWidth(module);
  if (channel >= width) {
    return fail("channel " + std::to_string(channel) + " is out of range (0-" + std::to_string(width - 1) + ")");
  }
  return applyRemap(module, channels, [channel](std::size_t w) {
    std::vector<int> sources = identity(w);
    for (std::size_t i = channel; i < w; ++i) {
      sources[i] = i + 1 < w ? static_cast<int>(i + 1) : -1;
    }
    return sources;
  });
}

ChannelEditResult moveChannel(Module& module, ChannelManager& channels, std::size_t from, std::size_t to) {
  const std::size_t width = layoutWidth(module);
  if (from >= width || to >= width) {
    return fail("channel out of range (0-" + std::to_string(width - 1) + ")");
  }
  if (from == to) {
    return fail("channel " + std::to_string(from) + " is already there");
  }
  return applyRemap(module, channels, [from, to](std::size_t w) {
    std::vector<int> sources = identity(w);
    if (from >= w || to >= w) {
      return sources;  // narrower pattern: leave it alone
    }
    sources.erase(sources.begin() + static_cast<std::ptrdiff_t>(from));
    sources.insert(sources.begin() + static_cast<std::ptrdiff_t>(to), static_cast<int>(from));
    return sources;
  });
}

ChannelEditResult duplicateChannel(Module& module, ChannelManager& channels, std::size_t channel) {
  const std::size_t width = layoutWidth(module);
  if (channel >= width) {
    return fail("channel " + std::to_string(channel) + " is out of range (0-" + std::to_string(width - 1) + ")");
  }
  if (channel + 1 >= width) {
    return fail("no room after the last channel; add a channel first");
  }
  if (std::string blocker = lastChannelBlocker(module, channel + 1); !blocker.empty()) {
    return fail(blocker);
  }
  return applyRemap(module, channels, [channel](std::size_t w) {
    std::vector<int> sources = identity(w);
    for (std::size_t i = channel + 1; i < w; ++i) {
      sources[i] = static_cast<int>(i - 1);
    }
    return sources;
  });
}

bool ChannelLayoutHistory::undo(Module& module, ChannelManager& channels, std::string& error) {
  if (undo_.empty()) {
    error = "nothing to undo";
    return false;
  }
  return step(undo_, redo_, module, channels, error);
}

bool ChannelLayoutHistory::redo(Module& module, ChannelManager& channels, std::string& error) {
  if (redo_.empty()) {
    error = "nothing to redo";
    return false;
  }
  return step(redo_, undo_, module, channels, error);
}

void ChannelLayoutHistory::clear() {
  undo_.clear();
  redo_.clear();
}

bool ChannelLayoutHistory::step(std::vector<Entry>& from,
                                std::vector<Entry>& to,
                                Module& module,
                                ChannelManager& channels,
                                std::string& error) {
  const Entry& entry = from.back();
  if (entry.snapshot.patterns.size() != module.patternCount() || patternHash(module) != entry.expectedHash) {
    error = "patterns changed since the channel edit; channel history cleared";
    from.clear();
    to.clear();
    return false;
  }

  Entry reverse{capture(module, channels), 0};
  for (std::size_t p = 0; p < entry.snapshot.patterns.size(); ++p) {
    module.patternEditor(p) = entry.snapshot.patterns[p];
  }
  channels = entry.snapshot.channels;
  reverse.expectedHash = patternHash(module);
  from.pop_back();
  push(to, std::move(reverse));
  return true;
}

void ChannelLayoutHistory::push(std::vector<Entry>& stack, Entry entry) {
  stack.push_back(std::move(entry));
  if (stack.size() > kMaxDepth) {
    stack.erase(stack.begin());
  }
}

ChannelLayoutHistory::Snapshot ChannelLayoutHistory::capture(const Module& module, const ChannelManager& channels) {
  Snapshot snapshot;
  snapshot.patterns.reserve(module.patternCount());
  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    snapshot.patterns.push_back(module.patternEditor(p));
  }
  snapshot.channels = channels;
  return snapshot;
}

std::uint64_t ChannelLayoutHistory::patternHash(const Module& module) {
  // FNV-1a over every cell of every pattern.
  std::uint64_t hash = 1469598103934665603ULL;
  auto mix = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  for (std::size_t p = 0; p < module.patternCount(); ++p) {
    const PatternEditor& editor = module.patternEditor(p);
    mix(editor.rows());
    mix(editor.channels());
    for (int row = 0; row < static_cast<int>(editor.rows()); ++row) {
      for (int ch = 0; ch < static_cast<int>(editor.channels()); ++ch) {
        const PatternClipboard::Step cell = PatternClipboard::readStep(editor, row, ch);
        mix(cell.hasNote ? 1u : 0u);
        mix(static_cast<std::uint64_t>(cell.note + 1));
        mix(cell.instrument);
        mix(cell.sample);
        mix(cell.gateTicks);
        mix(cell.velocity);
        mix(cell.retrigger ? 1u : 0u);
        mix(cell.effectCommand);
        mix(cell.effectValue);
      }
    }
  }
  return hash;
}

}  // namespace extracker
