#include "pattern_grid.h"
#include "app.h"
#include "extracker/pattern_editor.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

std::uint8_t defaultInsertInstrument(const ExTrackerApp& app, int channel) {
  // activeSampleSlot is written separately into the step's sample field, so it
  // must NOT also override the instrument field — that would corrupt notes placed
  // on other channels/instruments while a sample slot is armed in the panel.
  if (channel >= 0 && static_cast<std::size_t>(channel) < app.channelInstruments.size()) {
    return app.channelInstruments[static_cast<std::size_t>(channel)];
  }
  return static_cast<std::uint8_t>(std::clamp(app.midiInstrument, 0, 255));
}

std::string toUpperHex(unsigned int value, int width) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
  return out.str();
}

std::string formatTrackerNote(int midiNote) {
  if (midiNote < 0 || midiNote > 127) {
    return "...";
  }

  static const char* kNoteNames[12] = {
      "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
  const int octave = midiNote / 12;
  const int noteInOctave = midiNote % 12;
  return std::string(kNoteNames[noteInOctave]) + std::to_string(octave);
}

bool lockStateWithRetry(std::unique_lock<std::mutex>& lock) {
  constexpr int kAttempts = 12;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    if (lock.try_lock()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

}  // namespace

PatternGrid::PatternGrid(ExTrackerApp& app) : app(app) {
  setSize(800, 400);
  setWantsKeyboardFocus(true);
}

PatternGrid::~PatternGrid() {
  stopTimer();
}

void PatternGrid::setSelectionChangedCallback(std::function<void(int, int)> callback) {
  selectionChangedCallback = std::move(callback);
}

void PatternGrid::setKeyboardStateChangedCallback(std::function<void(int, int)> callback) {
  keyboardStateChangedCallback = std::move(callback);
}

void PatternGrid::setTogglePlaybackCallback(std::function<void()> callback) {
  togglePlaybackCallback = std::move(callback);
}

void PatternGrid::setFocusModuleMessageCallback(std::function<void()> callback) {
  focusModuleMessageCallback = std::move(callback);
}

void PatternGrid::setSearchNavigationCallback(std::function<void(bool)> callback) {
  searchNavigationCallback = std::move(callback);
}

void PatternGrid::setInsertDefaults(std::uint32_t gateTicks, std::uint8_t velocity) {
  insertGateTicks = gateTicks;
  insertVelocity = std::max<std::uint8_t>(velocity, 1);
}

void PatternGrid::setPreviewDurationMs(std::uint32_t durationMs) {
  previewDurationMs = std::clamp<std::uint32_t>(durationMs, 20, 2000);
}

void PatternGrid::setFollowPreviewOnSelect(bool enabled) {
  followPreviewOnSelect = enabled;
}

void PatternGrid::setKeyboardOctave(int octave) {
  int clamped = std::clamp(octave, 0, 8);
  if (clamped == keyboardOctave) {
    return;
  }
  keyboardOctave = clamped;
  if (keyboardStateChangedCallback) {
    keyboardStateChangedCallback(keyboardOctave, editStep);
  }
}

void PatternGrid::setEditStep(int step) {
  int clamped = std::clamp(step, 1, 16);
  if (clamped == editStep) {
    return;
  }
  editStep = clamped;
  if (keyboardStateChangedCallback) {
    keyboardStateChangedCallback(keyboardOctave, editStep);
  }
}

void PatternGrid::setCompactDensity(bool compact) {
  if (compactDensity == compact) {
    return;
  }

  compactDensity = compact;
  cellWidth = compactDensity ? 6 : 8;
  cellHeight = compactDensity ? 22 : 30;
  headerHeight = compactDensity ? 32 : 40;  // Channel numbers + channel info + hint band
  labelWidth = compactDensity ? 32 : 40;
  resized();
  repaint();
}

bool PatternGrid::isCompactDensity() const {
  return compactDensity;
}

void PatternGrid::jumpToCell(int row, int channel) {
  const int maxRow = std::max(0, static_cast<int>(app.module.currentEditor().rows()) - 1);
  const int maxChannel = std::max(0, static_cast<int>(app.module.currentEditor().channels()) - 1);
  const int targetRow = std::clamp(row, 0, maxRow);
  const int targetChannel = std::clamp(channel, 0, maxChannel);
  grabKeyboardFocus();
  selectCell(targetRow, targetChannel, false);
}

int PatternGrid::preferredCellWidth() const {
  return compactDensity ? 6 : 8;
}

int PatternGrid::preferredHeaderHeight() const {
  return compactDensity ? 30 : 36;
}

int PatternGrid::preferredLabelWidth() const {
  return compactDensity ? 32 : 40;
}

int PatternGrid::currentCellWidth() const { return cellWidth; }
int PatternGrid::currentLabelWidth() const { return labelWidth; }

bool PatternGrid::copySelection() {
  return copySelectionToClipboard();
}

bool PatternGrid::cutSelection() {
  return cutSelectionToClipboard();
}

bool PatternGrid::pasteSelection() {
  return pasteClipboardAtSelection();
}

bool PatternGrid::transposeSelectionUp(bool octave) {
  return transposeSelection(octave ? 12 : 1);
}

bool PatternGrid::transposeSelectionDown(bool octave) {
  return transposeSelection(octave ? -12 : -1);
}

void PatternGrid::recalculateGridSize() {
  if (getParentComponent() == nullptr) {
    return;
  }

  const int viewportWidth = getParentComponent()->getWidth();
  const int channels = static_cast<int>(app.module.currentEditor().channels());
  const int preferredWidth = labelWidth + (channels * cellWidth);

  if (preferredWidth != getWidth()) {
    setSize(std::max(preferredWidth, viewportWidth), getHeight());
  }
}

void PatternGrid::repaintPlaybackRows(int previousRow, int currentRow) {
  const int maxRow = std::max(0, static_cast<int>(app.module.currentEditor().rows()) - 1);
  auto repaintRow = [this, maxRow](int row) {
    if (row < 0 || row > maxRow) {
      return;
    }
    const int y = headerHeight + row * cellHeight;
    repaint(0, y, getWidth(), cellHeight);
  };

  repaintRow(previousRow);
  if (currentRow != previousRow) {
    repaintRow(currentRow);
  }
}

void PatternGrid::clampSelectionToBounds() {
  const int maxRow = std::max(0, static_cast<int>(app.module.currentEditor().rows()) - 1);
  const int maxChannel = std::max(0, static_cast<int>(app.module.currentEditor().channels()) - 1);

  int nextRow = selectedRow;
  int nextChannel = selectedChannel;
  if (nextRow < 0 || nextChannel < 0) {
    nextRow = 0;
    nextChannel = 0;
  } else {
    nextRow = std::clamp(nextRow, 0, maxRow);
    nextChannel = std::clamp(nextChannel, 0, maxChannel);
  }

  blockAnchorRow = std::clamp(blockAnchorRow, 0, maxRow);
  blockEndRow = std::clamp(blockEndRow, 0, maxRow);
  blockAnchorChannel = std::clamp(blockAnchorChannel, 0, maxChannel);
  blockEndChannel = std::clamp(blockEndChannel, 0, maxChannel);

  selectCell(nextRow, nextChannel, false);
  refreshSnapshot();
  repaint();
}

void PatternGrid::paint(juce::Graphics& g) {
  // Background
  g.fillAll(app.darkMode ? juce::Colour(0xFF111111) : juce::Colours::darkgrey);

  drawHeaders(g);
  drawGrid(g);

  if (hasKeyboardFocus(true)) {
    g.setColour(juce::Colour(0xFFE8C547));
    g.drawRect(getLocalBounds().reduced(1), 2);
  }
}

void PatternGrid::resized() {
  // Calculate sizes based on available space
  int availableWidth = getWidth() - labelWidth;
  int availableHeight = getHeight() - headerHeight;

  if (availableWidth > 0 && availableHeight > 0) {
    int numChannels = static_cast<int>(app.module.currentEditor().channels());
    int numRows = static_cast<int>(app.module.currentEditor().rows());

    if (numChannels > 0 && numRows > 0) {
      const int minCellWidth = compactDensity ? 18 : 24;
      const int minCellHeight = compactDensity ? 14 : 20;
      cellWidth = std::max(minCellWidth, availableWidth / numChannels);
      cellHeight = std::max(minCellHeight, availableHeight / numRows);
    }
  }
}

void PatternGrid::drawHeaders(juce::Graphics& g) {
  g.setColour(app.darkMode ? juce::Colour(0xFF0D1117) : juce::Colours::darkslategrey);
  g.fillRect(0, 0, getWidth(), headerHeight);

  const int channelNumberBandHeight = compactDensity ? 14 : 16;
  const int channelInfoBandHeight = 14;
  const int channelInfoBandY = channelNumberBandHeight;
  const int hintBandY = channelNumberBandHeight + channelInfoBandHeight;
  const int hintBandHeight = std::max(0, headerHeight - hintBandY);

  // Draw channel numbers band
  g.setColour(juce::Colours::white);
  g.setFont(10.0f);

  int numChannels = static_cast<int>(app.module.currentEditor().channels());
  for (int ch = 0; ch < numChannels; ++ch) {
    int x = labelWidth + ch * cellWidth;

    // Check if channel is muted
    bool isMuted = false;
    if (static_cast<std::size_t>(ch) < app.channelMuted.size()) {
      isMuted = app.channelMuted[static_cast<std::size_t>(ch)];
    }

    if (isMuted) {
      g.setColour(juce::Colour(0xFFFF6666));  // Red for muted
    } else {
      g.setColour(juce::Colours::white);
    }

    juce::String label = juce::String(ch);
    if (isMuted) {
      label += "⊘";
    }
    g.drawText(label, x, 0, cellWidth, channelNumberBandHeight, juce::Justification::centred);
  }

  g.setColour(juce::Colours::dimgrey);
  g.drawHorizontalLine(channelNumberBandHeight - 1, 0.0f, static_cast<float>(getWidth()));

  // Draw channel info band
  g.setColour(juce::Colours::lightgrey);
  g.setFont(compactDensity ? 9.0f : 10.0f);

  for (int ch = 0; ch < numChannels; ++ch) {
    int x = labelWidth + ch * cellWidth;
    int y = channelInfoBandY;

    // Instrument
    std::string infoText;
    if (static_cast<std::size_t>(ch) < app.channelInstruments.size()) {
      infoText = "I:" + toUpperHex(app.channelInstruments[static_cast<std::size_t>(ch)], 2);
    } else {
      infoText = "I:--";
    }

    // Pan
    std::uint8_t pan = app.sequencer.panByChannel(static_cast<std::size_t>(ch));
    if (pan < 0x50) {
      infoText += " L";
    } else if (pan > 0xB0) {
      infoText += " R";
    } else {
      infoText += " C";
    }

    // Voice count
    int voiceCount = 0;
    if (static_cast<std::size_t>(ch) < app.channelInstruments.size()) {
      voiceCount = static_cast<int>(app.plugins.activeVoiceCountForInstrument(
          app.channelInstruments[static_cast<std::size_t>(ch)]));
    }
    infoText += " " + std::to_string(std::min(voiceCount, 9)) + "v";

    g.drawText(juce::String(infoText), x + 1, y + 1, cellWidth - 2, channelInfoBandHeight - 2,
               juce::Justification::centred);
  }

  g.setColour(juce::Colours::dimgrey);
  g.drawHorizontalLine(hintBandY - 1, 0.0f, static_cast<float>(getWidth()));

  // Draw hint band
  g.setColour(juce::Colours::lightgrey);
  g.setFont(compactDensity ? 7.0f : 8.0f);
  juce::String hint = "KB" + juce::String(keyboardOctave) + " Stp" + juce::String(editStep);
  if (fxInputMode) hint = "[FX] " + hint;
  if (volumeInputMode) hint = "[VOL] " + hint;
  if (sampleInputMode) hint = "[SMP] " + hint;
  if (selectedRow >= 0 && selectedChannel >= 0) {
    int minRow = selectedRow;
    int maxRow = selectedRow;
    int minChannel = selectedChannel;
    int maxChannel = selectedChannel;
    getBlockBounds(minRow, maxRow, minChannel, maxChannel);
    hint += "  Sel:" + juce::String(maxRow - minRow + 1) + "x" + juce::String(maxChannel - minChannel + 1);
  }
  hint += "  FXadv:" + juce::String(fxCommitAutoAdvance ? "ON" : "OFF") + "(F6)";
  hint += "  F2:step-edit  </> switch col(Smp|Vol|FX)  Esc:exit  FX:|/`  VOL:'  SMP:;";
  hint += "  Alt+drag:scrub  Shift+arrows/drag:mark  Ctrl+C/X/V  Ctrl+Up/Down:transpose";
  if (hintBandHeight > 0) {
    g.drawText(hint, 4, hintBandY, getWidth() - 8, hintBandHeight, juce::Justification::centredLeft, true);
  }
}

void PatternGrid::drawGrid(juce::Graphics& g) {
  refreshSnapshot();

  int startY = headerHeight;
  int numRows = cachedRows;
  int numChannels = cachedChannels;
  std::uint32_t playbackRow = app.transport.currentRow();
  if (numRows <= 0 || numChannels <= 0) {
    return;
  }

  for (int row = 0; row < numRows; ++row) {
    int y = startY + row * cellHeight;

    for (int ch = 0; ch < numChannels; ++ch) {
      int x = labelWidth + ch * cellWidth;
      std::size_t index = static_cast<std::size_t>(row * numChannels + ch);

      drawCell(g,
                 cachedHasNote[index],
                 cachedNote[index],
                 cachedGate[index],
                 cachedVelocity[index],
                 cachedInstrument[index],
                 cachedSample[index],
                 cachedEffectCommand[index],
                 cachedEffectValue[index],
               row,
               ch,
               x,
               y);
    }

    // Draw row number
    g.setColour(juce::Colours::lightgrey);
    g.setFont(9.0f);
    g.drawText(juce::String(row), 0, y, labelWidth, cellHeight, juce::Justification::centred);

    // Shade every 4th tick (rows 0, 4, 8, 12... in 0-indexed) - draw AFTER cells as overlay
    if (row % 4 == 0) {
      g.setColour(app.darkMode ? juce::Colour(0x28FFFFFF) : juce::Colour(0x40383838));
      g.fillRect(labelWidth, y, getWidth() - labelWidth, cellHeight);
    }

    // Highlight playback row
      if (static_cast<int>(playbackRow) == row) {
      g.setColour(juce::Colour(0xFF444444));
      g.drawRect(labelWidth, y, getWidth() - labelWidth, cellHeight, 2);
    }
  }

  // Draw grid lines
  g.setColour(juce::Colours::black);
  for (int row = 0; row <= numRows; ++row) {
    int y = startY + row * cellHeight;
    g.drawLine(labelWidth, y, getWidth(), y, 1.0f);
  }

  for (int ch = 0; ch <= numChannels; ++ch) {
    int x = labelWidth + ch * cellWidth;
    g.drawLine(x, startY, x, getHeight(), 1.0f);
  }

  for (int ch = 0; ch < numChannels; ++ch) {
    if (static_cast<std::size_t>(ch) >= cachedChannelMuted.size() ||
        !cachedChannelMuted[static_cast<std::size_t>(ch)]) {
      continue;
    }

    int x = labelWidth + ch * cellWidth;
    g.setColour(juce::Colour(0x551A0000));
    g.fillRect(x + 1, startY + 1, cellWidth - 1, numRows * cellHeight - 1);
    g.setColour(juce::Colour(0x99FF6666));
    g.drawLine(static_cast<float>(x + 1),
               static_cast<float>(startY + 1),
               static_cast<float>(x + 1),
               static_cast<float>(startY + numRows * cellHeight - 1),
               1.5f);
    g.drawLine(static_cast<float>(x + cellWidth - 1),
               static_cast<float>(startY + 1),
               static_cast<float>(x + cellWidth - 1),
               static_cast<float>(startY + numRows * cellHeight - 1),
               1.5f);
  }

  if (fxInputMode) {
    const int badgeH = compactDensity ? 18 : 22;
    juce::Rectangle<int> badge(labelWidth + 6, headerHeight - badgeH - 2, 188, badgeH);
    g.setColour(juce::Colour(0xCC1F1F1F));
    g.fillRoundedRectangle(badge.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xFFFFCC00));
    g.drawRoundedRectangle(badge.toFloat(), 5.0f, 1.0f);
    g.setFont(compactDensity ? 11.0f : 12.0f);
    g.drawText("FX  (< Vol | F2/Esc: exit)", badge, juce::Justification::centred);
  }

  if (volumeInputMode) {
    const int badgeH = compactDensity ? 18 : 22;
    juce::Rectangle<int> badge(labelWidth + 202, headerHeight - badgeH - 2, 148, badgeH);
    g.setColour(juce::Colour(0xCC1F1F1F));
    g.fillRoundedRectangle(badge.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xFFFFCC00));
    g.drawRoundedRectangle(badge.toFloat(), 5.0f, 1.0f);
    g.setFont(compactDensity ? 11.0f : 12.0f);
    g.drawText("Vol  (< Smp | > FX)", badge, juce::Justification::centred);
  }

  if (sampleInputMode) {
    const int badgeH = compactDensity ? 18 : 22;
    juce::Rectangle<int> badge(labelWidth + 358, headerHeight - badgeH - 2, 188, badgeH);
    g.setColour(juce::Colour(0xCC1F1F1F));
    g.fillRoundedRectangle(badge.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xFFFFCC00));
    g.drawRoundedRectangle(badge.toFloat(), 5.0f, 1.0f);
    g.setFont(compactDensity ? 11.0f : 12.0f);
    g.drawText("Smp  (> Vol | F2/Esc: exit)", badge, juce::Justification::centred);
  }
}

void PatternGrid::drawCell(juce::Graphics& g,
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
                           int y) {
  juce::ignoreUnused(gateTicks);

  const bool hasEffect = (effectCommand != 0 || effectValue != 0);
  const bool isNoteOff = hasNote && note < 0;

  // Cell background
  if (isCellInBlockSelection(row, channel)) {
    g.setColour(hasNote ? juce::Colour(0xFF2F6A44) : juce::Colour(0xFF4A5D77));
  } else if (hoveredRow == row && hoveredChannel == channel) {
    g.setColour(app.darkMode ? juce::Colour(0xFF001A33) : juce::Colours::darkblue);
  } else if (isNoteOff) {
    g.setColour(juce::Colour(0xFF3A1A1A));
  } else if (hasNote) {
    g.setColour(app.darkMode ? juce::Colour(0xFF143C14) : juce::Colours::darkgreen);
  } else if (hasEffect) {
    g.setColour(juce::Colour(0xFF2A2A4A));
  } else {
    g.setColour(app.darkMode ? juce::Colour(0xFF1E1E1E) : juce::Colours::grey);
  }

  g.fillRect(x + 1, y + 1, cellWidth - 2, cellHeight - 2);

  if (selectedRow == row && selectedChannel == channel) {
    g.setColour(juce::Colour(0xFFF2CC60));
    g.drawRect(x + 1, y + 1, cellWidth - 2, cellHeight - 2, 2);
  }

    // Tracker-style tick text (single line):
    // Empty no-fx:   ... ... ... ....
    // Empty with fx: ... ... ... 1706
    // Filled:         C-4 100 a64 0E00

  std::string effectText = (effectCommand != 0 || effectValue != 0)
      ? toUpperHex(effectCommand, 2) + toUpperHex(effectValue, 2)
      : "....";
  const bool showLiveFxPreview = row == selectedRow && channel == selectedChannel && fxInputMode;
  if (showLiveFxPreview) {
    if (fxInputBuffer.empty() && hasEffect) {
      // Buffer not yet started — show committed effect so it remains visible while navigating in FX mode
    } else {
      effectText = fxInputBuffer;
      while (effectText.size() < 4) {
        effectText += '.';
      }
    }
  }

  const bool isVolumeOnlyEffect = (!hasNote && effectCommand == 0x0C && effectValue > 0);

  int sampleSlot = -1;
  if (sample != 0xFFFF) {
    sampleSlot = static_cast<int>(sample);
  } else {
    sampleSlot = app.plugins.sampleSlotForInstrument(instrument);
    if (sampleSlot < 0 && !app.plugins.samplePathForSlot(static_cast<std::uint16_t>(instrument)).empty()) {
      sampleSlot = instrument;
    }
  }
  const std::string sampleSlotText = sampleSlot >= 0 ? toUpperHex(static_cast<unsigned int>(sampleSlot), 3) : "...";

  juce::String tickText;
  if (isNoteOff) {
    tickText = "^^^ ... ... ....";
  } else if (hasNote) {
    const std::string noteText = formatTrackerNote(note);
    const bool velocityIsDefault = (velocity == extracker::PatternEditor::kDefaultVelocity);
    const std::string volumeFxText = velocityIsDefault ? "..." : std::string("a") + toUpperHex(velocity, 2);
    tickText = juce::String(noteText) + " " + juce::String(sampleSlotText) + " " +
               juce::String(volumeFxText) + " " + juce::String(effectText);
  } else if (isVolumeOnlyEffect) {
    const std::string volumeFxText = std::string("a") + toUpperHex(effectValue, 2);
    tickText = juce::String("... ") + juce::String(sampleSlotText) + " " +
               juce::String(volumeFxText) + " " + juce::String(effectText);
  } else if (hasEffect) {
    tickText = juce::String("... ") + juce::String(sampleSlotText) + " ... " + juce::String(effectText);
  } else {
    tickText = juce::String("... ") + juce::String(sampleSlotText) + " ... ....";
  }

  g.setColour(showLiveFxPreview ? juce::Colour(0xFFFFCC00) : juce::Colours::white);
  g.setFont(compactDensity ? 15.0f : 18.0f);
  const int innerX = x + 2;
  const int innerY = y + 2;
  const int innerW = cellWidth - 4;
  const int innerH = cellHeight - 4;
  g.drawText(tickText, innerX, innerY, innerW, innerH, juce::Justification::centredLeft, true);

  if (row == selectedRow && channel == selectedChannel && volumeInputMode) {
    std::string volVisual = volumeInputBuffer;
    while (volVisual.size() < 2) volVisual += '.';
    volVisual = std::string("a") + volVisual;
    g.setColour(juce::Colour(0xFFFFCC00));
    g.setFont(compactDensity ? 15.0f : 18.0f);
    g.drawText(juce::String(volVisual), innerX, innerY, innerW, innerH, juce::Justification::centredRight, false);
  }

  if (row == selectedRow && channel == selectedChannel && sampleInputMode) {
    std::string sampleVisual = sampleInputBuffer;
    while (sampleVisual.size() < 3) sampleVisual += '.';
    g.setColour(juce::Colour(0xFFFFCC00));
    g.setFont(compactDensity ? 15.0f : 18.0f);
    g.drawText(juce::String(sampleVisual), innerX, innerY, innerW, innerH, juce::Justification::centred, false);
  }
}

int PatternGrid::getRowAtY(int y) const {
  if (y < headerHeight) {
    return -1;
  }
  int row = (y - headerHeight) / cellHeight;
  if (row >= static_cast<int>(app.module.currentEditor().rows())) {
    return -1;
  }
  return row;
}

int PatternGrid::getChannelAtX(int x) const {
  if (x < labelWidth) {
    return -1;
  }
  int ch = (x - labelWidth) / cellWidth;
  if (ch >= static_cast<int>(app.module.currentEditor().channels())) {
    return -1;
  }
  return ch;
}

juce::Rectangle<int> PatternGrid::getCellBounds(int row, int channel) const {
  if (row < 0 || channel < 0) {
    return {};
  }
  if (row >= static_cast<int>(app.module.currentEditor().rows()) || channel >= static_cast<int>(app.module.currentEditor().channels())) {
    return {};
  }

  int x = labelWidth + channel * cellWidth;
  int y = headerHeight + row * cellHeight;
  return {x, y, cellWidth, cellHeight};
}

void PatternGrid::repaintCell(int row, int channel) {
  auto bounds = getCellBounds(row, channel);
  if (!bounds.isEmpty()) {
    repaint(bounds);
  }
}

void PatternGrid::mouseDown(const juce::MouseEvent& event) {
  int row = getRowAtY(event.y);
  int channel = getChannelAtX(event.x);
  const bool auditionScrub = event.mods.isAltDown();

  if (row >= 0 && channel >= 0) {
    grabKeyboardFocus();
    selectCell(row, channel);
    if (auditionScrub) {
      previewSelectedStepIfEnabled(true);
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
    if (!lockStateWithRetry(lock)) {
      return;
    }

    if (event.mods.isRightButtonDown()) {
      // Right-click: clear note
      app.module.currentEditor().clearStep(row, channel);
    } else if (event.mods.isLeftButtonDown() && !auditionScrub) {
      // Left-click: select existing note, or insert a default note into an empty cell.
      // While in a step-edit column mode (FX/Vol/Smp), click just navigates — no auto-insert.
      if (!fxInputMode && !volumeInputMode && !sampleInputMode &&
          !app.module.currentEditor().hasNoteAt(row, channel)) {
        const bool sampleArmed = app.activeSampleSlot >= 0 && app.activeSampleSlot <= 255;
        const int numCh = static_cast<int>(app.module.currentEditor().channels());
        const int targetChannel = (sampleArmed && app.sampleTargetChannel >= 0 &&
                                    app.sampleTargetChannel < numCh)
            ? app.sampleTargetChannel : channel;
        const std::uint8_t instrument = defaultInsertInstrument(app, targetChannel);
        app.module.currentEditor().insertNote(row, targetChannel, 60, instrument, insertGateTicks, insertVelocity, true);
        std::uint16_t sample = 0xFFFF;
        if (sampleArmed) {
          sample = static_cast<std::uint16_t>(app.activeSampleSlot);
          app.module.currentEditor().setSample(row, targetChannel, sample);
        } else {
          app.module.currentEditor().setSample(row, targetChannel, 0xFFFF);
        }
        lock.unlock();
        previewPlacedNote(instrument, sample, 60, insertVelocity);
        refreshSnapshot();
        repaintCell(row, targetChannel);
        return;
      }
    }

    lock.unlock();
    refreshSnapshot();
    repaintCell(row, channel);
  }
}

void PatternGrid::mouseDrag(const juce::MouseEvent& event) {
  int row = getRowAtY(event.y);
  int channel = getChannelAtX(event.x);
  if (row < 0 || channel < 0) {
    return;
  }

  if (event.mods.isAltDown()) {
    selectCell(row, channel, false);
    previewSelectedStepIfEnabled(true);
    return;
  }

  if (selectedRow < 0 || selectedChannel < 0) {
    selectCell(row, channel);
    return;
  }

  if (blockAnchorRow < 0 || blockAnchorChannel < 0) {
    blockAnchorRow = selectedRow;
    blockAnchorChannel = selectedChannel;
  }

  selectCell(row, channel, true);
  blockEndRow = selectedRow;
  blockEndChannel = selectedChannel;
  repaint();
}

bool PatternGrid::keyPressed(const juce::KeyPress& key) {
  const juce::juce_wchar rawChar = key.getTextCharacter();
  const auto mods = key.getModifiers();

  // Step-edit column helpers: left-to-right visual order is Smp | Vol | FX
  auto enterFxCol = [this]() {
    fxInputMode = true;
    fxInputBuffer.clear();
    fxInputPrefilledCommand = false;
    fxInputPrefilledValueDigits = 0;
    volumeInputMode = false;
    volumeInputBuffer.clear();
    sampleInputMode = false;
    sampleInputBuffer.clear();
    if (selectedRow >= 0 && selectedChannel >= 0) {
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (lock.owns_lock()) {
        const auto ec = app.module.currentEditor().effectCommandAt(selectedRow, selectedChannel);
        const auto ev = app.module.currentEditor().effectValueAt(selectedRow, selectedChannel);
        if (ec != 0 || ev != 0) {
          fxInputBuffer = toUpperHex(ec, 2) + toUpperHex(ev, 2);
          fxInputPrefilledCommand = true;
        }
      }
    }
  };
  auto enterVolCol = [this]() {
    volumeInputMode = true;
    volumeInputBuffer.clear();
    fxInputMode = false;
    fxInputBuffer.clear();
    fxInputPrefilledCommand = false;
    fxInputPrefilledValueDigits = 0;
    sampleInputMode = false;
    sampleInputBuffer.clear();
  };
  auto enterSmpCol = [this]() {
    sampleInputMode = true;
    sampleInputBuffer.clear();
    fxInputMode = false;
    fxInputBuffer.clear();
    fxInputPrefilledCommand = false;
    fxInputPrefilledValueDigits = 0;
    volumeInputMode = false;
    volumeInputBuffer.clear();
  };
  auto exitStepEdit = [this]() {
    fxInputMode = false;
    fxInputBuffer.clear();
    fxInputPrefilledCommand = false;
    fxInputPrefilledValueDigits = 0;
    volumeInputMode = false;
    volumeInputBuffer.clear();
    sampleInputMode = false;
    sampleInputBuffer.clear();
  };

  if (mods.isCommandDown() && key.getKeyCode() == 'M') {
    if (focusModuleMessageCallback) {
      focusModuleMessageCallback();
      return true;
    }
  }

  if (mods.isCommandDown() && key.getKeyCode() == juce::KeyPress::F4Key && searchNavigationCallback) {
    searchNavigationCallback(!mods.isShiftDown());
    return true;
  }

  if (key == juce::KeyPress::F6Key) {
    fxCommitAutoAdvance = !fxCommitAutoAdvance;
    repaint();
    return true;
  }

  // F2: toggle step-edit mode (enter FX column, or exit if already in any column)
  if (key == juce::KeyPress::F2Key) {
    if (fxInputMode || volumeInputMode || sampleInputMode) {
      exitStepEdit();
    } else {
      enterFxCol();
    }
    if (selectedRow >= 0 && selectedChannel >= 0)
      repaintCell(selectedRow, selectedChannel);
    return true;
  }

  // FX column direct toggle: ` or |
  if (rawChar == '`' || rawChar == '|') {
    if (fxInputMode) exitStepEdit(); else enterFxCol();
    if (selectedRow >= 0 && selectedChannel >= 0)
      repaintCell(selectedRow, selectedChannel);
    return true;
  }

  // Volume mode toggle: ' or F3. Input is 2 hex digits to set the volume column.
  if (rawChar == '\'' || key == juce::KeyPress::F3Key) {
    volumeInputMode = !volumeInputMode;
    volumeInputBuffer.clear();
    if (volumeInputMode) {
      fxInputMode = false;
      fxInputBuffer.clear();
      sampleInputMode = false;
      sampleInputBuffer.clear();
    }
    if (selectedRow >= 0 && selectedChannel >= 0) {
      repaintCell(selectedRow, selectedChannel);
    }
    return true;
  }

  // Sample mode toggle: ; or F4. Input is 3 hex digits for sample slot (000-0FF).
  if (rawChar == ';' || key == juce::KeyPress::F4Key) {
    sampleInputMode = !sampleInputMode;
    sampleInputBuffer.clear();
    if (sampleInputMode) {
      fxInputMode = false;
      fxInputBuffer.clear();
      volumeInputMode = false;
      volumeInputBuffer.clear();
    }
    if (selectedRow >= 0 && selectedChannel >= 0) {
      repaintCell(selectedRow, selectedChannel);
    }
    return true;
  }

  // In FX entry mode: hex digits go into the effect buffer
  if (fxInputMode) {
    auto commitFxInputBuffer = [this](bool advanceRow) -> bool {
      if (fxInputBuffer.empty() || selectedRow < 0 || selectedChannel < 0) {
        return false;
      }

      std::string normalized = fxInputBuffer;
      std::replace(normalized.begin(), normalized.end(), '.', '0');
      if (normalized.size() == 3) {
        // 3-char compatibility mode:
        // - CVV legacy shorthand (e.g. F06 -> 0F06)
        // - CCV shorthand for low command bytes (e.g. 170 -> 1700)
        //   This makes full-byte commands practical without always typing 4 chars.
        const unsigned int firstTwo = std::stoul(normalized.substr(0, 2), nullptr, 16);
        if (firstTwo <= 0x1F) {
          normalized.push_back('0');
        } else {
          normalized.insert(normalized.begin(), '0');
        }
      }
      if (normalized.size() != 4) {
        return false;
      }

      unsigned int cmd = std::stoul(normalized.substr(0, 2), nullptr, 16);
      unsigned int val = std::stoul(normalized.substr(2, 2), nullptr, 16);
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        return false;
      }

      app.module.currentEditor().setEffect(
          selectedRow,
          selectedChannel,
          static_cast<std::uint8_t>(cmd),
          static_cast<std::uint8_t>(val));
      int nextRow = std::min(static_cast<int>(app.module.currentEditor().rows()) - 1, selectedRow + editStep);
      lock.unlock();

      fxInputBuffer.clear();
      fxInputPrefilledCommand = false;
      fxInputPrefilledValueDigits = 0;
      refreshSnapshot();
      if (selectionChangedCallback) {
        selectionChangedCallback(selectedRow, selectedChannel);
      }
      if (advanceRow) {
        selectCell(nextRow, selectedChannel);
      } else {
        repaintCell(selectedRow, selectedChannel);
      }
      return true;
    };

    if (key == juce::KeyPress::escapeKey) {
      fxInputMode = false;
      fxInputBuffer.clear();
      fxInputPrefilledCommand = false;
      fxInputPrefilledValueDigits = 0;
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }

    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey) {
      if (!fxInputBuffer.empty()) {
        if (fxInputPrefilledCommand) {
          fxInputBuffer = fxInputBuffer.substr(0, std::min<std::size_t>(2, fxInputBuffer.size()));
          fxInputPrefilledCommand = false;
          fxInputPrefilledValueDigits = 0;
        } else {
          fxInputBuffer.pop_back();
          if (fxInputBuffer.size() < 2) {
            fxInputPrefilledCommand = false;
            fxInputPrefilledValueDigits = 0;
          }
        }
        if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      } else if (selectedRow >= 0 && selectedChannel >= 0) {
        std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
        if (lock.owns_lock()) {
          app.module.currentEditor().setEffect(selectedRow, selectedChannel, 0, 0);
          lock.unlock();
          refreshSnapshot();
          repaintCell(selectedRow, selectedChannel);
          if (selectionChangedCallback) selectionChangedCallback(selectedRow, selectedChannel);
        }
      }
      return true;
    }

    // Enter commits whatever is in the buffer.
    // 3 chars are interpreted as CVV or CCV shorthand, 4 as CCVV.
    // Fresh entry no longer auto-commits at 3 chars because that prevents
    // entering full CCVV values like 1706.
    if (key == juce::KeyPress::returnKey && !fxInputBuffer.empty() && selectedRow >= 0 && selectedChannel >= 0) {
      commitFxInputBuffer(fxCommitAutoAdvance);
      return true;
    }

    // FX entry accepts:
    // - CVV  (legacy shorthand, e.g. F06)
    // - CCV  (shorthand, e.g. 170 -> 1700)
    // - CCVV (full command byte, e.g. 1706)
    char c = static_cast<char>(juce::CharacterFunctions::toLowerCase(rawChar));
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
      if (selectedRow >= 0 && selectedChannel >= 0) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (fxInputPrefilledCommand) {
          if (fxInputBuffer.size() < 4) {
            fxInputBuffer.resize(4, '.');
          }
          if (fxInputPrefilledValueDigits == 0) {
            fxInputBuffer[2] = upper;
            fxInputBuffer[3] = '.';
            fxInputPrefilledValueDigits = 1;
            repaintCell(selectedRow, selectedChannel);
          } else {
            fxInputBuffer[3] = upper;
            fxInputPrefilledValueDigits = 2;
            if (!commitFxInputBuffer(fxCommitAutoAdvance)) {
              fxInputBuffer[3] = '.';
              fxInputPrefilledValueDigits = 1;
              repaintCell(selectedRow, selectedChannel);
            }
          }
          return true;
        }

        if (fxInputBuffer.size() >= 4) {
          return true;
        }
        fxInputBuffer += upper;
        if (fxInputBuffer.size() == 4) {
          if (!commitFxInputBuffer(fxCommitAutoAdvance)) {
            fxInputBuffer.pop_back(); // lock failed, revert last char
            repaintCell(selectedRow, selectedChannel);
          }
        } else {
          repaintCell(selectedRow, selectedChannel);
        }
      }
      return true;
    }

    // Left: move to Vol column (left of FX in the display)
    if (key == juce::KeyPress::leftKey) {
      enterVolCol();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }
    // Right: FX is the rightmost editable column — exit step-edit mode
    if (key == juce::KeyPress::rightKey) {
      exitStepEdit();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }
    // Other nav keys: clear partial buffer then fall through to row/channel navigation
    const bool isNavOrShortcut = key == juce::KeyPress::upKey || key == juce::KeyPress::downKey
        || key == juce::KeyPress::homeKey || key == juce::KeyPress::endKey
        || key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey
        || key == juce::KeyPress::returnKey || key == juce::KeyPress::tabKey
        || key == juce::KeyPress::spaceKey
        || key.getModifiers().isCommandDown();
    if (isNavOrShortcut) {
      if (!fxInputBuffer.empty()) {
        fxInputBuffer.clear();
        if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      }
      // fall through to normal key handling (row navigation stays in FX col)
    } else {
      return false;
    }
  }

  if (volumeInputMode) {
    auto applyVolumeInputAtSelection = [this](unsigned int parsed) -> bool {
      if (selectedRow < 0 || selectedChannel < 0) {
        return false;
      }

      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        return false;
      }

      auto& editor = app.module.currentEditor();
      const auto velocity = static_cast<std::uint8_t>(std::clamp(static_cast<int>(parsed), 1, 127));
      if (editor.hasNoteAt(selectedRow, selectedChannel)) {
        editor.setVelocity(selectedRow, selectedChannel, velocity);
      } else {
        // No note on this row: write a tracker-style Cxx volume command instead.
        editor.setEffect(selectedRow, selectedChannel, 0x0C, velocity);
      }

      const int nextRow = std::min(static_cast<int>(editor.rows()) - 1, selectedRow + editStep);
      lock.unlock();

      volumeInputBuffer.clear();
      refreshSnapshot();
      if (selectionChangedCallback) {
        selectionChangedCallback(selectedRow, selectedChannel);
      }
      selectCell(nextRow, selectedChannel);
      return true;
    };

    if (key == juce::KeyPress::escapeKey) {
      volumeInputMode = false;
      volumeInputBuffer.clear();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }

    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey) {
      if (!volumeInputBuffer.empty()) {
        volumeInputBuffer.pop_back();
        if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      }
      return true;
    }

    if (key == juce::KeyPress::returnKey && !volumeInputBuffer.empty() && selectedRow >= 0 && selectedChannel >= 0) {
      std::string padded = volumeInputBuffer;
      while (padded.size() < 2) padded += '0';
      const unsigned int parsed = std::stoul(padded.substr(0, 2), nullptr, 16);

      (void)applyVolumeInputAtSelection(parsed);
      return true;
    }

    char c = static_cast<char>(juce::CharacterFunctions::toLowerCase(rawChar));
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
      if (selectedRow >= 0 && selectedChannel >= 0) {
        volumeInputBuffer += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (volumeInputBuffer.size() == 2) {
          const unsigned int parsed = std::stoul(volumeInputBuffer.substr(0, 2), nullptr, 16);
          if (!applyVolumeInputAtSelection(parsed)) {
            volumeInputBuffer.pop_back();
            repaintCell(selectedRow, selectedChannel);
          }
        } else {
          repaintCell(selectedRow, selectedChannel);
        }
      }
      return true;
    }

    // Left: move to Smp column
    if (key == juce::KeyPress::leftKey) {
      enterSmpCol();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }
    // Right: move to FX column
    if (key == juce::KeyPress::rightKey) {
      enterFxCol();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }
    const bool isNavOrShortcut = key == juce::KeyPress::upKey || key == juce::KeyPress::downKey
        || key == juce::KeyPress::homeKey || key == juce::KeyPress::endKey
        || key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey
        || key == juce::KeyPress::returnKey || key == juce::KeyPress::tabKey
        || key == juce::KeyPress::spaceKey
        || key.getModifiers().isCommandDown();
    if (isNavOrShortcut) {
      if (!volumeInputBuffer.empty()) {
        volumeInputBuffer.clear();
        if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      }
    } else {
      return false;
    }
  }

  if (sampleInputMode) {
    auto applySampleInputAtSelection = [this](unsigned int parsed) -> bool {
      if (selectedRow < 0 || selectedChannel < 0) {
        return false;
      }

      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        return false;
      }

      auto& editor = app.module.currentEditor();
      const auto sample = static_cast<std::uint16_t>(std::clamp(static_cast<int>(parsed), 0, 255));
      editor.setSample(selectedRow, selectedChannel, sample);

      const int nextRow = std::min(static_cast<int>(editor.rows()) - 1, selectedRow + editStep);
      lock.unlock();

      sampleInputBuffer.clear();
      refreshSnapshot();
      if (selectionChangedCallback) {
        selectionChangedCallback(selectedRow, selectedChannel);
      }
      selectCell(nextRow, selectedChannel);
      return true;
    };

    if (key == juce::KeyPress::escapeKey) {
      sampleInputMode = false;
      sampleInputBuffer.clear();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }

    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey) {
      if (!sampleInputBuffer.empty()) {
        sampleInputBuffer.pop_back();
        if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      } else if (selectedRow >= 0 && selectedChannel >= 0) {
        std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
        if (lock.owns_lock()) {
          app.module.currentEditor().setSample(selectedRow, selectedChannel, 0xFFFF);
          lock.unlock();
          refreshSnapshot();
          repaintCell(selectedRow, selectedChannel);
          if (selectionChangedCallback) selectionChangedCallback(selectedRow, selectedChannel);
        }
      }
      return true;
    }

    if (key == juce::KeyPress::returnKey && !sampleInputBuffer.empty() && selectedRow >= 0 && selectedChannel >= 0) {
      std::string padded = sampleInputBuffer;
      while (padded.size() < 3) padded += '0';
      const unsigned int parsed = std::stoul(padded.substr(0, 3), nullptr, 16);
      (void)applySampleInputAtSelection(parsed);
      return true;
    }

    char c = static_cast<char>(juce::CharacterFunctions::toLowerCase(rawChar));
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
      if (selectedRow >= 0 && selectedChannel >= 0) {
        sampleInputBuffer += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (sampleInputBuffer.size() == 3) {
          const unsigned int parsed = std::stoul(sampleInputBuffer.substr(0, 3), nullptr, 16);
          if (!applySampleInputAtSelection(parsed)) {
            sampleInputBuffer.pop_back();
            repaintCell(selectedRow, selectedChannel);
          }
        } else {
          repaintCell(selectedRow, selectedChannel);
        }
      }
      return true;
    }

    // Left: Smp is leftmost — exit step-edit mode
    if (key == juce::KeyPress::leftKey) {
      exitStepEdit();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }
    // Right: move to Vol column
    if (key == juce::KeyPress::rightKey) {
      enterVolCol();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }
    const bool isNavOrShortcut = key == juce::KeyPress::upKey || key == juce::KeyPress::downKey
        || key == juce::KeyPress::homeKey || key == juce::KeyPress::endKey
        || key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey
        || key == juce::KeyPress::returnKey || key == juce::KeyPress::tabKey
        || key == juce::KeyPress::spaceKey
        || key.getModifiers().isCommandDown();
    if (isNavOrShortcut) {
      if (!sampleInputBuffer.empty()) {
        sampleInputBuffer.clear();
        if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      }
    } else {
      return false;
    }
  }

  if (key == juce::KeyPress::spaceKey) {
    if (togglePlaybackCallback) {
      togglePlaybackCallback();
      return true;
    }
  }

  if (mods.isCommandDown()) {
    if (key == juce::KeyPress::upKey) {
      return transposeSelection(mods.isShiftDown() ? 12 : 1);
    }
    if (key == juce::KeyPress::downKey) {
      return transposeSelection(mods.isShiftDown() ? -12 : -1);
    }

    const int keyCode = key.getKeyCode();
    if (keyCode == 'C') {
      return copySelectionToClipboard();
    }
    if (keyCode == 'X') {
      return cutSelectionToClipboard();
    }
    if (keyCode == 'V') {
      return pasteClipboardAtSelection();
    }
  }

  const juce::juce_wchar text = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

  if (text == '+' || text == '=') {
    setKeyboardOctave(keyboardOctave + 1);
    return true;
  }
  if (text == '-' || text == '_') {
    setKeyboardOctave(keyboardOctave - 1);
    return true;
  }
  if (text == '[' || text == '{') {
    setEditStep(editStep - 1);
    return true;
  }
  if (text == ']' || text == '}') {
    setEditStep(editStep + 1);
    return true;
  }

  if (selectedRow < 0 || selectedChannel < 0) {
    return false;
  }

  if (mods.isShiftDown() && text == 'o') {
    if (selectedRow < 0 || selectedChannel < 0) {
      return true;
    }
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return true;
    }
    auto& editor = app.module.currentEditor();
    const bool alreadyNoteOff = editor.hasNoteAt(selectedRow, selectedChannel) &&
                                 editor.noteAt(selectedRow, selectedChannel) < 0;
    if (alreadyNoteOff) {
      editor.clearStep(selectedRow, selectedChannel);
    } else {
      editor.insertNoteOff(selectedRow, selectedChannel);
    }
    const int nextRow = std::min(static_cast<int>(editor.rows()) - 1, selectedRow + editStep);
    lock.unlock();
    refreshSnapshot();
    if (selectionChangedCallback) {
      selectionChangedCallback(selectedRow, selectedChannel);
    }
    selectCell(nextRow, selectedChannel);
    return true;
  }

  const int maxRow = static_cast<int>(app.module.currentEditor().rows()) - 1;
  const int maxChannel = static_cast<int>(app.module.currentEditor().channels()) - 1;
  auto moveSelection = [this, maxRow, maxChannel](int row, int channel, bool extendSelection) {
    int targetRow = std::clamp(row, 0, maxRow);
    int targetChannel = std::clamp(channel, 0, maxChannel);

    if (extendSelection) {
      if (blockAnchorRow < 0 || blockAnchorChannel < 0) {
        blockAnchorRow = selectedRow;
        blockAnchorChannel = selectedChannel;
      }
      selectCell(targetRow, targetChannel, true);
      blockEndRow = selectedRow;
      blockEndChannel = selectedChannel;
      repaint();
      return;
    }

    selectCell(targetRow, targetChannel, false);
  };

  if (mods.isAltDown() &&
      (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey ||
       key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)) {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return true;
    }

    if (!app.module.currentEditor().hasNoteAt(selectedRow, selectedChannel)) {
      return true;
    }

    const int velocityStep = mods.isShiftDown() ? 8 : 1;
    const int gateStep = mods.isShiftDown() ? 4 : 1;

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
      const int delta = (key == juce::KeyPress::upKey) ? velocityStep : -velocityStep;
      const int current = static_cast<int>(app.module.currentEditor().velocityAt(selectedRow, selectedChannel));
      const auto next = static_cast<std::uint8_t>(std::clamp(current + delta, 1, 127));
      app.module.currentEditor().setVelocity(selectedRow, selectedChannel, next);
    } else {
      const int delta = (key == juce::KeyPress::rightKey) ? gateStep : -gateStep;
      const int current = static_cast<int>(app.module.currentEditor().gateTicksAt(selectedRow, selectedChannel));
      const auto next = static_cast<std::uint32_t>(std::max(current + delta, 0));
      app.module.currentEditor().setGateTicks(selectedRow, selectedChannel, next);
    }

    lock.unlock();
    refreshSnapshot();
    repaintCell(selectedRow, selectedChannel);
    if (selectionChangedCallback) {
      selectionChangedCallback(selectedRow, selectedChannel);
    }
    return true;
  }

  if (key == juce::KeyPress::returnKey) {
    int rowDelta = key.getModifiers().isShiftDown() ? -editStep : editStep;
    selectCell(std::clamp(selectedRow + rowDelta, 0, maxRow), selectedChannel);
    return true;
  }

  if (key == juce::KeyPress::tabKey) {
    int channelDelta = key.getModifiers().isShiftDown() ? -1 : 1;
    int nextChannel = selectedChannel + channelDelta;
    int nextRow = selectedRow;

    if (nextChannel > maxChannel) {
      nextChannel = 0;
      nextRow = std::min(maxRow, selectedRow + 1);
    } else if (nextChannel < 0) {
      nextChannel = maxChannel;
      nextRow = std::max(0, selectedRow - 1);
    }

    selectCell(nextRow, nextChannel);
    return true;
  }

  if (key == juce::KeyPress::insertKey) {
    if (selectedRow >= 0 && selectedChannel >= 0) {
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (lock.owns_lock()) {
        if (mods.isShiftDown())
          app.module.currentEditor().insertRowAllChannels(selectedRow);
        else
          app.module.currentEditor().insertRowAt(selectedRow, selectedChannel);
        lock.unlock();
        refreshSnapshot();
        repaint();
      }
    }
    return true;
  }

  if (key == juce::KeyPress::deleteKey) {
    if (selectedRow >= 0 && selectedChannel >= 0) {
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (lock.owns_lock()) {
        if (mods.isShiftDown())
          app.module.currentEditor().deleteRowAllChannels(selectedRow);
        else
          app.module.currentEditor().deleteRowAt(selectedRow, selectedChannel);
        lock.unlock();
        refreshSnapshot();
        repaint();
      }
    }
    return true;
  }

  if (key == juce::KeyPress::backspaceKey) {
    return clearSelectedCell();
  }

  if (key == juce::KeyPress::leftKey) {
    moveSelection(selectedRow, selectedChannel - 1, mods.isShiftDown());
    return true;
  }
  if (key == juce::KeyPress::rightKey) {
    moveSelection(selectedRow, selectedChannel + 1, mods.isShiftDown());
    return true;
  }
  if (key == juce::KeyPress::upKey) {
    moveSelection(selectedRow - 1, selectedChannel, mods.isShiftDown());
    return true;
  }
  if (key == juce::KeyPress::downKey) {
    moveSelection(selectedRow + 1, selectedChannel, mods.isShiftDown());
    return true;
  }
  if (key == juce::KeyPress::homeKey) {
    moveSelection(0, selectedChannel, mods.isShiftDown());
    return true;
  }
  if (key == juce::KeyPress::endKey) {
    moveSelection(maxRow, selectedChannel, mods.isShiftDown());
    return true;
  }
  if (key == juce::KeyPress::pageUpKey) {
    moveSelection(selectedRow - 16, selectedChannel, mods.isShiftDown());
    return true;
  }
  if (key == juce::KeyPress::pageDownKey) {
    moveSelection(selectedRow + 16, selectedChannel, mods.isShiftDown());
    return true;
  }

  int semitone = -1;
  int octave = keyboardOctave;
  static const int kWholeToneOffsets[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16};

  const juce::String highSemitoneKeys("123456789");
  const juce::String highWholeToneKeys("qwertyuiop");
  const juce::String lowSemitoneKeys("asdfghjkl");
  const juce::String lowWholeToneKeys("zxcvbnm,.");

  int keyIndex = highSemitoneKeys.indexOfChar(text);
  if (keyIndex >= 0) {
    semitone = keyIndex;
    octave = keyboardOctave + 1;
  }

  if (semitone < 0) {
    keyIndex = highWholeToneKeys.indexOfChar(text);
    if (keyIndex >= 0) {
      semitone = kWholeToneOffsets[keyIndex];
      octave = keyboardOctave + 1;
    }
  }

  if (semitone < 0) {
    keyIndex = lowSemitoneKeys.indexOfChar(text);
    if (keyIndex >= 0) {
      semitone = keyIndex;
      octave = keyboardOctave;
    }
  }

  if (semitone < 0) {
    keyIndex = lowWholeToneKeys.indexOfChar(text);
    if (keyIndex >= 0) {
      semitone = kWholeToneOffsets[keyIndex];
      octave = keyboardOctave;
    }
  }

  if (semitone < 0) {
    return false;
  }

  int midiNote = std::clamp(octave * 12 + semitone, 0, 127);
  return commitNoteFromKeyboard(midiNote);
}

void PatternGrid::selectCell(int row, int channel, bool preserveBlock) {
  if (selectedRow == row && selectedChannel == channel) {
    if (!preserveBlock) {
      blockAnchorRow = row;
      blockAnchorChannel = channel;
      blockEndRow = row;
      blockEndChannel = channel;
    }
    if (selectionChangedCallback) {
      selectionChangedCallback(row, channel);
    }
    return;
  }

  int oldRow = selectedRow;
  int oldChannel = selectedChannel;
  selectedRow = row;
  selectedChannel = channel;
  if (!preserveBlock) {
    blockAnchorRow = row;
    blockAnchorChannel = channel;
    blockEndRow = row;
    blockEndChannel = channel;
  }
  repaintCell(oldRow, oldChannel);
  repaintCell(selectedRow, selectedChannel);

  if (selectionChangedCallback) {
    selectionChangedCallback(row, channel);
  }

  previewSelectedStepIfEnabled();
}

bool PatternGrid::commitNoteFromKeyboard(int midiNote) {
  std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
  if (!lockStateWithRetry(lock)) {
    return false;
  }

  // When a sample is armed and a channel lock is active, redirect to the locked channel.
  const bool sampleArmed = app.activeSampleSlot >= 0 && app.activeSampleSlot <= 255;
  const int numChannels = static_cast<int>(app.module.currentEditor().channels());
  const int targetChannel = (sampleArmed && app.sampleTargetChannel >= 0 &&
                              app.sampleTargetChannel < numChannels)
      ? app.sampleTargetChannel : selectedChannel;

  const std::uint8_t instrument = defaultInsertInstrument(app, targetChannel);

  app.module.currentEditor().insertNote(selectedRow, targetChannel, midiNote, instrument, insertGateTicks, insertVelocity, true);
  std::uint16_t sample = 0xFFFF;
  if (sampleArmed) {
    sample = static_cast<std::uint16_t>(app.activeSampleSlot);
    app.module.currentEditor().setSample(selectedRow, targetChannel, sample);
  } else {
    app.module.currentEditor().setSample(selectedRow, targetChannel, 0xFFFF);
  }

  int nextRow = std::min(static_cast<int>(app.module.currentEditor().rows()) - 1, selectedRow + editStep);
  lock.unlock();
  previewPlacedNote(instrument, sample, midiNote, insertVelocity);
  refreshSnapshot();
  repaint();
  selectCell(nextRow, selectedChannel);
  return true;
}

void PatternGrid::previewPlacedNote(std::uint8_t instrument,
                                    std::uint16_t sample,
                                    int midiNote,
                                    std::uint8_t velocity) {
  if (midiNote < 0 || midiNote > 127) {
    return;
  }
  const bool triggered = app.plugins.triggerNoteOnResolved(
      instrument,
      sample,
      midiNote,
      std::clamp<std::uint8_t>(velocity, 1, 127),
      true);
  if (!triggered) {
    return;
  }

  PendingPreviewNoteOff pending;
  pending.instrument = instrument;
  pending.sample = sample;
  pending.midiNote = midiNote;
  pending.dueMs = juce::Time::getMillisecondCounter() + previewDurationMs;
  pendingPreviewNoteOffs.push_back(pending);

  if (!isTimerRunning()) {
    startTimer(16);
  }
}

void PatternGrid::previewSelectedStepIfEnabled(bool force) {
  if ((!followPreviewOnSelect && !force) || selectedRow < 0 || selectedChannel < 0) {
    return;
  }

  std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return;
  }

  if (!app.module.currentEditor().hasNoteAt(selectedRow, selectedChannel)) {
    return;
  }

  const int midiNote = app.module.currentEditor().noteAt(selectedRow, selectedChannel);
  if (midiNote < 0) {
    return;  // note-off step (^^^): no audio to preview
  }
  const std::uint8_t instrument = app.module.currentEditor().instrumentAt(selectedRow, selectedChannel);
  const std::uint16_t sample = app.module.currentEditor().sampleAt(selectedRow, selectedChannel);
  const std::uint8_t velocity = app.module.currentEditor().velocityAt(selectedRow, selectedChannel);
  lock.unlock();

  previewPlacedNote(instrument, sample, midiNote, velocity);
}

void PatternGrid::timerCallback() {
  if (pendingPreviewNoteOffs.empty()) {
    stopTimer();
    return;
  }

  const std::uint32_t now = juce::Time::getMillisecondCounter();
  auto next = pendingPreviewNoteOffs.begin();
  while (next != pendingPreviewNoteOffs.end()) {
    const auto due = next->dueMs;
    if (static_cast<std::int32_t>(now - due) >= 0) {
      app.plugins.triggerNoteOffResolved(next->instrument, next->sample, next->midiNote);
      next = pendingPreviewNoteOffs.erase(next);
    } else {
      ++next;
    }
  }

  if (pendingPreviewNoteOffs.empty()) {
    stopTimer();
  }
}

bool PatternGrid::clearSelectedCell() {
  std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
  if (!lockStateWithRetry(lock)) {
    return false;
  }

  app.module.currentEditor().clearStep(selectedRow, selectedChannel);
  lock.unlock();
  refreshSnapshot();
  repaintCell(selectedRow, selectedChannel);
  return true;
}

bool PatternGrid::copySelectionToClipboard() {
  if (selectedRow < 0 || selectedChannel < 0) {
    return false;
  }

  int minRow = selectedRow;
  int maxRow = selectedRow;
  int minChannel = selectedChannel;
  int maxChannel = selectedChannel;
  getBlockBounds(minRow, maxRow, minChannel, maxChannel);

  std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
  if (!lockStateWithRetry(lock)) {
    return false;
  }

  clipboardRows = maxRow - minRow + 1;
  clipboardChannels = maxChannel - minChannel + 1;
  clipboard.assign(static_cast<std::size_t>(clipboardRows * clipboardChannels), ClipboardStep{});

  for (int row = minRow; row <= maxRow; ++row) {
    for (int channel = minChannel; channel <= maxChannel; ++channel) {
      std::size_t index = static_cast<std::size_t>((row - minRow) * clipboardChannels + (channel - minChannel));
      ClipboardStep step;
      step.hasNote = app.module.currentEditor().hasNoteAt(row, channel);
      step.effectCommand = app.module.currentEditor().effectCommandAt(row, channel);
      step.effectValue = app.module.currentEditor().effectValueAt(row, channel);
      if (step.hasNote) {
        step.note = app.module.currentEditor().noteAt(row, channel);
        step.instrument = app.module.currentEditor().instrumentAt(row, channel);
        step.sample = app.module.currentEditor().sampleAt(row, channel);
        step.gateTicks = app.module.currentEditor().gateTicksAt(row, channel);
        step.velocity = app.module.currentEditor().velocityAt(row, channel);
        step.retrigger = app.module.currentEditor().retriggerAt(row, channel);
      }
      clipboard[index] = step;
    }
  }

  return true;
}

bool PatternGrid::cutSelectionToClipboard() {
  int minRow = selectedRow;
  int maxRow = selectedRow;
  int minChannel = selectedChannel;
  int maxChannel = selectedChannel;
  getBlockBounds(minRow, maxRow, minChannel, maxChannel);

  if (!copySelectionToClipboard()) {
    return false;
  }

  std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
  if (!lockStateWithRetry(lock)) {
    return false;
  }

  for (int row = minRow; row <= maxRow; ++row) {
    for (int channel = minChannel; channel <= maxChannel; ++channel) {
      app.module.currentEditor().clearStep(row, channel);
    }
  }

  lock.unlock();
  refreshSnapshot();
  repaint();
  return true;
}

bool PatternGrid::pasteClipboardAtSelection() {
  if (selectedRow < 0 || selectedChannel < 0) {
    return false;
  }
  if (clipboard.empty() || clipboardRows <= 0 || clipboardChannels <= 0) {
    return false;
  }

  std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
  if (!lockStateWithRetry(lock)) {
    return false;
  }

  const int totalRows = static_cast<int>(app.module.currentEditor().rows());
  const int totalChannels = static_cast<int>(app.module.currentEditor().channels());

  for (int row = 0; row < clipboardRows; ++row) {
    int targetRow = selectedRow + row;
    if (targetRow < 0 || targetRow >= totalRows) {
      continue;
    }

    for (int channel = 0; channel < clipboardChannels; ++channel) {
      int targetChannel = selectedChannel + channel;
      if (targetChannel < 0 || targetChannel >= totalChannels) {
        continue;
      }

      const std::size_t index = static_cast<std::size_t>(row * clipboardChannels + channel);
      const ClipboardStep& step = clipboard[index];
      if (step.hasNote && step.note < 0) {
        // Note-off (shift-O): insertNote rejects negative notes, use dedicated path
        app.module.currentEditor().insertNoteOff(targetRow, targetChannel);
        app.module.currentEditor().setEffect(targetRow, targetChannel,
                                             step.effectCommand, step.effectValue);
      } else if (step.hasNote) {
        app.module.currentEditor().insertNote(targetRow,
                              targetChannel,
                              step.note,
                              step.instrument,
                              step.gateTicks,
                              step.velocity,
                              step.retrigger,
                              step.effectCommand,
                              step.effectValue);
        if (step.sample != 0xFFFF) {
          app.module.currentEditor().setSample(targetRow, targetChannel, step.sample);
        }
      } else {
        app.module.currentEditor().clearStep(targetRow, targetChannel);
        if (step.effectCommand != 0 || step.effectValue != 0) {
          app.module.currentEditor().setEffect(targetRow, targetChannel, step.effectCommand, step.effectValue);
        }
      }
    }
  }

  lock.unlock();
  refreshSnapshot();
  repaint();
  return true;
}

bool PatternGrid::transposeSelection(int semitoneDelta) {
  if (selectedRow < 0 || selectedChannel < 0 || semitoneDelta == 0) {
    return false;
  }

  int minRow = selectedRow;
  int maxRow = selectedRow;
  int minChannel = selectedChannel;
  int maxChannel = selectedChannel;
  getBlockBounds(minRow, maxRow, minChannel, maxChannel);

  std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }

  for (int row = minRow; row <= maxRow; ++row) {
    for (int channel = minChannel; channel <= maxChannel; ++channel) {
      if (!app.module.currentEditor().hasNoteAt(row, channel)) {
        continue;
      }

      const int note = app.module.currentEditor().noteAt(row, channel);
      const int transposed = std::clamp(note + semitoneDelta, 0, 127);
      if (transposed == note) {
        continue;
      }

      app.module.currentEditor().insertNote(row,
                            channel,
                            transposed,
                            app.module.currentEditor().instrumentAt(row, channel),
                            app.module.currentEditor().gateTicksAt(row, channel),
                            app.module.currentEditor().velocityAt(row, channel),
                            app.module.currentEditor().retriggerAt(row, channel),
                            app.module.currentEditor().effectCommandAt(row, channel),
                            app.module.currentEditor().effectValueAt(row, channel));
    }
  }

  lock.unlock();
  refreshSnapshot();
  repaint();
  return true;
}

bool PatternGrid::hasBlockSelection() const {
  if (blockAnchorRow < 0 || blockAnchorChannel < 0 || blockEndRow < 0 || blockEndChannel < 0) {
    return false;
  }
  return blockAnchorRow != blockEndRow || blockAnchorChannel != blockEndChannel;
}

bool PatternGrid::applyEffectToSelection(std::uint8_t effectCommand, std::uint8_t effectValue) {
  if (selectedRow < 0 || selectedChannel < 0) {
    return false;
  }

  int minRow = selectedRow;
  int maxRow = selectedRow;
  int minChannel = selectedChannel;
  int maxChannel = selectedChannel;
  getBlockBounds(minRow, maxRow, minChannel, maxChannel);

  std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }

  for (int row = minRow; row <= maxRow; ++row) {
    for (int channel = minChannel; channel <= maxChannel; ++channel) {
      app.module.currentEditor().setEffect(row, channel, effectCommand, effectValue);
    }
  }

  lock.unlock();
  refreshSnapshot();
  repaint();
  return true;
}

bool PatternGrid::isCellInBlockSelection(int row, int channel) const {
  if (!hasBlockSelection()) {
    return false;
  }

  int minRow = 0;
  int maxRow = 0;
  int minChannel = 0;
  int maxChannel = 0;
  getBlockBounds(minRow, maxRow, minChannel, maxChannel);
  return row >= minRow && row <= maxRow && channel >= minChannel && channel <= maxChannel;
}

void PatternGrid::getBlockBounds(int& minRow, int& maxRow, int& minChannel, int& maxChannel) const {
  int anchorRow = blockAnchorRow;
  int anchorChannel = blockAnchorChannel;
  int endRow = blockEndRow;
  int endChannel = blockEndChannel;

  if (anchorRow < 0 || anchorChannel < 0 || endRow < 0 || endChannel < 0) {
    anchorRow = selectedRow;
    anchorChannel = selectedChannel;
    endRow = selectedRow;
    endChannel = selectedChannel;
  }

  minRow = std::min(anchorRow, endRow);
  maxRow = std::max(anchorRow, endRow);
  minChannel = std::min(anchorChannel, endChannel);
  maxChannel = std::max(anchorChannel, endChannel);
}

bool PatternGrid::refreshSnapshot() {
  // Never block the message thread during paint; if state is busy, keep previous snapshot.
  std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }

  int numRows = static_cast<int>(app.module.currentEditor().rows());
  int numChannels = static_cast<int>(app.module.currentEditor().channels());
  if (numRows <= 0 || numChannels <= 0) {
    return false;
  }

  std::size_t cellCount = static_cast<std::size_t>(numRows * numChannels);
  if (cachedRows != numRows || cachedChannels != numChannels || cachedHasNote.size() != cellCount) {
    cachedRows = numRows;
    cachedChannels = numChannels;
    cachedHasNote.assign(cellCount, false);
    cachedNote.assign(cellCount, -1);
    cachedGate.assign(cellCount, 0);
    cachedVelocity.assign(cellCount, 0);
    cachedInstrument.assign(cellCount, 0);
    cachedSample.assign(cellCount, 0xFFFF);
    cachedEffectCommand.assign(cellCount, 0);
    cachedEffectValue.assign(cellCount, 0);
    cachedChannelMuted.assign(static_cast<std::size_t>(numChannels), false);
  }

  if (cachedChannelMuted.size() != static_cast<std::size_t>(numChannels)) {
    cachedChannelMuted.assign(static_cast<std::size_t>(numChannels), false);
  }
  for (int ch = 0; ch < numChannels; ++ch) {
    cachedChannelMuted[static_cast<std::size_t>(ch)] =
        static_cast<std::size_t>(ch) < app.channelMuted.size() && app.channelMuted[static_cast<std::size_t>(ch)];
  }

  for (int row = 0; row < numRows; ++row) {
    for (int ch = 0; ch < numChannels; ++ch) {
      std::size_t index = static_cast<std::size_t>(row * numChannels + ch);
      bool hasNote = app.module.currentEditor().hasNoteAt(row, ch);
      cachedHasNote[index] = hasNote;
      if (hasNote) {
        cachedNote[index] = app.module.currentEditor().noteAt(row, ch);
        cachedGate[index] = app.module.currentEditor().gateTicksAt(row, ch);
        cachedVelocity[index] = app.module.currentEditor().velocityAt(row, ch);
        cachedInstrument[index] = app.module.currentEditor().instrumentAt(row, ch);
        cachedSample[index] = app.module.currentEditor().sampleAt(row, ch);
        cachedEffectCommand[index] = app.module.currentEditor().effectCommandAt(row, ch);
        cachedEffectValue[index] = app.module.currentEditor().effectValueAt(row, ch);
      } else {
        cachedNote[index] = -1;
        cachedGate[index] = 0;
        cachedVelocity[index] = 0;
        cachedInstrument[index] = 0;
        cachedSample[index] = 0xFFFF;
        // Effects can exist on empty cells; keep them visible in the grid.
        cachedEffectCommand[index] = app.module.currentEditor().effectCommandAt(row, ch);
        cachedEffectValue[index] = app.module.currentEditor().effectValueAt(row, ch);
      }
    }
  }

  return true;
}

bool PatternGrid::refreshSnapshotForPatternChange() {
  std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
  if (!lockStateWithRetry(lock)) {
    return false;
  }

  int numRows = static_cast<int>(app.module.currentEditor().rows());
  int numChannels = static_cast<int>(app.module.currentEditor().channels());
  if (numRows <= 0 || numChannels <= 0) {
    return false;
  }

  std::size_t cellCount = static_cast<std::size_t>(numRows * numChannels);
  if (cachedRows != numRows || cachedChannels != numChannels || cachedHasNote.size() != cellCount) {
    cachedRows = numRows;
    cachedChannels = numChannels;
    cachedHasNote.assign(cellCount, false);
    cachedNote.assign(cellCount, -1);
    cachedGate.assign(cellCount, 0);
    cachedVelocity.assign(cellCount, 0);
    cachedInstrument.assign(cellCount, 0);
    cachedSample.assign(cellCount, 0xFFFF);
    cachedEffectCommand.assign(cellCount, 0);
    cachedEffectValue.assign(cellCount, 0);
    cachedChannelMuted.assign(static_cast<std::size_t>(numChannels), false);
  }

  if (cachedChannelMuted.size() != static_cast<std::size_t>(numChannels)) {
    cachedChannelMuted.assign(static_cast<std::size_t>(numChannels), false);
  }
  for (int ch = 0; ch < numChannels; ++ch) {
    cachedChannelMuted[static_cast<std::size_t>(ch)] =
        static_cast<std::size_t>(ch) < app.channelMuted.size() && app.channelMuted[static_cast<std::size_t>(ch)];
  }

  for (int row = 0; row < numRows; ++row) {
    for (int ch = 0; ch < numChannels; ++ch) {
      std::size_t index = static_cast<std::size_t>(row * numChannels + ch);
      bool hasNote = app.module.currentEditor().hasNoteAt(row, ch);
      cachedHasNote[index] = hasNote;
      cachedNote[index] = hasNote ? app.module.currentEditor().noteAt(row, ch) : -1;
      cachedGate[index] = hasNote ? app.module.currentEditor().gateTicksAt(row, ch) : 0;
      cachedVelocity[index] = hasNote ? app.module.currentEditor().velocityAt(row, ch) : 0;
      cachedInstrument[index] = hasNote ? app.module.currentEditor().instrumentAt(row, ch) : 0;
      cachedSample[index] = hasNote ? app.module.currentEditor().sampleAt(row, ch) : static_cast<std::uint16_t>(0xFFFF);
      cachedEffectCommand[index] = app.module.currentEditor().effectCommandAt(row, ch);
      cachedEffectValue[index] = app.module.currentEditor().effectValueAt(row, ch);
    }
  }

  return true;
}

void PatternGrid::mouseMove(const juce::MouseEvent& event) {
  int newRow = getRowAtY(event.y);
  int newChannel = getChannelAtX(event.x);

  if (newRow != hoveredRow || newChannel != hoveredChannel) {
    int oldRow = hoveredRow;
    int oldChannel = hoveredChannel;
    hoveredRow = newRow;
    hoveredChannel = newChannel;
    repaintCell(oldRow, oldChannel);
    repaintCell(newRow, newChannel);
  }
}

void PatternGrid::focusGained(FocusChangeType cause) {
  juce::ignoreUnused(cause);
  repaint();
}

void PatternGrid::focusLost(FocusChangeType cause) {
  juce::ignoreUnused(cause);
  repaint();
}
