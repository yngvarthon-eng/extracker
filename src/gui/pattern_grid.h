#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ExTrackerApp;

class PatternGrid : public juce::Component {
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
  void setInsertDefaults(std::uint32_t gateTicks, std::uint8_t velocity);
  void setKeyboardOctave(int octave);
  void setEditStep(int step);
  void setCompactDensity(bool compact);
  bool isCompactDensity() const;
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
  std::uint32_t insertGateTicks = 0;
  std::uint8_t insertVelocity = 100;
  int keyboardOctave = 4;
  int editStep = 1;
  std::vector<ClipboardStep> clipboard;
  int clipboardRows = 0;
  int clipboardChannels = 0;
  bool compactDensity = false;

  // FX direct-entry mode (toggled with backtick)
  bool fxInputMode = false;
  std::string fxInputBuffer;  // up to 3 hex chars: [0]=cmd nibble, [1..2]=value byte

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
                std::uint8_t effectCommand,
                std::uint8_t effectValue,
                int row,
                int channel,
                int x,
                int y);
  void handleCellClick(int row, int channel);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternGrid)
};
