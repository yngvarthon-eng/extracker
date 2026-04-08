#include "main_window.h"
#include "pattern_grid.h"
#include "app.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

juce::String formatSampleSlotHex(int slot) {
  std::ostringstream out;
  out << "S" << std::uppercase << std::hex << std::setw(3) << std::setfill('0') << std::max(slot, 0);
  return juce::String(out.str());
}

juce::String formatHexByte(int value) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << std::clamp(value, 0, 255);
  return juce::String(out.str());
}

bool parseHexByte(const juce::String& text, int& value) {
  juce::String normalized = text.trim();
  if (normalized.startsWithIgnoreCase("0x")) {
    normalized = normalized.substring(2);
  }
  if (normalized.isEmpty() || normalized.length() > 2) {
    return false;
  }

  std::string raw = normalized.toStdString();
  for (char c : raw) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }

  std::stringstream ss;
  ss << std::hex << raw;
  int parsed = -1;
  ss >> parsed;
  if (!ss || parsed < 0 || parsed > 255) {
    return false;
  }

  value = parsed;
  return true;
}

class SlotActivityBar : public juce::Component {
public:
  void setLevel(double newLevel) {
    double clamped = std::clamp(newLevel, 0.0, 1.0);
    if (std::abs(clamped - level) < 0.001) {
      return;
    }
    level = clamped;
    repaint();
  }

  void paint(juce::Graphics& g) override {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF202327));
    g.fillRoundedRectangle(bounds, 2.0f);

    auto inner = bounds.reduced(1.0f, 1.0f);
    g.setColour(juce::Colour(0xFF111417));
    g.fillRoundedRectangle(inner, 2.0f);

    float width = static_cast<float>(inner.getWidth() * level);
    if (width > 0.5f) {
      juce::Rectangle<float> levelRect(inner.getX(), inner.getY(), width, inner.getHeight());
      g.setColour(juce::Colour(0xFF2EA043));
      g.fillRoundedRectangle(levelRect, 2.0f);
    }
  }

private:
  double level = 0.0;
};

// Simple wrapper component for panel controls that will be the viewed component of panelViewport
class PanelWrapper : public juce::Component {
public:
  void resized() override {
    // This component's size is managed by the viewport based on its content
    // Children positions are managed by TrackerMainComponent's resized() method
  }
};

class TrackerMainComponent : public juce::Component,
                             private juce::Timer {
public:
  explicit TrackerMainComponent(ExTrackerApp& appIn)
      : app(appIn),
        playButton("Play"),
        stopButton("Stop"),
        playModePatternButton("Pattern"),
        playModeSongButton("Song"),
        loopButton("Loop: Off"),
        applyChannelMapButton("Apply Channel Map"),
        patternGrid(appIn) {
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(playModePatternButton);
    addAndMakeVisible(playModeSongButton);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(helpButton);
    addAndMakeVisible(patternLabel);
    addAndMakeVisible(patternSelector);
    addAndMakeVisible(insertPatternBeforeButton);
    addAndMakeVisible(insertPatternAfterButton);
    addAndMakeVisible(removePatternButton);
    addAndMakeVisible(gridDensityButton);
    addAndMakeVisible(tempoSlider);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(ticksPerBeatLabel);
    addAndMakeVisible(ticksPerBeatSlider);
    addAndMakeVisible(expandPatternButton);
    addAndMakeVisible(shrinkPatternButton);
    addAndMakeVisible(expandChannelButton);
    addAndMakeVisible(shrinkChannelButton);
    addAndMakeVisible(savePatternButton);
    addAndMakeVisible(loadPatternButton);
    addAndMakeVisible(insertRowButton);
    addAndMakeVisible(removeRowButton);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(instrumentPanelTitle);
    addAndMakeVisible(songOrderTitle);
    addAndMakeVisible(songOrderListView);
    addAndMakeVisible(songOrderEntryLabel);
    addAndMakeVisible(songOrderEntrySelector);
    addAndMakeVisible(songOrderPatternLabel);
    addAndMakeVisible(songOrderPatternSelector);
    addAndMakeVisible(songOrderAddButton);
    addAndMakeVisible(songOrderRemoveButton);
    addAndMakeVisible(songOrderUpButton);
    addAndMakeVisible(songOrderDownButton);
    addAndMakeVisible(channelPanelTitle);
    addAndMakeVisible(slotPanelTitle);
    addAndMakeVisible(pluginPanelTitle);
    addAndMakeVisible(pluginStatusLabel);
    addAndMakeVisible(sampleBankTitle);
    addAndMakeVisible(sampleSlotLabel);
    addAndMakeVisible(sampleSlotSelector);
    addAndMakeVisible(sampleLoadButton);
    addAndMakeVisible(sampleAssignButton);
    addAndMakeVisible(sampleAssignToChannelButton);
    addAndMakeVisible(sampleRenameEditor);
    addAndMakeVisible(sampleRenameButton);
    addAndMakeVisible(sampleClearButton);
    addAndMakeVisible(samplePathLabel);
    addAndMakeVisible(slotLabel);
    addAndMakeVisible(slotSelector);
    addAndMakeVisible(pluginSelector);
    addAndMakeVisible(assignPluginButton);
    addAndMakeVisible(stepEditorTitle);
    addAndMakeVisible(selectedStepLabel);
    addAndMakeVisible(copyBlockButton);
    addAndMakeVisible(cutBlockButton);
    addAndMakeVisible(pasteBlockButton);
    addAndMakeVisible(transposeDownButton);
    addAndMakeVisible(transposeUpButton);
    addAndMakeVisible(applyFxToBlockButton);
    addAndMakeVisible(stepVelocityLabel);
    addAndMakeVisible(stepVelocitySlider);
    addAndMakeVisible(stepGateLabel);
    addAndMakeVisible(stepGateSlider);
    addAndMakeVisible(stepEffectCommandLabel);
    addAndMakeVisible(stepEffectCommandSlider);
    addAndMakeVisible(stepEffectCommandHexEditor);
    addAndMakeVisible(stepEffectValueLabel);
    addAndMakeVisible(stepEffectValueSlider);
    addAndMakeVisible(stepEffectValueHexEditor);
    addAndMakeVisible(keyboardOctaveLabel);
    addAndMakeVisible(keyboardOctaveSlider);
    addAndMakeVisible(editStepLabel);
    addAndMakeVisible(editStepSlider);
    addAndMakeVisible(gainLabel);
    addAndMakeVisible(gainSlider);
    addAndMakeVisible(attackLabel);
    addAndMakeVisible(attackSlider);
    addAndMakeVisible(releaseLabel);
    addAndMakeVisible(releaseSlider);
    addAndMakeVisible(slotActivityTitle);
    addAndMakeVisible(applyChannelMapButton);
    addAndMakeVisible(patternViewport);
    patternViewport.setViewedComponent(&patternGrid, false);
    patternViewport.setScrollBarsShown(true, true);
    patternViewport.setScrollBarThickness(12);

    // Create and set up panelWrapper for scrollable panel content
    panelWrapper = std::make_unique<PanelWrapper>();
    addAndMakeVisible(panelViewport);
    panelViewport.setViewedComponent(panelWrapper.get(), false);
    panelViewport.setScrollBarsShown(true, false);
    panelViewport.setScrollBarThickness(12);

    initPanelStyling();
    initChannelRows();
    initSlotActivityRows();

    // Reparent panel controls to panelWrapper for scrolling
    auto reparentToPanelWrapper = [this](juce::Component& comp) {
      removeChildComponent(&comp);
      panelWrapper->addAndMakeVisible(comp);
    };

    reparentToPanelWrapper(instrumentPanelTitle);
    reparentToPanelWrapper(songOrderTitle);
    reparentToPanelWrapper(songOrderListView);
    reparentToPanelWrapper(songOrderEntryLabel);
    reparentToPanelWrapper(songOrderEntrySelector);
    reparentToPanelWrapper(songOrderPatternLabel);
    reparentToPanelWrapper(songOrderPatternSelector);
    reparentToPanelWrapper(songOrderAddButton);
    reparentToPanelWrapper(songOrderRemoveButton);
    reparentToPanelWrapper(songOrderUpButton);
    reparentToPanelWrapper(songOrderDownButton);
    reparentToPanelWrapper(channelPanelTitle);
    reparentToPanelWrapper(applyChannelMapButton);
    for (auto& label : channelLabels) {
      reparentToPanelWrapper(*label);
    }
    for (auto& selector : channelInstrumentSelectors) {
      reparentToPanelWrapper(*selector);
    }
    for (auto& toggle : channelMuteToggles) {
      reparentToPanelWrapper(*toggle);
    }
    for (auto& label : channelPluginLabels) {
      reparentToPanelWrapper(*label);
    }
    reparentToPanelWrapper(slotPanelTitle);
    reparentToPanelWrapper(slotLabel);
    reparentToPanelWrapper(slotSelector);
    reparentToPanelWrapper(pluginPanelTitle);
    reparentToPanelWrapper(pluginSelector);
    reparentToPanelWrapper(assignPluginButton);
    reparentToPanelWrapper(pluginStatusLabel);
    reparentToPanelWrapper(sampleBankTitle);
    reparentToPanelWrapper(sampleSlotLabel);
    reparentToPanelWrapper(sampleSlotSelector);
    reparentToPanelWrapper(sampleLoadButton);
    reparentToPanelWrapper(sampleAssignButton);
    reparentToPanelWrapper(sampleAssignToChannelButton);
    reparentToPanelWrapper(sampleRenameEditor);
    reparentToPanelWrapper(sampleRenameButton);
    reparentToPanelWrapper(sampleClearButton);
    reparentToPanelWrapper(samplePathLabel);
    reparentToPanelWrapper(stepEditorTitle);
    reparentToPanelWrapper(selectedStepLabel);
    reparentToPanelWrapper(copyBlockButton);
    reparentToPanelWrapper(cutBlockButton);
    reparentToPanelWrapper(pasteBlockButton);
    reparentToPanelWrapper(transposeDownButton);
    reparentToPanelWrapper(transposeUpButton);
    reparentToPanelWrapper(applyFxToBlockButton);
    reparentToPanelWrapper(stepVelocityLabel);
    reparentToPanelWrapper(stepVelocitySlider);
    reparentToPanelWrapper(stepGateLabel);
    reparentToPanelWrapper(stepGateSlider);
    reparentToPanelWrapper(stepEffectCommandLabel);
    reparentToPanelWrapper(stepEffectCommandSlider);
    reparentToPanelWrapper(stepEffectCommandHexEditor);
    reparentToPanelWrapper(stepEffectValueLabel);
    reparentToPanelWrapper(stepEffectValueSlider);
    reparentToPanelWrapper(stepEffectValueHexEditor);
    reparentToPanelWrapper(keyboardOctaveLabel);
    reparentToPanelWrapper(keyboardOctaveSlider);
    reparentToPanelWrapper(editStepLabel);
    reparentToPanelWrapper(editStepSlider);
    reparentToPanelWrapper(gainLabel);
    reparentToPanelWrapper(gainSlider);
    reparentToPanelWrapper(attackLabel);
    reparentToPanelWrapper(attackSlider);
    reparentToPanelWrapper(releaseLabel);
    reparentToPanelWrapper(releaseSlider);
    reparentToPanelWrapper(slotActivityTitle);
    for (auto& label : slotActivityLabels) {
      reparentToPanelWrapper(*label);
    }
    for (auto& bar : slotActivityBars) {
      reparentToPanelWrapper(*bar);
    }

    patternGrid.setSelectionChangedCallback([this](int row, int channel) {
      selectedStepRow = row;
      selectedStepChannel = channel;
      updateStepEditorFromSelection();
    });
    patternGrid.setKeyboardStateChangedCallback([this](int octave, int step) {
      suppressKeyboardStateCallbacks = true;
      keyboardOctaveSlider.setValue(octave, juce::dontSendNotification);
      editStepSlider.setValue(step, juce::dontSendNotification);
      suppressKeyboardStateCallbacks = false;
    });
    patternGrid.setTogglePlaybackCallback([this]() {
      if (app.transport.isPlaying()) {
        app.transport.stop();
        app.transport.resetTickCount();
        app.sequencer.reset();
        app.plugins.allNotesOff();
        app.audio.allNotesOff();
      } else {
        app.transport.play();
      }
      updateStatusLabels();
      patternGrid.repaint();
    });
    refreshPluginChoices();
    refreshSlotSelector();
    refreshSampleSlotSelector();
    refreshChannelRows();
    refreshParameterSlidersFromSlot();
    refreshSampleSlotDetails();
    updateStepEditorFromSelection();
    patternGrid.setInsertDefaults(static_cast<std::uint32_t>(stepGateSlider.getValue()),
                    static_cast<std::uint8_t>(stepVelocitySlider.getValue()));
    patternGrid.setKeyboardOctave(static_cast<int>(keyboardOctaveSlider.getValue()));
    patternGrid.setEditStep(static_cast<int>(editStepSlider.getValue()));

    playButton.onClick = [this]() {
      app.lastSongModeRow = -1;  // Reset song mode row tracking
      app.transport.play();
      updateStatusLabels();
    };

    stopButton.onClick = [this]() {
      app.lastSongModeRow = -1;  // Reset song mode row tracking
      app.transport.stop();
      app.transport.resetTickCount();
      app.sequencer.reset();
      app.plugins.allNotesOff();
      app.audio.allNotesOff();
      updateStatusLabels();
      patternGrid.repaint();
    };

    loopButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.loopEnabled = !app.loopEnabled;
      updateLoopButtonText();
      updateStatusLabels();
    };

    playModePatternButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.playMode = PlayMode::PLAY_PATTERN;
      app.lastSongModeRow = -1;  // Reset row tracking
      updatePlayModeButtonStates();
      updateStatusLabels();
    };

    playModeSongButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.playMode = PlayMode::PLAY_SONG;
      app.lastSongModeRow = -1;  // Reset row tracking
      updatePlayModeButtonStates();
      updateStatusLabels();
    };

    patternSelector.onChange = [this]() {
      int patternIndex = patternSelector.getSelectedItemIndex();
      if (patternIndex >= 0) {
        {
          std::lock_guard<std::mutex> lock(app.stateMutex);
          app.module.switchToPattern(static_cast<std::size_t>(patternIndex));
          app.lastSongModeRow = -1;  // Reset row tracking when switching pattern
          app.currentPatternCache.store(app.module.currentPattern());
          app.patternCountCache.store(app.module.patternCount());
          app.currentSongOrderPositionCache.store(app.module.firstSongEntryForPattern(app.module.currentPattern()));
        }
        refreshPatternView();
      }
    };

    insertPatternBeforeButton.onClick = [this]() {
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.module.insertPatternBefore();
        app.currentPatternCache.store(app.module.currentPattern());
        app.patternCountCache.store(app.module.patternCount());
        app.currentSongOrderPositionCache.store(app.module.firstSongEntryForPattern(app.module.currentPattern()));
      }
      refreshPatternView();
    };

    insertPatternAfterButton.onClick = [this]() {
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.module.insertPatternAfter();
        app.currentPatternCache.store(app.module.currentPattern());
        app.patternCountCache.store(app.module.patternCount());
        app.currentSongOrderPositionCache.store(app.module.firstSongEntryForPattern(app.module.currentPattern()));
      }
      refreshPatternView();
    };

    removePatternButton.onClick = [this]() {
      bool removed = false;
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        removed = app.module.removeCurrentPattern();
        app.currentPatternCache.store(app.module.currentPattern());
        app.patternCountCache.store(app.module.patternCount());
        app.currentSongOrderPositionCache.store(app.module.firstSongEntryForPattern(app.module.currentPattern()));
      }
      if (removed) {
        refreshPatternView();
      }
    };

    songOrderEntrySelector.onChange = [this]() {
      const int selectedIndex = songOrderEntrySelector.getSelectedItemIndex();
      if (selectedIndex < 0) {
        return;
      }

      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.currentSongOrderPositionCache.store(static_cast<std::size_t>(selectedIndex));
        app.module.switchToPattern(app.module.songEntryAt(static_cast<std::size_t>(selectedIndex)));
        app.currentPatternCache.store(app.module.currentPattern());
      }
      refreshPatternView();
    };

    songOrderPatternSelector.onChange = [this]() {
      const int selectedOrderIndex = songOrderEntrySelector.getSelectedItemIndex();
      const int selectedPatternIndex = songOrderPatternSelector.getSelectedItemIndex();
      if (selectedOrderIndex < 0 || selectedPatternIndex < 0) {
        return;
      }

      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.module.setSongEntry(static_cast<std::size_t>(selectedOrderIndex), static_cast<std::size_t>(selectedPatternIndex));
        app.currentSongOrderPositionCache.store(static_cast<std::size_t>(selectedOrderIndex));
        app.module.switchToPattern(static_cast<std::size_t>(selectedPatternIndex));
        app.currentPatternCache.store(app.module.currentPattern());
        app.patternCountCache.store(app.module.patternCount());
      }
      refreshPatternView();
    };

    songOrderAddButton.onClick = [this]() {
      const std::size_t selectedOrderIndex = static_cast<std::size_t>(std::max(songOrderEntrySelector.getSelectedItemIndex(), 0));
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.module.insertSongEntry(selectedOrderIndex + 1, app.module.currentPattern());
        app.currentSongOrderPositionCache.store(std::min(selectedOrderIndex + 1, app.module.songLength() - 1));
      }
      refreshPatternView();
    };

    songOrderRemoveButton.onClick = [this]() {
      const std::size_t selectedOrderIndex = static_cast<std::size_t>(std::max(songOrderEntrySelector.getSelectedItemIndex(), 0));
      bool removed = false;
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        removed = app.module.removeSongEntry(selectedOrderIndex);
        if (removed) {
          const std::size_t clampedIndex = std::min(selectedOrderIndex, app.module.songLength() - 1);
          app.currentSongOrderPositionCache.store(clampedIndex);
          app.module.switchToPattern(app.module.songEntryAt(clampedIndex));
          app.currentPatternCache.store(app.module.currentPattern());
        }
      }
      if (removed) {
        refreshPatternView();
      }
    };

    songOrderUpButton.onClick = [this]() {
      const std::size_t selectedOrderIndex = static_cast<std::size_t>(std::max(songOrderEntrySelector.getSelectedItemIndex(), 0));
      bool moved = false;
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        moved = app.module.moveSongEntryUp(selectedOrderIndex);
        if (moved) {
          app.currentSongOrderPositionCache.store(selectedOrderIndex - 1);
        }
      }
      if (moved) {
        refreshPatternView();
      }
    };

    songOrderDownButton.onClick = [this]() {
      const std::size_t selectedOrderIndex = static_cast<std::size_t>(std::max(songOrderEntrySelector.getSelectedItemIndex(), 0));
      bool moved = false;
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        moved = app.module.moveSongEntryDown(selectedOrderIndex);
        if (moved) {
          app.currentSongOrderPositionCache.store(selectedOrderIndex + 1);
        }
      }
      if (moved) {
        refreshPatternView();
      }
    };

    helpButton.onClick = [this]() {
      juce::String msg =
        "=== TRANSPORT ===\n"
        "Play / Stop  — toolbar buttons or Space (grid focused)\n"
        "Loop         — toggle pattern looping\n"
        "Tempo slider — BPM (40–240)\n"
        "Ticks/Beat   — rows per beat (resolution)\n"
        "Effect 0Fxx  — set tempo to xx BPM at any row\n"
        "\n"
        "=== PATTERN GRID — NAVIGATION ===\n"
        "Click         — select cell\n"
        "Arrow keys    — move selection\n"
        "Tab / Shift+Tab — next / previous channel\n"
        "Return        — advance by step\n"
        "Home / End    — first / last row\n"
        "Page Up/Down  — jump 16 rows\n"
        "\n"
        "=== NOTE ENTRY ===\n"
        "1–9     high semitones (oct+1, semitones 0–8)\n"
        "Q–P     high whole tones (oct+1, white-key offsets)\n"
        "A–L     low semitones (oct, semitones 0–8)\n"
        "Z–.     low whole tones (oct, white-key offsets)\n"
        "+/-     octave up / down\n"
        "[ / ]   step size down / up\n"
        "Del/Bsp clear selected step\n"
        "Right-click  — clear step\n"
        "\n"
        "=== FX DIRECT ENTRY ===\n"
        "`       toggle FX mode (header shows [FX])\n"
        "        Type 3 hex chars: 1 command digit + 2 value digits\n"
        "        e.g. F80 → cmd=0F, val=80 (set tempo 128 BPM)\n"
        "        Row auto-advances after 3rd digit.\n"
        "Enter   commit partial buffer (zero-pads remaining digits)\n"
        "Bsp     remove last typed digit (or clear effect if empty)\n"
        "Esc     exit FX mode\n"
        "\n"
        "=== BLOCK EDITING ===\n"
        "Shift+Arrows / Click+Drag  — mark block\n"
        "Ctrl+C / X / V             — copy / cut / paste block\n"
        "Ctrl+Up / Down             — transpose ±1 semitone\n"
        "Ctrl+Shift+Up / Down       — transpose ±1 octave\n"
        "Panel: Copy / Cut / Paste / Transpose+/- buttons\n"
        "Panel: Apply FX to Block   — write current FX cmd/val to entire selection\n"
        "\n"
        "=== STEP EDITOR (right panel) ===\n"
        "Velocity    — note velocity (1–127)\n"
        "Gate        — note length in ticks (0 = sustain)\n"
        "FX Cmd      — effect command (slider or hex box)\n"
        "FX Val      — effect value   (slider or hex box)\n"
        "\n"
        "=== EFFECTS REFERENCE ===\n"
        "0xx  Arpeggio  x=hi semitone, x=lo semitone\n"
        "1xx  Slide up (pitch, per tick)\n"
        "2xx  Slide down\n"
        "3xx  Tone portamento (toward next note)\n"
        "4xx  Vibrato  hi=speed, lo=depth\n"
        "5xx  Tone portamento + volume slide\n"
        "6xx  Vibrato + volume slide\n"
        "9xx  Retrigger every xx ticks\n"
        "Axx  Volume slide  hi=up, lo=down nibbles\n"
        "Bxx  Jump to row xx\n"
        "Cxx  Set volume to xx\n"
        "Dxx  Pattern break (jump to row xx)\n"
        "E1x  Fine slide up\n"
        "E2x  Fine slide down\n"
        "E9x  Retrigger (sub-command)\n"
        "ECx  Note cut at tick x\n"
        "EDx  Note delay x ticks\n"
        "Fxx  Set tempo: xx<32 sets ticks/row, xx>=32 sets BPM\n"
        "\n"
        "=== INSTRUMENTS / SAMPLES ===\n"
        "Per-channel instrument selector — fallback target when no sample slot is armed\n"
        "Sample bank selector — arms the selected sample slot for new notes\n"
        "Mute toggle — mute channel\n"
        "Slot Editor — load WAV, adjust gain / attack / release\n"
        "Assign Plugin — load CLAP/LV2/internal plugin to slot\n";

      juce::AlertWindow::showMessageBoxAsync(
          juce::MessageBoxIconType::InfoIcon,
          "exTracker — Usage Reference",
          msg,
          "OK",
          this);
    };

    gridDensityButton.onClick = [this]() {
      patternGrid.setCompactDensity(!patternGrid.isCompactDensity());
      updateGridDensityButtonText();
      resized();
    };

    tempoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
    tempoSlider.setRange(40.0, 240.0, 0.5);
    tempoSlider.setValue(app.transport.tempoBpm(), juce::dontSendNotification);
    tempoSlider.onValueChange = [this]() {
      app.transport.setTempoBpm(tempoSlider.getValue());
      updateStatusLabels();
    };

    ticksPerBeatSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    ticksPerBeatSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
    ticksPerBeatSlider.setRange(1.0, 24.0, 1.0);
    ticksPerBeatSlider.setValue(static_cast<double>(app.transport.ticksPerBeat()), juce::dontSendNotification);
    ticksPerBeatSlider.onValueChange = [this]() {
      app.transport.setTicksPerBeat(static_cast<std::uint32_t>(std::lround(ticksPerBeatSlider.getValue())));
      updateStatusLabels();
    };

    ticksPerBeatLabel.setText("TPB", juce::dontSendNotification);
    ticksPerBeatLabel.setJustificationType(juce::Justification::centredLeft);

    expandPatternButton.onClick = [this]() { expandPattern(); };
    shrinkPatternButton.onClick = [this]() { shrinkPattern(); };
    expandChannelButton.onClick = [this]() { expandChannel(); };
    shrinkChannelButton.onClick = [this]() { shrinkChannel(); };
    savePatternButton.onClick = [this]() { savePattern(); };
    loadPatternButton.onClick = [this]() { loadPattern(); };
    insertRowButton.onClick = [this]() { insertRowAtSelection(); };
    removeRowButton.onClick = [this]() { removeRowAtSelection(); };
    copyBlockButton.onClick = [this]() { patternGrid.copySelection(); };
    cutBlockButton.onClick = [this]() { patternGrid.cutSelection(); };
    pasteBlockButton.onClick = [this]() { patternGrid.pasteSelection(); };
    transposeDownButton.onClick = [this]() { patternGrid.transposeSelectionDown(false); };
    transposeUpButton.onClick = [this]() { patternGrid.transposeSelectionUp(false); };
    applyFxToBlockButton.onClick = [this]() {
      auto cmd = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(stepEffectCommandSlider.getValue())), 0, 255));
      auto val = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(stepEffectValueSlider.getValue())), 0, 255));
      patternGrid.applyEffectToSelection(cmd, val);
    };

    tempoLabel.setText("Tempo", juce::dontSendNotification);
    tempoLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setJustificationType(juce::Justification::centredRight);
    updateGridDensityButtonText();

    slotSelector.onChange = [this]() {
      refreshParameterSlidersFromSlot();
      updateStatusLabels();
    };

    assignPluginButton.onClick = [this]() {
      int slot = getSelectedSlot();
      if (slot < 0) {
        return;
      }

      std::string pluginId = pluginSelector.getText().toStdString();
      if (pluginId.empty()) {
        return;
      }

      assignPluginButton.setEnabled(false);
      assignPluginButton.setButtonText("Assigning...");
      slotSelector.setEnabled(false);
      pluginSelector.setEnabled(false);

      bool assigned = false;
      bool loaded = app.plugins.loadPlugin(pluginId);
      assigned = loaded && app.plugins.assignInstrument(static_cast<std::uint8_t>(slot), pluginId);

      pluginStatusLabel.setText(
          assigned ? "Assigned: " + juce::String(pluginId)
                   : "Assign failed for: " + juce::String(pluginId),
          juce::dontSendNotification);
      refreshSlotSelector();
      refreshParameterSlidersFromSlot();
      assignPluginButton.setButtonText("Assign Plugin To Slot");
      assignPluginButton.setEnabled(true);
      slotSelector.setEnabled(true);
      pluginSelector.setEnabled(true);
    };

    sampleSlotSelector.onChange = [this]() {
      syncActiveSampleWriteSlot();
      refreshSampleSlotDetails();
    };

    auto applySelectedSampleName = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0) {
        pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
        return;
      }

      if (app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot)).empty()) {
        pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
        refreshSampleSlotDetails();
        return;
      }

      const juce::String trimmedName = sampleRenameEditor.getText().trim();
      if (trimmedName.isEmpty()) {
        pluginStatusLabel.setText("Sample name cannot be empty", juce::dontSendNotification);
        refreshSampleSlotDetails();
        return;
      }

      app.plugins.setSampleNameForSlot(static_cast<std::uint16_t>(selectedSampleSlot), trimmedName.toStdString());
      pluginStatusLabel.setText("Renamed " + formatSampleSlotHex(selectedSampleSlot) + " to \"" + trimmedName + "\"",
                                juce::dontSendNotification);
      refreshSampleSlotSelector();
      sampleSlotSelector.setSelectedId(selectedSampleSlot + 1, juce::dontSendNotification);
      refreshSampleSlotDetails();
      patternGrid.repaint();
    };
    sampleRenameEditor.onReturnKey = applySelectedSampleName;
    sampleRenameEditor.onFocusLost = applySelectedSampleName;
    sampleRenameButton.onClick = applySelectedSampleName;

    sampleLoadButton.onClick = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0) {
        return;
      }

      const juce::File initialFolder = lastSampleLoadFolder.isDirectory()
          ? lastSampleLoadFolder
          : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
      sampleFileChooser = std::make_unique<juce::FileChooser>("Load WAV Sample", initialFolder, "*.wav;*.wave");
      constexpr int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
      sampleFileChooser->launchAsync(flags, [this, selectedSampleSlot](const juce::FileChooser& chooser) {
        const juce::File file = chooser.getResult();
        sampleFileChooser.reset();

        if (!file.existsAsFile()) {
          return;
        }

        lastSampleLoadFolder = file.getParentDirectory();

        const bool loaded = app.plugins.loadSampleToSlot(
            static_cast<std::uint16_t>(selectedSampleSlot),
            file.getFullPathName().toStdString());
    if (loaded) {
      app.plugins.setSampleNameForSlot(
        static_cast<std::uint16_t>(selectedSampleSlot),
        std::filesystem::path(file.getFullPathName().toStdString()).stem().string());
    }
        const juce::String statusText = loaded
            ? "Loaded sample to " + formatSampleSlotHex(selectedSampleSlot)
            : "Failed to load sample to " + formatSampleSlotHex(selectedSampleSlot);

        syncActiveSampleWriteSlot();
        pluginStatusLabel.setText(statusText, juce::dontSendNotification);
        refreshSampleSlotSelector();
        refreshChannelPluginLabels();
        refreshSampleSlotDetails();
        patternGrid.repaint();
      });
    };

    sampleAssignButton.onClick = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0) {
        pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
        return;
      }

      if (selectedSampleSlot > 255) {
        pluginStatusLabel.setText("Preview supports sample slots 0-255", juce::dontSendNotification);
        return;
      }

      if (app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot)).empty()) {
        pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
        return;
      }

      const int previewNote = static_cast<int>(std::lround(keyboardOctaveSlider.getValue())) * 12 + 12;
      const bool started = app.plugins.triggerNoteOn(static_cast<std::uint8_t>(selectedSampleSlot),
                                                     previewNote,
                                                     127,
                                                     true);
      pluginStatusLabel.setText(
          started ? "Previewing " + formatSampleSlotHex(selectedSampleSlot) + " at MIDI note " + juce::String(previewNote)
                  : "Failed previewing " + formatSampleSlotHex(selectedSampleSlot),
          juce::dontSendNotification);
      refreshSampleSlotDetails();
    };

    sampleAssignToChannelButton.onClick = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0) {
        pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
        return;
      }

      if (selectedSampleSlot > 255) {
        pluginStatusLabel.setText("Preview supports sample slots 0-255", juce::dontSendNotification);
        return;
      }

      const int previewNote = static_cast<int>(std::lround(keyboardOctaveSlider.getValue())) * 12 + 12;
      const bool stopped = app.plugins.triggerNoteOff(static_cast<std::uint8_t>(selectedSampleSlot),
                                                      previewNote);
      pluginStatusLabel.setText(
          stopped ? "Stopped preview for " + formatSampleSlotHex(selectedSampleSlot)
                  : "No active preview for " + formatSampleSlotHex(selectedSampleSlot),
          juce::dontSendNotification);
      refreshSampleSlotDetails();
    };

    sampleClearButton.onClick = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0) {
        return;
      }

      const bool cleared = app.plugins.clearSampleSlot(static_cast<std::uint16_t>(selectedSampleSlot));
      syncActiveSampleWriteSlot();
      pluginStatusLabel.setText(
          cleared ? "Cleared sample slot " + formatSampleSlotHex(selectedSampleSlot)
            : "Failed clearing sample slot " + formatSampleSlotHex(selectedSampleSlot),
          juce::dontSendNotification);
      refreshSlotSelector();
      refreshChannelPluginLabels();
      refreshSampleSlotDetails();
      patternGrid.repaint();
    };

    gainSlider.onValueChange = [this]() { setSelectedSlotParameter("gain", gainSlider.getValue()); };
    attackSlider.onValueChange = [this]() { setSelectedSlotParameter("attack_ms", attackSlider.getValue()); };
    releaseSlider.onValueChange = [this]() { setSelectedSlotParameter("release_ms", releaseSlider.getValue()); };
    stepVelocitySlider.onValueChange = [this]() {
      if (!suppressStepSliderCallbacks) {
        pendingStepVelocity = static_cast<int>(std::lround(stepVelocitySlider.getValue()));
        patternGrid.setInsertDefaults(static_cast<std::uint32_t>(std::max(stepGateSlider.getValue(), 0.0)),
                                      static_cast<std::uint8_t>(std::max(stepVelocitySlider.getValue(), 1.0)));
      }
    };
    stepGateSlider.onValueChange = [this]() {
      if (!suppressStepSliderCallbacks) {
        pendingStepGate = static_cast<int>(std::lround(stepGateSlider.getValue()));
        patternGrid.setInsertDefaults(static_cast<std::uint32_t>(std::max(stepGateSlider.getValue(), 0.0)),
                                      static_cast<std::uint8_t>(std::max(stepVelocitySlider.getValue(), 1.0)));
      }
    };
    stepEffectCommandSlider.onValueChange = [this]() {
      if (!suppressStepSliderCallbacks) {
        pendingStepEffectCommand = static_cast<int>(std::lround(stepEffectCommandSlider.getValue()));
        suppressStepEffectTextCallbacks = true;
        stepEffectCommandHexEditor.setText(formatHexByte(pendingStepEffectCommand), false);
        suppressStepEffectTextCallbacks = false;
      }
    };
    stepEffectValueSlider.onValueChange = [this]() {
      if (!suppressStepSliderCallbacks) {
        pendingStepEffectValue = static_cast<int>(std::lround(stepEffectValueSlider.getValue()));
        suppressStepEffectTextCallbacks = true;
        stepEffectValueHexEditor.setText(formatHexByte(pendingStepEffectValue), false);
        suppressStepEffectTextCallbacks = false;
      }
    };
    auto applyEffectCommandHexText = [this]() {
      if (suppressStepEffectTextCallbacks) {
        return;
      }
      int parsed = 0;
      if (!parseHexByte(stepEffectCommandHexEditor.getText(), parsed)) {
        suppressStepEffectTextCallbacks = true;
        stepEffectCommandHexEditor.setText(formatHexByte(static_cast<int>(std::lround(stepEffectCommandSlider.getValue()))), false);
        suppressStepEffectTextCallbacks = false;
        return;
      }

      pendingStepEffectCommand = parsed;
      suppressStepSliderCallbacks = true;
      stepEffectCommandSlider.setValue(static_cast<double>(parsed), juce::dontSendNotification);
      suppressStepSliderCallbacks = false;
      suppressStepEffectTextCallbacks = true;
      stepEffectCommandHexEditor.setText(formatHexByte(parsed), false);
      suppressStepEffectTextCallbacks = false;
    };
    auto applyEffectValueHexText = [this]() {
      if (suppressStepEffectTextCallbacks) {
        return;
      }
      int parsed = 0;
      if (!parseHexByte(stepEffectValueHexEditor.getText(), parsed)) {
        suppressStepEffectTextCallbacks = true;
        stepEffectValueHexEditor.setText(formatHexByte(static_cast<int>(std::lround(stepEffectValueSlider.getValue()))), false);
        suppressStepEffectTextCallbacks = false;
        return;
      }

      pendingStepEffectValue = parsed;
      suppressStepSliderCallbacks = true;
      stepEffectValueSlider.setValue(static_cast<double>(parsed), juce::dontSendNotification);
      suppressStepSliderCallbacks = false;
      suppressStepEffectTextCallbacks = true;
      stepEffectValueHexEditor.setText(formatHexByte(parsed), false);
      suppressStepEffectTextCallbacks = false;
    };
    stepEffectCommandHexEditor.onReturnKey = applyEffectCommandHexText;
    stepEffectCommandHexEditor.onFocusLost = applyEffectCommandHexText;
    stepEffectValueHexEditor.onReturnKey = applyEffectValueHexText;
    stepEffectValueHexEditor.onFocusLost = applyEffectValueHexText;
    keyboardOctaveSlider.onValueChange = [this]() {
      if (suppressKeyboardStateCallbacks) {
        return;
      }
      patternGrid.setKeyboardOctave(static_cast<int>(std::lround(keyboardOctaveSlider.getValue())));
    };
    editStepSlider.onValueChange = [this]() {
      if (suppressKeyboardStateCallbacks) {
        return;
      }
      patternGrid.setEditStep(static_cast<int>(std::lround(editStepSlider.getValue())));
    };

    applyChannelMapButton.onClick = [this]() {
      flushPendingChannelInstrumentAssignments();
      {
        std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
          pluginStatusLabel.setText("Apply channel map skipped (engine busy)", juce::dontSendNotification);
          return;
        }
        int numRows = static_cast<int>(app.module.currentEditor().rows());
        int numChannels = static_cast<int>(app.module.currentEditor().channels());
        for (int row = 0; row < numRows; ++row) {
          for (int ch = 0; ch < numChannels; ++ch) {
            if (!app.module.currentEditor().hasNoteAt(row, ch)) {
              continue;
            }
            if (static_cast<std::size_t>(ch) < app.channelInstruments.size()) {
              app.module.currentEditor().setInstrument(row, ch, app.channelInstruments[static_cast<std::size_t>(ch)]);
            }
          }
        }
      }
      patternGrid.repaint();
      refreshChannelPluginLabels();
    };

    updateLoopButtonText();
    updatePlayModeButtonStates();
    updateStatusLabels();
    refreshPatternView();
    startTimerHz(60);
  }

  void refreshPatternView() {
    updatePatternSelector();
    refreshSongOrderEditor();
    patternGrid.recalculateGridSize();
    patternGrid.refreshSnapshotForPatternChange();
    patternGrid.repaint();
  }

  void handlePatternChangedFromPlayback() {
    refreshPatternView();
  }

  void resized() override {
    auto area = getLocalBounds();
    auto toolbar1 = area.removeFromTop(32).reduced(8, 4);
    auto toolbar2 = area.removeFromTop(32).reduced(8, 4);

    // Toolbar 1: Play controls, Pattern controls
    playButton.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(8);
    stopButton.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(8);
    loopButton.setBounds(toolbar1.removeFromLeft(110));
    toolbar1.removeFromLeft(8);
    playModePatternButton.setBounds(toolbar1.removeFromLeft(75));
    toolbar1.removeFromLeft(4);
    playModeSongButton.setBounds(toolbar1.removeFromLeft(60));
    toolbar1.removeFromLeft(12);
    patternLabel.setBounds(toolbar1.removeFromLeft(180));
    toolbar1.removeFromLeft(6);
    patternSelector.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(6);
    insertPatternBeforeButton.setBounds(toolbar1.removeFromLeft(95));
    toolbar1.removeFromLeft(4);
    insertPatternAfterButton.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(4);
    removePatternButton.setBounds(toolbar1.removeFromLeft(100));
    toolbar1.removeFromLeft(12);
    helpButton.setBounds(toolbar1.removeFromLeft(60));

    // Toolbar 2: Grid, Tempo, TPB, Pattern controls, Save/Load, Row controls
    gridDensityButton.setBounds(toolbar2.removeFromLeft(132));
    toolbar2.removeFromLeft(12);
    tempoLabel.setBounds(toolbar2.removeFromLeft(55));
    tempoSlider.setBounds(toolbar2.removeFromLeft(220));
    toolbar2.removeFromLeft(10);
    ticksPerBeatLabel.setBounds(toolbar2.removeFromLeft(36));
    ticksPerBeatSlider.setBounds(toolbar2.removeFromLeft(120));
    toolbar2.removeFromLeft(10);
    expandPatternButton.setBounds(toolbar2.removeFromLeft(86));
    toolbar2.removeFromLeft(4);
    shrinkPatternButton.setBounds(toolbar2.removeFromLeft(86));
    toolbar2.removeFromLeft(4);
    expandChannelButton.setBounds(toolbar2.removeFromLeft(46));
    toolbar2.removeFromLeft(4);
    shrinkChannelButton.setBounds(toolbar2.removeFromLeft(46));
    toolbar2.removeFromLeft(6);
    savePatternButton.setBounds(toolbar2.removeFromLeft(90));
    toolbar2.removeFromLeft(4);
    loadPatternButton.setBounds(toolbar2.removeFromLeft(90));
    toolbar2.removeFromLeft(4);
    insertRowButton.setBounds(toolbar2.removeFromLeft(86));
    toolbar2.removeFromLeft(4);
    removeRowButton.setBounds(toolbar2.removeFromLeft(96));
    statusLabel.setBounds(toolbar2);

    auto contentArea = area.reduced(8, 8);
      auto panelViewportBounds = contentArea.removeFromRight(320);
    patternViewport.setBounds(contentArea);
    panelViewport.setBounds(panelViewportBounds);
    panelViewport.setViewPosition(0, panelViewport.getViewPositionY());

    // Create a rectangle for panel layout that extends beyond viewport height for scrolling
    auto panelArea = juce::Rectangle<int>(0, 0, panelViewportBounds.getWidth(), 1760);

    // Set panelWrapper size to accommodate all content
    panelWrapper->setBounds(panelArea);

    const int gridColumns = static_cast<int>(app.module.currentEditor().channels());
    const int gridRows = static_cast<int>(app.module.currentEditor().rows());
    const int gridWidth = std::max(contentArea.getWidth(),
                     patternGrid.preferredLabelWidth() +
                     gridColumns * patternGrid.preferredCellWidth());
    const int gridHeight = std::max(contentArea.getHeight(),
                    patternGrid.preferredHeaderHeight() +
                    gridRows * (patternGrid.isCompactDensity() ? 16 : 20));
    patternGrid.setBounds(0, 0, gridWidth, gridHeight);

    songOrderTitle.setBounds(panelArea.removeFromTop(28));
    panelArea.removeFromTop(6);

    songOrderListView.setBounds(panelArea.removeFromTop(92));
    panelArea.removeFromTop(6);

    auto songEntryRow = panelArea.removeFromTop(26);
    songOrderEntryLabel.setBounds(songEntryRow.removeFromLeft(52));
    songOrderEntrySelector.setBounds(songEntryRow);

    panelArea.removeFromTop(4);
    auto songPatternRow = panelArea.removeFromTop(26);
    songOrderPatternLabel.setBounds(songPatternRow.removeFromLeft(52));
    songOrderPatternSelector.setBounds(songPatternRow);

    panelArea.removeFromTop(4);
    auto songButtonRow1 = panelArea.removeFromTop(26);
    songOrderAddButton.setBounds(songButtonRow1.removeFromLeft(152));
    songButtonRow1.removeFromLeft(6);
    songOrderRemoveButton.setBounds(songButtonRow1.removeFromLeft(152));

    panelArea.removeFromTop(4);
    auto songButtonRow2 = panelArea.removeFromTop(26);
    songOrderUpButton.setBounds(songButtonRow2.removeFromLeft(152));
    songButtonRow2.removeFromLeft(6);
    songOrderDownButton.setBounds(songButtonRow2.removeFromLeft(152));

    panelArea.removeFromTop(12);
    instrumentPanelTitle.setBounds(panelArea.removeFromTop(28));
    panelArea.removeFromTop(6);

    channelPanelTitle.setBounds(panelArea.removeFromTop(22));
    panelArea.removeFromTop(4);

    int channelCount = static_cast<int>(channelLabels.size());
    for (int i = 0; i < channelCount; ++i) {
      auto row = panelArea.removeFromTop(24);
      channelLabels[static_cast<std::size_t>(i)]->setBounds(row.removeFromLeft(40));
      channelInstrumentSelectors[static_cast<std::size_t>(i)]->setBounds(row.removeFromLeft(40));
      row.removeFromLeft(4);
      channelMuteToggles[static_cast<std::size_t>(i)]->setBounds(row.removeFromLeft(56));
      row.removeFromLeft(8);
      channelPluginLabels[static_cast<std::size_t>(i)]->setBounds(row);
      panelArea.removeFromTop(2);
    }

    panelArea.removeFromTop(6);
    applyChannelMapButton.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(10);

    slotPanelTitle.setBounds(panelArea.removeFromTop(22));
    panelArea.removeFromTop(4);

    auto slotRow = panelArea.removeFromTop(26);
    slotLabel.setBounds(slotRow.removeFromLeft(45));
    slotSelector.setBounds(slotRow.removeFromLeft(120));

    panelArea.removeFromTop(8);
    pluginPanelTitle.setBounds(panelArea.removeFromTop(22));
    panelArea.removeFromTop(4);

    pluginSelector.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    assignPluginButton.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(6);
    pluginStatusLabel.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(8);
    sampleBankTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);

    auto sampleSlotRow = panelArea.removeFromTop(26);
    sampleSlotLabel.setBounds(sampleSlotRow.removeFromLeft(80));
    sampleSlotSelector.setBounds(sampleSlotRow);

    panelArea.removeFromTop(4);
    auto sampleButtonRow = panelArea.removeFromTop(26);
    sampleLoadButton.setBounds(sampleButtonRow.removeFromLeft(98));
    sampleButtonRow.removeFromLeft(4);
    sampleAssignButton.setBounds(sampleButtonRow.removeFromLeft(110));
    sampleButtonRow.removeFromLeft(4);
    sampleAssignToChannelButton.setBounds(sampleButtonRow.removeFromLeft(110));

    panelArea.removeFromTop(4);
    auto sampleRenameRow = panelArea.removeFromTop(26);
    sampleRenameEditor.setBounds(sampleRenameRow.removeFromLeft(206));
    sampleRenameRow.removeFromLeft(4);
    sampleRenameButton.setBounds(sampleRenameRow);

    panelArea.removeFromTop(4);
    auto sampleButtonRow2 = panelArea.removeFromTop(26);
    sampleClearButton.setBounds(sampleButtonRow2);

    panelArea.removeFromTop(4);
    samplePathLabel.setBounds(panelArea.removeFromTop(44));

    panelArea.removeFromTop(8);
    stepEditorTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    selectedStepLabel.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);

    auto blockEditRow = panelArea.removeFromTop(26);
    copyBlockButton.setBounds(blockEditRow.removeFromLeft(100));
    blockEditRow.removeFromLeft(4);
    cutBlockButton.setBounds(blockEditRow.removeFromLeft(100));
    blockEditRow.removeFromLeft(4);
    pasteBlockButton.setBounds(blockEditRow.removeFromLeft(100));

    panelArea.removeFromTop(4);
    auto transposeRow = panelArea.removeFromTop(26);
    transposeDownButton.setBounds(transposeRow.removeFromLeft(152));
    transposeRow.removeFromLeft(6);
    transposeUpButton.setBounds(transposeRow.removeFromLeft(152));
    panelArea.removeFromTop(4);
    stepVelocityLabel.setBounds(panelArea.removeFromTop(20));
    stepVelocitySlider.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(6);
    stepGateLabel.setBounds(panelArea.removeFromTop(20));
    stepGateSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(6);
    stepEffectCommandLabel.setBounds(panelArea.removeFromTop(20));
    {
      auto row = panelArea.removeFromTop(24);
      stepEffectCommandSlider.setBounds(row.removeFromLeft(std::max(10, row.getWidth() - 56)));
      row.removeFromLeft(4);
      stepEffectCommandHexEditor.setBounds(row);
    }
    panelArea.removeFromTop(6);
    stepEffectValueLabel.setBounds(panelArea.removeFromTop(20));
    {
      auto row = panelArea.removeFromTop(24);
      stepEffectValueSlider.setBounds(row.removeFromLeft(std::max(10, row.getWidth() - 56)));
      row.removeFromLeft(4);
      stepEffectValueHexEditor.setBounds(row);
    }

    panelArea.removeFromTop(4);
    applyFxToBlockButton.setBounds(panelArea.removeFromTop(26));

    panelArea.removeFromTop(6);
    keyboardOctaveLabel.setBounds(panelArea.removeFromTop(20));
    keyboardOctaveSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(6);
    editStepLabel.setBounds(panelArea.removeFromTop(20));
    editStepSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(12);
    gainLabel.setBounds(panelArea.removeFromTop(20));
    gainSlider.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(6);
    attackLabel.setBounds(panelArea.removeFromTop(20));
    attackSlider.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(6);
    releaseLabel.setBounds(panelArea.removeFromTop(20));
    releaseSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(8);
    slotActivityTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);

    for (std::size_t i = 0; i < slotActivityLabels.size(); i += 2) {
      auto row = panelArea.removeFromTop(18);
      auto left = row.removeFromLeft(panelArea.getWidth() / 2);
      auto leftLabel = left.removeFromLeft(static_cast<int>(left.getWidth() * 0.72f));
      slotActivityLabels[i]->setBounds(leftLabel);
      if (i < slotActivityBars.size()) {
        slotActivityBars[i]->setBounds(left.reduced(2, 4));
      }
      if (i + 1 < slotActivityLabels.size()) {
        auto rightLabel = row.removeFromLeft(static_cast<int>(row.getWidth() * 0.72f));
        slotActivityLabels[i + 1]->setBounds(rightLabel);
        if (i + 1 < slotActivityBars.size()) {
          slotActivityBars[i + 1]->setBounds(row.reduced(2, 4));
        }
      }
      panelArea.removeFromTop(2);
    }
  }

  void visibilityChanged() override {
    if (!isShowing()) {
      return;
    }

    refreshChannelPluginLabels();
    refreshSlotActivityLabels();
    updateStatusLabels();
    patternGrid.repaint();
  }

private:
  void writeStep(int row, int channel, const extracker::PatternEditor::Step& step) {
    if (!step.hasNote) {
      app.module.currentEditor().clearStep(row, channel);
      return;
    }

    app.module.currentEditor().insertNote(row,
                          channel,
                          step.note,
                          step.instrument,
                          step.gateTicks,
                          step.velocity,
                          step.retrigger,
                          step.effectCommand,
                          step.effectValue);
  }

  extracker::PatternEditor::Step readStep(int row, int channel) const {
    extracker::PatternEditor::Step step;
    step.hasNote = app.module.currentEditor().hasNoteAt(row, channel);
    if (!step.hasNote) {
      return step;
    }

    step.note = app.module.currentEditor().noteAt(row, channel);
    step.instrument = app.module.currentEditor().instrumentAt(row, channel);
    step.gateTicks = app.module.currentEditor().gateTicksAt(row, channel);
    step.velocity = app.module.currentEditor().velocityAt(row, channel);
    step.retrigger = app.module.currentEditor().retriggerAt(row, channel);
    step.effectCommand = app.module.currentEditor().effectCommandAt(row, channel);
    step.effectValue = app.module.currentEditor().effectValueAt(row, channel);
    return step;
  }

  void expandPattern() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Expand skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    const int rows = static_cast<int>(app.module.currentEditor().rows());
    const int channels = static_cast<int>(app.module.currentEditor().channels());
    const int targetRows = std::min(rows * 2, 128);
    if (targetRows <= rows) {
      pluginStatusLabel.setText("Pattern already at max length (128)", juce::dontSendNotification);
      return;
    }

    std::vector<extracker::PatternEditor::Step> snapshot(static_cast<std::size_t>(rows * channels));

    for (int row = 0; row < rows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        snapshot[static_cast<std::size_t>(row * channels + channel)] = readStep(row, channel);
      }
    }

    app.module.currentEditor().resizeRows(static_cast<std::size_t>(targetRows));

    for (int row = 0; row < targetRows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        app.module.currentEditor().clearStep(row, channel);
      }
    }

    for (int row = 0; row < rows; ++row) {
      int dstRow = row * 2;
      if (dstRow >= targetRows) {
        continue;
      }
      for (int channel = 0; channel < channels; ++channel) {
        const auto& step = snapshot[static_cast<std::size_t>(row * channels + channel)];
        writeStep(dstRow, channel, step);
      }
    }

    lock.unlock();
    app.transport.setPatternRows(static_cast<std::uint32_t>(targetRows));
    app.transport.resetTickCount();
    app.sequencer.reset();
    resized();
    patternGrid.clampSelectionToBounds();
    updateStepEditorFromSelection();
    patternGrid.repaint();
    pluginStatusLabel.setText("Pattern expanded to " + juce::String(targetRows) + " rows", juce::dontSendNotification);
  }

  void shrinkPattern() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Shrink skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    const int rows = static_cast<int>(app.module.currentEditor().rows());
    const int channels = static_cast<int>(app.module.currentEditor().channels());
    const int targetRows = std::max(rows / 2, 16);
    if (targetRows >= rows) {
      pluginStatusLabel.setText("Pattern already at minimum length", juce::dontSendNotification);
      return;
    }

    std::vector<extracker::PatternEditor::Step> snapshot(static_cast<std::size_t>(rows * channels));

    for (int row = 0; row < rows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        snapshot[static_cast<std::size_t>(row * channels + channel)] = readStep(row, channel);
      }
    }

    app.module.currentEditor().resizeRows(static_cast<std::size_t>(targetRows));

    for (int row = 0; row < targetRows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        app.module.currentEditor().clearStep(row, channel);
      }
    }

    for (int row = 0; row < rows && (row / 2) < targetRows; row += 2) {
      int dstRow = row / 2;
      for (int channel = 0; channel < channels; ++channel) {
        const auto& step = snapshot[static_cast<std::size_t>(row * channels + channel)];
        writeStep(dstRow, channel, step);
      }
    }

    lock.unlock();
    app.transport.setPatternRows(static_cast<std::uint32_t>(targetRows));
    app.transport.resetTickCount();
    app.sequencer.reset();
    selectedStepRow = std::clamp(selectedStepRow, 0, targetRows - 1);
    resized();
    patternGrid.clampSelectionToBounds();
    updateStepEditorFromSelection();
    patternGrid.repaint();
    pluginStatusLabel.setText("Pattern shrunk to " + juce::String(targetRows) + " rows", juce::dontSendNotification);
  }

  void expandChannel() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Expand channel skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    const int channels = static_cast<int>(app.module.currentEditor().channels());
    const int maxChannels = 16;
    const int newChannels = channels + 1;
    if (newChannels > maxChannels) {
      pluginStatusLabel.setText("Already at maximum channels (" + juce::String(maxChannels) + ")", juce::dontSendNotification);
      return;
    }

    app.module.currentEditor().resizeChannels(static_cast<std::size_t>(newChannels));
    app.channelInstruments.resize(static_cast<std::size_t>(newChannels), 0);
    app.channelInstruments[static_cast<std::size_t>(newChannels - 1)] = 0;
    app.channelMuted.resize(static_cast<std::size_t>(newChannels), false);

    lock.unlock();
    selectedStepChannel = std::clamp(selectedStepChannel, 0, newChannels - 1);
    reinitChannelRows();
    patternGrid.recalculateGridSize();
    resized();
    patternGrid.clampSelectionToBounds();
    updateStepEditorFromSelection();
    patternGrid.repaint();
    pluginStatusLabel.setText("Channel expanded to " + juce::String(newChannels) + " channels", juce::dontSendNotification);
  }

  void shrinkChannel() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Shrink channel skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    const int channels = static_cast<int>(app.module.currentEditor().channels());
    const int minChannels = 1;
    const int newChannels = channels - 1;
    if (newChannels < minChannels) {
      pluginStatusLabel.setText("Already at minimum channels (" + juce::String(minChannels) + ")", juce::dontSendNotification);
      return;
    }

    app.module.currentEditor().resizeChannels(static_cast<std::size_t>(newChannels));
    if (static_cast<std::size_t>(newChannels) < app.channelInstruments.size()) {
      app.channelInstruments.resize(static_cast<std::size_t>(newChannels), 0);
    }
    if (static_cast<std::size_t>(newChannels) < app.channelMuted.size()) {
      app.channelMuted.resize(static_cast<std::size_t>(newChannels), false);
    }

    lock.unlock();
    selectedStepChannel = std::clamp(selectedStepChannel, 0, newChannels - 1);
    reinitChannelRows();
    patternGrid.recalculateGridSize();
    resized();
    patternGrid.clampSelectionToBounds();
    updateStepEditorFromSelection();
    patternGrid.repaint();
    pluginStatusLabel.setText("Channel shrunk to " + juce::String(newChannels) + " channels", juce::dontSendNotification);
  }

  void savePattern() {
    savePatternFileChooser = std::make_unique<juce::FileChooser>("Save song/module file",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xtp");
    savePatternFileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser) {
          const juce::File selectedFile = chooser.getResult();
          savePatternFileChooser.reset();
          if (selectedFile != juce::File()) {
            const std::string filePath = selectedFile.getFullPathName().toStdString();
            if (app.savePatternToFile(filePath)) {
              pluginStatusLabel.setText("Song saved: " + selectedFile.getFileName(), juce::dontSendNotification);
            } else {
              pluginStatusLabel.setText("Failed to save song", juce::dontSendNotification);
            }
          }
        });
  }

  void loadPattern() {
    loadPatternFileChooser = std::make_unique<juce::FileChooser>("Load song/module file",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.xtp");
    loadPatternFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode,
        [this](const juce::FileChooser& chooser) {
          const juce::File selectedFile = chooser.getResult();
          loadPatternFileChooser.reset();
          if (selectedFile != juce::File()) {
            const std::string filePath = selectedFile.getFullPathName().toStdString();
            if (app.loadPatternFromFile(filePath)) {
              refreshPatternView();
              patternGrid.clampSelectionToBounds();
              updateStepEditorFromSelection();
              refreshSlotSelector();
              refreshChannelRows();
              refreshSampleSlotSelector();
              refreshSampleSlotDetails();
              pluginStatusLabel.setText("Song loaded: " + selectedFile.getFileName(), juce::dontSendNotification);
            } else {
              pluginStatusLabel.setText("Failed to load song", juce::dontSendNotification);
            }
          }
        });
  }

  void insertRowAtSelection() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Insert row skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    const int rows = static_cast<int>(app.module.currentEditor().rows());
    const int channels = static_cast<int>(app.module.currentEditor().channels());
    const int insertRow = std::clamp(selectedStepRow, 0, std::max(rows - 1, 0));

    for (int row = rows - 1; row > insertRow; --row) {
      for (int channel = 0; channel < channels; ++channel) {
        writeStep(row, channel, readStep(row - 1, channel));
      }
    }

    for (int channel = 0; channel < channels; ++channel) {
      app.module.currentEditor().clearStep(insertRow, channel);
    }

    lock.unlock();
    app.transport.resetTickCount();
    app.sequencer.reset();
    updateStepEditorFromSelection();
    patternGrid.repaint();
    pluginStatusLabel.setText("Inserted row at " + juce::String(insertRow), juce::dontSendNotification);
  }

  void removeRowAtSelection() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Remove row skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    const int rows = static_cast<int>(app.module.currentEditor().rows());
    const int channels = static_cast<int>(app.module.currentEditor().channels());
    const int removeRow = std::clamp(selectedStepRow, 0, std::max(rows - 1, 0));

    for (int row = removeRow; row < rows - 1; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        writeStep(row, channel, readStep(row + 1, channel));
      }
    }

    for (int channel = 0; channel < channels; ++channel) {
      app.module.currentEditor().clearStep(rows - 1, channel);
    }

    lock.unlock();
    app.transport.resetTickCount();
    app.sequencer.reset();
    updateStepEditorFromSelection();
    patternGrid.repaint();
    pluginStatusLabel.setText("Removed row at " + juce::String(removeRow), juce::dontSendNotification);
  }

  void flushPendingStepEdit() {
    if (selectedStepRow < 0 || selectedStepChannel < 0) {
      pendingStepVelocity = -1;
      pendingStepGate = -1;
      pendingStepEffectCommand = -1;
      pendingStepEffectValue = -1;
      return;
    }

    if (pendingStepVelocity < 0 && pendingStepGate < 0 &&
        pendingStepEffectCommand < 0 && pendingStepEffectValue < 0) {
      return;
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }

    if (!app.module.currentEditor().hasNoteAt(selectedStepRow, selectedStepChannel)) {
      pendingStepVelocity = -1;
      pendingStepGate = -1;
    } else {
      if (pendingStepVelocity >= 0) {
        app.module.currentEditor().setVelocity(
            selectedStepRow,
            selectedStepChannel,
            static_cast<std::uint8_t>(std::clamp(pendingStepVelocity, 1, 127)));
        pendingStepVelocity = -1;
      }

      if (pendingStepGate >= 0) {
        app.module.currentEditor().setGateTicks(
            selectedStepRow,
            selectedStepChannel,
            static_cast<std::uint32_t>(std::max(pendingStepGate, 0)));
        pendingStepGate = -1;
      }
    }

    if (pendingStepEffectCommand >= 0 || pendingStepEffectValue >= 0) {
      int effectCommand = app.module.currentEditor().effectCommandAt(selectedStepRow, selectedStepChannel);
      int effectValue = app.module.currentEditor().effectValueAt(selectedStepRow, selectedStepChannel);
      if (pendingStepEffectCommand >= 0) {
        effectCommand = std::clamp(pendingStepEffectCommand, 0, 255);
        pendingStepEffectCommand = -1;
      }
      if (pendingStepEffectValue >= 0) {
        effectValue = std::clamp(pendingStepEffectValue, 0, 255);
        pendingStepEffectValue = -1;
      }
      app.module.currentEditor().setEffect(
          selectedStepRow,
          selectedStepChannel,
          static_cast<std::uint8_t>(effectCommand),
          static_cast<std::uint8_t>(effectValue));
    }

    lock.unlock();
    patternGrid.repaint();
  }

  void updateStepEditorFromSelection() {
    if (selectedStepRow < 0 || selectedStepChannel < 0) {
      selectedStepLabel.setText("No step selected", juce::dontSendNotification);
      stepVelocitySlider.setEnabled(false);
      stepGateSlider.setEnabled(false);
      stepEffectCommandSlider.setEnabled(false);
      stepEffectValueSlider.setEnabled(false);
      stepEffectCommandHexEditor.setEnabled(false);
      stepEffectValueHexEditor.setEnabled(false);
      patternGrid.setInsertDefaults(static_cast<std::uint32_t>(std::max(stepGateSlider.getValue(), 0.0)),
                                    static_cast<std::uint8_t>(std::max(stepVelocitySlider.getValue(), 1.0)));
      return;
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }

    juce::String cellText = "Step R" + juce::String(selectedStepRow) + " C" + juce::String(selectedStepChannel);
    if (!app.module.currentEditor().hasNoteAt(selectedStepRow, selectedStepChannel)) {
      selectedStepLabel.setText(cellText + " (empty)", juce::dontSendNotification);
      stepVelocitySlider.setEnabled(false);
      stepGateSlider.setEnabled(false);
      stepEffectCommandSlider.setEnabled(true);
      stepEffectValueSlider.setEnabled(true);
      stepEffectCommandHexEditor.setEnabled(true);
      stepEffectValueHexEditor.setEnabled(true);
      suppressStepSliderCallbacks = true;
      stepEffectCommandSlider.setValue(app.module.currentEditor().effectCommandAt(selectedStepRow, selectedStepChannel), juce::dontSendNotification);
      stepEffectValueSlider.setValue(app.module.currentEditor().effectValueAt(selectedStepRow, selectedStepChannel), juce::dontSendNotification);
      suppressStepSliderCallbacks = false;
      suppressStepEffectTextCallbacks = true;
      stepEffectCommandHexEditor.setText(formatHexByte(app.module.currentEditor().effectCommandAt(selectedStepRow, selectedStepChannel)), false);
      stepEffectValueHexEditor.setText(formatHexByte(app.module.currentEditor().effectValueAt(selectedStepRow, selectedStepChannel)), false);
      suppressStepEffectTextCallbacks = false;
      patternGrid.setInsertDefaults(static_cast<std::uint32_t>(std::max(stepGateSlider.getValue(), 0.0)),
                                    static_cast<std::uint8_t>(std::max(stepVelocitySlider.getValue(), 1.0)));
      return;
    }

    selectedStepLabel.setText(cellText, juce::dontSendNotification);
    stepVelocitySlider.setEnabled(true);
    stepGateSlider.setEnabled(true);
    stepEffectCommandSlider.setEnabled(true);
    stepEffectValueSlider.setEnabled(true);
    stepEffectCommandHexEditor.setEnabled(true);
    stepEffectValueHexEditor.setEnabled(true);

    suppressStepSliderCallbacks = true;
    stepVelocitySlider.setValue(app.module.currentEditor().velocityAt(selectedStepRow, selectedStepChannel), juce::dontSendNotification);
    stepGateSlider.setValue(app.module.currentEditor().gateTicksAt(selectedStepRow, selectedStepChannel), juce::dontSendNotification);
    stepEffectCommandSlider.setValue(app.module.currentEditor().effectCommandAt(selectedStepRow, selectedStepChannel), juce::dontSendNotification);
    stepEffectValueSlider.setValue(app.module.currentEditor().effectValueAt(selectedStepRow, selectedStepChannel), juce::dontSendNotification);
    suppressStepSliderCallbacks = false;
    suppressStepEffectTextCallbacks = true;
    stepEffectCommandHexEditor.setText(formatHexByte(app.module.currentEditor().effectCommandAt(selectedStepRow, selectedStepChannel)), false);
    stepEffectValueHexEditor.setText(formatHexByte(app.module.currentEditor().effectValueAt(selectedStepRow, selectedStepChannel)), false);
    suppressStepEffectTextCallbacks = false;
    patternGrid.setInsertDefaults(app.module.currentEditor().gateTicksAt(selectedStepRow, selectedStepChannel),
                                  app.module.currentEditor().velocityAt(selectedStepRow, selectedStepChannel));
  }

  void flushPendingChannelInstrumentAssignments() {
    bool hasPending = false;
    for (int slot : pendingChannelInstrumentSlots) {
      if (slot >= 0) {
        hasPending = true;
        break;
      }
    }
    if (!hasPending) {
      return;
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }

    for (std::size_t ch = 0; ch < pendingChannelInstrumentSlots.size(); ++ch) {
      int slot = pendingChannelInstrumentSlots[ch];
      if (slot < 0) {
        continue;
      }
      if (ch < app.channelInstruments.size()) {
        app.channelInstruments[ch] = static_cast<std::uint8_t>(slot);
      }
      pendingChannelInstrumentSlots[ch] = -1;
    }
  }

  void timerCallback() override {
    flushPendingChannelInstrumentAssignments();
    flushPendingStepEdit();

    if (!isShowing()) {
      return;
    }

    if (auto* peer = getPeer(); peer != nullptr && peer->isMinimised()) {
      return;
    }

    updateStatusLabels();
    ++refreshTickCounter;

    if ((refreshTickCounter % 10) == 0) {
      refreshChannelPluginLabels();
    }
    if ((refreshTickCounter % 4) == 0) {
      refreshSlotActivityLabels();
    }
    if ((refreshTickCounter % 8) == 0) {
      refreshSampleSlotDetails();
    }

    bool isPlayingNow = app.transport.isPlaying();
    int currentRow = static_cast<int>(app.transport.currentRow());

    if (isPlayingNow != lastTransportPlaying || currentRow != lastTransportRow) {
      patternGrid.repaintPlaybackRows(lastTransportRow, currentRow);
      lastTransportPlaying = isPlayingNow;
      lastTransportRow = currentRow;
    }
  }

  void initPanelStyling() {
    instrumentPanelTitle.setText("Instruments", juce::dontSendNotification);
    instrumentPanelTitle.setJustificationType(juce::Justification::centredLeft);
    instrumentPanelTitle.setFont(juce::Font(16.0f, juce::Font::bold));

    songOrderTitle.setText("Song Order", juce::dontSendNotification);
    songOrderTitle.setJustificationType(juce::Justification::centredLeft);
    songOrderTitle.setFont(juce::Font(16.0f, juce::Font::bold));

    songOrderListView.setText("", juce::dontSendNotification);
    songOrderListView.setJustificationType(juce::Justification::topLeft);
    songOrderListView.setColour(juce::Label::backgroundColourId, juce::Colour(0xFF101214));
    songOrderListView.setColour(juce::Label::outlineColourId, juce::Colour(0xFF2A2F34));
    songOrderListView.setColour(juce::Label::textColourId, juce::Colour(0xFFD0D7DE));
    songOrderListView.setBorderSize(juce::BorderSize<int>(6));

    songOrderEntryLabel.setText("Entry", juce::dontSendNotification);
    songOrderEntryLabel.setJustificationType(juce::Justification::centredLeft);

    songOrderPatternLabel.setText("Pattern", juce::dontSendNotification);
    songOrderPatternLabel.setJustificationType(juce::Justification::centredLeft);

    channelPanelTitle.setText("Per-Channel Instrument + Mute", juce::dontSendNotification);
    channelPanelTitle.setJustificationType(juce::Justification::centredLeft);

    slotPanelTitle.setText("Slot Editor", juce::dontSendNotification);
    slotPanelTitle.setJustificationType(juce::Justification::centredLeft);

    pluginPanelTitle.setText("Plugin Assignment", juce::dontSendNotification);
    pluginPanelTitle.setJustificationType(juce::Justification::centredLeft);

    sampleBankTitle.setText("Sample Bank", juce::dontSendNotification);
    sampleBankTitle.setJustificationType(juce::Justification::centredLeft);

    sampleSlotLabel.setText("Sample", juce::dontSendNotification);
    sampleSlotLabel.setJustificationType(juce::Justification::centredLeft);

    samplePathLabel.setText("Sample slot is empty", juce::dontSendNotification);
    samplePathLabel.setJustificationType(juce::Justification::centredLeft);

    sampleLoadButton.setTooltip("Load WAV into selected sample slot");
    sampleAssignButton.setTooltip("Preview the selected sample slot using the current keyboard octave");
    sampleAssignToChannelButton.setTooltip("Stop the current preview note for the selected sample slot");
    sampleRenameEditor.setTooltip("Edit the display name for the selected sample slot");
    sampleRenameEditor.setTextToShowWhenEmpty("Sample name", juce::Colour(0xFF6A737D));
    sampleRenameButton.setTooltip("Rename the selected sample slot");
    sampleClearButton.setTooltip("Clear the selected sample slot");

    pluginStatusLabel.setText("", juce::dontSendNotification);
    pluginStatusLabel.setJustificationType(juce::Justification::centredLeft);

    stepEditorTitle.setText("Step Editor", juce::dontSendNotification);
    stepEditorTitle.setJustificationType(juce::Justification::centredLeft);
    selectedStepLabel.setText("No step selected", juce::dontSendNotification);
    selectedStepLabel.setJustificationType(juce::Justification::centredLeft);
    stepVelocityLabel.setText("Velocity", juce::dontSendNotification);
    stepVelocityLabel.setJustificationType(juce::Justification::centredLeft);
    stepGateLabel.setText("Gate (ticks)", juce::dontSendNotification);
    stepGateLabel.setJustificationType(juce::Justification::centredLeft);
    stepEffectCommandLabel.setText("Effect Command (fx)", juce::dontSendNotification);
    stepEffectCommandLabel.setJustificationType(juce::Justification::centredLeft);
    stepEffectValueLabel.setText("Effect Value (fxval)", juce::dontSendNotification);
    stepEffectValueLabel.setJustificationType(juce::Justification::centredLeft);
    keyboardOctaveLabel.setText("Keyboard Octave", juce::dontSendNotification);
    keyboardOctaveLabel.setJustificationType(juce::Justification::centredLeft);
    editStepLabel.setText("Edit Step", juce::dontSendNotification);
    editStepLabel.setJustificationType(juce::Justification::centredLeft);

    slotLabel.setText("Slot", juce::dontSendNotification);
    slotLabel.setJustificationType(juce::Justification::centredLeft);

    gainLabel.setText("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType(juce::Justification::centredLeft);
    attackLabel.setText("Attack (ms)", juce::dontSendNotification);
    attackLabel.setJustificationType(juce::Justification::centredLeft);
    releaseLabel.setText("Release (ms)", juce::dontSendNotification);
    releaseLabel.setJustificationType(juce::Justification::centredLeft);
    slotActivityTitle.setText("Slot Activity", juce::dontSendNotification);
    slotActivityTitle.setJustificationType(juce::Justification::centredLeft);

    configureParameterSlider(gainSlider, 0.0, 1.0, 0.01);
    configureParameterSlider(attackSlider, 1.0, 500.0, 1.0);
    configureParameterSlider(releaseSlider, 1.0, 1000.0, 1.0);
    configureParameterSlider(stepVelocitySlider, 1.0, 127.0, 1.0);
    configureParameterSlider(stepGateSlider, 0.0, 32.0, 1.0);
    configureParameterSlider(stepEffectCommandSlider, 0.0, 255.0, 1.0);
    configureParameterSlider(stepEffectValueSlider, 0.0, 255.0, 1.0);
    configureParameterSlider(keyboardOctaveSlider, 0.0, 8.0, 1.0);
    configureParameterSlider(editStepSlider, 1.0, 16.0, 1.0);
    keyboardOctaveSlider.setValue(4.0, juce::dontSendNotification);
    editStepSlider.setValue(1.0, juce::dontSendNotification);
    stepVelocitySlider.setEnabled(false);
    stepGateSlider.setEnabled(false);
    stepEffectCommandSlider.setEnabled(false);
    stepEffectValueSlider.setEnabled(false);

    stepEffectCommandHexEditor.setInputRestrictions(2, "0123456789abcdefABCDEF");
    stepEffectCommandHexEditor.setTextToShowWhenEmpty("00", juce::Colours::grey);
    stepEffectCommandHexEditor.setText(formatHexByte(0), false);
    stepEffectCommandHexEditor.setJustification(juce::Justification::centred);
    stepEffectCommandHexEditor.setEnabled(false);

    stepEffectValueHexEditor.setInputRestrictions(2, "0123456789abcdefABCDEF");
    stepEffectValueHexEditor.setTextToShowWhenEmpty("00", juce::Colours::grey);
    stepEffectValueHexEditor.setText(formatHexByte(0), false);
    stepEffectValueHexEditor.setJustification(juce::Justification::centred);
    stepEffectValueHexEditor.setEnabled(false);
  }

  void initSlotActivityRows() {
    slotActivityLabels.reserve(extracker::PluginHost::kMaxInstrumentSlots);
    slotActivityBars.reserve(extracker::PluginHost::kMaxInstrumentSlots);
    for (int slot = 0; slot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots); ++slot) {
      auto label = std::make_unique<juce::Label>();
      label->setText("I" + juce::String(slot) + ": v0", juce::dontSendNotification);
      label->setJustificationType(juce::Justification::centredLeft);
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*label);
      } else {
        addAndMakeVisible(*label);
      }
      slotActivityLabels.push_back(std::move(label));

      auto bar = std::make_unique<SlotActivityBar>();
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*bar);
      } else {
        addAndMakeVisible(*bar);
      }
      slotActivityBars.push_back(std::move(bar));
    }
    refreshSlotActivityLabels();
  }

  void configureParameterSlider(juce::Slider& slider, double min, double max, double step) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    slider.setRange(min, max, step);
  }

  void initChannelRows() {
    int numChannels = static_cast<int>(app.module.currentEditor().channels());
    channelLabels.reserve(static_cast<std::size_t>(numChannels));
    channelInstrumentSelectors.reserve(static_cast<std::size_t>(numChannels));
    channelMuteToggles.reserve(static_cast<std::size_t>(numChannels));
    channelPluginLabels.reserve(static_cast<std::size_t>(numChannels));
    pendingChannelInstrumentSlots.assign(static_cast<std::size_t>(numChannels), -1);

    for (int ch = 0; ch < numChannels; ++ch) {
      auto label = std::make_unique<juce::Label>();
      label->setText("CH " + juce::String(ch), juce::dontSendNotification);
      label->setJustificationType(juce::Justification::centredLeft);
      label->setOpaque(true);
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*label);
      } else {
        addAndMakeVisible(*label);
      }

      auto combo = std::make_unique<juce::ComboBox>();
      for (int slot = 0; slot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots); ++slot) {
        combo->addItem("I" + juce::String(slot), slot + 1);
      }
      combo->onChange = [this, ch, comboPtr = combo.get()]() {
        int selectedSlot = comboPtr->getSelectedId() - 1;
        if (selectedSlot < 0) {
          return;
        }
        if (static_cast<std::size_t>(ch) < pendingChannelInstrumentSlots.size()) {
          pendingChannelInstrumentSlots[static_cast<std::size_t>(ch)] = selectedSlot;
        }
      };
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*combo);
      } else {
        addAndMakeVisible(*combo);
      }

      auto muteToggle = std::make_unique<juce::ToggleButton>("Mute");
      muteToggle->setTooltip("Toggle mute for channel " + juce::String(ch));
      muteToggle->onClick = [this, ch, togglePtr = muteToggle.get()]() {
        {
          std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
          if (!lock.owns_lock()) {
            pluginStatusLabel.setText("Mute toggle skipped (engine busy)", juce::dontSendNotification);
            bool muted = false;
            if (static_cast<std::size_t>(ch) < app.channelMuted.size()) {
              muted = app.channelMuted[static_cast<std::size_t>(ch)];
            }
            togglePtr->setToggleState(muted, juce::dontSendNotification);
            return;
          }

          if (static_cast<std::size_t>(ch) < app.channelMuted.size()) {
            app.channelMuted[static_cast<std::size_t>(ch)] = togglePtr->getToggleState();
          }

          app.plugins.allNotesOff();
          app.audio.allNotesOff();
        }

        refreshChannelRows();
        patternGrid.repaint();
      };
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*muteToggle);
      } else {
        addAndMakeVisible(*muteToggle);
      }

      auto pluginLabel = std::make_unique<juce::Label>();
      pluginLabel->setText("", juce::dontSendNotification);
      pluginLabel->setJustificationType(juce::Justification::centredLeft);
      pluginLabel->setOpaque(true);
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*pluginLabel);
      } else {
        addAndMakeVisible(*pluginLabel);
      }

      channelLabels.push_back(std::move(label));
      channelInstrumentSelectors.push_back(std::move(combo));
      channelMuteToggles.push_back(std::move(muteToggle));
      channelPluginLabels.push_back(std::move(pluginLabel));
    }
  }

  void reinitChannelRows() {
    // Clear existing channel controls
    for (auto& label : channelLabels) {
      if (auto* parent = label->getParentComponent()) {
        parent->removeChildComponent(label.get());
      }
    }
    for (auto& combo : channelInstrumentSelectors) {
      if (auto* parent = combo->getParentComponent()) {
        parent->removeChildComponent(combo.get());
      }
    }
    for (auto& toggle : channelMuteToggles) {
      if (auto* parent = toggle->getParentComponent()) {
        parent->removeChildComponent(toggle.get());
      }
    }
    for (auto& label : channelPluginLabels) {
      if (auto* parent = label->getParentComponent()) {
        parent->removeChildComponent(label.get());
      }
    }
    channelLabels.clear();
    channelInstrumentSelectors.clear();
    channelMuteToggles.clear();
    channelPluginLabels.clear();

    // Reinitialize with new channel count
    int numChannels = static_cast<int>(app.module.currentEditor().channels());
    channelLabels.reserve(static_cast<std::size_t>(numChannels));
    channelInstrumentSelectors.reserve(static_cast<std::size_t>(numChannels));
    channelMuteToggles.reserve(static_cast<std::size_t>(numChannels));
    channelPluginLabels.reserve(static_cast<std::size_t>(numChannels));
    pendingChannelInstrumentSlots.assign(static_cast<std::size_t>(numChannels), -1);

    for (int ch = 0; ch < numChannels; ++ch) {
      auto label = std::make_unique<juce::Label>();
      label->setText("CH " + juce::String(ch), juce::dontSendNotification);
      label->setJustificationType(juce::Justification::centredLeft);
      label->setOpaque(true);
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*label);
      } else {
        addAndMakeVisible(*label);
      }

      auto combo = std::make_unique<juce::ComboBox>();
      for (int slot = 0; slot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots); ++slot) {
        combo->addItem("I" + juce::String(slot), slot + 1);
      }
      combo->onChange = [this, ch, comboPtr = combo.get()]() {
        int selectedSlot = comboPtr->getSelectedId() - 1;
        if (selectedSlot < 0) {
          return;
        }
        if (static_cast<std::size_t>(ch) < pendingChannelInstrumentSlots.size()) {
          pendingChannelInstrumentSlots[static_cast<std::size_t>(ch)] = selectedSlot;
        }
      };
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*combo);
      } else {
        addAndMakeVisible(*combo);
      }

      auto muteToggle = std::make_unique<juce::ToggleButton>("Mute");
      muteToggle->setTooltip("Toggle mute for channel " + juce::String(ch));
      muteToggle->onClick = [this, ch, togglePtr = muteToggle.get()]() {
        {
          std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
          if (!lock.owns_lock()) {
            pluginStatusLabel.setText("Mute toggle skipped (engine busy)", juce::dontSendNotification);
            bool muted = false;
            if (static_cast<std::size_t>(ch) < app.channelMuted.size()) {
              muted = app.channelMuted[static_cast<std::size_t>(ch)];
            }
            togglePtr->setToggleState(muted, juce::dontSendNotification);
            return;
          }

          if (static_cast<std::size_t>(ch) < app.channelMuted.size()) {
            app.channelMuted[static_cast<std::size_t>(ch)] = togglePtr->getToggleState();
          }

          app.plugins.allNotesOff();
          app.audio.allNotesOff();
        }

        refreshChannelRows();
        patternGrid.repaint();
      };
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*muteToggle);
      } else {
        addAndMakeVisible(*muteToggle);
      }

      auto pluginLabel = std::make_unique<juce::Label>();
      pluginLabel->setText("", juce::dontSendNotification);
      pluginLabel->setJustificationType(juce::Justification::centredLeft);
      pluginLabel->setOpaque(true);
      if (panelWrapper) {
        panelWrapper->addAndMakeVisible(*pluginLabel);
      } else {
        addAndMakeVisible(*pluginLabel);
      }

      channelLabels.push_back(std::move(label));
      channelInstrumentSelectors.push_back(std::move(combo));
      channelMuteToggles.push_back(std::move(muteToggle));
      channelPluginLabels.push_back(std::move(pluginLabel));
    }
    refreshChannelRows();
  }

  void refreshPluginChoices() {
    pluginSelector.clear(juce::dontSendNotification);

    std::vector<std::string> plugins = app.plugins.discoverAvailablePlugins();

    int id = 1;
    for (const auto& pluginId : plugins) {
      pluginSelector.addItem(pluginId, id++);
    }
    if (!plugins.empty()) {
      pluginSelector.setSelectedId(1, juce::dontSendNotification);
    }
  }

  void refreshSlotSelector() {
    slotSelector.clear(juce::dontSendNotification);

    for (int slot = 0; slot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots); ++slot) {
      std::string pluginId = app.plugins.pluginForInstrument(static_cast<std::uint8_t>(slot));

      juce::String text = "I" + juce::String(slot);
      if (!pluginId.empty()) {
        text += " -> " + juce::String(pluginId);
      }
      slotSelector.addItem(text, slot + 1);
    }

    if (slotSelector.getSelectedId() == 0) {
      slotSelector.setSelectedId(1, juce::dontSendNotification);
    }
  }

  void refreshSampleSlotSelector() {
    const int previousSelectedId = sampleSlotSelector.getSelectedId();
    sampleSlotSelector.clear(juce::dontSendNotification);
    for (int slot = 0; slot <= 256; ++slot) {
      juce::String text = formatSampleSlotHex(slot);
      const std::string name = app.plugins.sampleNameForSlot(static_cast<std::uint16_t>(slot));
      if (!name.empty()) {
        text += " - " + juce::String(name);
      }
      sampleSlotSelector.addItem(text, slot + 1);
    }
    const int maxId = 257;
    const int targetId = (previousSelectedId >= 1 && previousSelectedId <= maxId) ? previousSelectedId : 1;
    sampleSlotSelector.setSelectedId(targetId, juce::dontSendNotification);
  }

  int getSelectedSampleSlot() const {
    int selected = sampleSlotSelector.getSelectedId() - 1;
    if (selected < 0 || selected > 256) {
      return -1;
    }
    return selected;
  }

  void syncActiveSampleWriteSlot() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot >= 0 && selectedSampleSlot <= 255 &&
        !app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot)).empty()) {
      app.activeSampleSlot = selectedSampleSlot;
    } else {
      app.activeSampleSlot = -1;
    }
  }

  void refreshSampleSlotDetails() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      samplePathLabel.setText("No sample slot selected", juce::dontSendNotification);
      sampleRenameEditor.setText("", false);
      return;
    }

    const std::string path = app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    const std::string name = app.plugins.sampleNameForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    sampleRenameEditor.setText(path.empty() ? juce::String() : juce::String(name), false);
    juce::String text;
    if (path.empty()) {
      text = formatSampleSlotHex(selectedSampleSlot) + ": empty";
    } else {
      text = formatSampleSlotHex(selectedSampleSlot);
      if (!name.empty()) {
        text += " \"" + juce::String(name) + "\"";
      }
      text += ": " + juce::String(path);
      if (selectedSampleSlot <= 255) {
        text += app.activeSampleSlot == selectedSampleSlot
            ? "  |  active write target for new notes"
            : "  |  select this slot to arm it for new notes";
      } else {
        text += "  |  slot 256 is bank-only and not pattern-addressable";
      }
    }

    samplePathLabel.setText(text, juce::dontSendNotification);
  }

  void refreshChannelRows() {
    {
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        return;
      }
      for (std::size_t ch = 0; ch < channelInstrumentSelectors.size(); ++ch) {
        int slot = 0;
        if (ch < app.channelInstruments.size()) {
          slot = static_cast<int>(app.channelInstruments[ch]);
        }
        channelInstrumentSelectors[ch]->setSelectedId(slot + 1, juce::dontSendNotification);
        bool muted = (ch < app.channelMuted.size()) ? app.channelMuted[ch] : false;
        if (ch < channelMuteToggles.size()) {
          channelMuteToggles[ch]->setToggleState(muted, juce::dontSendNotification);
          channelMuteToggles[ch]->setColour(juce::ToggleButton::textColourId,
                                            muted ? juce::Colour(0xFFFF9A9A) : juce::Colours::white);
        }
        if (ch < channelLabels.size()) {
          channelLabels[ch]->setText("CH " + juce::String(static_cast<int>(ch)) + (muted ? " [MUTED]" : ""),
                                     juce::dontSendNotification);
          channelLabels[ch]->setColour(juce::Label::textColourId,
                                       muted ? juce::Colour(0xFFFFB3B3) : juce::Colours::white);
          channelLabels[ch]->setColour(juce::Label::backgroundColourId,
                                       muted ? juce::Colour(0xFF4A1E1E) : juce::Colour(0xFF222222));
        }
        if (ch < channelPluginLabels.size()) {
          channelPluginLabels[ch]->setColour(juce::Label::textColourId,
                                             muted ? juce::Colour(0xFFFFA0A0) : juce::Colours::lightgrey);
          channelPluginLabels[ch]->setColour(juce::Label::backgroundColourId,
                                             muted ? juce::Colour(0xFF3A1717) : juce::Colour(0xFF1F1F1F));
        }
      }
    }

    refreshChannelPluginLabels();
  }

  void refreshChannelPluginLabels() {
    std::vector<int> slots(channelPluginLabels.size(), 0);

    {
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        return;
      }
      for (std::size_t ch = 0; ch < slots.size(); ++ch) {
        if (ch < app.channelInstruments.size()) {
          slots[ch] = static_cast<int>(app.channelInstruments[ch]);
        }
      }
    }

    for (std::size_t ch = 0; ch < channelPluginLabels.size(); ++ch) {
      int slot = std::clamp(slots[ch], 0, 15);
      std::string pluginId = app.plugins.pluginForInstrument(static_cast<std::uint8_t>(std::clamp(slot, 0, 15)));
      juce::String labelText = pluginId.empty() ? "(unassigned)" : juce::String(pluginId);
      if (ch >= cachedChannelPluginText.size()) {
        cachedChannelPluginText.resize(ch + 1);
      }
      if (cachedChannelPluginText[ch] != labelText) {
        cachedChannelPluginText[ch] = labelText;
        channelPluginLabels[ch]->setText(labelText, juce::dontSendNotification);
      }
    }
  }

  void refreshSlotActivityLabels() {
    std::array<std::size_t, extracker::PluginHost::kMaxInstrumentSlots> voiceCounts{};
    std::array<double, extracker::PluginHost::kMaxInstrumentSlots> firstFrequencies{};

    for (std::size_t slot = 0; slot < extracker::PluginHost::kMaxInstrumentSlots; ++slot) {
      std::uint8_t instrument = static_cast<std::uint8_t>(slot);
      voiceCounts[slot] = app.plugins.activeVoiceCountForInstrument(instrument);
      firstFrequencies[slot] = app.plugins.activeVoiceFrequencyHzForInstrument(instrument, 0);
    }

    for (std::size_t slot = 0; slot < slotActivityLabels.size(); ++slot) {
      juce::String text = "I" + juce::String(static_cast<int>(slot)) + ": v" +
                          juce::String(static_cast<int>(voiceCounts[slot]));
      if (voiceCounts[slot] > 0 && firstFrequencies[slot] > 0.0) {
        text += "  " + juce::String(firstFrequencies[slot], 1) + "Hz";
      }
      if (slot >= cachedSlotActivityText.size()) {
        cachedSlotActivityText.resize(slot + 1);
      }
      if (cachedSlotActivityText[slot] != text) {
        cachedSlotActivityText[slot] = text;
        slotActivityLabels[slot]->setText(text, juce::dontSendNotification);
      }

      if (slot < slotActivityBars.size()) {
        double level = std::min(1.0, static_cast<double>(voiceCounts[slot]) / 6.0);
        slotActivityBars[slot]->setLevel(level);
      }
    }
  }

  int getSelectedSlot() const {
    int selected = slotSelector.getSelectedId() - 1;
    if (selected < 0 || selected >= static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots)) {
      return -1;
    }
    return selected;
  }

  void refreshParameterSlidersFromSlot() {
    int slot = getSelectedSlot();
    if (slot < 0) {
      return;
    }

    double gain = app.plugins.getInstrumentParameter(static_cast<std::uint8_t>(slot), "gain");
    double attack = app.plugins.getInstrumentParameter(static_cast<std::uint8_t>(slot), "attack_ms");
    double release = app.plugins.getInstrumentParameter(static_cast<std::uint8_t>(slot), "release_ms");

    gainSlider.setValue(gain, juce::dontSendNotification);
    attackSlider.setValue(attack, juce::dontSendNotification);
    releaseSlider.setValue(release, juce::dontSendNotification);
  }

  void setSelectedSlotParameter(const std::string& name, double value) {
    int slot = getSelectedSlot();
    if (slot < 0) {
      return;
    }

    app.plugins.setInstrumentParameter(static_cast<std::uint8_t>(slot), name, value);
  }

  void updateLoopButtonText() {
    loopButton.setButtonText(app.loopEnabled ? "Loop: On" : "Loop: Off");
  }

  void updatePlayModeButtonStates() {
    if (app.playMode == PlayMode::PLAY_PATTERN) {
      playModePatternButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2EA043));
      playModeSongButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF505050));
    } else {
      playModePatternButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF505050));
      playModeSongButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2EA043));
    }
  }

  void updateGridDensityButtonText() {
    gridDensityButton.setButtonText(patternGrid.isCompactDensity() ? "Grid: Compact" : "Grid: Normal");
  }

  void updateStatusLabels() {
    bool playing = app.transport.isPlaying();
    int row = static_cast<int>(app.transport.currentRow());
    double bpm = app.transport.tempoBpm();
    int ticksPerBeat = static_cast<int>(app.transport.ticksPerBeat());

    juce::String text = juce::String(playing ? "Playing" : "Stopped") +
                        "  |  Row " + juce::String(row) +
              "  |  " + juce::String(bpm, 1) + " BPM" +
              "  |  TPB " + juce::String(ticksPerBeat);
    if (text != cachedStatusText) {
      cachedStatusText = text;
      statusLabel.setText(text, juce::dontSendNotification);
    }
  }

  void updatePatternSelector() {
    std::size_t patternCount = app.patternCountCache.load();
    std::size_t currentPattern = app.currentPatternCache.load();

    if (patternCount == 0) {
      patternCount = 1;
    }
    if (currentPattern >= patternCount) {
      currentPattern = patternCount - 1;
    }
    
    patternSelector.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < patternCount; ++i) {
      patternSelector.addItem("Pattern " + juce::String(static_cast<int>(i + 1)), static_cast<int>(i + 1));
    }
    patternSelector.setSelectedItemIndex(static_cast<int>(currentPattern), juce::dontSendNotification);
    
    patternLabel.setText(
      "Patterns: " + juce::String(static_cast<int>(patternCount)) + "  Current: " + juce::String(static_cast<int>(currentPattern + 1)),
      juce::dontSendNotification
    );
  }

  void refreshSongOrderEditor() {
    const std::size_t patternCount = std::max<std::size_t>(app.patternCountCache.load(), 1);
    std::size_t selectedSongEntry = app.currentSongOrderPositionCache.load();
    std::vector<std::size_t> songOrder;
    {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      songOrder.reserve(app.module.songLength());
      for (std::size_t i = 0; i < app.module.songLength(); ++i) {
        songOrder.push_back(app.module.songEntryAt(i));
      }
    }

    if (songOrder.empty()) {
      songOrder.push_back(0);
      selectedSongEntry = 0;
    }
    if (selectedSongEntry >= songOrder.size()) {
      selectedSongEntry = songOrder.size() - 1;
      app.currentSongOrderPositionCache.store(selectedSongEntry);
    }

    songOrderEntrySelector.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < songOrder.size(); ++i) {
      songOrderEntrySelector.addItem(
          juce::String(static_cast<int>(i + 1)).paddedLeft('0', 2) + " -> Pattern " + juce::String(static_cast<int>(songOrder[i] + 1)),
          static_cast<int>(i + 1));
    }
    songOrderEntrySelector.setSelectedItemIndex(static_cast<int>(selectedSongEntry), juce::dontSendNotification);

    songOrderPatternSelector.clear(juce::dontSendNotification);
    for (std::size_t patternIndex = 0; patternIndex < patternCount; ++patternIndex) {
      songOrderPatternSelector.addItem("Pattern " + juce::String(static_cast<int>(patternIndex + 1)), static_cast<int>(patternIndex + 1));
    }
    songOrderPatternSelector.setSelectedItemIndex(static_cast<int>(songOrder[selectedSongEntry]), juce::dontSendNotification);

    juce::String listText;
    for (std::size_t i = 0; i < songOrder.size(); ++i) {
      const bool isCurrent = (i == selectedSongEntry);
      listText += (isCurrent ? "> " : "  ");
      listText += juce::String(static_cast<int>(i + 1)).paddedLeft('0', 2);
      listText += " : Pattern ";
      listText += juce::String(static_cast<int>(songOrder[i] + 1));
      if (i + 1 < songOrder.size()) {
        listText += "\n";
      }
    }
    songOrderListView.setText(listText, juce::dontSendNotification);

    songOrderRemoveButton.setEnabled(songOrder.size() > 1);
    songOrderUpButton.setEnabled(selectedSongEntry > 0);
    songOrderDownButton.setEnabled(selectedSongEntry + 1 < songOrder.size());
  }

  ExTrackerApp& app;
  juce::TextButton playButton;
  juce::TextButton stopButton;
  juce::TextButton playModePatternButton{"Pattern"};
  juce::TextButton playModeSongButton{"Song"};
  juce::TextButton loopButton;
  juce::TextButton helpButton{"Help"};
  juce::Label patternLabel;
  juce::ComboBox patternSelector;
  juce::TextButton insertPatternBeforeButton{"Insert Before"};
  juce::TextButton insertPatternAfterButton{"Insert After"};
  juce::TextButton removePatternButton{"Remove Pattern"};
  juce::TextButton gridDensityButton;
  juce::TextButton applyChannelMapButton;
  juce::Slider tempoSlider;
  juce::Label tempoLabel;
  juce::Label ticksPerBeatLabel;
  juce::Slider ticksPerBeatSlider;
  juce::TextButton expandPatternButton{"Expand x2"};
  juce::TextButton shrinkPatternButton{"Shrink x2"};
  juce::TextButton expandChannelButton{"Ch+"};
  juce::TextButton shrinkChannelButton{"Ch-"};
  juce::TextButton savePatternButton{"Save Song"};
  juce::TextButton loadPatternButton{"Load Song"};
  juce::TextButton insertRowButton{"Insert Row"};
  juce::TextButton removeRowButton{"Remove Row"};
  juce::Label statusLabel;
  juce::Label instrumentPanelTitle;
  juce::Label songOrderTitle;
  juce::Label songOrderListView;
  juce::Label songOrderEntryLabel;
  juce::ComboBox songOrderEntrySelector;
  juce::Label songOrderPatternLabel;
  juce::ComboBox songOrderPatternSelector;
  juce::TextButton songOrderAddButton{"Add Slot"};
  juce::TextButton songOrderRemoveButton{"Remove Slot"};
  juce::TextButton songOrderUpButton{"Move Up"};
  juce::TextButton songOrderDownButton{"Move Down"};
  juce::Label channelPanelTitle;
  juce::Label slotPanelTitle;
  juce::Label pluginPanelTitle;
  juce::Label pluginStatusLabel;
  juce::Label sampleBankTitle;
  juce::Label sampleSlotLabel;
  juce::ComboBox sampleSlotSelector;
  juce::TextButton sampleLoadButton{"Load WAV"};
  juce::TextButton sampleAssignButton{"Preview"};
  juce::TextButton sampleAssignToChannelButton{"Stop Preview"};
  juce::TextEditor sampleRenameEditor;
  juce::TextButton sampleRenameButton{"Rename"};
  juce::TextButton sampleClearButton{"Clear"};
  juce::Label samplePathLabel;
  juce::Label stepEditorTitle;
  juce::Label selectedStepLabel;
  juce::TextButton copyBlockButton{"Copy"};
  juce::TextButton cutBlockButton{"Cut"};
  juce::TextButton pasteBlockButton{"Paste"};
  juce::TextButton transposeDownButton{"Transpose -"};
  juce::TextButton transposeUpButton{"Transpose +"};
  juce::TextButton applyFxToBlockButton{"Apply FX to Block"};
  juce::Label stepVelocityLabel;
  juce::Slider stepVelocitySlider;
  juce::Label stepGateLabel;
  juce::Slider stepGateSlider;
  juce::Label stepEffectCommandLabel;
  juce::Slider stepEffectCommandSlider;
  juce::TextEditor stepEffectCommandHexEditor;
  juce::Label stepEffectValueLabel;
  juce::Slider stepEffectValueSlider;
  juce::TextEditor stepEffectValueHexEditor;
  juce::Label keyboardOctaveLabel;
  juce::Slider keyboardOctaveSlider;
  juce::Label editStepLabel;
  juce::Slider editStepSlider;
  juce::Label slotLabel;
  juce::ComboBox slotSelector;
  juce::ComboBox pluginSelector;
  juce::TextButton assignPluginButton{"Assign Plugin To Slot"};
  juce::Label gainLabel;
  juce::Slider gainSlider;
  juce::Label attackLabel;
  juce::Slider attackSlider;
  juce::Label releaseLabel;
  juce::Slider releaseSlider;
  juce::Label slotActivityTitle;
  std::vector<std::unique_ptr<juce::Label>> channelLabels;
  std::vector<std::unique_ptr<juce::ComboBox>> channelInstrumentSelectors;
  std::vector<std::unique_ptr<juce::ToggleButton>> channelMuteToggles;
  std::vector<std::unique_ptr<juce::Label>> channelPluginLabels;
  std::vector<int> pendingChannelInstrumentSlots;
  std::vector<std::unique_ptr<juce::Label>> slotActivityLabels;
  std::vector<std::unique_ptr<SlotActivityBar>> slotActivityBars;
  std::vector<juce::String> cachedChannelPluginText;
  std::vector<juce::String> cachedSlotActivityText;
  std::unique_ptr<juce::FileChooser> sampleFileChooser;
  std::unique_ptr<juce::FileChooser> savePatternFileChooser;
  std::unique_ptr<juce::FileChooser> loadPatternFileChooser;
  juce::File lastSampleLoadFolder;
  juce::String cachedStatusText;
  int selectedStepRow = -1;
  int selectedStepChannel = -1;
  int pendingStepVelocity = -1;
  int pendingStepGate = -1;
  int pendingStepEffectCommand = -1;
  int pendingStepEffectValue = -1;
  bool suppressStepSliderCallbacks = false;
  bool suppressStepEffectTextCallbacks = false;
  bool suppressKeyboardStateCallbacks = false;
  int refreshTickCounter = 0;
  bool lastTransportPlaying = false;
  int lastTransportRow = -1;
  juce::Viewport patternViewport;
  juce::Viewport panelViewport;
  std::unique_ptr<PanelWrapper> panelWrapper;
  PatternGrid patternGrid;
};

}  // namespace

MainWindow::MainWindow(ExTrackerApp& app)
    : DocumentWindow("exTracker", juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId), juce::DocumentWindow::allButtons),
      app(app) {
  setUsingNativeTitleBar(true);

  content = std::make_unique<TrackerMainComponent>(app);
  content->setSize(1200, 600);
  setContentOwned(content.release(), true);

  // Set window properties
  setSize(1200, 600);
  centreWithSize(getWidth(), getHeight());
  setVisible(true);
}

MainWindow::~MainWindow() = default;

void MainWindow::notifyPatternChanged() {
  if (auto* tracker = dynamic_cast<TrackerMainComponent*>(getContentComponent())) {
    tracker->handlePatternChangedFromPlayback();
  }
}

void MainWindow::closeButtonPressed() {
  juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
