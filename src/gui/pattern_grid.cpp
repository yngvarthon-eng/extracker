#include "pattern_grid.h"
#include "app.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

std::uint8_t defaultInsertInstrument(const ExTrackerApp& app, int channel) {
  if (app.activeSampleSlot >= 0 && app.activeSampleSlot <= 255) {
    return static_cast<std::uint8_t>(app.activeSampleSlot);
  }
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

PatternGrid::~PatternGrid() = default;

void PatternGrid::setSelectionChangedCallback(std::function<void(int, int)> callback) {
  selectionChangedCallback = std::move(callback);
}

void PatternGrid::setKeyboardStateChangedCallback(std::function<void(int, int)> callback) {
  keyboardStateChangedCallback = std::move(callback);
}

void PatternGrid::setTogglePlaybackCallback(std::function<void()> callback) {
  togglePlaybackCallback = std::move(callback);
}

void PatternGrid::setInsertDefaults(std::uint32_t gateTicks, std::uint8_t velocity) {
  insertGateTicks = gateTicks;
  insertVelocity = std::max<std::uint8_t>(velocity, 1);
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
  headerHeight = compactDensity ? 34 : 40;
  labelWidth = compactDensity ? 32 : 40;
  resized();
  repaint();
}

bool PatternGrid::isCompactDensity() const {
  return compactDensity;
}

int PatternGrid::preferredCellWidth() const {
  return compactDensity ? 6 : 8;
}

int PatternGrid::preferredHeaderHeight() const {
  return compactDensity ? 34 : 40;
}

int PatternGrid::preferredLabelWidth() const {
  return compactDensity ? 32 : 40;
}

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
  g.fillAll(juce::Colours::darkgrey);

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
  g.setColour(juce::Colours::darkslategrey);
  g.fillRect(0, 0, getWidth(), headerHeight);

  // Draw channel headers
  g.setColour(juce::Colours::white);
  g.setFont(10.0f);

  for (int ch = 0; ch < static_cast<int>(app.module.currentEditor().channels()); ++ch) {
    int x = labelWidth + ch * cellWidth;
    g.drawText(juce::String(ch), x, 0, cellWidth, headerHeight, juce::Justification::centred);
  }

  g.setColour(juce::Colours::lightgrey);
  g.setFont(9.0f);
  juce::String hint = "KB Oct " + juce::String(keyboardOctave) + "  Step " + juce::String(editStep);
  if (fxInputMode) {
    hint = "[FX] " + hint;
  }
  if (selectedRow >= 0 && selectedChannel >= 0) {
    int minRow = selectedRow;
    int maxRow = selectedRow;
    int minChannel = selectedChannel;
    int maxChannel = selectedChannel;
    getBlockBounds(minRow, maxRow, minChannel, maxChannel);
    const int blockRows = maxRow - minRow + 1;
    const int blockChannels = maxChannel - minChannel + 1;
    hint += "  Sel " + juce::String(blockRows) + "x" + juce::String(blockChannels);
  }
  hint += "  FX toggle: | or ` or F2";
  hint += "  Shift+Arrows/Drag mark  Ctrl/Cmd+C/X/V  Ctrl/Cmd+Up/Down transpose";
  g.drawText(hint, 0, 0, getWidth() - 6, headerHeight, juce::Justification::centredRight);
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
    for (int ch = 0; ch < numChannels; ++ch) {
      int x = labelWidth + ch * cellWidth;
      int y = startY + row * cellHeight;
      std::size_t index = static_cast<std::size_t>(row * numChannels + ch);

      drawCell(g,
                 cachedHasNote[index],
                 cachedNote[index],
                 cachedGate[index],
                 cachedVelocity[index],
                 cachedInstrument[index],
                 cachedEffectCommand[index],
                 cachedEffectValue[index],
               row,
               ch,
               x,
               y);
    }

    // Draw row number
    int y = startY + row * cellHeight;
    g.setColour(juce::Colours::lightgrey);
    g.setFont(9.0f);
    g.drawText(juce::String(row), 0, y, labelWidth, cellHeight, juce::Justification::centred);

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
    juce::Rectangle<int> badge(labelWidth + 6, headerHeight + 6, 132, compactDensity ? 18 : 22);
    g.setColour(juce::Colour(0xCC1F1F1F));
    g.fillRoundedRectangle(badge.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xFFFFCC00));
    g.drawRoundedRectangle(badge.toFloat(), 5.0f, 1.0f);
    g.setFont(compactDensity ? 11.0f : 12.0f);
    g.drawText("FX ON (|/`/F2)", badge, juce::Justification::centred);
  }
}

void PatternGrid::drawCell(juce::Graphics& g,
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
                           int y) {
  juce::ignoreUnused(gateTicks);

  const bool hasEffect = (effectCommand != 0 || effectValue != 0);

  // Cell background
  if (isCellInBlockSelection(row, channel)) {
    g.setColour(hasNote ? juce::Colour(0xFF2F6A44) : juce::Colour(0xFF4A5D77));
  } else if (hoveredRow == row && hoveredChannel == channel) {
    g.setColour(juce::Colours::darkblue);
  } else if (hasNote) {
    g.setColour(juce::Colours::darkgreen);
  } else if (hasEffect) {
    g.setColour(juce::Colour(0xFF2A2A4A));
  } else {
    g.setColour(juce::Colours::grey);
  }

  g.fillRect(x + 1, y + 1, cellWidth - 2, cellHeight - 2);

  if (selectedRow == row && selectedChannel == channel) {
    g.setColour(juce::Colour(0xFFF2CC60));
    g.drawRect(x + 1, y + 1, cellWidth - 2, cellHeight - 2, 2);
  }

  // Tracker-style tick text (single line):
  // Empty no-fx:  ... ... ... ....
  // Empty with fx: ... ... ... F06
  // Filled:        C-4 100 a64 0E00
  const std::string effectText = (effectCommand != 0 || effectValue != 0)
      ? toUpperHex(effectCommand, 1) + toUpperHex(effectValue, 2)
      : "...";

  juce::String tickText;
  if (hasNote) {
    const std::string noteText = formatTrackerNote(note);
    int sampleSlot = app.plugins.sampleSlotForInstrument(instrument);
    if (sampleSlot < 0 && !app.plugins.samplePathForSlot(static_cast<std::uint16_t>(instrument)).empty()) {
      sampleSlot = instrument;
    }
    const std::string sampleSlotText = sampleSlot >= 0 ? toUpperHex(static_cast<unsigned int>(sampleSlot), 3) : "...";
    const std::string volumeFxText = std::string("a") + toUpperHex(velocity, 2);
    tickText = juce::String(noteText) + " " + juce::String(sampleSlotText) + " " +
               juce::String(volumeFxText) + " " + juce::String(effectText);
  } else if (hasEffect) {
    tickText = juce::String("... ... ... ") + juce::String(effectText);
  } else {
    tickText = "... ... ... ....";
  }

  g.setColour(juce::Colours::white);
  g.setFont(compactDensity ? 15.0f : 18.0f);
  const int innerX = x + 2;
  const int innerY = y + 2;
  const int innerW = cellWidth - 4;
  const int innerH = cellHeight - 4;
  g.drawText(tickText, innerX, innerY, innerW, innerH, juce::Justification::centredLeft, true);

  // FX entry mode overlay: show partial hex buffer in amber on the selected cell
  // Format mirrors the display: 1 char command + 2 chars value = "F80"
  if (row == selectedRow && channel == selectedChannel && fxInputMode) {
    std::string fxVisual = fxInputBuffer;
    while (fxVisual.size() < 3) fxVisual += '.';
    g.setColour(juce::Colour(0xFFFFCC00));
    g.setFont(compactDensity ? 15.0f : 18.0f);
    g.drawText(juce::String(fxVisual), innerX, innerY, innerW, innerH, juce::Justification::centredRight, false);
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

  if (row >= 0 && channel >= 0) {
    grabKeyboardFocus();
    selectCell(row, channel);

    std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
    if (!lockStateWithRetry(lock)) {
      return;
    }

    if (event.mods.isRightButtonDown()) {
      // Right-click: clear note
      app.module.currentEditor().clearStep(row, channel);
    } else if (event.mods.isLeftButtonDown()) {
      // Left-click: select existing note, or insert a default note into an empty cell.
      if (!app.module.currentEditor().hasNoteAt(row, channel)) {
        const std::uint8_t instrument = defaultInsertInstrument(app, channel);
        app.module.currentEditor().insertNote(row, channel, 60, instrument, insertGateTicks, insertVelocity, true);
              // If an active sample slot is armed, explicitly set it so the sample field takes priority
              if (app.activeSampleSlot >= 0 && app.activeSampleSlot <= 255) {
                app.module.currentEditor().setSample(row, channel, static_cast<std::uint16_t>(app.activeSampleSlot));
              }
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

  // FX mode toggle supports multiple layouts: `, |, or F2.
  if (rawChar == '`' || rawChar == '|' || key == juce::KeyPress::F2Key) {
    fxInputMode = !fxInputMode;
    fxInputBuffer.clear();
    if (selectedRow >= 0 && selectedChannel >= 0) {
      repaintCell(selectedRow, selectedChannel);
    }
    return true;
  }

  // In FX entry mode: hex digits go into the effect buffer
  if (fxInputMode) {
    if (key == juce::KeyPress::escapeKey) {
      fxInputMode = false;
      fxInputBuffer.clear();
      if (selectedRow >= 0 && selectedChannel >= 0) repaintCell(selectedRow, selectedChannel);
      return true;
    }

    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey) {
      if (!fxInputBuffer.empty()) {
        fxInputBuffer.pop_back();
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

    // Enter commits whatever is in the partial buffer (zero-padded to 3 chars)
    if (key == juce::KeyPress::returnKey && !fxInputBuffer.empty() && selectedRow >= 0 && selectedChannel >= 0) {
      std::string padded = fxInputBuffer;
      while (padded.size() < 3) padded += '0';
      unsigned int cmd = std::stoul(padded.substr(0, 1), nullptr, 16);
      unsigned int val = std::stoul(padded.substr(1, 2), nullptr, 16);
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (lock.owns_lock()) {
        app.module.currentEditor().setEffect(selectedRow, selectedChannel,
            static_cast<std::uint8_t>(cmd), static_cast<std::uint8_t>(val));
        int nextRow = std::min(static_cast<int>(app.module.currentEditor().rows()) - 1, selectedRow + editStep);
        lock.unlock();
        fxInputBuffer.clear();
        refreshSnapshot();
        if (selectionChangedCallback) selectionChangedCallback(selectedRow, selectedChannel);
        selectCell(nextRow, selectedChannel);
      }
      return true;
    }

    // 3-char hex entry: 1 char = command (0-F), 2 chars = value (00-FF)
    // Matches the display format "F80" exactly.
    char c = static_cast<char>(juce::CharacterFunctions::toLowerCase(rawChar));
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
      if (selectedRow >= 0 && selectedChannel >= 0) {
        fxInputBuffer += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (fxInputBuffer.size() == 3) {
          unsigned int cmd = std::stoul(fxInputBuffer.substr(0, 1), nullptr, 16);
          unsigned int val = std::stoul(fxInputBuffer.substr(1, 2), nullptr, 16);
          std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
          if (lock.owns_lock()) {
            app.module.currentEditor().setEffect(selectedRow, selectedChannel,
                static_cast<std::uint8_t>(cmd), static_cast<std::uint8_t>(val));
            int nextRow = std::min(static_cast<int>(app.module.currentEditor().rows()) - 1, selectedRow + editStep);
            lock.unlock();
            fxInputBuffer.clear();
            refreshSnapshot();
            if (selectionChangedCallback) selectionChangedCallback(selectedRow, selectedChannel);
            selectCell(nextRow, selectedChannel);
          } else {
            fxInputBuffer.pop_back(); // lock failed, revert last char
            repaintCell(selectedRow, selectedChannel);
          }
        } else {
          repaintCell(selectedRow, selectedChannel);
        }
      }
      return true;
    }

    // Navigation keys and Ctrl shortcuts: clear partial buffer, then fall through
    const bool isNavOrShortcut = key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey
        || key == juce::KeyPress::upKey || key == juce::KeyPress::downKey
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
      // fall through to normal key handling
    } else {
      return false; // consume to avoid accidental note entry from non-hex keys
    }
  }

  if (key == juce::KeyPress::spaceKey) {
    if (togglePlaybackCallback) {
      togglePlaybackCallback();
      return true;
    }
  }

  const auto mods = key.getModifiers();

  if (mods.isCommandDown()) {
    if (key == juce::KeyPress::upKey) {
      return transposeSelection(mods.isShiftDown() ? 12 : 1);
    }
    if (key == juce::KeyPress::downKey) {
      return transposeSelection(mods.isShiftDown() ? -12 : -1);
    }

    const juce::juce_wchar cmd = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (cmd == 'c') {
      return copySelectionToClipboard();
    }
    if (cmd == 'x') {
      return cutSelectionToClipboard();
    }
    if (cmd == 'v') {
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

  if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
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
}

bool PatternGrid::commitNoteFromKeyboard(int midiNote) {
  std::unique_lock<std::mutex> lock(app.stateMutex, std::defer_lock);
  if (!lockStateWithRetry(lock)) {
    return false;
  }

  std::uint8_t instrument = defaultInsertInstrument(app, selectedChannel);
  if (app.module.currentEditor().hasNoteAt(selectedRow, selectedChannel)) {
    instrument = app.module.currentEditor().instrumentAt(selectedRow, selectedChannel);
  }

  app.module.currentEditor().insertNote(selectedRow, selectedChannel, midiNote, instrument, insertGateTicks, insertVelocity, true);
    // If an active sample slot is armed, explicitly set it so the sample field takes priority
    if (app.activeSampleSlot >= 0 && app.activeSampleSlot <= 255) {
      app.module.currentEditor().setSample(selectedRow, selectedChannel, static_cast<std::uint16_t>(app.activeSampleSlot));
    }

  int nextRow = std::min(static_cast<int>(app.module.currentEditor().rows()) - 1, selectedRow + editStep);
  lock.unlock();
  refreshSnapshot();
  repaint();
  selectCell(nextRow, selectedChannel);
  return true;
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

  std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
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
      if (step.hasNote) {
        step.note = app.module.currentEditor().noteAt(row, channel);
        step.instrument = app.module.currentEditor().instrumentAt(row, channel);
        step.sample = app.module.currentEditor().sampleAt(row, channel);
        step.gateTicks = app.module.currentEditor().gateTicksAt(row, channel);
        step.velocity = app.module.currentEditor().velocityAt(row, channel);
        step.retrigger = app.module.currentEditor().retriggerAt(row, channel);
        step.effectCommand = app.module.currentEditor().effectCommandAt(row, channel);
        step.effectValue = app.module.currentEditor().effectValueAt(row, channel);
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

  std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
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

  std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
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
      if (step.hasNote) {
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
        cachedEffectCommand[index] = 0;
        cachedEffectValue[index] = 0;
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
        cachedEffectCommand[index] = 0;
        cachedEffectValue[index] = 0;
      }
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
