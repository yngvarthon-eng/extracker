#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <atomic>
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
  std::vector<std::uint8_t> channelInstruments;
  std::vector<bool> channelMuted;

private:
  std::unique_ptr<MainWindow> mainWindow;
  std::thread sequencerThread;
};
