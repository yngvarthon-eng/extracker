#include <iostream>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
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
#include "extracker/note_cli.hpp"
#include "extracker/pattern_cli.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_cli.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/record_cli.hpp"
#include "extracker/record_workflow.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

int main() {
  extracker::AudioEngine audio;
  extracker::PatternEditor editor;
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
    int row = extracker::chooseRecordRow(editor, transport, recordState);
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
        editor,
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
  if (!plugins.assignInstrument(0, "builtin.sine")) {
    std::cout << "Failed to assign builtin.sine to instrument 0" << '\n';
  }
  if (!plugins.assignInstrument(1, "builtin.square")) {
    std::cout << "Failed to assign builtin.square to instrument 1" << '\n';
  }
  audio.setPluginHost(&plugins);
  editor.insertNote(0, 0, 48, 0, 0, 120, true);
  editor.insertNote(0, 1, 55, 1, 0, 96, false);
  editor.insertNote(1, 0, 52, 0, 0, 80, false);
  editor.insertNote(1, 1, 59, 1, 0, 70, true);
  editor.insertNote(2, 0, 55, 0, 0, 100, false);
  editor.insertNote(4, 1, 55, 1, 0, 127, true);
  transport.setTempoBpm(125.0);
  transport.setTicksPerBeat(6);
  transport.setTicksPerRow(1);
  transport.setPatternRows(static_cast<std::uint32_t>(editor.rows()));
  transport.resetTickCount();

  auto savePatternToFile = [&](const std::string& path) -> bool {
    std::ofstream out(path);
    if (!out) {
      return false;
    }

    out << "EXTRACKER_MODULE_V1 " << editor.rows() << " " << editor.channels() << "\n";
    for (std::size_t row = 0; row < editor.rows(); ++row) {
      for (std::size_t channel = 0; channel < editor.channels(); ++channel) {
        int iRow = static_cast<int>(row);
        int iChannel = static_cast<int>(channel);
        out << row << " " << channel << " "
            << (editor.hasNoteAt(iRow, iChannel) ? 1 : 0) << " "
            << editor.noteAt(iRow, iChannel) << " "
            << static_cast<int>(editor.instrumentAt(iRow, iChannel)) << " "
            << editor.gateTicksAt(iRow, iChannel) << " "
            << static_cast<int>(editor.velocityAt(iRow, iChannel)) << " "
            << (editor.retriggerAt(iRow, iChannel) ? 1 : 0) << " "
            << static_cast<int>(editor.effectCommandAt(iRow, iChannel)) << " "
            << static_cast<int>(editor.effectValueAt(iRow, iChannel)) << "\n";
      }
    }

    out << "MIDI_MAP";
    for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
      out << " " << midiChannelMap[ch];
    }
    out << "\n";

    out << "MIDI_TRANSPORT " << midiClockTimeout.count() << " " << (midiFallbackLockTempo ? 1 : 0) << "\n";
    extracker::writeRecordState(out, recordState);

    return true;
  };

  auto loadPatternFromFile = [&](const std::string& path) -> bool {
    std::ifstream in(path);
    if (!in) {
      return false;
    }

    std::string magic;
    std::size_t fileRows = 0;
    std::size_t fileChannels = 0;
    in >> magic >> fileRows >> fileChannels;
    if (!in || (magic != "EXTRACKER_PATTERN_V1" && magic != "EXTRACKER_MODULE_V1") ||
        fileRows != editor.rows() || fileChannels != editor.channels()) {
      return false;
    }

    for (std::size_t row = 0; row < editor.rows(); ++row) {
      for (std::size_t channel = 0; channel < editor.channels(); ++channel) {
        editor.clearStep(static_cast<int>(row), static_cast<int>(channel));
      }
    }

    for (std::size_t i = 0; i < editor.rows() * editor.channels(); ++i) {
      int row = 0;
      int channel = 0;
      int hasNote = 0;
      int note = -1;
      int instrument = 0;
      int gateTicks = 0;
      int velocity = 100;
      int retrigger = 0;
      int effectCommand = 0;
      int effectValue = 0;

      in >> row >> channel >> hasNote >> note >> instrument >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue;
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
      if (tailToken == "MIDI_MAP") {
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
        sequencer.update(editor, transport, audio, plugins);

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

  std::cout << "Commands: help, play, stop, tempo <bpm>, loop <on|off|range>, status, reset, save <file>, load <file>, quit" << '\n';
  std::cout << "Plugin commands: plugin list, plugin load <id>, plugin assign <instrument> <id>, sine <instrument>" << '\n';
  std::cout << "Pattern commands: note set <row> <ch> <midi> <instr> [vel] [fx] [fxval], note set dry <row> <ch> <midi> <instr> [vel] [fx] [fxval], note clear <row> <ch>, note clear dry <row> <ch>, note vel <row> <ch> <vel>, note vel dry <row> <ch> <vel>, note gate <row> <ch> <ticks>, note gate dry <row> <ch> <ticks>, note fx <row> <ch> <fx> <fxval>, note fx dry <row> <ch> <fx> <fxval>, pattern print [from] [to], pattern play [from] [to], pattern template <blank|house|electro>, pattern transpose [dry [preview [verbose]]] <semitones> [from] [to] [ch] [step <n>], pattern velocity [dry [preview [verbose]]] <percent> [from] [to] [ch] [step <n>], pattern gate [dry [preview [verbose]]] <percent> [from] [to] [ch] [step <n>], pattern effect [dry [preview [verbose]]] <fx> <fxval> [from] [to] [ch] [step <n>], pattern copy <from> <to> [chFrom] [chTo], pattern paste [dry [preview [verbose]]] <destRow> [channelOffset], pattern humanize [dry [preview [verbose]]] <velRange> <gateRangePercent> <seed> [from] [to] [ch] [step <n>], pattern randomize [dry [preview [verbose]]] <probabilityPercent> <seed> [from] [to] [ch] [step <n>], pattern undo, pattern redo" << '\n';
  std::cout << "Record commands: record on [channel], record off, record channel <index|status>, record cursor <row|+delta|-delta|start|end|next|prev|status>, record note <midi> [instr] [vel] [fx] [fxval], record note <midi> vel <vel> [fx] [fxval], record note <midi> fx <fx> <fxval>, record note <midi> instr <i> [vel <v>] [fx <f> <fv>], record note dry <midi> ..., record quantize <on|off|status>, record overdub <on|off|status>, record jump <ticks|ratio|status>, record undo, record redo" << '\n';
  std::cout << "MIDI commands: midi on, midi off, midi status, midi quick [all|compact], midi thru <on|off>, midi instrument <index>, midi learn <on|off|status>, midi map <ch> <instr|clear>, midi map <status|clear all>, midi transport <on|off|toggle|status|timeout|lock|reset>, midi clock <help|quick|sources|autoconnect|diagnose>" << '\n';
  std::cout << "exTracker> " << std::flush;

  extracker::CommandRegistry commandRegistry;

  extracker::CoreCommandContext coreContext{transport,
                                            editor,
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
                                            midiClockAliveFn,
                                            transportSourceFn,
                                            normalizeModulePathFn,
                                            savePatternToFileFn,
                                            loadPatternFromFileFn};

  extracker::RecordCommandContext recordContext{editor,
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

  extracker::PatternCommandContext patternContext{editor,
                                                   stateMutex,
                                                   transport,
                                                   sequencer,
                                                   audio,
                                                   playRangeFrom,
                                                   playRangeTo,
                                                   playRangeActive,
                                                   loopEnabled,
                                                   recordCanUndo,
                                                   recordCanRedo,
                                                   recordCursorRow};

  extracker::CommandBindings commandBindings = extracker::createDefaultCommandBindings(
      extracker::DefaultCommandBindingCallbacks{
          [](std::istringstream&) {
            extracker::handleHelpCommand();
          },
          [&](std::istringstream& input) {
            extracker::handlePluginCommand(plugins, input);
          },
          [&](std::istringstream& input) {
            extracker::handleSineCommand(plugins, input);
          },
          [&](std::istringstream& input) {
            extracker::handleNoteCommand(editor, stateMutex, input);
          },
          [&](std::istringstream& input) {
            extracker::handlePatternCommand(patternContext, input);
          },
          [&](std::istringstream& input) {
            extracker::handleRecordCommand(input, recordContext);
          },
          [&](std::istringstream& input) {
            extracker::handleMidiCommand(input, midiContext);
          },
          [&](const std::string& command, std::istringstream& input) {
            extracker::handleCoreCommand(command, input, coreContext);
          }});
  extracker::registerCommandHandlers(commandRegistry, commandBindings);

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string command;
    input >> command;

    if (command == "quit" || command == "exit") {
      break;
    } else if (!command.empty()) {
      auto commandIt = commandRegistry.find(command);
      if (commandIt != commandRegistry.end()) {
        commandIt->second(input);
      } else {
        std::cout << "Unknown command: " << command << '\n';
      }
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
