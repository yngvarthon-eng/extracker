#include <iostream>
#include <string>

#include "extracker/channel_layout.hpp"
#include "extracker/module.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

using extracker::ChannelManager;
using extracker::Module;

// Two patterns, 4 channels; channel c holds note 60+c in pattern 0 and 70+c in pattern 1.
void setup(Module& module, ChannelManager& channels) {
  module.reset(8, 4, 1);
  module.insertPatternAfter();
  for (int c = 0; c < 3; ++c) {
    module.patternEditor(0).insertNote(0, c, 60 + c);
    module.patternEditor(1).insertNote(0, c, 70 + c);
  }
  channels = ChannelManager(4);
  channels.setName(1, "Bass");
  channels.setMuted(1, true);
  channels.setInstrument(1, 3);
}

int noteAt(const Module& module, std::size_t pattern, int channel) {
  const auto& editor = module.patternEditor(pattern);
  return editor.hasNoteAt(0, channel) ? editor.noteAt(0, channel) : -1;
}

}  // namespace

int main() {
  // Insert shifts every pattern and the channel state right.
  {
    Module module;
    ChannelManager channels;
    setup(module, channels);
    const auto result = extracker::insertChannel(module, channels, 1);
    check(result.ok, "insert ok");
    check(noteAt(module, 0, 0) == 60 && noteAt(module, 0, 1) == -1 && noteAt(module, 0, 2) == 61 &&
              noteAt(module, 0, 3) == 62,
          "insert shifts pattern 0");
    check(noteAt(module, 1, 2) == 71, "insert shifts pattern 1 too");
    check(channels.name(2) == "Bass" && channels.isMuted(2) && channels.instrument(2) == 3 && channels.name(1).empty(),
          "insert carries channel state");
    check(result.newIndexOf(1) == 2 && result.newIndexOf(3) == -1, "insert mapping");
  }

  // Insert refuses when it would push content off the end.
  {
    Module module;
    ChannelManager channels;
    setup(module, channels);
    module.patternEditor(1).insertNote(4, 3, 50);
    const auto result = extracker::insertChannel(module, channels, 0);
    check(!result.ok && result.error.find("pattern 1") != std::string::npos, "insert blocked by last channel");
    check(noteAt(module, 0, 0) == 60, "blocked insert changes nothing");
  }

  // Delete shifts left and appends an empty channel.
  {
    Module module;
    ChannelManager channels;
    setup(module, channels);
    const auto result = extracker::deleteChannel(module, channels, 0);
    check(result.ok && noteAt(module, 0, 0) == 61 && noteAt(module, 0, 2) == -1 && noteAt(module, 1, 1) == 72,
          "delete shifts left");
    check(channels.name(0) == "Bass" && !channels.isMuted(3), "delete carries state and appends defaults");
    check(module.patternEditor(0).channels() == 4, "delete keeps channel count");
  }

  // Move and duplicate.
  {
    Module module;
    ChannelManager channels;
    setup(module, channels);
    check(extracker::moveChannel(module, channels, 2, 0).ok, "move ok");
    check(noteAt(module, 0, 0) == 62 && noteAt(module, 0, 1) == 60 && noteAt(module, 0, 2) == 61,
          "move reorders");
    check(channels.name(2) == "Bass", "move carries name");
    check(!extracker::moveChannel(module, channels, 1, 1).ok, "move onto itself rejected");

    const auto dup = extracker::duplicateChannel(module, channels, 2);
    check(dup.ok && noteAt(module, 0, 2) == 61 && noteAt(module, 0, 3) == 61 && channels.name(3) == "Bass",
          "duplicate copies notes and state");
    check(!extracker::duplicateChannel(module, channels, 3).ok, "duplicate of last channel rejected");
  }

  // History: undo/redo across edits, and refusal after later pattern edits.
  {
    Module module;
    ChannelManager channels;
    setup(module, channels);
    extracker::ChannelLayoutHistory history;
    std::string error;

    history.apply(module, channels, [&] { return extracker::insertChannel(module, channels, 0); });
    history.apply(module, channels, [&] { return extracker::moveChannel(module, channels, 3, 0); });
    const auto failed = history.apply(module, channels, [&] { return extracker::moveChannel(module, channels, 9, 0); });
    check(!failed.ok, "failed edit");

    check(history.undo(module, channels, error) && history.undo(module, channels, error), "two undos");
    check(!history.canUndo(), "failed edit was not recorded");
    check(noteAt(module, 0, 0) == 60 && channels.name(1) == "Bass", "undo restores layout and state");
    check(history.redo(module, channels, error) && noteAt(module, 0, 1) == 60, "redo reapplies");

    module.patternEditor(1).insertNote(5, 0, 40);
    check(!history.undo(module, channels, error) && error.find("changed") != std::string::npos,
          "undo refused after later pattern edit");
    check(module.patternEditor(1).hasNoteAt(5, 0), "later edit kept");
    check(!history.canUndo() && !history.canRedo(), "stale history cleared");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "channel layout test passed" << '\n';
  return 0;
}
