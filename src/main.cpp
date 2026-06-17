#include <iostream>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#include "extracker/audio_engine.hpp"
#include "extracker/command_registry.hpp"
#include "extracker/core_cli.hpp"
#include "extracker/default_command_bindings.hpp"
#include "extracker/midi_cli.hpp"
#include "extracker/midi_input.hpp"
#include "extracker/module.hpp"
#include "extracker/module_cli.hpp"
#include "extracker/note_cli.hpp"
#include "extracker/pattern_cli.hpp"
#include "extracker/plugin_cli.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/record_cli.hpp"
#include "extracker/sample_cli.hpp"
#include "extracker/record_workflow.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

namespace {

std::string escapeModuleMessage(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (char ch : text) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
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

std::string unescapeModuleMessage(const std::string& text) {
  std::string decoded;
  decoded.reserve(text.size());
  bool escaping = false;
  for (char ch : text) {
    if (!escaping) {
      if (ch == '\\') {
        escaping = true;
      } else {
        decoded.push_back(ch);
      }
      continue;
    }

    switch (ch) {
      case 'n':
        decoded.push_back('\n');
        break;
      case 'r':
        decoded.push_back('\r');
        break;
      case 't':
        decoded.push_back('\t');
        break;
      case '\\':
        decoded.push_back('\\');
        break;
      default:
        decoded.push_back(ch);
        break;
    }
    escaping = false;
  }
  if (escaping) {
    decoded.push_back('\\');
  }
  return decoded;
}

}  // namespace

int main() {
  extracker::AudioEngine audio;
  extracker::Module module;
  extracker::PluginHost plugins;
  extracker::Sequencer sequencer;
  extracker::Transport transport;
  extracker::MidiInput midiInput;
  std::atomic<bool> running{true};
  std::mutex stateMutex;
  bool loopEnabled = false;
  bool playRangeActive = false;
  int playRangeFrom = 0;
  int playRangeTo = 0;
  int playRangeStep = 1;
  std::atomic<bool> songModeEnabled{false};
  std::atomic<std::size_t> songPlaybackPosition{0};
  extracker::RecordWorkflowState recordState;
  bool& recordEnabled = recordState.enabled;
  int& recordChannel = recordState.channel;
  int& recordCursorRow = recordState.cursorRow;
  bool& recordQuantizeEnabled = recordState.quantizeEnabled;
  bool& recordOverdubEnabled = recordState.overdubEnabled;
  int& recordInsertJump = recordState.insertJump;
  bool midiThruEnabled = true;
  int midiInstrument = 0;
  bool midiLearnEnabled = false;
  bool midiTransportSyncEnabled = false;
  bool midiTransportRunning = false;
  bool midiFallbackLockTempo = true;
  bool hasMidiClockTimestamp = false;
  std::chrono::steady_clock::time_point lastMidiClockTimestamp{};
  double midiClockEstimatedBpm = 0.0;
  std::chrono::milliseconds midiClockTimeout{1500};
  std::array<int, 16> midiChannelMap{};
  midiChannelMap.fill(-1);

  extracker::RecordEditState& recordUndoState = recordState.undoState;
  extracker::RecordEditState& recordRedoState = recordState.redoState;
  bool& recordCanUndo = recordState.canUndo;
  bool& recordCanRedo = recordState.canRedo;

  auto midiClockAlive = [&]() {
    if (!hasMidiClockTimestamp) {
      return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - lastMidiClockTimestamp > midiClockTimeout) {
      hasMidiClockTimestamp = false;
      midiClockEstimatedBpm = 0.0;
      return false;
    }

    return true;
  };

  auto transportSource = [&]() -> const char* {
    if (midiTransportSyncEnabled && midiTransportRunning && hasMidiClockTimestamp) {
      return "external-midi-clock";
    }

    if (midiTransportSyncEnabled && transport.isPlaying() && !midiTransportRunning) {
      return "internal-fallback";
    }

    return "internal";
  };

  auto midiNoteToFrequencyHz = [](int midiNote) {
    int clamped = std::clamp(midiNote, 0, 127);
    return 440.0 * std::pow(2.0, static_cast<double>(clamped - 69) / 12.0);
  };

  auto chooseRecordRow = [&](int channel) {
    int originalChannel = recordState.channel;
    recordState.channel = channel;
    int row = extracker::chooseRecordRow(module.currentEditor(), transport, recordState);
    recordState.channel = originalChannel;
    return row;
  };

  auto applyRecordWrite = [&](int row,
                              int channel,
                              int note,
                              std::uint8_t instrument,
                              std::uint32_t gateTicks,
                              std::uint8_t velocity,
                              bool retrigger,
                              std::uint8_t effectCommand,
                              std::uint8_t effectValue) {
    int originalChannel = recordState.channel;
    recordState.channel = channel;
    extracker::applyRecordWrite(
        module.currentEditor(),
      recordState,
        row,
        note,
        instrument,
        gateTicks,
        velocity,
        retrigger,
        effectCommand,
        effectValue);
    recordState.channel = originalChannel;
  };

  std::function<int(int)> chooseRecordRowFn = chooseRecordRow;
  std::function<void(int,
                     int,
                     int,
                     std::uint8_t,
                     std::uint32_t,
                     std::uint8_t,
                     bool,
                     std::uint8_t,
                     std::uint8_t)>
      applyRecordWriteFn = applyRecordWrite;
  std::function<double(int)> midiNoteToFrequencyHzFn = midiNoteToFrequencyHz;

  auto toLower = [](std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return value;
  };

  auto parseEndpointToken = [](const std::string& token, int& client, int& port) {
    auto colon = token.find(':');
    if (colon == std::string::npos) {
      return false;
    }

    std::istringstream a(token.substr(0, colon));
    std::istringstream b(token.substr(colon + 1));
    int parsedClient = -1;
    int parsedPort = -1;
    a >> parsedClient;
    b >> parsedPort;
    if (!a || !b || parsedClient < 0 || parsedPort < 0) {
      return false;
    }

    client = parsedClient;
    port = parsedPort;
    return true;
  };

  auto parseHintEndpoint = [&](const std::string& hint, int& client, int& port) {
    std::size_t lastSpace = hint.find_last_of(" \t");
    std::string candidate = (lastSpace == std::string::npos) ? hint : hint.substr(lastSpace + 1);
    return parseEndpointToken(candidate, client, port);
  };

  auto readCommandOutput = [](const std::string& command, std::string& output) {
    output.clear();
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
      return false;
    }

    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      output += buffer;
    }

    int status = pclose(pipe);
    return status == 0;
  };

  auto parseAconnectPorts = [](const std::string& text) {
    std::vector<extracker::MidiPortEntry> entries;
    std::istringstream input(text);
    std::string line;
    int currentClient = -1;
    std::string currentClientName;

    while (std::getline(input, line)) {
      if (line.rfind("client ", 0) == 0) {
        std::size_t colon = line.find(':', 7);
        if (colon == std::string::npos) {
          currentClient = -1;
          currentClientName.clear();
          continue;
        }

        std::istringstream clientStream(line.substr(7, colon - 7));
        clientStream >> currentClient;
        if (!clientStream) {
          currentClient = -1;
          currentClientName.clear();
          continue;
        }

        std::size_t q1 = line.find('\'', colon);
        std::size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('\'', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
          currentClientName = line.substr(q1 + 1, q2 - q1 - 1);
        } else {
          currentClientName.clear();
        }
        continue;
      }

      if (currentClient < 0) {
        continue;
      }

      std::size_t q1 = line.find('\'');
      std::size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('\'', q1 + 1);
      if (q1 == std::string::npos || q2 == std::string::npos) {
        continue;
      }

      std::string left = line.substr(0, q1);
      std::istringstream portStream(left);
      int port = -1;
      portStream >> port;
      if (!portStream || port < 0) {
        continue;
      }

      extracker::MidiPortEntry entry;
      entry.client = currentClient;
      entry.port = port;
      entry.clientName = currentClientName;
      entry.portName = line.substr(q1 + 1, q2 - q1 - 1);
      entries.push_back(entry);
    }

    return entries;
  };

  auto normalizeModulePath = [](const std::string& rawPath) {
    std::filesystem::path path(rawPath);
    if (!path.has_extension()) {
      path += ".xtp";
    }
    return path.string();
  };

  auto onMidiEvent = [&](const extracker::MidiEvent& event) {
    std::lock_guard<std::mutex> lock(stateMutex);
    extracker::handleMidiEventLocked(
        event,
        extracker::MidiEventContext{transport,
                         sequencer,
                         audio,
                         plugins,
                         midiTransportSyncEnabled,
                         midiTransportRunning,
                         hasMidiClockTimestamp,
                         lastMidiClockTimestamp,
                         midiClockEstimatedBpm,
                         midiInstrument,
                         midiChannelMap,
                         midiLearnEnabled,
                         midiThruEnabled,
                         recordEnabled,
                         recordChannel,
                         chooseRecordRowFn,
                         applyRecordWriteFn,
                         midiNoteToFrequencyHzFn});
  };

  std::cout << "exTracker prototype boot" << '\n';
    audio.setPluginHost(&plugins);

  if (audio.start()) {
    std::cout << "Audio started using backend: " << audio.backendName() << '\n';
  } else {
    std::cout << "Audio start failed for selected backend" << '\n';
  }

  if (!plugins.loadPlugin("builtin.sine")) {
    std::cout << "Failed to load builtin.sine plugin" << '\n';
  }
  if (!plugins.loadPlugin("builtin.square")) {
    std::cout << "Failed to load builtin.square plugin" << '\n';
  }
  // Assign builtin plugins to default instruments
  if (!plugins.assignInstrument(0, "builtin.sine")) {
    std::cout << "Failed to assign builtin.sine to instrument 0" << '\n';
  }
  if (!plugins.assignInstrument(1, "builtin.square")) {
    std::cout << "Failed to assign builtin.square to instrument 1" << '\n';
  }
  transport.setTempoBpm(125.0);
  transport.setTicksPerBeat(6);
  transport.setTicksPerRow(1);
  transport.setPatternRows(static_cast<std::uint32_t>(module.currentEditor().rows()));
  transport.resetTickCount();

  auto savePatternToFile = [&](const std::string& path) -> bool {
    std::ofstream out(path);
    if (!out) {
      return false;
    }

    out << "EXTRACKER_SONG_V1 "
        << module.currentEditor().rows() << " "
        << module.currentEditor().channels() << " "
        << module.patternCount() << " "
        << module.songLength() << " "
        << module.currentPattern() << " "
        << module.firstSongEntryForPattern(module.currentPattern()) << "\n";

    for (std::size_t patternIndex = 0; patternIndex < module.patternCount(); ++patternIndex) {
      const auto& editor = module.patternEditor(patternIndex);
      out << "PATTERN " << patternIndex << "\n";
      for (std::size_t row = 0; row < editor.rows(); ++row) {
        for (std::size_t channel = 0; channel < editor.channels(); ++channel) {
          int iRow = static_cast<int>(row);
          int iChannel = static_cast<int>(channel);
          out << row << " " << channel << " "
              << (editor.hasNoteAt(iRow, iChannel) ? 1 : 0) << " "
              << editor.noteAt(iRow, iChannel) << " "
              << static_cast<int>(editor.instrumentAt(iRow, iChannel)) << " "
              << editor.sampleAt(iRow, iChannel) << " "
              << editor.gateTicksAt(iRow, iChannel) << " "
              << static_cast<int>(editor.velocityAt(iRow, iChannel)) << " "
              << (editor.retriggerAt(iRow, iChannel) ? 1 : 0) << " "
              << static_cast<int>(editor.effectCommandAt(iRow, iChannel)) << " "
              << static_cast<int>(editor.effectValueAt(iRow, iChannel)) << "\n";
        }
      }
    }

    out << "SONG_ORDER";
    for (std::size_t songIndex = 0; songIndex < module.songLength(); ++songIndex) {
      out << " " << module.songEntryAt(songIndex);
    }
    out << "\n";

    out << "PATTERN_SWING";
    for (std::size_t patternIndex = 0; patternIndex < module.patternCount(); ++patternIndex) {
      out << " " << static_cast<int>(module.patternSwing(patternIndex));
    }
    out << "\n";

    out << "INSERT_SWING_INHERIT " << (module.inheritSwingOnInsert() ? 1 : 0) << "\n";
    out << "ROW_EDIT_SCOPE " << (module.rowEditAllChannels() ? 1 : 0) << "\n";

    out << "TRANSPORT " << transport.tempoBpm() << " " << transport.ticksPerBeat() << " " << transport.ticksPerRow() << "\n";

    out << "MIDI_MAP";
    for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
      out << " " << midiChannelMap[ch];
    }
    out << "\n";

    out << "MIDI_TRANSPORT " << midiClockTimeout.count() << " " << (midiFallbackLockTempo ? 1 : 0) << "\n";
    for (std::size_t sampleSlot = 0; sampleSlot < extracker::PluginHost::kMaxSampleSlots; ++sampleSlot) {
      const std::string samplePath = plugins.samplePathForSlot(static_cast<std::uint16_t>(sampleSlot));
      if (!samplePath.empty()) {
        const std::string sampleName = plugins.sampleNameForSlot(static_cast<std::uint16_t>(sampleSlot));
        out << "SAMPLE_ENTRY " << sampleSlot << " " << std::quoted(sampleName) << " " << std::quoted(samplePath) << "\n";
      }
    }
    out << "MODULE_MESSAGE " << std::quoted(escapeModuleMessage(module.message())) << "\n";
    extracker::writeRecordState(out, recordState);

    return true;
  };

  auto loadPatternFromFile = [&](const std::string& path) -> bool {
    std::ifstream in(path);
    if (!in) {
      return false;
    }

    std::string magic;
    const std::filesystem::path modulePath(path);
    const std::filesystem::path moduleDirectory = modulePath.has_parent_path() ? modulePath.parent_path() : std::filesystem::current_path();
    std::size_t fileRows = 0;
    std::size_t fileChannels = 0;
    in >> magic >> fileRows >> fileChannels;
    if (!in || fileRows == 0 || fileChannels == 0) {
      return false;
    }

    const bool isSongV1 = (magic == "EXTRACKER_SONG_V1");
    const bool isLegacyV1 = (magic == "EXTRACKER_PATTERN_V1" || magic == "EXTRACKER_MODULE_V1");
    const bool isLegacyV2 = (magic == "EXTRACKER_PATTERN_V2" || magic == "EXTRACKER_MODULE_V2");
    if (!isSongV1 && !isLegacyV1 && !isLegacyV2) {
      return false;
    }

    for (std::size_t instrument = 0; instrument < extracker::PluginHost::kMaxInstrumentSlots; ++instrument) {
      plugins.clearSampleFromInstrument(static_cast<std::uint8_t>(instrument));
    }
    for (std::size_t sampleSlot = 0; sampleSlot < extracker::PluginHost::kMaxSampleSlots; ++sampleSlot) {
      plugins.clearSampleSlot(static_cast<std::uint16_t>(sampleSlot));
    }

    if (isSongV1) {
      std::size_t filePatternCount = 0;
      std::size_t fileSongLength = 0;
      std::size_t fileCurrentPattern = 0;
      std::size_t fileCurrentSongPosition = 0;
      in >> filePatternCount >> fileSongLength >> fileCurrentPattern >> fileCurrentSongPosition;
      if (!in || filePatternCount == 0 || fileSongLength == 0) {
        return false;
      }

      module.reset(fileRows, fileChannels, filePatternCount);

      auto isSongTailToken = [](const std::string& token) {
        return token == "SONG_ORDER" || token == "PATTERN_SWING" ||
               token == "INSERT_SWING_INHERIT" || token == "ROW_EDIT_SCOPE" ||
               token == "TRANSPORT" || token == "MIDI_MAP" ||
               token == "MIDI_TRANSPORT" || token == "SAMPLE_BANK" ||
               token == "SAMPLE_ENTRY" || token == "MODULE_MESSAGE" ||
               token.rfind("RECORD_", 0) == 0;
      };

      std::string pendingToken;
      bool hasPendingToken = false;

      for (std::size_t patternIndex = 0; patternIndex < filePatternCount; ++patternIndex) {
        std::string patternToken;
        std::size_t storedPatternIndex = 0;
        if (hasPendingToken) {
          patternToken = pendingToken;
          hasPendingToken = false;
        } else {
          in >> patternToken;
        }
        in >> storedPatternIndex;
        if (!in || patternToken != "PATTERN" || storedPatternIndex != patternIndex) {
          return false;
        }

        auto& editor = module.patternEditor(patternIndex);
        while (true) {
          std::string firstToken;
          if (!(in >> firstToken)) {
            if (patternIndex + 1 < filePatternCount) {
              return false;
            }
            break;
          }

          if (firstToken == "PATTERN" || isSongTailToken(firstToken)) {
            pendingToken = firstToken;
            hasPendingToken = true;
            break;
          }

          int parsedRow = 0;
          {
            std::istringstream rowParser(firstToken);
            char extra = '\0';
            if (!(rowParser >> parsedRow) || (rowParser >> extra)) {
              return false;
            }
          }

          int parsedChannel = 0;
          int hasNote = 0;
          int note = -1;
          int instrument = 0;
          int sample = 0xFFFF;
          int gateTicks = 0;
          int velocity = 100;
          int retrigger = 0;
          int effectCommand = 0;
          int effectValue = 0;

          in >> parsedChannel >> hasNote >> note >> instrument >> sample >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue;
          if (!in) {
            return false;
          }

          if (parsedRow < 0 || parsedChannel < 0 ||
              static_cast<std::size_t>(parsedRow) >= editor.rows() ||
              static_cast<std::size_t>(parsedChannel) >= editor.channels()) {
            continue;
          }

          if (hasNote != 0) {
            editor.insertNote(
                parsedRow,
                parsedChannel,
                note,
                static_cast<std::uint8_t>(std::clamp(instrument, 0, 255)),
                static_cast<std::uint32_t>(std::max(gateTicks, 0)),
                static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)),
                retrigger != 0,
                static_cast<std::uint8_t>(std::clamp(effectCommand, 0, 255)),
                static_cast<std::uint8_t>(std::clamp(effectValue, 0, 255)));
            if (sample != 0xFFFF) {
              editor.setSample(parsedRow, parsedChannel, static_cast<std::uint16_t>(std::clamp(sample, 0, 65535)));
            }
          } else if (effectCommand != 0 || effectValue != 0) {
            editor.setEffect(
                parsedRow,
                parsedChannel,
                static_cast<std::uint8_t>(std::clamp(effectCommand, 0, 255)),
                static_cast<std::uint8_t>(std::clamp(effectValue, 0, 255)));
          }
        }
      }

      std::vector<std::size_t> songOrder;
      std::string tailToken;
      while (hasPendingToken || (in >> tailToken)) {
        if (hasPendingToken) {
          tailToken = pendingToken;
          hasPendingToken = false;
        }
        if (tailToken == "SONG_ORDER") {
          songOrder.clear();
          for (std::size_t i = 0; i < fileSongLength; ++i) {
            std::size_t entry = 0;
            in >> entry;
            if (!in) {
              return false;
            }
            songOrder.push_back(entry);
          }
        } else if (tailToken == "PATTERN_SWING") {
          std::vector<std::uint8_t> parsedSwing;
          parsedSwing.reserve(module.patternCount());
          for (std::size_t patternIndex = 0; patternIndex < module.patternCount(); ++patternIndex) {
            int swingPercent = 50;
            if (!(in >> swingPercent)) {
              return false;
            }
            parsedSwing.push_back(static_cast<std::uint8_t>(std::clamp(swingPercent, 50, 75)));
          }
          for (std::size_t patternIndex = 0; patternIndex < parsedSwing.size(); ++patternIndex) {
            module.setPatternSwing(patternIndex, parsedSwing[patternIndex]);
          }
        } else if (tailToken == "INSERT_SWING_INHERIT") {
          int enabled = 0;
          if (!(in >> enabled)) {
            return false;
          }
          module.setInheritSwingOnInsert(enabled != 0);
        } else if (tailToken == "ROW_EDIT_SCOPE") {
          int allChannels = 0;
          if (!(in >> allChannels)) {
            return false;
          }
          module.setRowEditAllChannels(allChannels != 0);
        } else if (tailToken == "TRANSPORT") {
          std::string transportLine;
          std::getline(in, transportLine);
          std::istringstream transportParser(transportLine);
          double tempoBpm = transport.tempoBpm();
          int ticksPerBeat = static_cast<int>(transport.ticksPerBeat());
          int ticksPerRow = static_cast<int>(transport.ticksPerRow());
          if (transportParser >> tempoBpm >> ticksPerBeat) {
            if (tempoBpm > 0.0) {
              transport.setTempoBpm(tempoBpm);
            }
            if (ticksPerBeat > 0) {
              transport.setTicksPerBeat(static_cast<std::uint32_t>(ticksPerBeat));
            }
            if (transportParser >> ticksPerRow) {
              if (ticksPerRow > 0) {
                transport.setTicksPerRow(static_cast<std::uint32_t>(ticksPerRow));
              }
            }
          }
        } else if (tailToken == "MIDI_MAP") {
          for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
            int mapped = -1;
            if (!(in >> mapped)) {
              return false;
            }
            midiChannelMap[ch] = mapped;
          }
        } else if (tailToken == "MIDI_TRANSPORT") {
          long long timeoutMs = midiClockTimeout.count();
          int lockTempo = midiFallbackLockTempo ? 1 : 0;
          if (!(in >> timeoutMs >> lockTempo)) {
            return false;
          }
          timeoutMs = std::clamp<long long>(timeoutMs, 100, 10000);
          midiClockTimeout = std::chrono::milliseconds(timeoutMs);
          midiFallbackLockTempo = (lockTempo != 0);
        } else if (tailToken == "SAMPLE_BANK") {
          int sampleSlot = -1;
          std::string samplePath;
          if (!(in >> sampleSlot >> std::quoted(samplePath))) {
            return false;
          }
          if (sampleSlot < 0 || sampleSlot >= static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
            return false;
          }
          std::filesystem::path resolvedSamplePath(samplePath);
          if (!resolvedSamplePath.is_absolute()) {
            resolvedSamplePath = moduleDirectory / resolvedSamplePath;
          }
          if (plugins.loadSampleToSlot(static_cast<std::uint16_t>(sampleSlot), resolvedSamplePath.string())) {
            plugins.setSampleNameForSlot(
                static_cast<std::uint16_t>(sampleSlot),
                std::filesystem::path(samplePath).stem().string());
          }
        } else if (tailToken == "SAMPLE_ENTRY") {
          int sampleSlot = -1;
          std::string sampleName;
          std::string samplePath;
          if (!(in >> sampleSlot >> std::quoted(sampleName) >> std::quoted(samplePath))) {
            return false;
          }
          if (sampleSlot < 0 || sampleSlot >= static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
            return false;
          }
          std::filesystem::path resolvedSamplePath(samplePath);
          if (!resolvedSamplePath.is_absolute()) {
            resolvedSamplePath = moduleDirectory / resolvedSamplePath;
          }
          if (plugins.loadSampleToSlot(static_cast<std::uint16_t>(sampleSlot), resolvedSamplePath.string())) {
            plugins.setSampleNameForSlot(static_cast<std::uint16_t>(sampleSlot), sampleName);
          }
        } else if (tailToken == "MODULE_MESSAGE") {
          std::string escapedMessage;
          if (!(in >> std::quoted(escapedMessage))) {
            return false;
          }
          module.setMessage(unescapeModuleMessage(escapedMessage));
        } else {
          bool handledRecordToken = false;
          if (!extracker::applyRecordFileToken(
                  in,
                  tailToken,
                  module.currentEditor().rows(),
                  recordState,
                  handledRecordToken)) {
            return false;
          }
        }
      }

      if (!songOrder.empty()) {
        module.setSongOrder(songOrder);
      }
      module.switchToPattern(std::min(fileCurrentPattern, module.patternCount() - 1));
      transport.setSwingPercent(module.currentPatternSwing());
      (void)fileCurrentSongPosition;
      return true;
    }

    module.reset(fileRows, fileChannels, 1);
    auto& editor = module.currentEditor();
    const bool isV2Format = isLegacyV2;

    for (std::size_t i = 0; i < editor.rows() * editor.channels(); ++i) {
      int row = 0;
      int channel = 0;
      int hasNote = 0;
      int note = -1;
      int instrument = 0;
      int sample = 0xFFFF;
      int gateTicks = 0;
      int velocity = 100;
      int retrigger = 0;
      int effectCommand = 0;
      int effectValue = 0;

      if (isV2Format) {
        in >> row >> channel >> hasNote >> note >> instrument >> sample >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue;
      } else {
        in >> row >> channel >> hasNote >> note >> instrument >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue;
      }
      if (!in) {
        return false;
      }

      if (hasNote != 0) {
        editor.insertNote(
            row,
            channel,
            note,
            static_cast<std::uint8_t>(std::clamp(instrument, 0, 255)),
            static_cast<std::uint32_t>(std::max(gateTicks, 0)),
            static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)),
            retrigger != 0,
            static_cast<std::uint8_t>(std::clamp(effectCommand, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(effectValue, 0, 255)));
        if (isV2Format && sample != 0xFFFF) {
          editor.setSample(row, channel, static_cast<std::uint16_t>(std::clamp(sample, 0, 65535)));
        }
      } else if (effectCommand != 0 || effectValue != 0) {
        editor.setEffect(
            row,
            channel,
            static_cast<std::uint8_t>(std::clamp(effectCommand, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(effectValue, 0, 255)));
      }
    }

    std::string tailToken;
    while (in >> tailToken) {
      if (tailToken == "PATTERN_SWING") {
        std::vector<std::uint8_t> parsedSwing;
        parsedSwing.reserve(module.patternCount());
        for (std::size_t patternIndex = 0; patternIndex < module.patternCount(); ++patternIndex) {
          int swingPercent = 50;
          if (!(in >> swingPercent)) {
            return false;
          }
          parsedSwing.push_back(static_cast<std::uint8_t>(std::clamp(swingPercent, 50, 75)));
        }
        for (std::size_t patternIndex = 0; patternIndex < parsedSwing.size(); ++patternIndex) {
          module.setPatternSwing(patternIndex, parsedSwing[patternIndex]);
        }
      } else if (tailToken == "INSERT_SWING_INHERIT") {
        int enabled = 0;
        if (!(in >> enabled)) {
          return false;
        }
        module.setInheritSwingOnInsert(enabled != 0);
      } else if (tailToken == "ROW_EDIT_SCOPE") {
        int allChannels = 0;
        if (!(in >> allChannels)) {
          return false;
        }
        module.setRowEditAllChannels(allChannels != 0);
      } else if (tailToken == "TRANSPORT") {
        std::string transportLine;
        std::getline(in, transportLine);
        std::istringstream transportParser(transportLine);
        double tempoBpm = transport.tempoBpm();
        int ticksPerBeat = static_cast<int>(transport.ticksPerBeat());
        int ticksPerRow = static_cast<int>(transport.ticksPerRow());
        if (transportParser >> tempoBpm >> ticksPerBeat) {
          if (tempoBpm > 0.0) {
            transport.setTempoBpm(tempoBpm);
          }
          if (ticksPerBeat > 0) {
            transport.setTicksPerBeat(static_cast<std::uint32_t>(ticksPerBeat));
          }
          if (transportParser >> ticksPerRow) {
            if (ticksPerRow > 0) {
              transport.setTicksPerRow(static_cast<std::uint32_t>(ticksPerRow));
            }
          }
        }
      } else if (tailToken == "MIDI_MAP") {
        for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
          int mapped = -1;
          if (!(in >> mapped)) {
            return false;
          }
          midiChannelMap[ch] = mapped;
        }
      } else if (tailToken == "MIDI_TRANSPORT") {
        long long timeoutMs = midiClockTimeout.count();
        int lockTempo = midiFallbackLockTempo ? 1 : 0;
        if (!(in >> timeoutMs >> lockTempo)) {
          return false;
        }
        timeoutMs = std::clamp<long long>(timeoutMs, 100, 10000);
        midiClockTimeout = std::chrono::milliseconds(timeoutMs);
        midiFallbackLockTempo = (lockTempo != 0);
      } else if (tailToken == "SAMPLE_BANK") {
        // Backward-compatible: old format had no name; use filename stem as name
        int sampleSlot = -1;
        std::string samplePath;
        if (!(in >> sampleSlot >> std::quoted(samplePath))) {
          return false;
        }
        if (sampleSlot < 0 || sampleSlot >= static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
          return false;
        }
        std::filesystem::path resolvedSamplePath(samplePath);
        if (!resolvedSamplePath.is_absolute()) {
          resolvedSamplePath = moduleDirectory / resolvedSamplePath;
        }
        if (plugins.loadSampleToSlot(static_cast<std::uint16_t>(sampleSlot), resolvedSamplePath.string())) {
          // Derive a default name from the filename stem
          plugins.setSampleNameForSlot(
              static_cast<std::uint16_t>(sampleSlot),
              std::filesystem::path(samplePath).stem().string());
        }
      } else if (tailToken == "SAMPLE_ENTRY") {
        int sampleSlot = -1;
        std::string sampleName;
        std::string samplePath;
        if (!(in >> sampleSlot >> std::quoted(sampleName) >> std::quoted(samplePath))) {
          return false;
        }
        if (sampleSlot < 0 || sampleSlot >= static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
          return false;
        }
        std::filesystem::path resolvedSamplePath(samplePath);
        if (!resolvedSamplePath.is_absolute()) {
          resolvedSamplePath = moduleDirectory / resolvedSamplePath;
        }
        if (plugins.loadSampleToSlot(static_cast<std::uint16_t>(sampleSlot), resolvedSamplePath.string())) {
          plugins.setSampleNameForSlot(static_cast<std::uint16_t>(sampleSlot), sampleName);
        }
      } else if (tailToken == "MODULE_MESSAGE") {
        std::string escapedMessage;
        if (!(in >> std::quoted(escapedMessage))) {
          return false;
        }
        module.setMessage(unescapeModuleMessage(escapedMessage));
      } else {
        bool handledRecordToken = false;
        if (!extracker::applyRecordFileToken(
                in,
                tailToken,
                editor.rows(),
                recordState,
                handledRecordToken)) {
          return false;
        }
      }
    }

    transport.setSwingPercent(module.currentPatternSwing());

    return true;
  };

  std::function<void(const extracker::MidiEvent&)> onMidiEventFn = onMidiEvent;
  std::function<bool()> midiClockAliveFn = midiClockAlive;
  std::function<const char*()> transportSourceFn = transportSource;
  std::function<std::string(const std::string&)> normalizeModulePathFn = normalizeModulePath;
  std::function<bool(const std::string&)> savePatternToFileFn = savePatternToFile;
  std::function<bool(const std::string&)> loadPatternFromFileFn = loadPatternFromFile;
  std::function<bool(const std::string&, std::string&)> readCommandOutputFn = readCommandOutput;
  std::function<std::vector<extracker::MidiPortEntry>(const std::string&)> parseAconnectPortsFn = parseAconnectPorts;
  std::function<std::string(std::string)> toLowerFn = toLower;
  std::function<bool(const std::string&, int&, int&)> parseHintEndpointFn = parseHintEndpoint;
  std::function<std::size_t()> songLengthFn = [&module]() {
    return module.songLength();
  };
  std::function<std::size_t(std::size_t)> songEntryAtFn = [&module](std::size_t index) {
    return module.songEntryAt(index);
  };
  std::function<bool()> midiInputRunningFn = [&midiInput]() {
    return midiInput.isRunning();
  };
  std::function<std::string()> midiLastErrorFn = [&midiInput]() {
    return midiInput.lastError();
  };
  std::function<std::string()> midiEndpointHintFn = [&midiInput]() {
    return midiInput.endpointHint();
  };
  std::function<int(const std::string&)> executeSystemCommandFn =
      [](const std::string& command) {
        return std::system(command.c_str());
      };

  std::thread sequencerThread([&]() {
    while (running.load()) {
      {
        std::lock_guard<std::mutex> lock(stateMutex);
        bool hadClock = hasMidiClockTimestamp;
        double fallbackBpm = midiClockEstimatedBpm;
        bool clockIsAlive = midiClockAlive();

        if (!clockIsAlive && hadClock && midiTransportSyncEnabled && midiTransportRunning) {
          midiTransportRunning = false;
          if (!transport.isPlaying()) {
            if (midiFallbackLockTempo && fallbackBpm > 0.0) {
              transport.setTempoBpm(fallbackBpm);
            }
            transport.play();
            std::cout << "[midi transport] clock timeout, switched to internal transport" << '\n';
          }
        }
      }

      if (transport.isPlaying() || midiTransportRunning) {
        std::lock_guard<std::mutex> lock(stateMutex);

        bool skipDispatchThisTick = false;
        if (playRangeActive) {
          int currentRow = static_cast<int>(transport.currentRow());
          if (currentRow < playRangeFrom || currentRow > playRangeTo) {
            if (loopEnabled) {
              transport.jumpToRow(static_cast<std::uint32_t>(playRangeFrom));
              currentRow = playRangeFrom;
            } else {
              transport.stop();
              playRangeActive = false;
            }
          }

          if (playRangeActive && playRangeStep > 1 && currentRow >= playRangeFrom && currentRow <= playRangeTo) {
            int delta = (currentRow - playRangeFrom) % playRangeStep;
            if (delta != 0) {
              int nextRow = currentRow + (playRangeStep - delta);
              if (nextRow <= playRangeTo) {
                transport.jumpToRow(static_cast<std::uint32_t>(nextRow));
              } else if (loopEnabled) {
                transport.jumpToRow(static_cast<std::uint32_t>(playRangeFrom));
              } else {
                transport.stop();
                playRangeActive = false;
              }
              skipDispatchThisTick = true;
            }
          }
        }

        if (!skipDispatchThisTick) {
          sequencer.update(module.currentEditor(), transport, audio, plugins);
        }

        if (!skipDispatchThisTick && songModeEnabled.load() && !playRangeActive) {
          bool patternWrapped = sequencer.consumePatternWrapEvent();
          if (patternWrapped && module.songLength() > 0) {
            std::size_t currentPos = std::min(songPlaybackPosition.load(), module.songLength() - 1);
            std::size_t nextPos = currentPos + 1;
            if (nextPos >= module.songLength()) {
              if (!loopEnabled) {
                transport.stop();
                nextPos = module.songLength() - 1;
              } else {
                nextPos = 0;
              }
            }
            songPlaybackPosition.store(nextPos);
            module.switchToPattern(module.songEntryAt(nextPos));
            transport.setSwingPercent(module.currentPatternSwing());
            sequencer.reset();
            audio.allNotesOff();
            transport.resetTickCount();
          }
        }

        if (playRangeActive) {
          int currentRow = static_cast<int>(transport.currentRow());
          if (currentRow < playRangeFrom || currentRow > playRangeTo) {
            if (loopEnabled) {
              transport.jumpToRow(static_cast<std::uint32_t>(playRangeFrom));
            } else {
              transport.stop();
              playRangeActive = false;
            }
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
  });

  std::cout << "Commands: help (h), play (p), stop (s), tempo <bpm> (bpm <value>), loop <on|off|clear|range>, status [--json [--minimal] [--pretty]] (st), reset, save <file> (w), load <file> (r), message <set|get> <text>, quit (q)" << '\n';
  std::cout << "Plugin commands: plugin list, plugin load <id>, plugin assign <instrument> <id>, sample <load|unload|rename|play|stop|list|status> ..., sine <instrument>" << '\n';
  std::cout << "Pattern commands: note set <row> <ch> <midi> <instr> [vel] [fx] [fxval], note set dry <row> <ch> <midi> <instr> [vel] [fx] [fxval], note off <row> <ch> <fadeout_ticks>, note off dry <row> <ch> <fadeout_ticks>, note clear <row> <ch>, note clear dry <row> <ch>, note vel <row> <ch> <vel>, note vel dry <row> <ch> <vel>, note gate <row> <ch> <ticks>, note gate dry <row> <ch> <ticks>, note fx <row> <ch> <fx> <fxval>, note fx dry <row> <ch> <fx> <fxval>, pattern print [from] [to], pattern display [from] [to], pattern watch [update_interval_ms], pattern play [from] [to] [step <n>], pattern template <blank|house|electro>, pattern transpose [dry [preview [verbose]]] <semitones> [from] [to] [ch] [step <n>] [chance <p>], pattern velocity [dry [preview [verbose]]] <percent> [from] [to] [ch] [step <n>] [chance <p>], pattern gate [dry [preview [verbose]]] <percent> [from] [to] [ch] [step <n>] [chance <p>], pattern effect [dry [preview [verbose]]] <fx> <fxval> [from] [to] [ch] [step <n>] [chance <p>], pattern copy <from> <to> [chFrom] [chTo] [step <n>], pattern paste [dry [preview [verbose]]] <destRow> [channelOffset] [step <n>], pattern humanize [dry [preview [verbose]]] <velRange> <gateRangePercent> <seed> [from] [to] [ch] [step <n>], pattern randomize [dry [preview [verbose]]] <probabilityPercent> <seed> [from] [to] [ch] [step <n>], pattern scale-duration [dry [preview [verbose]]] <percent> [from] [to] [ch] [step <n>] [chance <p>], pattern invert-notes [dry [preview [verbose]]] [centerNote] [from] [to] [ch] [step <n>], pattern filter-notes [dry [preview [verbose]]] <minNote> <maxNote> [minVel] [maxVel] [from] [to] [ch] [delete], pattern undo, pattern redo" << '\n';
  std::cout << "Song commands: pattern duplicate [index] (pattern dup), pattern switch <index> (pattern sw), pattern list (pattern ls), pattern insert <before|after> (pattern in), pattern insert-swing <on|off|status>, pattern remove (pattern del), song status (song st), song list (song ls), song pos (song p, song gp), song set <entry> <pattern> (song se), song insert <entry> <pattern> (song si), song append <pattern> (song ap), song remove <entry> (song rm), song move <entry> <up|down> (song mv), song goto <entry> (song g), song first (song f), song last (song l), song next [wrap] (song n), song prev [wrap] (song b), song play <pattern|song|status> (song pl)" << '\n';
  std::cout << "Record commands: record on [channel] (rec [channel]), record off, record channel <index|status>, record cursor <row|+delta|-delta|start|end|next|prev|status>, record note <midi> [instr] [vel] [fx] [fxval], record note <midi> vel <vel> [fx] [fxval], record note <midi> fx <fx> <fxval>, record note <midi> instr <i> [vel <v>] [fx <f> <fv>], record note dry <midi> ..., record quantize <on|off|status>, record overdub <on|off|status>, record jump <ticks|ratio|status>, record undo, record redo" << '\n';
  std::cout << "MIDI commands: midi on, midi off, midi status, midi quick [all|compact], midi thru <on|off>, midi instrument <index>, midi learn <on|off|status>, midi map <ch> <instr|clear>, midi map <status|clear all>, midi transport <on|off|toggle|status|timeout|lock|reset>, midi clock <help|quick|sources|autoconnect|diagnose>" << '\n';
  std::cout << "exTracker> " << std::flush;

  extracker::CommandRegistry commandRegistry;

  auto makeCoreContext = [&]() -> extracker::CoreCommandContext {
    return extracker::CoreCommandContext{transport,
                                         module.currentEditor(),
                                         stateMutex,
                                         loopEnabled,
                                         playRangeFrom,
                                         playRangeTo,
                                         playRangeActive,
                                         recordEnabled,
                                         recordQuantizeEnabled,
                                         recordOverdubEnabled,
                                         recordInsertJump,
                                         recordCanUndo,
                                         recordCanRedo,
                                         recordChannel,
                                         recordCursorRow,
                                         midiInput,
                                         midiThruEnabled,
                                         midiInstrument,
                                         midiLearnEnabled,
                                         midiTransportSyncEnabled,
                                         midiTransportRunning,
                                         midiClockTimeout,
                                         midiFallbackLockTempo,
                                         hasMidiClockTimestamp,
                                         midiClockEstimatedBpm,
                                         midiChannelMap,
                                         sequencer,
                                         plugins,
                                         audio,
                                         module,
                                         midiClockAliveFn,
                                         transportSourceFn,
                                         songLengthFn,
                                         songEntryAtFn,
                                         &songModeEnabled,
                                         &songPlaybackPosition,
                                         normalizeModulePathFn,
                                         savePatternToFileFn,
                                         loadPatternFromFileFn};
  };

  auto makeRecordContext = [&]() -> extracker::RecordCommandContext {
    return extracker::RecordCommandContext{module.currentEditor(),
                                           transport,
                                           stateMutex,
                                           recordState,
                                           recordChannel,
                                           recordCursorRow,
                                           recordEnabled,
                                           recordQuantizeEnabled,
                                           recordOverdubEnabled,
                                           recordInsertJump,
                                           midiInstrument,
                                           chooseRecordRowFn,
                                           applyRecordWriteFn};
  };

  extracker::MidiCommandContext midiContext{midiInput,
                                 onMidiEventFn,
                                 midiThruEnabled,
                                 midiInstrument,
                                 midiLearnEnabled,
                                 midiChannelMap,
                                 midiTransportSyncEnabled,
                                 midiTransportRunning,
                                 stateMutex,
                                 midiClockAliveFn,
                                 midiClockTimeout,
                                 midiFallbackLockTempo,
                                 transportSourceFn,
                                 hasMidiClockTimestamp,
                                 lastMidiClockTimestamp,
                                 midiClockEstimatedBpm,
                                 readCommandOutputFn,
                                 parseAconnectPortsFn,
                                 toLowerFn,
                                 parseHintEndpointFn,
                                 midiInputRunningFn,
                                 midiLastErrorFn,
                                   midiEndpointHintFn,
                                   executeSystemCommandFn};

  auto makePatternContext = [&]() -> extracker::PatternCommandContext {
    return extracker::PatternCommandContext{module.currentEditor(),
                                            stateMutex,
                                            transport,
                                            sequencer,
                                            audio,
                                            playRangeFrom,
                                            playRangeTo,
                                            playRangeStep,
                                            playRangeActive,
                                            loopEnabled,
                                            recordCanUndo,
                                            recordCanRedo,
                                            recordCursorRow};
  };

  extracker::CommandBindings commandBindings = extracker::createDefaultCommandBindings(
      extracker::DefaultCommandBindingCallbacks{
          [](std::istringstream&) {
            extracker::handleHelpCommand();
          },
          [&](std::istringstream& input) {
            extracker::handlePluginCommand(plugins, input);
          },
          [&](std::istringstream& input) {
            extracker::handleSampleCommand(plugins, input);
          },
          [&](std::istringstream& input) {
            extracker::handleSineCommand(plugins, input);
          },
          [&](std::istringstream& input) {
            extracker::handleNoteCommand(module.currentEditor(), stateMutex, input);
          },
          [&](std::istringstream& input) {
            extracker::handlePatternCommand(makePatternContext(), input);
          },
          [&](std::istringstream& input) {
            extracker::handleRecordCommand(input, makeRecordContext());
          },
          [&](std::istringstream& input) {
            extracker::handleMidiCommand(input, midiContext);
          },
          [&](const std::string& command, std::istringstream& input) {
            extracker::handleCoreCommand(command, input, makeCoreContext());
          }});
  extracker::registerCommandHandlers(commandRegistry, commandBindings);

  commandRegistry["fx"] = [](std::istringstream& input) {
    std::string sub;
    input >> sub;
    if (sub != "list") {
      std::cout << "Usage: fx list\n";
      return;
    }
    struct FxEntry {
      const char* code;
      const char* name;
      const char* value;
      bool implemented;
    };
    static const FxEntry fx[] = {
      { "00", "Arpeggio",          "xxyy  (x=+semitone1, y=+semitone2)",    true  },
      { "01", "Slide Up",          "speed",                                  true  },
      { "02", "Slide Down",        "speed",                                  true  },
      { "03", "Tone Portamento",   "speed",                                  true  },
      { "04", "Vibrato",           "xxyy  (x=speed, y=depth)",               true  },
      { "05", "Vol Slide+Porta",   "xxyy  (x=up, y=down)",                   true  },
      { "06", "Vol Slide+Vibrato", "xxyy  (x=up, y=down)",                   true  },
      { "07", "Tremolo",           "xxyy  (x=speed, y=depth)",               true  },
      { "08", "Pan",               "00..FF  (internal synth stereo pan)",      true  },
      { "09", "Retrigger",         "ticks",                                   true  },
      { "0A", "Volume Slide",      "xxyy  (x=up nibble, y=down nibble)",      true  },
      { "0B", "Pattern Jump",      "row",                                     true  },
      { "0C", "Set Volume",        "00..7F",                                  true  },
      { "0D", "Pattern Break",     "row",                                     true  },
      { "0E", "Extended",          "subcommand (E0/E1/E2/E3/E4/E5/E6/E7/E8/E9/EA/EB/EC/ED/EE/EF)", true  },
      { "0F", "Speed/Tempo",       "<32 = set TPR, >=32 = set BPM",           true  },
      { "17", "Set TPB",           "ticks-per-beat (1..255)",                 true  },
    };
    std::cout << "FX  Name                Value                              OK\n";
    std::cout << "--- ------------------- ---------------------------------- ---\n";
    std::cout << "Input formats: CCVV (1706), CCV+Enter (170), edit existing FX value with VV in GUI FX mode\n";
    for (const auto& e : fx) {
      char line[128];
      std::snprintf(line, sizeof(line), "%-3s %-19s %-34s %s\n",
        e.code, e.name, e.value, e.implemented ? "yes" : "no");
      std::cout << line;
    }
  };

  auto trim = [](const std::string& text) -> std::string {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
      return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
  };

  auto splitChainedCommands = [&](const std::string& rawLine) {
    std::vector<std::string> commands;
    std::string current;
    for (char ch : rawLine) {
      if (ch == ';') {
        std::string candidate = trim(current);
        if (!candidate.empty()) {
          commands.push_back(std::move(candidate));
        }
        current.clear();
      } else {
        current.push_back(ch);
      }
    }
    std::string candidate = trim(current);
    if (!candidate.empty()) {
      commands.push_back(std::move(candidate));
    }
    return commands;
  };

  bool shouldExit = false;
  auto executeCommandLine = [&](const std::string& commandLine) {
    std::istringstream input(commandLine);
    std::string command;
    input >> command;

    if (command == "quit" || command == "exit" || command == "q") {
      shouldExit = true;
      return;
    }

    if (command == "pattern" || command == "song") {
      // Handle both pattern management and pattern commands.
      std::vector<std::string> tokens;
      std::string token;
      while (input >> token) {
        tokens.push_back(token);
      }

      if (command == "song") {
        extracker::ModuleCommandContext moduleContext{module, &songModeEnabled, &songPlaybackPosition};
        extracker::handleModuleCommand(command, tokens, moduleContext);
      } else {
        // Check if this is a module pattern-management command (list, switch, insert, remove).
        if (!tokens.empty() && (tokens[0] == "list" || tokens[0] == "status" ||
                                tokens[0] == "switch" || tokens[0] == "insert" ||
                                tokens[0] == "remove" || tokens[0] == "duplicate" ||
                                tokens[0] == "dup" || tokens[0] == "sw" || tokens[0] == "del" || tokens[0] == "ls" || tokens[0] == "in")) {
          extracker::ModuleCommandContext moduleContext{module, &songModeEnabled, &songPlaybackPosition};
          extracker::handleModuleCommand(command, tokens, moduleContext);
        } else {
          // Regular pattern commands.
          std::istringstream patternInput(commandLine);
          std::string dummy;
          patternInput >> dummy;  // consume "pattern"
          extracker::handlePatternCommand(makePatternContext(), patternInput);
        }
      }
      return;
    }

    if (command.empty()) {
      return;
    }

    auto commandIt = commandRegistry.find(command);
    if (commandIt != commandRegistry.end()) {
      commandIt->second(input);
    } else {
      std::cout << "Unknown command: " << command << '\n';
    }
  };

  std::string line;
  while (std::getline(std::cin, line)) {
    const auto chainedCommands = splitChainedCommands(line);
    for (const auto& commandLine : chainedCommands) {
      executeCommandLine(commandLine);
      if (shouldExit) {
        break;
      }
    }
    if (shouldExit) {
      break;
    }
    std::cout << "exTracker> " << std::flush;
  }

  running.store(false);
  if (sequencerThread.joinable()) {
    sequencerThread.join();
  }

  if (transport.isPlaying()) {
    transport.stop();
  }
  midiInput.stop();

  std::cout << "Final status" << '\n';
  std::cout << "Transport ticks: " << transport.tickCount() << '\n';
  std::cout << "Transport row advances: " << transport.rowAdvanceCount() << '\n';
  std::cout << "Transport current row: " << transport.currentRow() << '\n';
  std::cout << "Sequencer dispatch count: " << sequencer.dispatchCount() << '\n';
  std::cout << "Sequencer active voices: " << sequencer.activeVoiceCount() << '\n';
  std::cout << "Audio active test voices: " << audio.testToneVoiceCount() << '\n';
  std::cout << "Audio first test tone Hz: " << audio.testToneFrequencyHz() << '\n';
  std::cout << "Instrument 0 plugin: " << plugins.pluginForInstrument(0) << '\n';
  std::cout << "Instrument 1 plugin: " << plugins.pluginForInstrument(1) << '\n';
  std::cout << "Plugin note-on events: " << plugins.noteOnEventCount() << '\n';
  std::cout << "Plugin note-off events: " << plugins.noteOffEventCount() << '\n';
  std::cout << plugins.status() << '\n';

  audio.stop();

  return 0;
}
