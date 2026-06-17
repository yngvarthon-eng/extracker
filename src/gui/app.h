#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>
#include <cstdint>
#include <memory>
#include <atomic>
#include <array>
#include <thread>
#include <mutex>
#include <vector>

#include "extracker/audio_engine.hpp"
#include "extracker/module.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"
#include "extracker/midi_input.hpp"

class MainWindow;

enum class PlayMode {
  PLAY_PATTERN,  // Play current pattern only (loop or stop at end)
  PLAY_SONG      // Play all patterns in sequence
};

class ExTrackerApp : public juce::JUCEApplication {
public:
  enum class MidiEditorAction {
    None = 0,
    Velocity,
    Gate,
    EffectCommand,
    EffectValue
  };

  ExTrackerApp();
  ~ExTrackerApp() override;

  const juce::String getApplicationName() override { return "exTracker"; }
  const juce::String getApplicationVersion() override { return "0.1.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String& commandLine) override;
  void shutdown() override;
  void systemRequestedQuit() override { quit(); }
  void anotherInstanceStarted(const juce::String& commandLine) override;

  // File I/O
  bool savePatternToFile(const std::string& path);
  bool loadPatternFromFile(const std::string& path);
  void armMidiEditorLearn(MidiEditorAction action);
  void clearMidiEditorCcMappings();
  int midiEditorCcMappingCode(MidiEditorAction action) const;

  // Engine and state (owned by app)
  extracker::AudioEngine audio;
  extracker::Module module;
  extracker::PluginHost plugins;
  extracker::Sequencer sequencer;
  extracker::Transport transport;
  extracker::MidiInput midiInput;

  // State synchronization
  std::mutex stateMutex;
  std::atomic<bool> running{true};

  // Playback mode
  PlayMode playMode = PlayMode::PLAY_PATTERN;
  int lastSongModeRow = -1;  // Track row for PLAY_SONG pattern detection
  std::atomic<std::size_t> currentPatternCache{0};
  std::atomic<std::size_t> patternCountCache{1};
  std::atomic<std::size_t> currentSongOrderPositionCache{0};

  // UI state
  bool darkMode = false;

  // Pattern state
  bool loopEnabled = false;
  bool playRangeActive = false;
  int playRangeFrom = 0;
  int playRangeTo = 0;
  int playRangeStep = 1;

  // MIDI state
  bool midiThruEnabled = true;
  int midiInstrument = 0;
  int activeSampleSlot = -1;
  bool midiLearnEnabled = false;
  bool midiTransportSyncEnabled = false;
  std::atomic<bool> midiTransportRunning{false};
  std::array<int, 16> midiChannelMap{};
  std::atomic<int> midiEditorLearnTarget{0};  // 0 means no armed target.
  std::array<std::atomic<int>, 4> midiEditorCcMappings{};   // channel*128 + controller, or -1.
  std::array<std::atomic<int>, 4> midiEditorPendingValues{};  // Latest CC values per action, or -1.
  std::vector<std::uint8_t> channelInstruments;
  std::vector<bool> channelMuted;
  std::atomic<bool> recoveryAutoSaveEnabled{true};
  std::atomic<std::uint64_t> lastRecoverySnapshotEpochMs{0};

private:
  std::unique_ptr<MainWindow> mainWindow;
  std::thread sequencerThread;
};
