#include "app.h"
#include "main_window.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

ExTrackerApp::ExTrackerApp() {
  midiChannelMap.fill(-1);
  channelInstruments.resize(module.currentEditor().channels(), 0);
  channelMuted.resize(module.currentEditor().channels(), false);
  currentPatternCache.store(module.currentPattern());
  patternCountCache.store(module.patternCount());
  currentSongOrderPositionCache.store(module.firstSongEntryForPattern(module.currentPattern()));
}

ExTrackerApp::~ExTrackerApp() = default;

void ExTrackerApp::initialise(const juce::String& commandLine) {
  juce::ignoreUnused(commandLine);
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

  // Keep builtins loaded but leave instrument slots unassigned by default.

  // Load default pattern
  module.currentEditor().insertNote(0, 0, 48, 0, 0, 120, true);
  module.currentEditor().insertNote(0, 1, 55, 1, 0, 96, false);
  module.currentEditor().insertNote(1, 0, 52, 0, 0, 80, false);
  module.currentEditor().insertNote(1, 1, 59, 1, 0, 70, true);
  module.currentEditor().insertNote(2, 0, 55, 0, 0, 100, false);
  module.currentEditor().insertNote(4, 1, 55, 1, 0, 127, true);

  // Configure transport
  transport.setTempoBpm(125.0);
  transport.setTicksPerBeat(6);
  transport.setTicksPerRow(1);
  transport.setPatternRows(static_cast<std::uint32_t>(module.currentEditor().rows()));
  transport.resetTickCount();

  // Start sequencer thread
  sequencerThread = std::thread([this]() {
    while (running.load()) {
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

  out << "MIDI_MAP";
  for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
    out << " " << midiChannelMap[ch];
  }
  out << "\n";

  for (std::size_t sampleSlot = 0; sampleSlot < extracker::PluginHost::kMaxSampleSlots; ++sampleSlot) {
    const std::string samplePath = plugins.samplePathForSlot(static_cast<std::uint16_t>(sampleSlot));
    if (!samplePath.empty()) {
      const std::string sampleName = plugins.sampleNameForSlot(static_cast<std::uint16_t>(sampleSlot));
      out << "SAMPLE_ENTRY " << sampleSlot << " " << std::quoted(sampleName) << " " << std::quoted(samplePath) << "\n";
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
  if ((!isSongV1 && !isLegacyV1 && !isLegacyV2) ||
      fileRows != module.currentEditor().rows() || fileChannels != module.currentEditor().channels()) {
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

    for (std::size_t patternIndex = 0; patternIndex < filePatternCount; ++patternIndex) {
      std::string patternToken;
      std::size_t storedPatternIndex = 0;
      in >> patternToken >> storedPatternIndex;
      if (!in || patternToken != "PATTERN" || storedPatternIndex != patternIndex) {
        return false;
      }

      auto& editor = module.patternEditor(patternIndex);
      for (std::size_t row = 0; row < editor.rows(); ++row) {
        for (std::size_t channel = 0; channel < editor.channels(); ++channel) {
          int parsedRow = 0;
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

          in >> parsedRow >> parsedChannel >> hasNote >> note >> instrument >> sample >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue;
          if (!in) {
            return false;
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
    }

    std::string tailToken;
    std::vector<std::size_t> songOrder;
    while (in >> tailToken) {
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
      } else if (tailToken == "MIDI_MAP") {
        for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
          int mapped = -1;
          in >> mapped;
          if (in) {
            midiChannelMap[ch] = mapped;
          }
        }
      } else if (tailToken == "SAMPLE_BANK") {
        int slot = 0;
        std::string samplePath;
        in >> slot >> std::quoted(samplePath);
        if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
          plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), samplePath);
          plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), std::filesystem::path(samplePath).stem().string());
        }
      } else if (tailToken == "SAMPLE_ENTRY") {
        int slot = 0;
        std::string sampleName;
        std::string samplePath;
        in >> slot >> std::quoted(sampleName) >> std::quoted(samplePath);
        if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
          plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), samplePath);
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
      }
    }

    if (!songOrder.empty()) {
      module.setSongOrder(songOrder);
    }

    module.switchToPattern(std::min(fileCurrentPattern, module.patternCount() - 1));
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
    if (tailToken == "MIDI_MAP") {
      for (std::size_t ch = 0; ch < midiChannelMap.size(); ++ch) {
        int mapped = -1;
        in >> mapped;
        if (in) {
          midiChannelMap[ch] = mapped;
        }
      }
    } else if (tailToken == "SAMPLE_BANK") {
      int slot = 0;
      std::string samplePath;
      in >> slot >> std::quoted(samplePath);
      if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
        plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), samplePath);
        plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), std::filesystem::path(samplePath).stem().string());
      }
    } else if (tailToken == "SAMPLE_ENTRY") {
      int slot = 0;
      std::string sampleName;
      std::string samplePath;
      in >> slot >> std::quoted(sampleName) >> std::quoted(samplePath);
      if (in && slot >= 0 && slot < static_cast<int>(extracker::PluginHost::kMaxSampleSlots)) {
        plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), samplePath);
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
    }
  }

  currentPatternCache.store(module.currentPattern());
  patternCountCache.store(module.patternCount());
  currentSongOrderPositionCache.store(module.firstSongEntryForPattern(module.currentPattern()));

  return true;
}
