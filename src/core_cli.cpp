#include "extracker/core_cli.hpp"

#include "extracker/cli_parse_utils.hpp"

#include <algorithm>
#include <iostream>

namespace extracker {

namespace {

std::string jsonEscape(const std::string& input) {
  std::string escaped;
  escaped.reserve(input.size() + 8);
  for (char ch : input) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

}  // namespace

bool handleCoreCommand(const std::string& command,
                       std::istringstream& coreInput,
                       CoreCommandContext context) {
  auto& transport = context.transport;
  auto& editor = context.editor;
  auto& stateMutex = context.stateMutex;
  auto& loopEnabled = context.loopEnabled;
  auto& playRangeFrom = context.playRangeFrom;
  auto& playRangeTo = context.playRangeTo;
  auto& playRangeActive = context.playRangeActive;
  const auto& recordEnabled = context.recordEnabled;
  const auto& recordQuantizeEnabled = context.recordQuantizeEnabled;
  const auto& recordOverdubEnabled = context.recordOverdubEnabled;
  const auto& recordInsertJump = context.recordInsertJump;
  const auto& recordCanUndo = context.recordCanUndo;
  const auto& recordCanRedo = context.recordCanRedo;
  const auto& recordChannel = context.recordChannel;
  const auto& recordCursorRow = context.recordCursorRow;
  auto& midiInput = context.midiInput;
  const auto& midiThruEnabled = context.midiThruEnabled;
  const auto& midiInstrument = context.midiInstrument;
  const auto& midiLearnEnabled = context.midiLearnEnabled;
  const auto& midiTransportSyncEnabled = context.midiTransportSyncEnabled;
  const auto& midiTransportRunning = context.midiTransportRunning;
  const auto& midiClockTimeout = context.midiClockTimeout;
  const auto& midiFallbackLockTempo = context.midiFallbackLockTempo;
  const auto& hasMidiClockTimestamp = context.hasMidiClockTimestamp;
  const auto& midiClockEstimatedBpm = context.midiClockEstimatedBpm;
  const auto& midiChannelMap = context.midiChannelMap;
  auto& sequencer = context.sequencer;
  auto& plugins = context.plugins;
  auto& audio = context.audio;
  auto& module = context.module;
  const auto& midiClockAlive = context.midiClockAlive;
  const auto& transportSource = context.transportSource;
  const auto& songLength = context.songLength;
  const auto& songEntryAt = context.songEntryAt;
  const auto* songModeEnabled = context.songModeEnabled;
  const auto* songPlaybackPosition = context.songPlaybackPosition;
  const auto& normalizeModulePath = context.normalizeModulePath;
  const auto& savePatternToFile = context.savePatternToFile;
  const auto& loadPatternFromFile = context.loadPatternFromFile;

  const bool hasSongContext = songModeEnabled != nullptr && songPlaybackPosition != nullptr;
  const std::size_t songLen = songLength();
  bool songMode = false;
  std::size_t safeSongPos = 0;
  if (hasSongContext) {
    songMode = songModeEnabled->load();
    if (songLen > 0) {
      safeSongPos = std::min(songPlaybackPosition->load(), songLen - 1);
    }
  }
  if (command == "play") {
    if (transport.play()) {
      std::cout << "Playback started" << '\n';
    } else {
      std::cout << "Playback already running" << '\n';
    }
    return true;
  }

  if (command == "stop") {
    transport.stop();
    std::cout << "Playback stopped" << '\n';
    return true;
  }

  if (command == "tempo") {
    double bpm = 0.0;
    if (cli::parseStrictDoubleFromStream(coreInput, bpm) && bpm > 0.0 && !cli::hasExtraTokens(coreInput)) {
      transport.setTempoBpm(bpm);
      std::cout << "Tempo set to " << transport.tempoBpm() << " BPM" << '\n';
    } else {
      std::cout << "Usage: tempo <positive_bpm>" << '\n';
    }
    return true;
  }

  if (command == "loop") {
    std::string mode;
    coreInput >> mode;
    if (mode == "on") {
      loopEnabled = true;
      std::cout << "Loop enabled" << '\n';
    } else if (mode == "off") {
      loopEnabled = false;
      std::cout << "Loop disabled" << '\n';
    } else if (mode == "clear") {
      std::lock_guard<std::mutex> lock(stateMutex);
      playRangeActive = false;
      std::cout << "Loop/play range cleared" << '\n';
    } else if (mode == "range") {
      int from = -1;
      int to = -1;
      if (!cli::parseStrictIntFromStream(coreInput, from) ||
          !cli::parseStrictIntFromStream(coreInput, to) ||
          cli::hasExtraTokens(coreInput)) {
        std::cout << "Usage: loop range <from> <to>" << '\n';
      } else {
        if (from > to) {
          std::swap(from, to);
        }
        from = std::max(from, 0);
        to = std::min(to, static_cast<int>(editor.rows()) - 1);
        std::lock_guard<std::mutex> lock(stateMutex);
        playRangeFrom = from;
        playRangeTo = to;
        playRangeActive = true;
        std::cout << "Loop/play range set to " << playRangeFrom << ".." << playRangeTo << '\n';
      }
    } else {
      std::cout << "Usage: loop <on|off|clear|range>" << '\n';
    }
    return true;
  }

  if (command == "status") {
    bool jsonMode = false;
    bool minimalJsonMode = false;
    bool prettyJsonMode = false;
    std::string token;
    while (coreInput >> token) {
      if (token == "--json") {
        if (jsonMode) {
          std::cout << "Usage: status [--json [--minimal] [--pretty]]" << '\n';
          return true;
        }
        jsonMode = true;
      } else if (token == "--minimal") {
        if (minimalJsonMode) {
          std::cout << "Usage: status [--json [--minimal] [--pretty]]" << '\n';
          return true;
        }
        minimalJsonMode = true;
      } else if (token == "--pretty") {
        if (prettyJsonMode) {
          std::cout << "Usage: status [--json [--minimal] [--pretty]]" << '\n';
          return true;
        }
        prettyJsonMode = true;
      } else {
        std::cout << "Usage: status [--json [--minimal] [--pretty]]" << '\n';
        return true;
      }
    }

    if ((minimalJsonMode || prettyJsonMode) && !jsonMode) {
      std::cout << "Usage: status [--json [--minimal] [--pretty]]" << '\n';
      return true;
    }

    {
      std::lock_guard<std::mutex> lock(stateMutex);
      midiClockAlive();
    }

    auto activeRowSpan = editor.activeRowSpan();

    if (jsonMode) {
      if (prettyJsonMode) {
        if (minimalJsonMode) {
          std::cout << "{\n";
          std::cout << "  \"transport\": {\n";
          std::cout << "    \"playing\": " << (transport.isPlaying() ? "true" : "false") << ",\n";
          std::cout << "    \"tempo\": " << transport.tempoBpm() << ",\n";
          std::cout << "    \"row\": " << transport.currentRow() << "\n";
          std::cout << "  },\n";
          std::cout << "  \"loopEnabled\": " << (loopEnabled ? "true" : "false") << ",\n";
          std::cout << "  \"playRangeActive\": " << (playRangeActive ? "true" : "false") << ",\n";
          std::cout << "  \"patternNoteCount\": " << editor.noteCount() << ",\n";
          std::cout << "  \"patternActiveRowFrom\": " << activeRowSpan.first << ",\n";
          std::cout << "  \"patternActiveRowTo\": " << activeRowSpan.second << ",\n";
          std::cout << "  \"recordEnabled\": " << (recordEnabled ? "true" : "false") << ",\n";
          std::cout << "  \"midiRunning\": " << (midiInput.isRunning() ? "true" : "false") << ",\n";
          std::cout << "  \"songMode\": " << (songMode ? "true" : "false") << ",\n";
          std::cout << "  \"songLength\": " << songLen << ",\n";
          if (songLen > 0) {
            std::cout << "  \"songPosition\": " << safeSongPos << ",\n";
            std::cout << "  \"songPattern\": " << songEntryAt(safeSongPos) << ",\n";
          } else {
            std::cout << "  \"songPosition\": null,\n";
            std::cout << "  \"songPattern\": null,\n";
          }
          std::cout << "  \"transportSource\": \"" << jsonEscape(transportSource()) << "\"\n";
          std::cout << "}" << '\n';
          return true;
        }

        std::cout << "{\n";
        std::cout << "  \"audioStatus\": \"" << jsonEscape(audio.status()) << "\",\n";
        std::cout << "  \"editorStatus\": \"" << jsonEscape(editor.status()) << "\",\n";
        std::cout << "  \"transport\": {\n";
        std::cout << "    \"playing\": " << (transport.isPlaying() ? "true" : "false") << ",\n";
        std::cout << "    \"tempo\": " << transport.tempoBpm() << ",\n";
        std::cout << "    \"ticks\": " << transport.tickCount() << ",\n";
        std::cout << "    \"row\": " << transport.currentRow() << "\n";
        std::cout << "  },\n";
        std::cout << "  \"loopEnabled\": " << (loopEnabled ? "true" : "false") << ",\n";
        std::cout << "  \"playRangeActive\": " << (playRangeActive ? "true" : "false") << ",\n";
        std::cout << "  \"playRangeFrom\": " << playRangeFrom << ",\n";
        std::cout << "  \"playRangeTo\": " << playRangeTo << ",\n";
        std::cout << "  \"patternNoteCount\": " << editor.noteCount() << ",\n";
        std::cout << "  \"patternActiveRowFrom\": " << activeRowSpan.first << ",\n";
        std::cout << "  \"patternActiveRowTo\": " << activeRowSpan.second << ",\n";
        std::cout << "  \"record\": {\n";
        std::cout << "    \"enabled\": " << (recordEnabled ? "true" : "false") << ",\n";
        std::cout << "    \"quantize\": " << (recordQuantizeEnabled ? "true" : "false") << ",\n";
        std::cout << "    \"overdub\": " << (recordOverdubEnabled ? "true" : "false") << ",\n";
        std::cout << "    \"jump\": " << recordInsertJump << ",\n";
        std::cout << "    \"canUndo\": " << (recordCanUndo ? "true" : "false") << ",\n";
        std::cout << "    \"canRedo\": " << (recordCanRedo ? "true" : "false") << ",\n";
        std::cout << "    \"channel\": " << recordChannel << ",\n";
        std::cout << "    \"cursorRow\": " << recordCursorRow << "\n";
        std::cout << "  },\n";
        std::cout << "  \"midi\": {\n";
        std::cout << "    \"running\": " << (midiInput.isRunning() ? "true" : "false") << ",\n";
        std::cout << "    \"thru\": " << (midiThruEnabled ? "true" : "false") << ",\n";
        std::cout << "    \"instrument\": " << midiInstrument << ",\n";
        std::cout << "    \"learn\": " << (midiLearnEnabled ? "true" : "false") << ",\n";
        std::cout << "    \"transportSync\": " << (midiTransportSyncEnabled ? "true" : "false") << ",\n";
        std::cout << "    \"transportRunning\": " << (midiTransportRunning ? "true" : "false") << ",\n";
        std::cout << "    \"clockTimeoutMs\": " << midiClockTimeout.count() << ",\n";
        std::cout << "    \"fallbackTempoLock\": " << (midiFallbackLockTempo ? "true" : "false") << ",\n";
        std::cout << "    \"clockActive\": "
                  << ((hasMidiClockTimestamp && midiClockEstimatedBpm > 0.0) ? "true" : "false")
                  << ",\n";
        if (hasMidiClockTimestamp && midiClockEstimatedBpm > 0.0) {
          std::cout << "    \"clockEstimatedBpm\": " << midiClockEstimatedBpm << ",\n";
        } else {
          std::cout << "    \"clockEstimatedBpm\": null,\n";
        }
        std::cout << "    \"channelMap\": [";
        for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
          if (ch > 0) {
            std::cout << ", ";
          }
          std::cout << midiChannelMap[ch];
        }
        std::cout << "]\n";
        std::cout << "  },\n";
        std::cout << "  \"song\": {\n";
        std::cout << "    \"mode\": \"" << (songMode ? "song" : "pattern") << "\",\n";
        std::cout << "    \"length\": " << songLen << ",\n";
        if (songLen > 0) {
          std::cout << "    \"position\": " << safeSongPos << ",\n";
          std::cout << "    \"pattern\": " << songEntryAt(safeSongPos) << "\n";
        } else {
          std::cout << "    \"position\": null,\n";
          std::cout << "    \"pattern\": null\n";
        }
        std::cout << "  },\n";
        std::cout << "  \"transportSource\": \"" << jsonEscape(transportSource()) << "\",\n";
        std::cout << "  \"sequencerDispatchCount\": " << sequencer.dispatchCount() << ",\n";
        std::cout << "  \"sequencerActiveVoices\": " << sequencer.activeVoiceCount() << ",\n";
        std::cout << "  \"pluginNoteOnEvents\": " << plugins.noteOnEventCount() << ",\n";
        std::cout << "  \"pluginNoteOffEvents\": " << plugins.noteOffEventCount() << ",\n";
        std::cout << "  \"instrument0Plugin\": \"" << jsonEscape(plugins.pluginForInstrument(0)) << "\",\n";
        std::cout << "  \"instrument1Plugin\": \"" << jsonEscape(plugins.pluginForInstrument(1)) << "\",\n";
        std::cout << "  \"pluginStatus\": \"" << jsonEscape(plugins.status()) << "\"\n";
        std::cout << "}" << '\n';
        return true;
      }

      if (minimalJsonMode) {
        std::cout << "{";
        std::cout << "\"transport\":{";
        std::cout << "\"playing\":" << (transport.isPlaying() ? "true" : "false") << ",";
        std::cout << "\"tempo\":" << transport.tempoBpm() << ",";
        std::cout << "\"row\":" << transport.currentRow() << "},";
        std::cout << "\"loopEnabled\":" << (loopEnabled ? "true" : "false") << ",";
        std::cout << "\"playRangeActive\":" << (playRangeActive ? "true" : "false") << ",";
        std::cout << "\"patternNoteCount\":" << editor.noteCount() << ",";
        std::cout << "\"patternActiveRowFrom\":" << activeRowSpan.first << ",";
        std::cout << "\"patternActiveRowTo\":" << activeRowSpan.second << ",";
        std::cout << "\"recordEnabled\":" << (recordEnabled ? "true" : "false") << ",";
        std::cout << "\"midiRunning\":" << (midiInput.isRunning() ? "true" : "false") << ",";
        std::cout << "\"songMode\":" << (songMode ? "true" : "false") << ",";
        std::cout << "\"songLength\":" << songLen << ",";
        if (songLen > 0) {
          std::cout << "\"songPosition\":" << safeSongPos << ",";
          std::cout << "\"songPattern\":" << songEntryAt(safeSongPos) << ",";
        } else {
          std::cout << "\"songPosition\":null,";
          std::cout << "\"songPattern\":null,";
        }
        std::cout << "\"transportSource\":\"" << jsonEscape(transportSource()) << "\"";
        std::cout << "}" << '\n';
        return true;
      }

      std::cout << "{";
      std::cout << "\"audioStatus\":\"" << jsonEscape(audio.status()) << "\",";
      std::cout << "\"editorStatus\":\"" << jsonEscape(editor.status()) << "\",";
      std::cout << "\"transport\":{";
      std::cout << "\"playing\":" << (transport.isPlaying() ? "true" : "false") << ",";
      std::cout << "\"tempo\":" << transport.tempoBpm() << ",";
      std::cout << "\"ticks\":" << transport.tickCount() << ",";
      std::cout << "\"row\":" << transport.currentRow() << "},";
      std::cout << "\"loopEnabled\":" << (loopEnabled ? "true" : "false") << ",";
      std::cout << "\"playRangeActive\":" << (playRangeActive ? "true" : "false") << ",";
      std::cout << "\"playRangeFrom\":" << playRangeFrom << ",";
      std::cout << "\"playRangeTo\":" << playRangeTo << ",";
      std::cout << "\"patternNoteCount\":" << editor.noteCount() << ",";
      std::cout << "\"patternActiveRowFrom\":" << activeRowSpan.first << ",";
      std::cout << "\"patternActiveRowTo\":" << activeRowSpan.second << ",";
      std::cout << "\"record\":{";
      std::cout << "\"enabled\":" << (recordEnabled ? "true" : "false") << ",";
      std::cout << "\"quantize\":" << (recordQuantizeEnabled ? "true" : "false") << ",";
      std::cout << "\"overdub\":" << (recordOverdubEnabled ? "true" : "false") << ",";
      std::cout << "\"jump\":" << recordInsertJump << ",";
      std::cout << "\"canUndo\":" << (recordCanUndo ? "true" : "false") << ",";
      std::cout << "\"canRedo\":" << (recordCanRedo ? "true" : "false") << ",";
      std::cout << "\"channel\":" << recordChannel << ",";
      std::cout << "\"cursorRow\":" << recordCursorRow << "},";
      std::cout << "\"midi\":{";
      std::cout << "\"running\":" << (midiInput.isRunning() ? "true" : "false") << ",";
      std::cout << "\"thru\":" << (midiThruEnabled ? "true" : "false") << ",";
      std::cout << "\"instrument\":" << midiInstrument << ",";
      std::cout << "\"learn\":" << (midiLearnEnabled ? "true" : "false") << ",";
      std::cout << "\"transportSync\":" << (midiTransportSyncEnabled ? "true" : "false") << ",";
      std::cout << "\"transportRunning\":" << (midiTransportRunning ? "true" : "false") << ",";
      std::cout << "\"clockTimeoutMs\":" << midiClockTimeout.count() << ",";
      std::cout << "\"fallbackTempoLock\":" << (midiFallbackLockTempo ? "true" : "false") << ",";
      std::cout << "\"clockActive\":"
                << ((hasMidiClockTimestamp && midiClockEstimatedBpm > 0.0) ? "true" : "false")
                << ",";
      if (hasMidiClockTimestamp && midiClockEstimatedBpm > 0.0) {
        std::cout << "\"clockEstimatedBpm\":" << midiClockEstimatedBpm << ",";
      } else {
        std::cout << "\"clockEstimatedBpm\":null,";
      }
      std::cout << "\"channelMap\":[";
      for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
        if (ch > 0) {
          std::cout << ",";
        }
        std::cout << midiChannelMap[ch];
      }
      std::cout << "]},";
      std::cout << "\"song\":{";
      std::cout << "\"mode\":\"" << (songMode ? "song" : "pattern") << "\",";
      std::cout << "\"length\":" << songLen << ",";
      if (songLen > 0) {
        std::cout << "\"position\":" << safeSongPos << ",";
        std::cout << "\"pattern\":" << songEntryAt(safeSongPos);
      } else {
        std::cout << "\"position\":null,";
        std::cout << "\"pattern\":null";
      }
      std::cout << "},";
      std::cout << "\"transportSource\":\"" << jsonEscape(transportSource()) << "\",";
      std::cout << "\"sequencerDispatchCount\":" << sequencer.dispatchCount() << ",";
      std::cout << "\"sequencerActiveVoices\":" << sequencer.activeVoiceCount() << ",";
      std::cout << "\"pluginNoteOnEvents\":" << plugins.noteOnEventCount() << ",";
      std::cout << "\"pluginNoteOffEvents\":" << plugins.noteOffEventCount() << ",";
      std::cout << "\"instrument0Plugin\":\"" << jsonEscape(plugins.pluginForInstrument(0)) << "\",";
      std::cout << "\"instrument1Plugin\":\"" << jsonEscape(plugins.pluginForInstrument(1)) << "\",";
      std::cout << "\"pluginStatus\":\"" << jsonEscape(plugins.status()) << "\"";
      std::cout << "}" << '\n';
      return true;
    }

    std::cout << audio.status() << '\n';
    std::cout << editor.status() << '\n';
    std::cout << "Transport playing: " << (transport.isPlaying() ? "yes" : "no") << '\n';
    std::cout << "Transport tempo: " << transport.tempoBpm() << '\n';
    std::cout << "Transport ticks: " << transport.tickCount() << '\n';
    std::cout << "Transport row: " << transport.currentRow() << '\n';
    std::cout << "Loop enabled: " << (loopEnabled ? "yes" : "no") << '\n';
    if (playRangeActive) {
      std::cout << "Play range: " << playRangeFrom << ".." << playRangeTo << '\n';
    } else {
      std::cout << "Play range: (full pattern)" << '\n';
    }
    std::cout << "Pattern note count: " << editor.noteCount() << '\n';
    if (activeRowSpan.first >= 0) {
      std::cout << "Pattern active rows: " << activeRowSpan.first << ".." << activeRowSpan.second << '\n';
    } else {
      std::cout << "Pattern active rows: (empty)" << '\n';
    }
    std::cout << "Record enabled: " << (recordEnabled ? "yes" : "no") << '\n';
    std::cout << "Record quantize: " << (recordQuantizeEnabled ? "on" : "off") << '\n';
    std::cout << "Record overdub: " << (recordOverdubEnabled ? "on" : "off") << '\n';
    std::cout << "Record jump: " << recordInsertJump << '\n';
    std::cout << "Record undo available: " << (recordCanUndo ? "yes" : "no") << '\n';
    std::cout << "Record redo available: " << (recordCanRedo ? "yes" : "no") << '\n';
    if (recordEnabled) {
      std::cout << "Record channel: " << recordChannel << '\n';
      std::cout << "Record cursor row: " << recordCursorRow << '\n';
    }
    std::cout << "MIDI running: " << (midiInput.isRunning() ? "yes" : "no") << '\n';
    std::cout << "MIDI thru: " << (midiThruEnabled ? "on" : "off") << '\n';
    std::cout << "MIDI instrument: " << midiInstrument << '\n';
    std::cout << "MIDI learn: " << (midiLearnEnabled ? "on" : "off") << '\n';
    std::cout << "MIDI transport sync: " << (midiTransportSyncEnabled ? "on" : "off") << '\n';
    std::cout << "MIDI transport running: " << (midiTransportRunning ? "yes" : "no") << '\n';
    std::cout << "MIDI clock timeout ms: " << midiClockTimeout.count() << '\n';
    std::cout << "MIDI fallback tempo lock: " << (midiFallbackLockTempo ? "on" : "off") << '\n';
    std::cout << "Transport source: " << transportSource() << '\n';
    std::cout << "Song playback mode: " << (songMode ? "song" : "pattern") << '\n';
    if (songLen > 0) {
      std::cout << "Song position: " << (safeSongPos + 1)
                << "/" << songLen
                << " (pattern " << (songEntryAt(safeSongPos) + 1) << ")" << '\n';
    } else {
      std::cout << "Song position: (empty)" << '\n';
    }
    if (hasMidiClockTimestamp && midiClockEstimatedBpm > 0.0) {
      std::cout << "MIDI clock active: yes" << '\n';
      std::cout << "MIDI clock BPM (estimated): " << midiClockEstimatedBpm << '\n';
    } else {
      std::cout << "MIDI clock active: no" << '\n';
      std::cout << "MIDI clock BPM (estimated): n/a" << '\n';
    }
    std::cout << "MIDI channel map:";
    bool hasMidiMap = false;
    for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
      if (midiChannelMap[ch] >= 0) {
        hasMidiMap = true;
        std::cout << " ch" << ch << "->" << midiChannelMap[ch];
      }
    }
    if (!hasMidiMap) {
      std::cout << " (empty)";
    }
    std::cout << '\n';
    std::cout << "Sequencer dispatch count: " << sequencer.dispatchCount() << '\n';
    std::cout << "Sequencer active voices: " << sequencer.activeVoiceCount() << '\n';
    std::cout << "Plugin note-on events: " << plugins.noteOnEventCount() << '\n';
    std::cout << "Plugin note-off events: " << plugins.noteOffEventCount() << '\n';
    std::cout << "Instrument 0 plugin: " << plugins.pluginForInstrument(0) << '\n';
    std::cout << "Instrument 1 plugin: " << plugins.pluginForInstrument(1) << '\n';
    std::cout << plugins.status() << '\n';
    return true;
  }

  if (command == "reset") {
    transport.stop();
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      transport.resetTickCount();
      sequencer.reset();
      audio.allNotesOff();
      playRangeActive = false;
    }
    std::cout << "Playback state reset" << '\n';
    return true;
  }

  if (command == "save") {
    std::string path;
    coreInput >> path;
    if (path.empty()) {
      std::cout << "Usage: save <file>" << '\n';
    } else {
      std::lock_guard<std::mutex> lock(stateMutex);
      std::string resolvedPath = normalizeModulePath(path);
      if (savePatternToFile(resolvedPath)) {
        std::cout << "Module saved to " << resolvedPath << '\n';
      } else {
        std::cout << "Failed to save module to " << resolvedPath << '\n';
      }
    }
    return true;
  }

  if (command == "load") {
    std::string path;
    coreInput >> path;
    if (path.empty()) {
      std::cout << "Usage: load <file>" << '\n';
    } else {
      std::lock_guard<std::mutex> lock(stateMutex);
      std::string resolvedPath = normalizeModulePath(path);
      if (loadPatternFromFile(resolvedPath)) {
        sequencer.reset();
        audio.allNotesOff();
        std::cout << "Module loaded from " << resolvedPath << '\n';
      } else {
        std::cout << "Failed to load module from " << resolvedPath << '\n';
      }
    }
    return true;
  }

  if (command == "message") {
    std::string subcommand;
    coreInput >> subcommand;
    if (subcommand == "set") {
      std::string message;
      std::getline(coreInput, message);
      if (!message.empty() && message[0] == ' ') {
        message = message.substr(1);
      }
      std::lock_guard<std::mutex> lock(stateMutex);
      module.setMessage(message);
      std::cout << "Module message set" << '\n';
    } else if (subcommand == "get") {
      std::lock_guard<std::mutex> lock(stateMutex);
      std::cout << "Module message: '" << module.message() << "'" << '\n';
    } else {
      std::cout << "Usage: message set <text> | message get" << '\n';
    }
    return true;
  }

  return false;
}

}  // namespace extracker
