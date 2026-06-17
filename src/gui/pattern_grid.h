#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ExTrackerApp;

class PatternGrid : public juce::Component,
                    private juce::Timer {
public:
  explicit PatternGrid(ExTrackerApp& app);
  ~PatternGrid() override;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent& event) override;
  void mouseDrag(const juce::MouseEvent& event) override;
  void mouseMove(const juce::MouseEvent& event) override;
  bool keyPressed(const juce::KeyPress& key) override;
  void focusGained(FocusChangeType cause) override;
  void focusLost(FocusChangeType cause) override;
  void setSelectionChangedCallback(std::function<void(int, int)> callback);
  void setKeyboardStateChangedCallback(std::function<void(int, int)> callback);
  void setTogglePlaybackCallback(std::function<void()> callback);
  void setFocusModuleMessageCallback(std::function<void()> callback);
  void setSearchNavigationCallback(std::function<void(bool)> callback);
  void setInsertDefaults(std::uint32_t gateTicks, std::uint8_t velocity);
  void setPreviewDurationMs(std::uint32_t durationMs);
  void setFollowPreviewOnSelect(bool enabled);
  void setKeyboardOctave(int octave);
  void setEditStep(int step);
  void setCompactDensity(bool compact);
  bool isCompactDensity() const;
  void jumpToCell(int row, int channel);
  void clampSelectionToBounds();
  void recalculateGridSize();
  void repaintPlaybackRows(int previousRow, int currentRow);
  bool refreshSnapshotForPatternChange();
  int preferredCellWidth() const;
  int preferredHeaderHeight() const;
  int preferredLabelWidth() const;
  bool copySelection();
  bool cutSelection();
  bool pasteSelection();
  bool transposeSelectionUp(bool octave = false);
  bool transposeSelectionDown(bool octave = false);
  bool applyEffectToSelection(std::uint8_t effectCommand, std::uint8_t effectValue);

private:
  struct PendingPreviewNoteOff {
    std::uint8_t instrument = 0;
    std::uint16_t sample = 0xFFFF;
    int midiNote = -1;
    std::uint32_t dueMs = 0;
  };

  struct ClipboardStep {
    bool hasNote = false;
    int note = -1;
    std::uint8_t instrument = 0;
    std::uint16_t sample = 0xFFFF;  // 0xFFFF means no sample
    std::uint32_t gateTicks = 0;
    std::uint8_t velocity = 100;
    bool retrigger = false;
    std::uint8_t effectCommand = 0;
    std::uint8_t effectValue = 0;
  };

  ExTrackerApp& app;

  // Grid layout
  int cellWidth = 8;
  int cellHeight = 30;
  int headerHeight = 40;
  int labelWidth = 40;

  // Interaction state
  int hoveredRow = -1;
  int hoveredChannel = -1;
  int selectedRow = -1;
  int selectedChannel = -1;
  int blockAnchorRow = -1;
  int blockAnchorChannel = -1;
  int blockEndRow = -1;
  int blockEndChannel = -1;
  std::function<void(int, int)> selectionChangedCallback;
  std::function<void(int, int)> keyboardStateChangedCallback;
  std::function<void()> togglePlaybackCallback;
  std::function<void()> focusModuleMessageCallback;
  std::function<void(bool)> searchNavigationCallback;
  std::uint32_t insertGateTicks = 0;
  std::uint8_t insertVelocity = 100;
  std::uint32_t previewDurationMs = 160;
  bool followPreviewOnSelect = false;
  int keyboardOctave = 4;
  int editStep = 1;
  std::vector<ClipboardStep> clipboard;
  int clipboardRows = 0;
  int clipboardChannels = 0;
  bool compactDensity = false;
  std::vector<PendingPreviewNoteOff> pendingPreviewNoteOffs;

  // FX direct-entry mode (toggled with backtick)
  bool fxInputMode = false;
  std::string fxInputBuffer;  // 3 chars (CVV) or 4 chars (CCVV)
  bool fxInputPrefilledCommand = false;
  int fxInputPrefilledValueDigits = 0;
  bool fxCommitAutoAdvance = true;
  bool volumeInputMode = false;
  std::string volumeInputBuffer;  // up to 2 hex chars: velocity byte (00-FF, clamped to 1-127)
  bool sampleInputMode = false;
  std::string sampleInputBuffer;  // up to 3 hex chars: sample slot (000-0FF)

  // Cached snapshot used when the sequencer thread currently owns app.stateMutex.
  int cachedRows = 0;
  int cachedChannels = 0;
  std::uint32_t cachedPlaybackRow = 0;
  std::vector<bool> cachedHasNote;
  std::vector<int> cachedNote;
  std::vector<std::uint32_t> cachedGate;
  std::vector<std::uint8_t> cachedVelocity;
  std::vector<std::uint8_t> cachedInstrument;
  std::vector<std::uint16_t> cachedSample;
  std::vector<std::uint8_t> cachedEffectCommand;
  std::vector<std::uint8_t> cachedEffectValue;
  std::vector<bool> cachedChannelMuted;

  int getRowAtY(int y) const;
  int getChannelAtX(int x) const;
  juce::Rectangle<int> getCellBounds(int row, int channel) const;
  void repaintCell(int row, int channel);
  bool refreshSnapshot();
  void timerCallback() override;
  void previewPlacedNote(std::uint8_t instrument, std::uint16_t sample, int midiNote, std::uint8_t velocity);
  void previewSelectedStepIfEnabled(bool force = false);
  void selectCell(int row, int channel, bool preserveBlock = false);
  bool commitNoteFromKeyboard(int midiNote);
  bool clearSelectedCell();
  bool copySelectionToClipboard();
  bool cutSelectionToClipboard();
  bool pasteClipboardAtSelection();
  bool transposeSelection(int semitoneDelta);
  bool hasBlockSelection() const;
  bool isCellInBlockSelection(int row, int channel) const;
  void getBlockBounds(int& minRow, int& maxRow, int& minChannel, int& maxChannel) const;

  void drawGrid(juce::Graphics& g);
  void drawHeaders(juce::Graphics& g);
  void drawCell(juce::Graphics& g,
                bool hasNote,
                int note,
                std::uint32_t gateTicks,
                std::uint8_t velocity,
                std::uint8_t instrument,
                std::uint16_t sample,
                std::uint8_t effectCommand,
                std::uint8_t effectValue,
                int row,
                int channel,
                int x,
                int y);
  void handleCellClick(int row, int channel);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternGrid)
};
