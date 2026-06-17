#include "app.h"
#include "main_window.h"
#include "extracker/cli_parse_utils.hpp"
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

std::string sanitizeFileToken(std::string text) {
  for (char& ch : text) {
    const bool isAlphaNum = (ch >= 'a' && ch <= 'z') ||
                            (ch >= 'A' && ch <= 'Z') ||
                            (ch >= '0' && ch <= '9');
    if (!isAlphaNum && ch != '-' && ch != '_') {
      ch = '_';
    }
  }
  if (text.empty()) {
    return "sample";
  }
  return text;
}

std::string resolveSamplePathForLoad(const std::filesystem::path& moduleDirectory,
                                     const std::string& storedPath) {
  if (storedPath.empty()) {
    return storedPath;
  }

  std::filesystem::path resolved(storedPath);
  if (resolved.is_relative() && !moduleDirectory.empty()) {
    resolved = moduleDirectory / resolved;
  }

  std::error_code ec;
  const auto normalized = std::filesystem::weakly_canonical(resolved, ec);
  if (!ec) {
    return normalized.string();
  }
  return resolved.lexically_normal().string();
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
      if (!plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(targetInstrument))) {
        plugins.loadPlugin("builtin.sine");
        plugins.assignInstrument(static_cast<std::uint8_t>(targetInstrument), "builtin.sine");
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
    } else if (event.type == extracker::MidiEvent::Type::NoteOff) {
      if (!plugins.triggerNoteOff(static_cast<std::uint8_t>(targetInstrument), event.note)) {
        audio.noteOff(event.note, static_cast<std::uint8_t>(targetInstrument));
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
        const juce::String snapshotPath = recoverySnapshotFile().getFullPathName();
        if (savePatternToFile(snapshotPath.toStdString())) {
          lastRecoverySnapshotEpochMs.store(static_cast<std::uint64_t>(juce::Time::currentTimeMillis()));
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
          // In SONG mode, detect pattern wrap-around (row resets to 0)
          int currentRow = static_cast<int>(transport.currentRow());
          
          if (lastSongModeRow >= 0 && currentRow < lastSongModeRow) {
            // Row wrapped: we've reached the end of the pattern and looped
            std::size_t nextSongPos = currentSongOrderPositionCache.load() + 1;
            if (nextSongPos < module.songLength()) {
              currentSongOrderPositionCache.store(nextSongPos);
              module.switchToPattern(module.songEntryAt(nextSongPos));
              currentPatternCache.store(module.currentPattern());
              patternCountCache.store(module.patternCount());
              safeMainWindow = juce::Component::SafePointer<MainWindow>(mainWindow.get());
              shouldNotifyPatternChanged = true;
              // Don't reset transport; let it continue playing from current row
            } else {
              // No more patterns, stop playback
              transport.stop();
              lastSongModeRow = -1;
            }
          }
          lastSongModeRow = currentRow;
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

  if (hadStaleSession && hasRecoverySnapshot) {
    const juce::String snapshotTime = recoverySnapshot.getLastModificationTime().toString(true, true);
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
        restored = loadPatternFromFile(recoverySnapshot.getFullPathName().toStdString());
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
      recoverySnapshot.deleteFile();
    }
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

bool ExTrackerApp::savePatternToFile(const std::string& path) {
  std::unique_lock<std::mutex> lock(stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;  // Engine is busy
  }

  std::ofstream out(path);
  if (!out) {
    return false;
  }

  const std::filesystem::path modulePath(path);
  const std::filesystem::path moduleDirectory = modulePath.parent_path();
  const std::filesystem::path sampleBundleDirectory =
      moduleDirectory / (modulePath.stem().string() + "_samples");
  std::unordered_map<std::string, std::string> bundledSamples;

  std::error_code createDirError;
  if (!moduleDirectory.empty()) {
    std::filesystem::create_directories(sampleBundleDirectory, createDirError);
  }

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

  for (std::size_t sampleSlot = 0; sampleSlot < extracker::PluginHost::kMaxSampleSlots; ++sampleSlot) {
    const std::string samplePathRaw = plugins.samplePathForSlot(static_cast<std::uint16_t>(sampleSlot));
    if (!samplePathRaw.empty()) {
      const std::string sampleName = plugins.sampleNameForSlot(static_cast<std::uint16_t>(sampleSlot));
      std::string storedPath = samplePathRaw;

      if (createDirError.value() == 0 && !moduleDirectory.empty()) {
        std::filesystem::path sourcePath(samplePathRaw);
        if (sourcePath.is_relative()) {
          sourcePath = moduleDirectory / sourcePath;
        }

        std::error_code existsError;
        if (std::filesystem::exists(sourcePath, existsError) && !existsError) {
          const std::string sourceKey = sourcePath.lexically_normal().string();
          auto existing = bundledSamples.find(sourceKey);
          if (existing != bundledSamples.end()) {
            storedPath = existing->second;
          } else {
            std::ostringstream fileName;
            fileName << "slot_" << std::setw(3) << std::setfill('0') << sampleSlot
                     << "_" << sanitizeFileToken(sampleName.empty() ? sourcePath.stem().string() : sampleName)
                     << sourcePath.extension().string();
            const std::filesystem::path bundledPath = sampleBundleDirectory / fileName.str();

            std::error_code copyError;
            std::filesystem::copy_file(
                sourcePath,
                bundledPath,
                std::filesystem::copy_options::overwrite_existing,
                copyError);
            if (!copyError) {
              std::error_code relError;
              const auto relativePath = std::filesystem::relative(bundledPath, moduleDirectory, relError);
              if (!relError) {
                storedPath = relativePath.generic_string();
              } else {
                storedPath = bundledPath.string();
              }
              bundledSamples.emplace(sourceKey, storedPath);
            }
          }
        }
      }

      out << "SAMPLE_ENTRY " << sampleSlot << " " << std::quoted(sampleName) << " " << std::quoted(storedPath) << "\n";
    }
  }

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

  out << "MODULE_MESSAGE " << std::quoted(escapeModuleMessage(module.message())) << "\n";

  return out.good();
}

bool ExTrackerApp::loadPatternFromFile(const std::string& path) {
  std::unique_lock<std::mutex> lock(stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;  // Engine is busy
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
             token == "MIDI_EDITOR_CC_MAP" || token == "SAMPLE_BANK" ||
             token == "SAMPLE_ENTRY" || token == "CHANNEL_INSTRUMENTS" ||
             token == "CHANNEL_MUTED" || token == "MODULE_MESSAGE";
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
          const std::string resolvedPath = resolveSamplePathForLoad(moduleDirectory, samplePath);
          plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), resolvedPath);
          plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), std::filesystem::path(resolvedPath).stem().string());
        }
      } else if (tailToken == "SAMPLE_ENTRY") {
        int slot = 0;
        std::string sampleName;
        std::string samplePath;
        in >> slot >> std::quoted(sampleName) >> std::quoted(samplePath);
        if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
          const std::string resolvedPath = resolveSamplePathForLoad(moduleDirectory, samplePath);
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
      } else if (tailToken == "MODULE_MESSAGE") {
        std::string escapedMessage;
        in >> std::quoted(escapedMessage);
        if (in) {
          module.setMessage(unescapeModuleMessage(escapedMessage));
        }
      }
    }

    if (!songOrder.empty()) {
      module.setSongOrder(songOrder);
    }

    module.switchToPattern(std::min(fileCurrentPattern, module.patternCount() - 1));
    transport.setSwingPercent(module.currentPatternSwing());
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
        const std::string resolvedPath = resolveSamplePathForLoad(moduleDirectory, samplePath);
        plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), resolvedPath);
        plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), std::filesystem::path(resolvedPath).stem().string());
      }
    } else if (tailToken == "SAMPLE_ENTRY") {
      int slot = 0;
      std::string sampleName;
      std::string samplePath;
      in >> slot >> std::quoted(sampleName) >> std::quoted(samplePath);
      if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
        const std::string resolvedPath = resolveSamplePathForLoad(moduleDirectory, samplePath);
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
  transport.setSwingPercent(module.currentPatternSwing());

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
