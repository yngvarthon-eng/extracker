#include "app.h"
#include "main_window.h"
#include "extracker/cli_parse_utils.hpp"
#include "extracker/song_bundle.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace {

int midiEditorActionIndex(ExTrackerApp::MidiEditorAction action) {
  switch (action) {
    case ExTrackerApp::MidiEditorAction::Velocity:
      return 0;
    case ExTrackerApp::MidiEditorAction::Gate:
      return 1;
    case ExTrackerApp::MidiEditorAction::EffectCommand:
      return 2;
    case ExTrackerApp::MidiEditorAction::EffectValue:
      return 3;
    case ExTrackerApp::MidiEditorAction::None:
    default:
      return -1;
  }
}

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

juce::File recoveryStateDirectory() {
  auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("extracker")
                  .getChildFile("recovery");
  base.createDirectory();
  return base;
}

juce::File recoverySnapshotFile() {
  return recoveryStateDirectory().getChildFile("autosave_snapshot.xtp");
}

juce::File recoverySessionMarkerFile() {
  return recoveryStateDirectory().getChildFile("session_active.flag");
}

void writeRecoverySessionMarker() {
  const juce::File marker = recoverySessionMarkerFile();
  marker.replaceWithText("started=" + juce::Time::getCurrentTime().toString(true, true) + "\n");
}

void clearRecoverySessionMarker() {
  recoverySessionMarkerFile().deleteFile();
}

}  // namespace

ExTrackerApp::ExTrackerApp() {
  midiChannelMap.fill(-1);
  for (auto& mapping : midiEditorCcMappings) {
    mapping.store(-1);
  }
  for (auto& pendingValue : midiEditorPendingValues) {
    pendingValue.store(-1);
  }
  channelInstruments.resize(module.currentEditor().channels(), 0);
  channelMuted.resize(module.currentEditor().channels(), false);
  currentPatternCache.store(module.currentPattern());
  patternCountCache.store(module.patternCount());
  currentSongOrderPositionCache.store(module.firstSongEntryForPattern(module.currentPattern()));
}

ExTrackerApp::~ExTrackerApp() = default;

void ExTrackerApp::initialise(const juce::String& commandLine) {
  juce::ignoreUnused(commandLine);
  const juce::File recoverySnapshot = recoverySnapshotFile();
  const bool hadStaleSession = recoverySessionMarkerFile().existsAsFile();
  const bool hasRecoverySnapshot = recoverySnapshot.existsAsFile();
  audio.setPluginHost(&plugins);
  currentPatternCache.store(module.currentPattern());
  patternCountCache.store(module.patternCount());
  currentSongOrderPositionCache.store(module.firstSongEntryForPattern(module.currentPattern()));

  // Scan external plugins before starting audio.  fork() is unsafe after
  // PipeWire/ALSA threads are running (PipeWire is not fork-safe), so we
  // probe VST3/LV2 bundles here while the process is still single-threaded.
  {
    const std::size_t found = plugins.rescanExternalPlugins();
    if (found > 0) {
      std::cout << "Scan found " << found << " plugin(s)" << std::endl;
    }
  }

  // Initialize audio engine
  if (!audio.start()) {
    std::cerr << "Failed to start audio engine" << std::endl;
    quit();
    return;
  }
  std::cout << "Audio started using backend: " << audio.backendName() << std::endl;

  // Load default plugins
  if (!plugins.loadPlugin("builtin.sine")) {
    std::cerr << "Failed to load builtin.sine plugin" << std::endl;
  }
  if (!plugins.loadPlugin("builtin.square")) {
    std::cerr << "Failed to load builtin.square plugin" << std::endl;
  }

  if (!plugins.assignInstrument(0, "builtin.sine")) {
    std::cerr << "Failed to assign builtin.sine to instrument 0" << std::endl;
  }
  if (!plugins.assignInstrument(1, "builtin.square")) {
    std::cerr << "Failed to assign builtin.square to instrument 1" << std::endl;
  }

  auto onMidiEvent = [this](const extracker::MidiEvent& event) {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (event.type == extracker::MidiEvent::Type::ControlChange) {
      const int learnTarget = midiEditorLearnTarget.exchange(0);
      const int ccCode = static_cast<int>(event.channel) * 128 + static_cast<int>(event.controller);
      if (learnTarget >= static_cast<int>(MidiEditorAction::Velocity) &&
          learnTarget <= static_cast<int>(MidiEditorAction::EffectValue)) {
        const int idx = learnTarget - 1;
        midiEditorCcMappings[static_cast<std::size_t>(idx)].store(ccCode);
      }

      for (std::size_t idx = 0; idx < midiEditorCcMappings.size(); ++idx) {
        if (midiEditorCcMappings[idx].load() == ccCode) {
          midiEditorPendingValues[idx].store(static_cast<int>(event.value));
        }
      }
      return;
    }

    int targetInstrument = midiInstrument;
    if (event.channel < midiChannelMap.size() && midiChannelMap[event.channel] >= 0) {
      targetInstrument = midiChannelMap[event.channel];
    } else if (midiLearnEnabled && event.type == extracker::MidiEvent::Type::NoteOn) {
      targetInstrument = static_cast<int>(event.channel) % static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots);
      if (event.channel < midiChannelMap.size()) {
        midiChannelMap[event.channel] = targetInstrument;
      }
    }

    if (!midiThruEnabled) {
      return;
    }

    if (event.type == extracker::MidiEvent::Type::NoteOn) {
      if (!plugins.triggerNoteOn(static_cast<std::uint8_t>(targetInstrument), event.note, event.velocity, true)) {
        audio.noteOn(event.note,
                     440.0 * std::pow(2.0, static_cast<double>(static_cast<int>(event.note) - 69) / 12.0),
                     static_cast<double>(event.velocity) / 127.0,
                     true,
                     static_cast<std::uint8_t>(targetInstrument));
      }
      const bool punchOk = !recordState.punchEnabled ||
          (transport.isPlaying() &&
           static_cast<int>(transport.currentRow()) >= recordState.punchIn &&
           static_cast<int>(transport.currentRow()) <= recordState.punchOut);
      if (recordState.enabled && punchOk) {
        const int savedChannel = recordState.channel;
        recordState.channel = targetInstrument;
        int row;
        if (transport.isPlaying()) {
          row = extracker::chooseRecordRow(module.currentEditor(), transport, recordState);
        } else {
          row = std::clamp(recordState.cursorRow, 0, static_cast<int>(module.currentEditor().rows()) - 1);
          // Advance cursor now so overlapping chords land on different rows
          recordState.cursorRow = (row + std::max(recordState.insertJump, 1)) % static_cast<int>(module.currentEditor().rows());
        }
        recordState.channel = savedChannel;
        if (row >= 0) {
          pendingRecordNotes[static_cast<int>(event.note)] = {
              row, targetInstrument, static_cast<std::uint8_t>(targetInstrument),
              event.velocity, std::chrono::steady_clock::now()};
        }
      }
    } else if (event.type == extracker::MidiEvent::Type::NoteOff) {
      if (!plugins.triggerNoteOff(static_cast<std::uint8_t>(targetInstrument), event.note)) {
        audio.noteOff(event.note, static_cast<std::uint8_t>(targetInstrument));
      }
      if (recordState.enabled) {
        auto it = pendingRecordNotes.find(static_cast<int>(event.note));
        if (it != pendingRecordNotes.end()) {
          const auto& p = it->second;
          const auto heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - p.startTime).count();
          const double bpm = transport.tempoBpm();
          const std::uint32_t tpr = transport.ticksPerRow();
          const std::uint32_t gate = static_cast<std::uint32_t>(
              std::clamp(static_cast<double>(heldMs) * bpm * static_cast<double>(tpr) / 60000.0,
                         1.0, static_cast<double>(tpr * 8)));
          // Write directly — cursor was already advanced on NoteOn
          module.currentEditor().insertNote(p.row, p.channel, static_cast<int>(event.note),
                                            p.instrument, gate, p.velocity, false, 0, 0);
          module.currentEditor().setSample(p.row, p.channel, 0xFFFF);
          pendingRecordNotes.erase(it);
          recordDirty.store(true, std::memory_order_relaxed);
        }
      }
    }
  };

  if (!midiInput.start(onMidiEvent)) {
    std::cerr << "MIDI input not started: " << midiInput.lastError() << std::endl;
  } else {
    std::cout << "MIDI input started (" << midiInput.backendName() << ")" << std::endl;
    std::cout << midiInput.endpointHint() << std::endl;
  }

  // Configure transport
  transport.setTempoBpm(125.0);
  transport.setTicksPerBeat(6);
  transport.setTicksPerRow(1);
  transport.setPatternRows(static_cast<std::uint32_t>(module.currentEditor().rows()));
  transport.setSwingPercent(module.currentPatternSwing());
  transport.resetTickCount();

  // Start sequencer thread
  sequencerThread = std::thread([this]() {
    auto lastRecoverySnapshot = std::chrono::steady_clock::now();
    constexpr auto kRecoverySnapshotInterval = std::chrono::seconds(5);
    while (running.load()) {
      const auto now = std::chrono::steady_clock::now();
      if (now - lastRecoverySnapshot >= kRecoverySnapshotInterval) {
        lastRecoverySnapshot = now;
        // Skip snapshot while playing: the file write holds stateMutex long enough
        // to block the sequencer poll and drop rows at faster tempos.
        if (!transport.isPlaying() && !midiTransportRunning.load()) {
          const juce::String snapshotPath = recoverySnapshotFile().getFullPathName();
          if (savePatternToFile(snapshotPath.toStdString())) {
            lastRecoverySnapshotEpochMs.store(static_cast<std::uint64_t>(juce::Time::currentTimeMillis()));
          }
        }
      }

      if (!transport.isPlaying() && !midiTransportRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        continue;
      }

      extracker::PatternEditor patternSnapshot;
      std::vector<bool> mutedChannelsSnapshot;
      bool shouldNotifyPatternChanged = false;
      juce::Component::SafePointer<MainWindow> safeMainWindow;

      std::unique_lock<std::mutex> lock(stateMutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }

      if (transport.isPlaying() || midiTransportRunning.load()) {
        transport.setSwingPercent(module.currentPatternSwing());
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
        } else if (playMode == PlayMode::PLAY_SONG) {
          // Detect pattern completion using the transport's monotonic rowAdvanceCount.
          // This is more reliable than the old currentRow < lastSongModeRow heuristic,
          // which could be defeated by effect-triggered row jumps or a GUI-thread write
          // to lastSongModeRow racing with the wrap detection window.
          const std::uint64_t advanceCount = transport.rowAdvanceCount();
          const std::uint64_t patternRows  = transport.patternRows();
          if (patternRows > 0 &&
              advanceCount >= songModePatternAdvanceBaseline + patternRows) {
            // One full pattern cycle has elapsed: advance song or stop.
            std::size_t nextSongPos = currentSongOrderPositionCache.load() + 1;
            if (nextSongPos < module.songLength()) {
              currentSongOrderPositionCache.store(nextSongPos);
              module.switchToPattern(module.songEntryAt(nextSongPos));
              transport.setPatternRows(static_cast<std::uint32_t>(module.currentEditor().rows()));
              transport.setSwingPercent(module.currentPatternSwing());
              sequencer.reset();
              plugins.allNotesOff();
              audio.allNotesOff();
              transport.resetTickCount();
              songModePatternAdvanceBaseline = 0;  // rowAdvanceCount reset to 0
              currentPatternCache.store(module.currentPattern());
              patternCountCache.store(module.patternCount());
              safeMainWindow = juce::Component::SafePointer<MainWindow>(mainWindow.get());
              shouldNotifyPatternChanged = true;
            } else {
              // End of song: stop and reset to pattern 0 so the next play
              // restarts from the beginning rather than replaying the last pattern.
              transport.stop();
              plugins.allNotesOff();
              audio.allNotesOff();
              sequencer.reset();
              if (module.songLength() > 0) {
                module.switchToPattern(module.songEntryAt(0));
                transport.setPatternRows(static_cast<std::uint32_t>(module.currentEditor().rows()));
                transport.setSwingPercent(module.currentPatternSwing());
              }
              currentSongOrderPositionCache.store(0);
              transport.resetTickCount();
              songModePatternAdvanceBaseline = 0;
              currentPatternCache.store(module.currentPattern());
              patternCountCache.store(module.patternCount());
              safeMainWindow = juce::Component::SafePointer<MainWindow>(mainWindow.get());
              shouldNotifyPatternChanged = true;
              skipDispatchThisTick = true;
            }
          }
        }

        if (!skipDispatchThisTick) {
          patternSnapshot = module.currentEditor();
          mutedChannelsSnapshot = channelMuted;
        }

        lock.unlock();

        if (shouldNotifyPatternChanged) {
          juce::MessageManager::callAsync([safeMainWindow]() {
            if (safeMainWindow != nullptr) {
              safeMainWindow->notifyPatternChanged();
            }
          });
        }

        if (!skipDispatchThisTick) {
          sequencer.update(patternSnapshot, transport, audio, plugins, &mutedChannelsSnapshot);
        }
      } else {
        lock.unlock();
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
  });

  // Create main window
  mainWindow = std::make_unique<MainWindow>(*this);
  mainWindow->setVisible(true);

  if (!hadStaleSession) {
    // Normal startup: auto-load the last used song file so the user's work is
    // immediately available without having to click Load.
    juce::MessageManager::callAsync([this]() {
      if (mainWindow) {
        mainWindow->autoLoadLastSong();
      }
    });
  }

  if (hadStaleSession && hasRecoverySnapshot) {
    // Defer the dialog to the first event-loop iteration. Showing a synchronous
    // modal AlertWindow from inside initialise() crashes on Linux because JUCE's
    // outer dispatch loop isn't running yet when the nested modal loop starts.
    const juce::String snapshotPath = recoverySnapshot.getFullPathName();
    const juce::String snapshotTime = recoverySnapshot.getLastModificationTime().toString(true, true);
    juce::MessageManager::callAsync([this, snapshotPath, snapshotTime]() {
      const int action = juce::AlertWindow::showYesNoCancelBox(
          juce::MessageBoxIconType::QuestionIcon,
          "Recover Previous Session?",
          "exTracker detected an unexpected previous shutdown.\n"
          "A recovery snapshot is available from: " + snapshotTime + "\n\n"
          "Restore the snapshot now?",
          "Restore",
          "Discard",
          "Later",
          mainWindow.get(),
          nullptr);

      if (action == 1) {
        bool restored = false;
        for (int attempt = 0; attempt < 30 && !restored; ++attempt) {
          restored = loadPatternFromFile(snapshotPath.toStdString());
          if (!restored) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        }
        if (restored) {
          transport.setPatternRows(static_cast<std::uint32_t>(module.currentEditor().rows()));
          transport.resetTickCount();
          sequencer.reset();
          if (mainWindow) {
            mainWindow->notifyPatternChanged();
          }
        } else {
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon,
              "Recovery Failed",
              "Could not load the recovery snapshot (engine busy or file invalid).",
              "OK",
              mainWindow.get());
        }
      } else if (action == 2) {
        juce::File(snapshotPath).deleteFile();
      }
    });
  }

  writeRecoverySessionMarker();
}

void ExTrackerApp::shutdown() {
  running.store(false);
  if (sequencerThread.joinable()) {
    sequencerThread.join();
  }

  if (transport.isPlaying()) {
    transport.stop();
  }
  midiInput.stop();
  audio.stop();
  plugins.allNotesOff();
  plugins.unloadAll();

  mainWindow.reset();
  clearRecoverySessionMarker();
}

void ExTrackerApp::anotherInstanceStarted(const juce::String& commandLine) {
  juce::ignoreUnused(commandLine);
  // Bring main window to front if another instance was started
  if (mainWindow) {
    mainWindow->toFront(true);
  }
}

bool ExTrackerApp::savePatternToFile(const std::string& path, bool blocking) {
  std::unique_lock<std::mutex> lock(stateMutex, std::defer_lock);
  if (blocking) {
    lock.lock();
  } else if (!lock.try_lock()) {
    return false;
  }

  std::ofstream out(path);
  if (!out) {
    return false;
  }

  const std::filesystem::path modulePath(path);
  const std::filesystem::path moduleDirectory = modulePath.parent_path();
  const std::filesystem::path songStem = modulePath.stem();
  std::unordered_map<std::string, std::string> bundledSamples;
  std::unordered_map<std::string, std::string> bundledInstrumentFiles;

  auto padSlot = [](std::size_t slot) {
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << slot;
    return oss.str();
  };

  out << "EXTRACKER_SONG_V1 "
      << module.currentEditor().rows() << " "
      << module.currentEditor().channels() << " "
      << module.patternCount() << " "
      << module.songLength() << " "
      << module.currentPattern() << " "
      << currentSongOrderPositionCache.load() << "\n";

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

  out << "MIDI_EDITOR_CC_MAP";
  for (const auto& mapping : midiEditorCcMappings) {
    out << " " << mapping.load();
  }
  out << "\n";

  out << "CHANNEL_INSTRUMENTS";
  for (std::size_t ch = 0; ch < channelInstruments.size(); ++ch) {
    out << " " << static_cast<int>(channelInstruments[ch]);
  }
  out << "\n";

  out << "CHANNEL_MUTED";
  for (std::size_t ch = 0; ch < channelMuted.size(); ++ch) {
    out << " " << (channelMuted[ch] ? 1 : 0);
  }
  out << "\n";

  out << "CHANNEL_VOLUME";
  for (std::size_t ch = 0; ch < module.currentEditor().channels(); ++ch) {
    out << " " << static_cast<int>(std::lround(sequencer.channelVolume(ch) * 100.0f));
  }
  out << "\n";

  std::ostringstream instrumentNamesOut;
  std::ostringstream instrumentAssignsOut;
  std::ostringstream instrumentParamsOut;

  for (std::size_t instrSlot = 0; instrSlot < extracker::PluginHost::kMaxInstrumentSlots; ++instrSlot) {
    const std::uint8_t slot = static_cast<std::uint8_t>(instrSlot);
    const std::string pluginId = plugins.pluginForInstrument(slot);
    if (pluginId.empty()) {
      continue;
    }

    const std::string slotLabel = "instr_" + padSlot(instrSlot);
    const auto parts = extracker::parseInstrumentId(pluginId);

    std::string displayName;
    if (parts.kind == extracker::InstrumentIdKind::NotBundlable) {
      displayName = plugins.pluginDisplayName(pluginId);
    } else {
      displayName = std::filesystem::path(parts.path).stem().string();
    }

    std::string storedId = pluginId;
    if (parts.kind == extracker::InstrumentIdKind::SingleFile) {
      const std::string bundledPath = extracker::bundleFile(
          parts.path, moduleDirectory, songStem, "instruments", slotLabel, displayName,
          bundledInstrumentFiles);
      storedId = extracker::rebuildInstrumentId(parts, bundledPath);
    } else if (parts.kind == extracker::InstrumentIdKind::DirectoryOfSiblingFiles) {
      const std::string bundledPath = extracker::bundleDirectory(
          parts.path, moduleDirectory, songStem, "instruments", slotLabel, displayName,
          bundledInstrumentFiles);
      storedId = extracker::rebuildInstrumentId(parts, bundledPath);
    }

    instrumentNamesOut << "INSTRUMENT_NAME " << instrSlot << " " << std::quoted(displayName) << "\n";
    instrumentAssignsOut << "INSTRUMENT_ASSIGN " << instrSlot << " " << std::quoted(storedId) << "\n";

    const bool isPresetBasedPlugin =
        pluginId.compare(0, 5, "vst3.") == 0 || pluginId.compare(0, 4, "lv2:") == 0;
    if (isPresetBasedPlugin) {
      const std::filesystem::path presetDir = moduleDirectory / (songStem.string() + "_instruments");
      std::error_code presetDirError;
      std::filesystem::create_directories(presetDir, presetDirError);
      if (!presetDirError) {
        const std::filesystem::path presetPath = presetDir / (slotLabel + ".preset");
        if (plugins.saveInstrumentPreset(slot, presetPath.string())) {
          std::error_code relError;
          const auto relativePreset = std::filesystem::relative(presetPath, moduleDirectory, relError);
          const std::string storedPresetPath =
              (!relError && !relativePreset.empty()) ? relativePreset.generic_string() : presetPath.string();
          instrumentParamsOut << "INSTRUMENT_PRESET " << instrSlot << " " << std::quoted(storedPresetPath) << "\n";
        }
      }
    } else {
      for (const auto& paramName : plugins.listInstrumentParameters(slot)) {
        const double value = plugins.getInstrumentParameter(slot, paramName);
        instrumentParamsOut << "INSTRUMENT_PARAM " << instrSlot << " " << paramName << " " << value << "\n";
      }
    }

    const auto filterParams = plugins.getInstrumentFilterParams(slot);
    if (filterParams.isActive()) {
      instrumentParamsOut << "INSTRUMENT_FILTER " << instrSlot << " "
                           << static_cast<int>(filterParams.type) << " "
                           << filterParams.cutoffNorm << " " << filterParams.resonanceNorm << "\n";
    }

    const auto effectParams = plugins.getInstrumentEffectParams(slot);
    if (effectParams.isActive()) {
      instrumentParamsOut << "INSTRUMENT_EFFECTS " << instrSlot << " "
                           << static_cast<int>(effectParams.distortion.type) << " "
                           << effectParams.distortion.drive << " " << effectParams.distortion.mix << " "
                           << effectParams.delay.timeMs << " " << effectParams.delay.feedback << " "
                           << effectParams.delay.wet << " "
                           << effectParams.chorus.rate << " " << effectParams.chorus.depth << " "
                           << effectParams.chorus.wet << "\n";
    }

    const float pitch = plugins.getInstrumentPitch(slot);
    if (pitch != 0.0f) {
      instrumentParamsOut << "INSTRUMENT_PITCH " << instrSlot << " " << pitch << "\n";
    }
    const float reverb = plugins.getInstrumentReverbSend(slot);
    if (reverb != 0.0f) {
      instrumentParamsOut << "INSTRUMENT_REVERB " << instrSlot << " " << reverb << "\n";
    }
    const float depth = plugins.getInstrumentDepth(slot);
    if (depth != 0.5f) {
      instrumentParamsOut << "INSTRUMENT_DEPTH " << instrSlot << " " << depth << "\n";
    }
  }

  out << instrumentNamesOut.str();
  out << instrumentAssignsOut.str();
  out << instrumentParamsOut.str();

  for (std::size_t sampleSlot = 0; sampleSlot < extracker::PluginHost::kMaxSampleSlots; ++sampleSlot) {
    const std::string samplePathRaw = plugins.samplePathForSlot(static_cast<std::uint16_t>(sampleSlot));
    if (!samplePathRaw.empty()) {
      const std::string sampleName = plugins.sampleNameForSlot(static_cast<std::uint16_t>(sampleSlot));
      const std::string storedPath = extracker::bundleFile(
          samplePathRaw, moduleDirectory, songStem, "samples", "slot_" + padSlot(sampleSlot),
          sampleName, bundledSamples);
      out << "SAMPLE_ENTRY " << sampleSlot << " " << std::quoted(sampleName) << " " << std::quoted(storedPath) << "\n";
    }
  }

  for (std::size_t instrSlot = 0; instrSlot < extracker::PluginHost::kMaxInstrumentSlots; ++instrSlot) {
    const int sampleSlot = plugins.sampleSlotForInstrument(static_cast<std::uint8_t>(instrSlot));
    if (sampleSlot >= 0) {
      out << "INSTR_SAMPLE_SLOT " << instrSlot << " " << sampleSlot << "\n";
    }
  }

  out << "MODULE_MESSAGE " << std::quoted(escapeModuleMessage(module.message())) << "\n";

  return out.good();
}

bool ExTrackerApp::loadPatternFromFile(const std::string& path, bool blocking) {
  std::unique_lock<std::mutex> lock(stateMutex, std::defer_lock);
  if (blocking) {
    lock.lock();
  } else if (!lock.try_lock()) {
    return false;
  }

  std::ifstream in(path);
  if (!in) {
    return false;
  }

  const std::filesystem::path moduleDirectory = std::filesystem::path(path).parent_path();

  std::string magic;
  std::size_t fileRows = 0;
  std::size_t fileChannels = 0;
  in >> magic >> fileRows >> fileChannels;
  if (!in) {
    return false;
  }

  const bool isSongV1 = (magic == "EXTRACKER_SONG_V1");
  const bool isLegacyV1 = (magic == "EXTRACKER_PATTERN_V1" || magic == "EXTRACKER_MODULE_V1");
  const bool isLegacyV2 = (magic == "EXTRACKER_PATTERN_V2" || magic == "EXTRACKER_MODULE_V2");
  if ((!isSongV1 && !isLegacyV1 && !isLegacyV2) || fileRows == 0 || fileChannels == 0) {
    return false;
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
             token == "MIDI_TRANSPORT" || token == "MIDI_EDITOR_CC_MAP" ||
             token == "SAMPLE_BANK" || token == "SAMPLE_ENTRY" ||
             token == "CHANNEL_INSTRUMENTS" || token == "CHANNEL_MUTED" ||
             token == "CHANNEL_VOLUME" ||
             token == "MODULE_MESSAGE" || token == "INSTRUMENT_ASSIGN" ||
             token == "INSTR_SAMPLE_SLOT" ||
             token == "INSTRUMENT_NAME" || token == "INSTRUMENT_PARAM" ||
             token == "INSTRUMENT_PRESET" ||
             token == "INSTRUMENT_FILTER" || token == "INSTRUMENT_EFFECTS" ||
             token == "INSTRUMENT_PITCH" || token == "INSTRUMENT_REVERB" ||
             token == "INSTRUMENT_DEPTH" ||
             token.rfind("RECORD_", 0) == 0;
    };

    std::string pendingToken;
    bool hasPendingToken = false;
    std::unordered_map<int, std::string> pendingInstrumentNames;
    bool rescannedExternalPlugins = false;

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
        if (!extracker::cli::parseStrictIntToken(firstToken, parsedRow)) {
          return false;
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
          if (note < 0) {
            editor.insertNoteOff(parsedRow, parsedChannel);
          } else {
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

    // Debug: count notes in all patterns
    {
      std::size_t totalNotes = 0;
      for (std::size_t i = 0; i < module.patternCount(); ++i) {
        const auto& editor = module.patternEditor(i);
        for (std::size_t row = 0; row < editor.rows(); ++row) {
          for (std::size_t ch = 0; ch < editor.channels(); ++ch) {
            if (editor.hasNoteAt(static_cast<int>(row), static_cast<int>(ch))) {
              totalNotes++;
            }
          }
        }
      }
    }

    std::string tailToken;
    std::vector<std::size_t> songOrder;
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
        for (std::size_t patternIndex = 0; patternIndex < module.patternCount(); ++patternIndex) {
          int swingPercent = 50;
          in >> swingPercent;
          if (in) {
            module.setPatternSwing(patternIndex, static_cast<std::uint8_t>(std::clamp(swingPercent, 50, 75)));
          }
        }
      } else if (tailToken == "INSERT_SWING_INHERIT") {
        int enabled = 0;
        in >> enabled;
        if (in) {
          module.setInheritSwingOnInsert(enabled != 0);
        }
      } else if (tailToken == "ROW_EDIT_SCOPE") {
        int allChannels = 0;
        in >> allChannels;
        if (in) {
          module.setRowEditAllChannels(allChannels != 0);
        }
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
          in >> mapped;
          if (in) {
            midiChannelMap[ch] = mapped;
          }
        }
      } else if (tailToken == "MIDI_TRANSPORT") {
        // CLI-only: read and discard (timeout ms + lock-tempo flag)
        long long timeoutMs = 0;
        int lockTempo = 0;
        in >> timeoutMs >> lockTempo;
      } else if (tailToken == "MIDI_EDITOR_CC_MAP") {
        for (auto& mapping : midiEditorCcMappings) {
          int code = -1;
          in >> code;
          if (in) {
            mapping.store(code);
          }
        }
      } else if (tailToken == "SAMPLE_BANK") {
        int slot = 0;
        std::string samplePath;
        in >> slot >> std::quoted(samplePath);
        if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
          const std::string resolvedPath = extracker::resolveStoredPath(moduleDirectory, samplePath);
          plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), resolvedPath);
          plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), std::filesystem::path(resolvedPath).stem().string());
        }
      } else if (tailToken == "SAMPLE_ENTRY") {
        int slot = 0;
        std::string sampleName;
        std::string samplePath;
        in >> slot >> std::quoted(sampleName) >> std::quoted(samplePath);
        if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
          const std::string resolvedPath = extracker::resolveStoredPath(moduleDirectory, samplePath);
          plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), resolvedPath);
          plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), sampleName);
        }
      } else if (tailToken == "CHANNEL_INSTRUMENTS") {
        for (std::size_t ch = 0; ch < channelInstruments.size(); ++ch) {
          int slot = 0;
          in >> slot;
          if (in) {
            channelInstruments[ch] = static_cast<std::uint8_t>(std::clamp(slot, 0, 15));
          }
        }
      } else if (tailToken == "CHANNEL_MUTED") {
        for (std::size_t ch = 0; ch < channelMuted.size(); ++ch) {
          int muted = 0;
          in >> muted;
          if (in) {
            channelMuted[ch] = (muted != 0);
          }
        }
      } else if (tailToken == "CHANNEL_VOLUME") {
        for (std::size_t ch = 0; ch < module.currentEditor().channels(); ++ch) {
          int pct = 100;
          in >> pct;
          if (in) {
            sequencer.setChannelVolume(ch, static_cast<float>(std::clamp(pct, 0, 200)) / 100.0f);
          }
        }
      } else if (tailToken == "MODULE_MESSAGE") {
        std::string escapedMessage;
        in >> std::quoted(escapedMessage);
        if (in) {
          module.setMessage(unescapeModuleMessage(escapedMessage));
        }
      } else if (tailToken == "INSTRUMENT_NAME") {
        int instrSlot = -1;
        std::string name;
        if (in >> instrSlot >> std::quoted(name)) {
          pendingInstrumentNames[instrSlot] = name;
        }
      } else if (tailToken == "INSTRUMENT_ASSIGN") {
        int instrSlot = -1;
        std::string storedId;
        if (in >> instrSlot >> std::quoted(storedId)) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
            const auto parts = extracker::parseInstrumentId(storedId);
            std::string resolvedId = storedId;
            if (parts.kind != extracker::InstrumentIdKind::NotBundlable) {
              resolvedId = extracker::rebuildInstrumentId(
                  parts, extracker::resolveStoredPath(moduleDirectory, parts.path));
            }
            bool loaded = plugins.loadInstrumentAuto(resolvedId, static_cast<std::uint8_t>(instrSlot));
            if (!loaded && !rescannedExternalPlugins &&
                (storedId.compare(0, 5, "vst3.") == 0 || storedId.compare(0, 4, "lv2:") == 0)) {
              plugins.rescanExternalPlugins();
              rescannedExternalPlugins = true;
              loaded = plugins.loadInstrumentAuto(resolvedId, static_cast<std::uint8_t>(instrSlot));
            }
            if (!loaded) {
              const auto nameIt = pendingInstrumentNames.find(instrSlot);
              const std::string displayName =
                  (nameIt != pendingInstrumentNames.end()) ? nameIt->second : storedId;
              std::cerr << "Warning: could not restore instrument " << instrSlot
                        << " (\"" << displayName << "\", originally " << storedId << ")\n";
            }
          }
        }
      } else if (tailToken == "INSTR_SAMPLE_SLOT") {
        int instrSlot = -1;
        int sampleSlot = -1;
        if (in >> instrSlot >> sampleSlot) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots) &&
              sampleSlot >= 0 && sampleSlot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
            plugins.assignSampleSlotToInstrument(static_cast<std::uint16_t>(sampleSlot),
                                                 static_cast<std::uint8_t>(instrSlot));
          }
        }
      } else if (tailToken == "INSTRUMENT_PRESET") {
        int instrSlot = -1;
        std::string presetPath;
        if (in >> instrSlot >> std::quoted(presetPath)) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
            const std::string resolvedPresetPath = extracker::resolveStoredPath(moduleDirectory, presetPath);
            plugins.loadInstrumentPreset(static_cast<std::uint8_t>(instrSlot), resolvedPresetPath);
          }
        }
      } else if (tailToken == "INSTRUMENT_PARAM") {
        int instrSlot = -1;
        std::string paramName;
        double value = 0.0;
        if (in >> instrSlot >> paramName >> value) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
            plugins.setInstrumentParameter(static_cast<std::uint8_t>(instrSlot), paramName, value);
          }
        }
      } else if (tailToken == "INSTRUMENT_FILTER") {
        int instrSlot = -1;
        int filterType = 0;
        float cutoffNorm = 1.0f;
        float resonanceNorm = 0.0f;
        if (in >> instrSlot >> filterType >> cutoffNorm >> resonanceNorm) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
            plugins.setInstrumentFilter(static_cast<std::uint8_t>(instrSlot),
                                        static_cast<extracker::BiquadType>(filterType),
                                        cutoffNorm, resonanceNorm);
          }
        }
      } else if (tailToken == "INSTRUMENT_EFFECTS") {
        int instrSlot = -1;
        extracker::InstrumentEffectParams effectParams;
        int distortionType = 0;
        if (in >> instrSlot >> distortionType >> effectParams.distortion.drive >> effectParams.distortion.mix >>
                effectParams.delay.timeMs >> effectParams.delay.feedback >> effectParams.delay.wet >>
                effectParams.chorus.rate >> effectParams.chorus.depth >> effectParams.chorus.wet) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
            effectParams.distortion.type = static_cast<extracker::DistortionType>(distortionType);
            plugins.setInstrumentEffects(static_cast<std::uint8_t>(instrSlot), effectParams);
          }
        }
      } else if (tailToken == "INSTRUMENT_PITCH") {
        int instrSlot = -1;
        float semitones = 0.0f;
        if (in >> instrSlot >> semitones) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
            plugins.setInstrumentPitch(static_cast<std::uint8_t>(instrSlot), semitones);
          }
        }
      } else if (tailToken == "INSTRUMENT_REVERB") {
        int instrSlot = -1;
        float send = 0.0f;
        if (in >> instrSlot >> send) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
            plugins.setInstrumentReverbSend(static_cast<std::uint8_t>(instrSlot), send);
          }
        }
      } else if (tailToken == "INSTRUMENT_DEPTH") {
        int instrSlot = -1;
        float depth = 0.5f;
        if (in >> instrSlot >> depth) {
          if (instrSlot >= 0 && instrSlot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
            plugins.setInstrumentDepth(static_cast<std::uint8_t>(instrSlot), depth);
          }
        }
      }
    }

    if (!songOrder.empty()) {
      module.setSongOrder(songOrder);
    }

    module.switchToPattern(std::min(fileCurrentPattern, module.patternCount() - 1));
    transport.setPatternRows(static_cast<std::uint32_t>(module.currentEditor().rows()));
    transport.setSwingPercent(module.currentPatternSwing());
    transport.resetTickCount();
    currentPatternCache.store(module.currentPattern());
    patternCountCache.store(module.patternCount());
    currentSongOrderPositionCache.store(std::min(fileCurrentSongPosition, module.songLength() - 1));
    return true;
  }

  module.reset(fileRows, fileChannels, 1);

  const bool isLegacyV2Format = isLegacyV2;
  for (std::size_t i = 0; i < module.currentEditor().rows() * module.currentEditor().channels(); ++i) {
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

    if (isLegacyV2Format) {
      in >> row >> channel >> hasNote >> note >> instrument >> sample >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue;
    } else {
      in >> row >> channel >> hasNote >> note >> instrument >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue;
    }
    if (!in) {
      return false;
    }

    if (hasNote != 0) {
      if (note < 0) {
        module.currentEditor().insertNoteOff(row, channel);
      } else {
        module.currentEditor().insertNote(
            row,
            channel,
            note,
            static_cast<std::uint8_t>(std::clamp(instrument, 0, 255)),
            static_cast<std::uint32_t>(std::max(gateTicks, 0)),
            static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)),
            retrigger != 0,
            static_cast<std::uint8_t>(std::clamp(effectCommand, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(effectValue, 0, 255)));
        if (isLegacyV2Format && sample != 0xFFFF) {
          module.currentEditor().setSample(row, channel, static_cast<std::uint16_t>(std::clamp(sample, 0, 65535)));
        }
      }
    } else if (effectCommand != 0 || effectValue != 0) {
      module.currentEditor().setEffect(
          row,
          channel,
          static_cast<std::uint8_t>(std::clamp(effectCommand, 0, 255)),
          static_cast<std::uint8_t>(std::clamp(effectValue, 0, 255)));
    }
  }

  std::string tailToken;
  while (in >> tailToken) {
    if (tailToken == "PATTERN_SWING") {
      for (std::size_t patternIndex = 0; patternIndex < module.patternCount(); ++patternIndex) {
        int swingPercent = 50;
        in >> swingPercent;
        if (in) {
          module.setPatternSwing(patternIndex, static_cast<std::uint8_t>(std::clamp(swingPercent, 50, 75)));
        }
      }
    } else if (tailToken == "INSERT_SWING_INHERIT") {
      int enabled = 0;
      in >> enabled;
      if (in) {
        module.setInheritSwingOnInsert(enabled != 0);
      }
    } else if (tailToken == "ROW_EDIT_SCOPE") {
      int allChannels = 0;
      in >> allChannels;
      if (in) {
        module.setRowEditAllChannels(allChannels != 0);
      }
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
        in >> mapped;
        if (in) {
          midiChannelMap[ch] = mapped;
        }
      }
    } else if (tailToken == "MIDI_EDITOR_CC_MAP") {
      for (auto& mapping : midiEditorCcMappings) {
        int code = -1;
        in >> code;
        if (in) {
          mapping.store(code);
        }
      }
    } else if (tailToken == "SAMPLE_BANK") {
      int slot = 0;
      std::string samplePath;
      in >> slot >> std::quoted(samplePath);
      if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
        const std::string resolvedPath = extracker::resolveStoredPath(moduleDirectory, samplePath);
        plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), resolvedPath);
        plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), std::filesystem::path(resolvedPath).stem().string());
      }
    } else if (tailToken == "SAMPLE_ENTRY") {
      int slot = 0;
      std::string sampleName;
      std::string samplePath;
      in >> slot >> std::quoted(sampleName) >> std::quoted(samplePath);
      if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
        const std::string resolvedPath = extracker::resolveStoredPath(moduleDirectory, samplePath);
        plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), resolvedPath);
        plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), sampleName);
      }
    } else if (tailToken == "CHANNEL_INSTRUMENTS") {
      for (std::size_t ch = 0; ch < channelInstruments.size(); ++ch) {
        int slot = 0;
        in >> slot;
        if (in) {
          channelInstruments[ch] = static_cast<std::uint8_t>(std::clamp(slot, 0, 15));
        }
      }
    } else if (tailToken == "CHANNEL_MUTED") {
      for (std::size_t ch = 0; ch < channelMuted.size(); ++ch) {
        int muted = 0;
        in >> muted;
        if (in) {
          channelMuted[ch] = (muted != 0);
        }
      }
    } else if (tailToken == "CHANNEL_VOLUME") {
      for (std::size_t ch = 0; ch < module.currentEditor().channels(); ++ch) {
        int pct = 100;
        in >> pct;
        if (in) {
          sequencer.setChannelVolume(ch, static_cast<float>(std::clamp(pct, 0, 200)) / 100.0f);
        }
      }
    } else if (tailToken == "MODULE_MESSAGE") {
      std::string escapedMessage;
      in >> std::quoted(escapedMessage);
      if (in) {
        module.setMessage(unescapeModuleMessage(escapedMessage));
      }
    }
  }

  currentPatternCache.store(module.currentPattern());
  patternCountCache.store(module.patternCount());
  currentSongOrderPositionCache.store(module.firstSongEntryForPattern(module.currentPattern()));
  transport.setPatternRows(static_cast<std::uint32_t>(module.currentEditor().rows()));
  transport.setSwingPercent(module.currentPatternSwing());
  transport.resetTickCount();

  return true;
}

void ExTrackerApp::armMidiEditorLearn(MidiEditorAction action) {
  midiEditorLearnTarget.store(static_cast<int>(action));
}

void ExTrackerApp::clearMidiEditorCcMappings() {
  for (auto& mapping : midiEditorCcMappings) {
    mapping.store(-1);
  }
  for (auto& pendingValue : midiEditorPendingValues) {
    pendingValue.store(-1);
  }
  midiEditorLearnTarget.store(static_cast<int>(MidiEditorAction::None));
}

int ExTrackerApp::midiEditorCcMappingCode(MidiEditorAction action) const {
  const int idx = midiEditorActionIndex(action);
  if (idx < 0) {
    return -1;
  }
  return midiEditorCcMappings[static_cast<std::size_t>(idx)].load();
}
