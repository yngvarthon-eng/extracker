#include "extracker/channel_cli.hpp"

#include "extracker/pattern_cli.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace extracker {

namespace {

constexpr const char* kUsage =
    "Usage: channel [list] | vol [<ch>] [<0-200>] | mute <ch> [on|off] | "
    "solo <ch> [on|off] | solo off | name <ch> [<name>|-] | "
    "insert <ch> | delete <ch> | move <from> <to> | dup <ch> | undo | redo | track ...";

constexpr const char* kTrackUsage =
    "Usage: channel track [list] | add <first> <last> [<name>] | remove <ch> | "
    "mute <ch> [on|off] | solo <ch> [on|off] | vol <ch> [<0-200>] | name <ch> [<name>|-]";

void printHelp() {
  std::cout << "channel list                 show name, volume, mute and solo for every channel\n";
  std::cout << "channel vol [<ch>] [<0-200>]  set/show per-channel volume (100 = unity)\n";
  std::cout << "  ch vol         show all channel volumes\n";
  std::cout << "  ch vol <ch>    show volume for channel <ch>\n";
  std::cout << "  ch vol <ch> <0-200>  set channel volume\n";
  std::cout << "channel mute <ch> [on|off]   toggle or set mute\n";
  std::cout << "channel solo <ch> [on|off]   toggle or set solo (any solo silences non-soloed channels)\n";
  std::cout << "channel solo off             clear solo on all channels\n";
  std::cout << "channel name <ch> [<name>|-] show, set or clear (-) a channel name\n";
  std::cout << "channel insert <ch>          insert an empty channel at <ch>, shifting the rest right\n";
  std::cout << "channel delete <ch>          delete <ch> in every pattern, shifting the rest left\n";
  std::cout << "channel move <from> <to>     move a channel to another position\n";
  std::cout << "channel dup <ch>             duplicate <ch> into the next channel\n";
  std::cout << "channel undo | redo          undo/redo the last insert/delete/move/dup\n";
  std::cout << "  Structural edits apply to all patterns and keep the channel count:\n";
  std::cout << "  insert and dup need the last channel to be empty.\n";
  std::cout << "channel track [list]          show tracks (groups of adjacent channels, e.g. a chord)\n";
  std::cout << "channel track add <first> <last> [<name>]  group channels first..last into a track\n";
  std::cout << "channel track remove <ch>     ungroup the track holding <ch> (notes are kept)\n";
  std::cout << "channel track mute|solo <ch> [on|off]  mute/solo the whole track\n";
  std::cout << "channel track vol <ch> [<0-200>]       track volume, multiplied with each channel's\n";
  std::cout << "channel track name <ch> [<name>|-]     show, set or clear a track name\n";
  std::cout << "  <ch> is any channel inside the track. A note set with instrument '-' on a\n";
  std::cout << "  track channel takes the instrument of the track's first note on that row.\n";
}

int volumePercent(const ChannelManager& channels, std::size_t ch) {
  return static_cast<int>(std::lround(channels.volume(ch) * 100.0f));
}

bool parseChannel(const std::string& token, std::size_t numChannels, std::size_t& channel) {
  int ch = -1;
  try {
    std::size_t consumed = 0;
    ch = std::stoi(token, &consumed);
    if (consumed != token.size()) {
      ch = -1;
    }
  } catch (...) {
  }
  if (ch < 0 || static_cast<std::size_t>(ch) >= numChannels) {
    std::cout << "Invalid channel index: " << token
              << " (valid: 0-" << (numChannels - 1) << ")\n";
    return false;
  }
  channel = static_cast<std::size_t>(ch);
  return true;
}

// Reads an optional on/off argument. Returns false (after printing usage) on a
// bad token; value is left unchanged when the argument is omitted.
bool parseOnOff(std::istringstream& input, bool& value) {
  std::string token;
  if (!(input >> token)) {
    return true;
  }
  if (token == "on" || token == "1") {
    value = true;
  } else if (token == "off" || token == "0") {
    value = false;
  } else {
    std::cout << kUsage << '\n';
    return false;
  }
  return true;
}

void printList(const ChannelManager& channels, std::size_t numChannels) {
  const bool anySolo = channels.anySoloed();
  std::cout << "Channels" << (anySolo ? " (solo active)" : "") << ":\n";
  for (std::size_t ch = 0; ch < numChannels; ++ch) {
    std::cout << "  ch " << std::setw(2) << ch << "  "
              << std::left << std::setw(16) << channels.displayName(ch) << std::right
              << " vol " << std::setw(3) << volumePercent(channels, ch) << "%"
              << (channels.isMuted(ch) ? "  M" : "   ")
              << (channels.isSoloed(ch) ? " S" : "  ")
              << (channels.isAudible(ch) ? "" : "  (silent)");
    if (channels.inTrack(ch)) {
      const ChannelManager::TrackInfo track = channels.track(ch);
      std::cout << "  [track " << track.first << "-" << track.last
                << (track.name.empty() ? "" : " " + track.name) << "]";
    }
    std::cout << '\n';
  }
}

std::string trackLabel(const ChannelManager::TrackInfo& track) {
  std::string label = "Track " + std::to_string(track.first) + "-" + std::to_string(track.last);
  if (!track.name.empty()) {
    label += " (" + track.name + ")";
  }
  return label;
}

void printTracks(const ChannelManager& channels, std::size_t numChannels) {
  const std::vector<ChannelManager::TrackInfo> tracks = channels.tracks();
  if (tracks.empty()) {
    std::cout << "No tracks\n";
    return;
  }
  std::cout << "Tracks:\n";
  for (const ChannelManager::TrackInfo& track : tracks) {
    if (track.first >= numChannels) {
      continue;
    }
    std::cout << "  ch " << std::setw(2) << track.first << "-" << std::setw(2) << track.last << "  "
              << std::left << std::setw(16) << (track.name.empty() ? "-" : track.name) << std::right
              << " vol " << std::setw(3) << static_cast<int>(std::lround(track.volume * 100.0f)) << "%"
              << (track.muted ? "  M" : "   ") << (track.soloed ? " S" : "  ") << '\n';
  }
}

// Parses <ch> and checks it is inside a track.
bool parseTrackChannel(std::istringstream& input, const ChannelManager& channels, std::size_t numChannels,
                       std::size_t& channel) {
  std::string token;
  if (!(input >> token)) {
    std::cout << kTrackUsage << '\n';
    return false;
  }
  if (!parseChannel(token, numChannels, channel)) {
    return false;
  }
  if (!channels.inTrack(channel)) {
    std::cout << "Channel " << channel << " is not in a track\n";
    return false;
  }
  return true;
}

void handleTrack(std::istringstream& input, ChannelManager& channels, std::size_t numChannels) {
  std::string subcommand;
  input >> subcommand;

  if (subcommand.empty() || subcommand == "list") {
    printTracks(channels, numChannels);
    return;
  }

  if (subcommand == "add") {
    std::string firstToken;
    std::string lastToken;
    if (!(input >> firstToken >> lastToken)) {
      std::cout << kTrackUsage << '\n';
      return;
    }
    std::size_t first = 0;
    std::size_t last = 0;
    if (!parseChannel(firstToken, numChannels, first) || !parseChannel(lastToken, numChannels, last)) {
      return;
    }
    std::string name;
    std::getline(input >> std::ws, name);
    std::string error;
    if (!channels.addTrack(first, last, name, error)) {
      std::cout << "Track add failed: " << error << '\n';
      return;
    }
    std::cout << "Added " << trackLabel(channels.track(first)) << '\n';
    return;
  }

  const bool known = subcommand == "remove" || subcommand == "mute" || subcommand == "solo" ||
                     subcommand == "vol" || subcommand == "volume" || subcommand == "name";
  if (!known) {
    std::cout << "Unknown track subcommand: " << subcommand << '\n';
    std::cout << kTrackUsage << '\n';
    return;
  }

  std::size_t ch = 0;
  if (!parseTrackChannel(input, channels, numChannels, ch)) {
    return;
  }
  const ChannelManager::TrackInfo track = channels.track(ch);

  if (subcommand == "remove") {
    channels.removeTrack(ch);
    std::cout << "Removed " << trackLabel(track) << '\n';
  } else if (subcommand == "mute" || subcommand == "solo") {
    const bool isSolo = subcommand == "solo";
    bool state = isSolo ? !track.soloed : !track.muted;
    if (!parseOnOff(input, state)) {
      return;
    }
    if (isSolo) {
      channels.setTrackSoloed(ch, state);
      std::cout << trackLabel(track) << " solo " << (state ? "on" : "off") << '\n';
    } else {
      channels.setTrackMuted(ch, state);
      std::cout << trackLabel(track) << (state ? " muted" : " unmuted") << '\n';
    }
  } else if (subcommand == "vol" || subcommand == "volume") {
    std::string valToken;
    if (!(input >> valToken)) {
      std::cout << trackLabel(track) << " volume: " << static_cast<int>(std::lround(track.volume * 100.0f))
                << "%\n";
      return;
    }
    int pct = -1;
    try { pct = std::stoi(valToken); } catch (...) {}
    if (pct < 0 || pct > 200) {
      std::cout << "Volume out of range: " << valToken << " (valid: 0-200)\n";
      return;
    }
    channels.setTrackVolume(ch, static_cast<float>(pct) / 100.0f);
    std::cout << trackLabel(track) << " volume set to " << pct << "%\n";
  } else {
    std::string name;
    std::getline(input >> std::ws, name);
    if (name.empty()) {
      std::cout << trackLabel(track) << " name: " << (track.name.empty() ? "-" : track.name) << '\n';
    } else if (name == "-") {
      channels.setTrackName(ch, "");
      std::cout << trackLabel(track) << " name cleared\n";
    } else {
      channels.setTrackName(ch, name);
      std::cout << "Track " << track.first << "-" << track.last << " name set to " << name << '\n';
    }
  }
}

void handleVolume(std::istringstream& input, ChannelManager& channels, std::size_t numChannels) {
  std::string chToken;
  input >> chToken;

  if (chToken.empty()) {
    std::cout << "Channel volumes:\n";
    for (std::size_t ch = 0; ch < numChannels; ++ch) {
      std::cout << "  ch " << std::setw(2) << ch << ": " << volumePercent(channels, ch) << "%\n";
    }
    return;
  }

  std::size_t ch = 0;
  if (!parseChannel(chToken, numChannels, ch)) {
    return;
  }

  std::string valToken;
  input >> valToken;

  if (valToken.empty()) {
    std::cout << "Channel " << ch << " volume: " << volumePercent(channels, ch) << "%\n";
    return;
  }

  int pct = -1;
  try { pct = std::stoi(valToken); } catch (...) {}
  if (pct < 0 || pct > 200) {
    std::cout << "Volume out of range: " << valToken << " (valid: 0-200)\n";
    return;
  }

  channels.setVolume(ch, static_cast<float>(pct) / 100.0f);
  std::cout << "Channel " << ch << " volume set to " << pct << "%\n";
}

// Resets playback and carries index-based state across a structural edit.
void afterLayoutChange(ChannelCommandContext& context, const ChannelEditResult* edit) {
  if (edit != nullptr) {
    const int moved = edit->newIndexOf(context.record.channel);
    context.record.channel = std::max(moved, 0);
  }
  context.record.canUndo = false;
  context.record.canRedo = false;
  discardPatternUndoHistory();
  context.sequencer.reset();
  context.audio.allNotesOff();
}

bool handleLayoutCommand(const std::string& subcommand,
                         std::istringstream& input,
                         ChannelCommandContext& context,
                         std::size_t numChannels) {
  if (subcommand == "undo" || subcommand == "redo") {
    std::string error;
    const bool ok = subcommand == "undo" ? context.history.undo(context.module, context.channels, error)
                                         : context.history.redo(context.module, context.channels, error);
    if (!ok) {
      std::cout << "Channel " << subcommand << " failed: " << error << '\n';
      return true;
    }
    afterLayoutChange(context, nullptr);
    std::cout << "Channel " << subcommand << " applied\n";
    return true;
  }

  const bool isMove = subcommand == "move";
  const bool isLayoutEdit = isMove || subcommand == "insert" || subcommand == "delete" || subcommand == "del" ||
                            subcommand == "dup" || subcommand == "duplicate";
  if (!isLayoutEdit) {
    return false;
  }

  std::string firstToken;
  std::string secondToken;
  if (!(input >> firstToken) || (isMove && !(input >> secondToken))) {
    std::cout << kUsage << '\n';
    return true;
  }
  std::size_t first = 0;
  std::size_t second = 0;
  if (!parseChannel(firstToken, numChannels, first) || (isMove && !parseChannel(secondToken, numChannels, second))) {
    return true;
  }

  Module& module = context.module;
  ChannelManager& channels = context.channels;
  const ChannelEditResult result = context.history.apply(module, channels, [&]() {
    if (subcommand == "insert") return insertChannel(module, channels, first);
    if (subcommand == "delete" || subcommand == "del") return deleteChannel(module, channels, first);
    if (isMove) return moveChannel(module, channels, first, second);
    return duplicateChannel(module, channels, first);
  });
  if (!result.ok) {
    std::cout << "Channel " << subcommand << " failed: " << result.error << '\n';
    return true;
  }
  afterLayoutChange(context, &result);

  if (subcommand == "insert") {
    std::cout << "Inserted empty channel at " << first << '\n';
  } else if (isMove) {
    std::cout << "Moved channel " << first << " to " << second << '\n';
  } else if (subcommand == "dup" || subcommand == "duplicate") {
    std::cout << "Duplicated channel " << first << " into " << first + 1 << '\n';
  } else {
    std::cout << "Deleted channel " << first << '\n';
  }
  return true;
}

}  // namespace

// channel [list] | vol | mute | solo | name | insert | delete | move | dup | undo | redo | track
void handleChannelCommand(std::istringstream& input, ChannelCommandContext context) {
  std::string subcommand;
  input >> subcommand;

  if (subcommand == "help") {
    printHelp();
    return;
  }

  std::lock_guard<std::mutex> lock(context.stateMutex);
  ChannelManager& channels = context.channels;
  const std::size_t numChannels = context.module.currentEditor().channels();

  if (handleLayoutCommand(subcommand, input, context, numChannels)) {
    return;
  }

  if (subcommand.empty() || subcommand == "list" || subcommand == "ls") {
    printList(channels, numChannels);
    return;
  }

  if (subcommand == "vol" || subcommand == "volume") {
    handleVolume(input, channels, numChannels);
    return;
  }

  if (subcommand == "track") {
    handleTrack(input, channels, numChannels);
    return;
  }

  if (subcommand == "mute" || subcommand == "solo") {
    const bool isSolo = subcommand == "solo";
    std::string chToken;
    if (!(input >> chToken)) {
      std::cout << kUsage << '\n';
      return;
    }
    if (isSolo && (chToken == "off" || chToken == "clear")) {
      channels.clearSolo();
      std::cout << "Solo cleared\n";
      return;
    }

    std::size_t ch = 0;
    if (!parseChannel(chToken, numChannels, ch)) {
      return;
    }
    bool state = isSolo ? !channels.isSoloed(ch) : !channels.isMuted(ch);
    if (!parseOnOff(input, state)) {
      return;
    }

    if (isSolo) {
      channels.setSoloed(ch, state);
      std::cout << "Channel " << ch << " solo " << (state ? "on" : "off") << '\n';
    } else {
      channels.setMuted(ch, state);
      std::cout << "Channel " << ch << (state ? " muted" : " unmuted") << '\n';
    }
    return;
  }

  if (subcommand == "name") {
    std::string chToken;
    if (!(input >> chToken)) {
      std::cout << kUsage << '\n';
      return;
    }
    std::size_t ch = 0;
    if (!parseChannel(chToken, numChannels, ch)) {
      return;
    }

    std::string name;
    std::getline(input >> std::ws, name);
    if (name.empty()) {
      std::cout << "Channel " << ch << " name: " << channels.displayName(ch) << '\n';
      return;
    }
    if (name == "-") {
      channels.setName(ch, "");
      std::cout << "Channel " << ch << " name cleared\n";
      return;
    }
    channels.setName(ch, name);
    std::cout << "Channel " << ch << " name set to " << name << '\n';
    return;
  }

  std::cout << "Unknown channel subcommand: " << subcommand << "\n";
  std::cout << kUsage << "\n";
}

}  // namespace extracker
