#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "extracker/pattern_clipboard.hpp"
#include "extracker/pattern_editor.hpp"

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
  void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
  bool keyPressed(const juce::KeyPress& key) override;
  void focusGained(FocusChangeType cause) override;
  void focusLost(FocusChangeType cause) override;
  void setSelectionChangedCallback(std::function<void(int, int)> callback);
  void setKeyboardStateChangedCallback(std::function<void(int, int)> callback);
  void setTogglePlaybackCallback(std::function<void()> callback);
  void setFocusModuleMessageCallback(std::function<void()> callback);
  void setSearchNavigationCallback(std::function<void(bool)> callback);
  // Ctrl+Z (false) / Ctrl+Shift+Z or Ctrl+Y (true): undo / redo.
  void setUndoCallback(std::function<void(bool redo)> callback);
  // Right-click on a channel header.
  void setChannelHeaderMenuCallback(std::function<void(int)> callback);
  // Called with the row of a left-clicked cell.
  void setRowClickedCallback(std::function<void(int)> callback);
  // Copy, cut, slot selection or clearing changed the clipboard history.
  void setClipboardChangedCallback(std::function<void()> callback);
  const extracker::ClipboardHistory& clipboardHistory() const { return clipboardHistory_; }
  bool selectClipboardEntry(std::size_t index);
  void clearClipboardHistory();
  void setInsertDefaults(std::uint32_t gateTicks, std::uint8_t velocity);
  void setPreviewDurationMs(std::uint32_t durationMs);
  void setFollowPreviewOnSelect(bool enabled);
  void setKeyboardOctave(int octave);
  void setEditStep(int step);
  void setCompactDensity(bool compact);
  bool isCompactDensity() const;
  // Centered view: a fixed line in the middle of the grid holds the playing
  // row (or, when stopped, the cursor row) and the pattern scrolls smoothly
  // under it, with the neighbouring patterns dimmed above and below. Off: the
  // whole pattern is laid out and the viewport scrolls it.
  void setCenteredView(bool enabled);
  bool isCenteredView() const { return centeredView; }
  // Called every GUI frame: moves the centered view with the playback position.
  void updatePlaybackView();
  // Row on the center line (fractional while playing).
  double viewCenterRow() const { return viewRow; }
  void setViewCenterRow(double row);
  void jumpToCell(int row, int channel);
  void clampSelectionToBounds();
  void recalculateGridSize();
  void repaintPlaybackRows(int previousRow, int currentRow);
  bool refreshSnapshotForPatternChange();
  int preferredCellWidth() const;
  int preferredHeaderHeight() const;
  int preferredLabelWidth() const;
  int currentCellWidth() const;
  int currentLabelWidth() const;
  bool copySelection();
  bool cutSelection();
  bool pasteSelection(extracker::PatternClipboard::PasteMode mode = extracker::PatternClipboard::PasteMode::Overwrite,
                      bool flood = false);
  // Which step columns the next paste touches (PatternClipboard::Column bits,
  // never 0). Resets to all columns after every paste.
  unsigned pasteColumns() const { return pasteColumns_; }
  void setPasteColumns(unsigned columns);
  bool transposeSelectionUp(bool octave = false);
  bool transposeSelectionDown(bool octave = false);
  // Gives every note in the marked block (or, with no block, the selected
  // channel) `instrument`. Returns the number of notes changed, -1 if busy.
  int setInstrumentOnSelection(std::uint8_t instrument);
  bool hasBlockSelection() const;
  // Channels covered by the marked block (or the selected cell).
  void selectedChannelRange(int& first, int& last) const;
  bool applyEffectToSelection(std::uint8_t effectCommand, std::uint8_t effectValue);

private:
  struct PendingPreviewNoteOff {
    std::uint8_t instrument = 0;
    int midiNote = -1;
    std::uint32_t dueMs = 0;
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
  std::function<void(bool)> undoCallback;
  std::function<void(int)> channelHeaderMenuCallback;
  std::function<void(int)> rowClickedCallback;
  std::uint32_t insertGateTicks = 0;
  std::uint8_t insertVelocity = 100;
  std::uint32_t previewDurationMs = 160;
  bool followPreviewOnSelect = false;
  int keyboardOctave = 4;
  int editStep = 1;
  extracker::ClipboardHistory clipboardHistory_;
  std::function<void()> clipboardChangedCallback;
  unsigned pasteColumns_ = extracker::PatternClipboard::kAllColumns;
  bool compactDensity = false;
  std::vector<PendingPreviewNoteOff> pendingPreviewNoteOffs;

  // Centered view state.
  bool centeredView = true;
  double viewRow = 0.0;
  bool followingPlayback = false;
  bool recenterOnSelect = true;  // off while the mouse selects, so rows don't move under it
  std::uint64_t lastSeenTick = 0;
  double lastTickMs = 0.0;
  double lastFrameMs = 0.0;
  struct GhostPattern {
    int rows = 0;
    int channels = 0;
    std::vector<extracker::PatternEditor::Step> steps;
  };
  GhostPattern ghostBefore;  // pattern shown above row 0
  GhostPattern ghostAfter;   // pattern shown below the last row

  // FX direct-entry mode (toggled with backtick)
  bool fxInputMode = false;
  std::string fxInputBuffer;  // 3 chars (CVV) or 4 chars (CCVV)
  bool fxInputPrefilledCommand = false;
  int fxInputPrefilledValueDigits = 0;
  bool fxCommitAutoAdvance = true;
  bool volumeInputMode = false;
  std::string volumeInputBuffer;  // up to 2 hex chars: velocity byte (00-FF, clamped to 1-127)
  bool instrumentInputMode = false;
  std::string instrumentInputBuffer;  // up to 2 hex chars: instrument (00-FF)

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

  double centerRowTop() const;
  int rowTop(int row) const;
  void snapshotGhostPatterns();
  int getRowAtY(int y) const;
  int getChannelAtX(int x) const;
  juce::Rectangle<int> getCellBounds(int row, int channel) const;
  void repaintCell(int row, int channel);
  bool refreshSnapshot();
  void timerCallback() override;
  void previewPlacedNote(std::uint8_t instrument, int midiNote, std::uint8_t velocity);
  void previewSelectedStepIfEnabled(bool force = false);
  void selectCell(int row, int channel, bool preserveBlock = false);
  bool commitNoteFromKeyboard(int midiNote);
  bool clearSelectedCell();
  bool copySelectionToClipboard();
  bool cutSelectionToClipboard();
  bool pasteClipboardAtSelection(extracker::PatternClipboard::PasteMode mode, bool flood);
  bool transposeSelection(int semitoneDelta);
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
                int y,
                bool ghost = false);
  void handleCellClick(int row, int channel);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternGrid)
};
