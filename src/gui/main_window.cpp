#include "main_window.h"
#include "pattern_grid.h"
#include "app.h"
#include "extracker/pattern_templates.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
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

bool parseTrackerNote(const juce::String& text, int& midiNote) {
  juce::String normalized = text.trim().toUpperCase();
  if (normalized.isEmpty()) {
    return false;
  }

  bool numericOnly = true;
  for (int i = 0; i < normalized.length(); ++i) {
    if (!juce::CharacterFunctions::isDigit(normalized[i])) {
      numericOnly = false;
      break;
    }
  }

  if (numericOnly) {
    const int parsed = normalized.getIntValue();
    if (parsed < 0 || parsed > 127) {
      return false;
    }
    midiNote = parsed;
    return true;
  }

  juce::String compact = normalized.replaceCharacter('-', ' ');
  compact = compact.removeCharacters(" ");
  if (compact.length() < 2) {
    return false;
  }

  int semitone = -1;
  switch (compact[0]) {
    case 'C': semitone = 0; break;
    case 'D': semitone = 2; break;
    case 'E': semitone = 4; break;
    case 'F': semitone = 5; break;
    case 'G': semitone = 7; break;
    case 'A': semitone = 9; break;
    case 'B': semitone = 11; break;
    default: return false;
  }

  int octavePos = 1;
  if (compact.length() >= 3 && compact[1] == '#') {
    ++semitone;
    octavePos = 2;
  }
  if (octavePos >= compact.length()) {
    return false;
  }

  juce::String octaveText = compact.substring(octavePos);
  for (int i = 0; i < octaveText.length(); ++i) {
    if (!juce::CharacterFunctions::isDigit(octaveText[i])) {
      return false;
    }
  }

  const int octave = octaveText.getIntValue();
  const int parsed = octave * 12 + semitone;
  if (parsed < 0 || parsed > 127) {
    return false;
  }

  midiNote = parsed;
  return true;
}

bool parseSearchByte(const juce::String& text, int& value) {
  juce::String normalized = text.trim();
  if (normalized.isEmpty()) {
    return false;
  }

  const bool prefixedHex = normalized.startsWithIgnoreCase("0x");
  bool hasHexDigits = false;
  for (int i = 0; i < normalized.length(); ++i) {
    const juce::juce_wchar c = normalized[i];
    if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
      hasHexDigits = true;
      break;
    }
  }

  if (prefixedHex || hasHexDigits) {
    return parseHexByte(normalized, value);
  }

  for (int i = 0; i < normalized.length(); ++i) {
    if (!juce::CharacterFunctions::isDigit(normalized[i])) {
      return false;
    }
  }

  const int parsed = normalized.getIntValue();
  if (parsed < 0 || parsed > 255) {
    return false;
  }
  value = parsed;
  return true;
}

std::string normalizeTemplateName(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return name;
}

juce::String displayTemplateName(const std::string& templateName) {
  if (templateName == "house") {
    return "House";
  }
  if (templateName == "electro") {
    return "Electro";
  }
  return "Blank";
}

juce::File startupTemplatePrefsFile() {
  auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("extracker");
  base.createDirectory();
  return base.getChildFile("startup_template.txt");
}

std::string loadStartupTemplatePreference() {
  const juce::File prefs = startupTemplatePrefsFile();
  if (!prefs.existsAsFile()) {
    return "blank";
  }

  const juce::String text = prefs.loadFileAsString().trim();
  const std::string normalized = normalizeTemplateName(text.toStdString());
  if (normalized == "house" || normalized == "electro" || normalized == "blank") {
    return normalized;
  }
  return "blank";
}

void saveStartupTemplatePreference(const std::string& templateName) {
  const juce::File prefs = startupTemplatePrefsFile();
  prefs.replaceWithText(juce::String(normalizeTemplateName(templateName)) + "\n");
}

juce::File sampleFoldersPrefsFile() {
  auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("extracker");
  base.createDirectory();
  return base.getChildFile("sample_folders.txt");
}

juce::File songFilePrefsFile() {
  auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("extracker");
  base.createDirectory();
  return base.getChildFile("last_song_file.txt");
}

juce::File loadLastSongFilePreference() {
  const juce::File prefs = songFilePrefsFile();
  if (!prefs.existsAsFile()) {
    return {};
  }

  const juce::String path = prefs.loadFileAsString().trim();
  if (path.isEmpty()) {
    return {};
  }

  const juce::File candidate(path);
  if (candidate.existsAsFile()) {
    return candidate;
  }

  if (candidate.getParentDirectory().isDirectory()) {
    return candidate;
  }

  return {};
}

void saveLastSongFilePreference(const juce::File& file) {
  if (file == juce::File()) {
    return;
  }
  songFilePrefsFile().replaceWithText(file.getFullPathName() + "\n");
}

juce::File normalizeSongSaveTarget(const juce::File& selectedFile) {
  if (selectedFile == juce::File()) {
    return selectedFile;
  }

  const juce::String extension = selectedFile.getFileExtension().toLowerCase();
  if (extension == ".xtp" || extension == ".xtd") {
    return selectedFile;
  }

  return selectedFile.withFileExtension(".xtd");
}

std::vector<juce::File> loadSampleFolderPreferences() {
  std::vector<juce::File> folders;
  const juce::File prefs = sampleFoldersPrefsFile();
  if (!prefs.existsAsFile()) {
    return folders;
  }

  const juce::StringArray lines = juce::StringArray::fromLines(prefs.loadFileAsString());
  for (const auto& line : lines) {
    const juce::String trimmed = line.trim();
    if (trimmed.isEmpty()) {
      continue;
    }

    const juce::File candidate(trimmed);
    if (!candidate.isDirectory()) {
      continue;
    }

    const juce::String path = candidate.getFullPathName();
    bool alreadyPresent = false;
    for (const auto& existing : folders) {
      if (existing.getFullPathName() == path) {
        alreadyPresent = true;
        break;
      }
    }
    if (!alreadyPresent) {
      folders.push_back(candidate);
    }
  }

  return folders;
}

void saveSampleFolderPreferences(const std::vector<juce::File>& folders) {
  juce::StringArray lines;
  for (const auto& folder : folders) {
    if (folder.isDirectory()) {
      lines.add(folder.getFullPathName());
    }
  }
  sampleFoldersPrefsFile().replaceWithText(lines.joinIntoString("\n") + "\n");
}

void rememberSampleFolderPreference(std::vector<juce::File>& folders, const juce::File& folder) {
  if (!folder.isDirectory()) {
    return;
  }

  const juce::String path = folder.getFullPathName();
  folders.erase(
      std::remove_if(
          folders.begin(),
          folders.end(),
          [&path](const juce::File& existing) {
            return existing.getFullPathName() == path;
          }),
      folders.end());

  folders.insert(folders.begin(), folder);

  constexpr std::size_t kMaxRememberedSampleFolders = 12;
  if (folders.size() > kMaxRememberedSampleFolders) {
    folders.resize(kMaxRememberedSampleFolders);
  }

  saveSampleFolderPreferences(folders);
}

class MiniPianoKeyboard : public juce::Component {
public:
  std::function<void(int)> onNoteOn;
  std::function<void(int)> onNoteOff;

  void setLowestOctave(int octave) {
    lowestOctave = std::clamp(octave, 0, 8);
    repaint();
  }

  void paint(juce::Graphics& g) override {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float wkw = w / static_cast<float>(kNumOctaves * 7);
    const float bkw = wkw * 0.58f;
    const float bkh = h * 0.60f;

    g.fillAll(juce::Colour(0xFF1A1D21));

    for (int o = 0; o < kNumOctaves; ++o) {
      for (int wi = 0; wi < 7; ++wi) {
        const int note = (lowestOctave + o) * 12 + kWhiteToSemi[wi];
        const float x = static_cast<float>(o * 7 + wi) * wkw;
        const bool pressed = (note == activeNote);
        juce::Rectangle<float> r(x + 1.f, 1.f, wkw - 2.f, h - 2.f);
        g.setColour(pressed ? juce::Colour(0xFF8AB4F8) : juce::Colours::white);
        g.fillRect(r);
        g.setColour(juce::Colour(0xFF444444));
        g.drawRect(r, 1.f);
      }
    }

    for (int o = 0; o < kNumOctaves; ++o) {
      for (int bi = 0; bi < 5; ++bi) {
        const int note = (lowestOctave + o) * 12 + kBlackToSemi[bi];
        if (note < 0 || note > 127) continue;
        const float cx = (static_cast<float>(o * 7) + kBlackCenter[bi]) * wkw;
        const float x = cx - bkw * 0.5f;
        const bool pressed = (note == activeNote);
        juce::Rectangle<float> r(x, 0.f, bkw, bkh);
        g.setColour(pressed ? juce::Colour(0xFF5A8AE0) : juce::Colour(0xFF111417));
        g.fillRect(r);
        g.setColour(juce::Colour(0xFF555555));
        g.drawRect(r, 1.f);
      }
    }
  }

  void mouseDown(const juce::MouseEvent& e) override {
    const int note = noteAt(e.x, e.y);
    if (note != activeNote) {
      releaseActive();
      activeNote = note;
      repaint();
      if (activeNote >= 0 && onNoteOn) onNoteOn(activeNote);
    }
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    const int note = noteAt(e.x, e.y);
    if (note != activeNote) {
      releaseActive();
      activeNote = note;
      repaint();
      if (activeNote >= 0 && onNoteOn) onNoteOn(activeNote);
    }
  }

  void mouseUp(const juce::MouseEvent&) override { releaseActive(); repaint(); }
  void mouseExit(const juce::MouseEvent&) override { releaseActive(); repaint(); }

private:
  static constexpr int kNumOctaves = 4;
  static constexpr int kWhiteToSemi[7] = {0, 2, 4, 5, 7, 9, 11};
  static constexpr int kBlackToSemi[5] = {1, 3, 6, 8, 10};
  static constexpr float kBlackCenter[5] = {0.67f, 1.83f, 3.67f, 4.67f, 5.83f};

  int lowestOctave = 4;
  int activeNote = -1;

  void releaseActive() {
    if (activeNote >= 0) {
      if (onNoteOff) onNoteOff(activeNote);
      activeNote = -1;
    }
  }

  int noteAt(int px, int py) const {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float wkw = w / static_cast<float>(kNumOctaves * 7);
    const float bkw = wkw * 0.58f;
    const float bkh = h * 0.60f;
    const float fx = static_cast<float>(px);
    const float fy = static_cast<float>(py);

    if (fy < bkh) {
      for (int o = 0; o < kNumOctaves; ++o) {
        for (int bi = 0; bi < 5; ++bi) {
          const float cx = (static_cast<float>(o * 7) + kBlackCenter[bi]) * wkw;
          if (fx >= cx - bkw * 0.5f && fx < cx + bkw * 0.5f) {
            const int note = (lowestOctave + o) * 12 + kBlackToSemi[bi];
            return (note >= 0 && note <= 127) ? note : -1;
          }
        }
      }
    }

    for (int o = 0; o < kNumOctaves; ++o) {
      for (int wi = 0; wi < 7; ++wi) {
        const float x = static_cast<float>(o * 7 + wi) * wkw;
        if (fx >= x && fx < x + wkw) {
          const int note = (lowestOctave + o) * 12 + kWhiteToSemi[wi];
          return (note >= 0 && note <= 127) ? note : -1;
        }
      }
    }
    return -1;
  }
};

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

class SampleWaveformView : public juce::Component {
public:
  void setWaveform(std::vector<float> samples) {
    waveform = std::move(samples);
    repaint();
  }

  void setSelection(double startNorm, double endNorm) {
    selectionStart = std::clamp(startNorm, 0.0, 1.0);
    selectionEnd = std::clamp(endNorm, selectionStart + 0.001, 1.0);
    repaint();
  }

  void setSelectionChangedCallback(std::function<void(double, double)> callback) {
    selectionChanged = std::move(callback);
  }

  void setCropShortcutCallback(std::function<void()> callback) {
    cropShortcut = std::move(callback);
  }

  void setSelectionDragStateCallback(std::function<void(bool)> callback) {
    selectionDragStateChanged = std::move(callback);
  }

  bool keyPressed(const juce::KeyPress& key) override {
    const auto lower = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (key.getModifiers().isCommandDown() && lower == 'k') {
      if (cropShortcut) {
        cropShortcut();
        return true;
      }
    }
    return false;
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(juce::Colour(0xFF13181C));
    auto r = getLocalBounds().reduced(2);
    g.setColour(juce::Colour(0xFF2A2F34));
    g.drawRect(r, 1);

    if (waveform.empty() || r.getWidth() < 4 || r.getHeight() < 4) {
      g.setColour(juce::Colour(0xFF6A737D));
      g.drawText("No waveform", r, juce::Justification::centred);
      return;
    }

    const int leftX = xForNorm(selectionStart, r);
    const int rightX = xForNorm(selectionEnd, r);

    g.setColour(juce::Colour(0x44242A31));
    g.fillRect(r.withRight(leftX));
    g.fillRect(r.withLeft(rightX));

    juce::Path waveformPath;
    const float midY = static_cast<float>(r.getCentreY());
    const float amp = static_cast<float>(r.getHeight()) * 0.45f;
    for (int x = r.getX(); x < r.getRight(); ++x) {
      const double t = static_cast<double>(x - r.getX()) / static_cast<double>(std::max(1, r.getWidth() - 1));
      const std::size_t idx = static_cast<std::size_t>(std::llround(t * static_cast<double>(waveform.size() - 1)));
      const float y = midY - std::clamp(waveform[idx], -1.0f, 1.0f) * amp;
      if (x == r.getX()) {
        waveformPath.startNewSubPath(static_cast<float>(x), y);
      } else {
        waveformPath.lineTo(static_cast<float>(x), y);
      }
    }

    g.setColour(juce::Colour(0xFF8AB4F8));
    g.strokePath(waveformPath, juce::PathStrokeType(1.2f));

    g.setColour(juce::Colour(0xFF2EA043));
    g.drawLine(static_cast<float>(leftX), static_cast<float>(r.getY()), static_cast<float>(leftX), static_cast<float>(r.getBottom()), 2.0f);
    g.drawLine(static_cast<float>(rightX), static_cast<float>(r.getY()), static_cast<float>(rightX), static_cast<float>(r.getBottom()), 2.0f);

    if (hasKeyboardFocus(true)) {
      g.setColour(juce::Colour(0xFFE8C547));
      g.drawRect(getLocalBounds().reduced(1), 1);
    }
  }

  void mouseDown(const juce::MouseEvent& e) override {
    grabKeyboardFocus();
    if (selectionDragStateChanged) {
      selectionDragStateChanged(true);
    }
    auto r = getLocalBounds().reduced(2);
    const int leftX = xForNorm(selectionStart, r);
    const int rightX = xForNorm(selectionEnd, r);
    const int dxLeft = std::abs(e.x - leftX);
    const int dxRight = std::abs(e.x - rightX);
    dragMode = dxLeft <= dxRight ? DragMode::Start : DragMode::End;
    updateSelectionFromX(e.x, r);
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    auto r = getLocalBounds().reduced(2);
    updateSelectionFromX(e.x, r);
  }

  void mouseUp(const juce::MouseEvent&) override {
    if (selectionDragStateChanged) {
      selectionDragStateChanged(false);
    }
  }

private:
  enum class DragMode { Start, End };

  int xForNorm(double n, const juce::Rectangle<int>& r) const {
    return r.getX() + static_cast<int>(std::llround(n * static_cast<double>(std::max(1, r.getWidth() - 1))));
  }

  void updateSelectionFromX(int x, const juce::Rectangle<int>& r) {
    const double t = std::clamp(static_cast<double>(x - r.getX()) / static_cast<double>(std::max(1, r.getWidth() - 1)), 0.0, 1.0);
    if (dragMode == DragMode::Start) {
      selectionStart = std::min(t, selectionEnd - 0.001);
    } else {
      selectionEnd = std::max(t, selectionStart + 0.001);
    }
    if (selectionChanged) {
      selectionChanged(selectionStart, selectionEnd);
    }
    repaint();
  }

  std::vector<float> waveform;
  double selectionStart = 0.0;
  double selectionEnd = 1.0;
  DragMode dragMode = DragMode::Start;
  std::function<void(double, double)> selectionChanged;
  std::function<void(bool)> selectionDragStateChanged;
  std::function<void()> cropShortcut;
};

// ── Channel Mixer Strip ───────────────────────────────────────────────────────
// A row of vertical volume sliders, one per channel, shown below the pattern grid.
// Width matches the pattern grid content so it scrolls in sync via mixerViewport.
class ChannelMixerStrip : public juce::Component {
  std::vector<std::unique_ptr<juce::Slider>> sliders_;
  int labelWidth_ = 40;
  int cellWidth_  = 80;
  bool darkMode_  = false;
public:
  std::function<void(int, float)> onVolumeChanged;

  void rebuild(int numChannels, int labelWidth, int cellWidth,
               bool darkMode, std::function<float(int)> getVol) {
    removeAllChildren();
    sliders_.clear();
    labelWidth_ = labelWidth;
    cellWidth_  = cellWidth;
    darkMode_   = darkMode;
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& sl = *sliders_.emplace_back(std::make_unique<juce::Slider>());
      sl.setSliderStyle(juce::Slider::LinearVertical);
      sl.setTextBoxStyle(juce::Slider::TextBoxBelow, true, std::max(32, cellWidth - 4), 14);
      sl.setRange(0.0, 200.0, 1.0);
      sl.setValue(static_cast<double>(getVol(ch)) * 100.0, juce::dontSendNotification);
      sl.setNumDecimalPlacesToDisplay(0);
      sl.setTextValueSuffix("%");
      sl.setTooltip("Ch " + juce::String(ch) + " volume (0-200%)");
      const int idx = ch;
      sl.onValueChange = [this, idx]() {
        if (onVolumeChanged)
          onVolumeChanged(idx, static_cast<float>(sliders_[idx]->getValue()) / 100.0f);
      };
      addAndMakeVisible(sl);
    }
    resized();
    repaint();
  }

  void syncLayout(int labelWidth, int cellWidth) {
    if (labelWidth_ == labelWidth && cellWidth_ == cellWidth) return;
    labelWidth_ = labelWidth;
    cellWidth_  = cellWidth;
    resized();
  }

  void setChannelVolume(int ch, float vol) {
    if (ch >= 0 && ch < static_cast<int>(sliders_.size()))
      sliders_[ch]->setValue(static_cast<double>(vol) * 100.0, juce::dontSendNotification);
  }

  int numSliders() const { return static_cast<int>(sliders_.size()); }

  void setDarkMode(bool dark) { darkMode_ = dark; repaint(); }

  void paint(juce::Graphics& g) override {
    g.fillAll(darkMode_ ? juce::Colour(0xFF181820) : juce::Colour(0xFF2C2C34));
    g.setColour(darkMode_ ? juce::Colour(0xFF333348) : juce::Colour(0xFF50506A));
    g.fillRect(0, 0, getWidth(), 2);  // top accent line
    // Label column background
    g.setColour(darkMode_ ? juce::Colour(0xFF111118) : juce::Colour(0xFF222228));
    g.fillRect(0, 2, labelWidth_, getHeight() - 2);
    g.setColour(darkMode_ ? juce::Colour(0xFFAAAAAA) : juce::Colour(0xFFCCCCCC));
    g.setFont(juce::Font(10.0f));
    g.drawText("Vol", 0, 0, labelWidth_, getHeight(), juce::Justification::centred);
  }

  void resized() override {
    const int h = getHeight();
    for (int ch = 0; ch < static_cast<int>(sliders_.size()); ++ch) {
      sliders_[ch]->setBounds(labelWidth_ + ch * cellWidth_, 2,
                              cellWidth_, h - 2);
    }
  }
};

class TrackerMainComponent : public juce::Component,
                             private juce::Timer {
public:
  static constexpr int kModuleMessageMaxChars = 2048;

  void paint(juce::Graphics& g) override {
    g.fillAll(app.darkMode ? juce::Colour(0xFF111111) : juce::Colour(0xFF383838));
  }

  bool keyPressed(const juce::KeyPress& key) override {
    // Ctrl+C/X/V: route to pattern grid even when focus is on a panel control.
    // JUCE only calls this after the focused component and all intermediate parents
    // return false, so text editors still handle their own copy/paste first.
    if (key.getModifiers().isCommandDown()) {
      const int keyCode = key.getKeyCode();
      if (keyCode == 'C' || keyCode == 'c') {
        if (patternGrid.copySelection()) { patternGrid.grabKeyboardFocus(); return true; }
        return false;
      }
      if (keyCode == 'X' || keyCode == 'x') {
        if (patternGrid.cutSelection()) { patternGrid.grabKeyboardFocus(); return true; }
        return false;
      }
      if (keyCode == 'V' || keyCode == 'v') {
        if (patternGrid.pasteSelection()) { patternGrid.grabKeyboardFocus(); return true; }
        return false;
      }
    }
    if (key == juce::KeyPress::F1Key) {
      saveHelpToFile();
      return true;
    }
    if (key == juce::KeyPress::F5Key) {
      // Play entire song from beginning
      app.transport.stop();
      app.transport.resetTickCount();
      app.sequencer.reset();
      app.plugins.allNotesOff();
      app.audio.allNotesOff();
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.playMode = PlayMode::PLAY_SONG;
        app.lastSongModeRow = -1;
        app.songModePatternAdvanceBaseline = 0;  // transport.resetTickCount() was called above
        app.currentSongOrderPositionCache.store(0);
        if (app.module.songLength() > 0) {
          app.module.switchToPattern(app.module.songEntryAt(0));
          app.transport.setPatternRows(static_cast<std::uint32_t>(app.module.currentEditor().rows()));
        }
        app.currentPatternCache.store(app.module.currentPattern());
      }
      app.transport.play();
      updateStatusLabels();
      patternGrid.repaint();
      return true;
    }
    if (key == juce::KeyPress::F6Key) {
      // Play current pattern from beginning
      app.transport.stop();
      app.transport.resetTickCount();
      app.sequencer.reset();
      app.plugins.allNotesOff();
      app.audio.allNotesOff();
      app.playMode = PlayMode::PLAY_PATTERN;
      app.lastSongModeRow = -1;
      app.transport.play();
      updateStatusLabels();
      patternGrid.repaint();
      return true;
    }
    if (key == juce::KeyPress::F7Key) {
      // Play from cursor row
      const int startRow = std::max(0, selectedStepRow);
      app.playMode = PlayMode::PLAY_PATTERN;
      app.lastSongModeRow = -1;
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.transport.jumpToRow(static_cast<std::uint32_t>(startRow));
      }
      app.transport.play();
      updateStatusLabels();
      patternGrid.repaint();
      return true;
    }
    return false;
  }

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
    addAndMakeVisible(recordButton);
    addAndMakeVisible(playFromCursorButton);
    addAndMakeVisible(overdubButton);
    addAndMakeVisible(punchButton);
    addAndMakeVisible(recordStartRowLabel);
    addAndMakeVisible(recordStartRowSlider);
    addAndMakeVisible(recordStepLabel);
    addAndMakeVisible(recordStepSlider);
    addAndMakeVisible(playModePatternButton);
    addAndMakeVisible(playModeSongButton);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(helpButton);
    addAndMakeVisible(darkModeButton);
    addAndMakeVisible(pianoToggleButton);
    addAndMakeVisible(bounceWavButton);
    addAndMakeVisible(patternLabel);
    addAndMakeVisible(patternSelector);
    addAndMakeVisible(insertPatternBeforeButton);
    addAndMakeVisible(insertPatternAfterButton);
    addAndMakeVisible(removePatternButton);
    addAndMakeVisible(gridDensityButton);
    addAndMakeVisible(tempoSlider);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(swingLabel);
    addAndMakeVisible(swingSlider);
    addAndMakeVisible(volumeLabel);
    addAndMakeVisible(volumeSlider);
    addAndMakeVisible(ticksPerBeatLabel);
    addAndMakeVisible(ticksPerBeatSlider);
    addAndMakeVisible(ticksPerRowLabel);
    addAndMakeVisible(ticksPerRowSlider);
    addAndMakeVisible(expandPatternButton);
    addAndMakeVisible(shrinkPatternButton);
    addAndMakeVisible(expandChannelButton);
    addAndMakeVisible(shrinkChannelButton);
    addAndMakeVisible(newModuleButton);
    addAndMakeVisible(savePatternButton);
    addAndMakeVisible(loadPatternButton);
    addAndMakeVisible(insertRowButton);
    addAndMakeVisible(removeRowButton);
    addAndMakeVisible(rowEditScopeButton);
    addAndMakeVisible(insertSwingModeButton);
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
    addAndMakeVisible(songArrangerTitle);
    addAndMakeVisible(songArrangerBarsLabel);
    addAndMakeVisible(songArrangerBarsSlider);
    addAndMakeVisible(songArrangerDuplicateButton);
    addAndMakeVisible(songArrangerInsertBarsButton);
    addAndMakeVisible(songArrangerRippleLeftButton);
    addAndMakeVisible(songArrangerRippleRightButton);
    addAndMakeVisible(moduleMessageTitle);
    addAndMakeVisible(moduleMessageEditor);
    addAndMakeVisible(moduleMessageCounterLabel);
    addAndMakeVisible(moduleMessageStateLabel);
    addAndMakeVisible(moduleMessageApplyButton);
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
    addAndMakeVisible(sampleRouteKeystationButton);
    addAndMakeVisible(sampleArmButton);
    addAndMakeVisible(sampleArmChannelLabel);
    addAndMakeVisible(sampleArmChannelSelector);
    addAndMakeVisible(sampleRenameEditor);
    addAndMakeVisible(sampleRenameButton);
    addAndMakeVisible(sampleClearButton);
    addAndMakeVisible(sampleTrimStartLabel);
    addAndMakeVisible(sampleTrimStartSlider);
    addAndMakeVisible(sampleTrimEndLabel);
    addAndMakeVisible(sampleTrimEndSlider);
    addAndMakeVisible(sampleTrimApplyButton);
    addAndMakeVisible(sampleTrimReloadButton);
    addAndMakeVisible(sampleNormalizeButton);
    addAndMakeVisible(sampleFadeInButton);
    addAndMakeVisible(sampleFadeOutButton);
    addAndMakeVisible(sampleReverseButton);
    addAndMakeVisible(sampleRateSelector);
    addAndMakeVisible(sampleResampleButton);
    addAndMakeVisible(sampleBitDepthSelector);
    addAndMakeVisible(sampleBitDepthButton);
    addAndMakeVisible(sampleLoopXfadeButton);
    addAndMakeVisible(sampleWaveformView);
    addAndMakeVisible(samplePathLabel);
    addAndMakeVisible(sampleVolumeLabel);
    addAndMakeVisible(sampleVolumeSlider);
    addAndMakeVisible(samplePanLabel);
    addAndMakeVisible(samplePanSlider);
    addAndMakeVisible(sampleTransposeLabel);
    addAndMakeVisible(sampleTransposeSlider);
    addAndMakeVisible(sampleLoopModeLabel);
    addAndMakeVisible(sampleLoopModeBox);
    addAndMakeVisible(slotLabel);
    addAndMakeVisible(slotSelector);
    addAndMakeVisible(pluginSelector);
    addAndMakeVisible(scanPluginsButton);
    addAndMakeVisible(assignPluginButton);
    addAndMakeVisible(loadInstrumentFileButton);
    addAndMakeVisible(openPluginEditorButton);
    addAndMakeVisible(stepEditorTitle);
    addAndMakeVisible(selectedStepLabel);
    addAndMakeVisible(patternSearchTitle);
    addAndMakeVisible(patternSearchMode);
    addAndMakeVisible(patternSearchValue);
    addAndMakeVisible(patternSearchPrevButton);
    addAndMakeVisible(patternSearchNextButton);
    addAndMakeVisible(patternSearchStatusLabel);
    addAndMakeVisible(patternCompareTitle);
    addAndMakeVisible(patternCompareCaptureButton);
    addAndMakeVisible(patternCompareToggleButton);
    addAndMakeVisible(patternCompareStatusLabel);
    addAndMakeVisible(copyBlockButton);
    addAndMakeVisible(cutBlockButton);
    addAndMakeVisible(pasteBlockButton);
    addAndMakeVisible(transposeDownButton);
    addAndMakeVisible(transposeUpButton);
    addAndMakeVisible(applyFxToBlockButton);
    addAndMakeVisible(patternMacroTitle);
    addAndMakeVisible(patternMacroFillHatsButton);
    addAndMakeVisible(patternMacroAccent4Button);
    addAndMakeVisible(patternMacroInvertVelocityButton);
    addAndMakeVisible(undoHistoryTitle);
    addAndMakeVisible(undoHistoryTimelineLabel);
    addAndMakeVisible(undoHistorySelector);
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
    addAndMakeVisible(midiLearnTitle);
    addAndMakeVisible(midiLearnVelocityButton);
    addAndMakeVisible(midiLearnGateButton);
    addAndMakeVisible(midiLearnEffectCommandButton);
    addAndMakeVisible(midiLearnEffectValueButton);
    addAndMakeVisible(midiLearnClearButton);
    addAndMakeVisible(midiLearnStatusLabel);
    addAndMakeVisible(keyboardOctaveLabel);
    addAndMakeVisible(keyboardOctaveSlider);
    addAndMakeVisible(editStepLabel);
    addAndMakeVisible(editStepSlider);
    addAndMakeVisible(notePreviewMsLabel);
    addAndMakeVisible(notePreviewMsSlider);
    addAndMakeVisible(followPreviewToggle);
    addAndMakeVisible(gainLabel);
    addAndMakeVisible(gainSlider);
    addAndMakeVisible(attackLabel);
    addAndMakeVisible(attackSlider);
    addAndMakeVisible(releaseLabel);
    addAndMakeVisible(releaseSlider);
    addAndMakeVisible(pitchLabel);
    addAndMakeVisible(pitchSlider);
    addAndMakeVisible(depthLabel);
    addAndMakeVisible(depthSlider);
    addAndMakeVisible(instrumentRootLabel);
    addAndMakeVisible(instrumentRootSlider);
    addAndMakeVisible(instrumentPanLabel);
    addAndMakeVisible(instrumentPanSlider);
    addAndMakeVisible(instrumentLoopModeLabel);
    addAndMakeVisible(instrumentLoopModeBox);
    addAndMakeVisible(instrumentLoopStartLabel);
    addAndMakeVisible(instrumentLoopStartSlider);
    addAndMakeVisible(instrumentLoopEndLabel);
    addAndMakeVisible(instrumentLoopEndSlider);
    addAndMakeVisible(fxResetButton);
    addAndMakeVisible(fxSectionLabel);
    addAndMakeVisible(fxDelayLabel);
    addAndMakeVisible(fxDelayTimeSlider);
    addAndMakeVisible(fxDelayFeedbackSlider);
    addAndMakeVisible(fxDelayWetSlider);
    addAndMakeVisible(fxDistLabel);
    addAndMakeVisible(fxDistTypeBox);
    addAndMakeVisible(fxDistDriveSlider);
    addAndMakeVisible(fxChorusLabel);
    addAndMakeVisible(fxChorusRateSlider);
    addAndMakeVisible(fxChorusDepthSlider);
    addAndMakeVisible(fxChorusWetSlider);
    addAndMakeVisible(fxReverbSendLabel);
    addAndMakeVisible(fxReverbSendSlider);
    addAndMakeVisible(reverbSectionLabel);
    addAndMakeVisible(reverbRoomSlider);
    addAndMakeVisible(reverbDampSlider);
    addAndMakeVisible(reverbWetSlider);
    addAndMakeVisible(reverbWidthSlider);
    addAndMakeVisible(filterSectionLabel);
    addAndMakeVisible(filterChannelLabel);
    addAndMakeVisible(filterTypeBox);
    addAndMakeVisible(filterCutoffLabel);
    addAndMakeVisible(filterCutoffSlider);
    addAndMakeVisible(filterResonanceLabel);
    addAndMakeVisible(filterResonanceSlider);
    addAndMakeVisible(slotActivityTitle);
    addAndMakeVisible(applyChannelMapButton);
    addAndMakeVisible(patternViewport);
    addAndMakeVisible(patternRowSlider);
    addAndMakeVisible(mixerViewport);
    mixerViewport.setViewedComponent(&mixerStrip, false);
    mixerViewport.setScrollBarsShown(false, false);
    mixerStrip.onVolumeChanged = [this](int ch, float vol) {
      app.sequencer.setChannelVolume(static_cast<std::size_t>(ch), vol);
    };
    patternViewport.setViewedComponent(&patternGrid, false);
    patternViewport.setScrollBarsShown(true, true);
    patternViewport.setScrollBarThickness(12);
    patternRowSlider.setSliderStyle(juce::Slider::LinearVertical);
    patternRowSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    patternRowSlider.setRange(0.0, 1.0, 1.0);
    patternRowSlider.onValueChange = [this]() {
      if (suppressPatternRowSliderCallback) {
        return;
      }
      patternViewport.setViewPosition(
          patternViewport.getViewPositionX(),
          static_cast<int>(std::lround(patternRowSlider.getValue())));
    };
    patternSelector.setScrollWheelEnabled(true);
    patternSelector.addMouseListener(this, false);
    songOrderListView.setInterceptsMouseClicks(true, false);
    songOrderListView.addMouseListener(this, false);

    // Piano keyboard
    pianoKeyboard.setLowestOctave(4);
    pianoKeyboard.onNoteOn = [this](int note) {
      const auto instr = static_cast<std::uint8_t>(app.midiInstrument);
      app.plugins.triggerNoteOn(instr, note, 100, true);
    };
    pianoKeyboard.onNoteOff = [this](int note) {
      app.plugins.triggerNoteOff(static_cast<std::uint8_t>(app.midiInstrument), note);
    };
    addAndMakeVisible(pianoKeyboard);

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
    reparentToPanelWrapper(songArrangerTitle);
    reparentToPanelWrapper(songArrangerBarsLabel);
    reparentToPanelWrapper(songArrangerBarsSlider);
    reparentToPanelWrapper(songArrangerDuplicateButton);
    reparentToPanelWrapper(songArrangerInsertBarsButton);
    reparentToPanelWrapper(songArrangerRippleLeftButton);
    reparentToPanelWrapper(songArrangerRippleRightButton);
    reparentToPanelWrapper(startupTemplateLabel);
    reparentToPanelWrapper(startupTemplateSelector);
    reparentToPanelWrapper(startupTemplatePreviewButton);
    reparentToPanelWrapper(startupTemplateSetDefaultButton);
    reparentToPanelWrapper(moduleMessageTitle);
    reparentToPanelWrapper(moduleMessageEditor);
    reparentToPanelWrapper(moduleMessageCounterLabel);
    reparentToPanelWrapper(moduleMessageStateLabel);
    reparentToPanelWrapper(moduleMessageApplyButton);
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
    reparentToPanelWrapper(scanPluginsButton);
    reparentToPanelWrapper(pluginSelector);
    reparentToPanelWrapper(assignPluginButton);
    reparentToPanelWrapper(loadInstrumentFileButton);
    reparentToPanelWrapper(openPluginEditorButton);
    reparentToPanelWrapper(pluginStatusLabel);
    reparentToPanelWrapper(sampleBankTitle);
    reparentToPanelWrapper(sampleSlotLabel);
    reparentToPanelWrapper(sampleSlotSelector);
    reparentToPanelWrapper(sampleLoadButton);
    reparentToPanelWrapper(sampleAssignButton);
    reparentToPanelWrapper(sampleAssignToChannelButton);
    reparentToPanelWrapper(sampleRouteKeystationButton);
    reparentToPanelWrapper(sampleArmButton);
    reparentToPanelWrapper(sampleArmChannelLabel);
    reparentToPanelWrapper(sampleArmChannelSelector);
    reparentToPanelWrapper(sampleRenameEditor);
    reparentToPanelWrapper(sampleRenameButton);
    reparentToPanelWrapper(sampleClearButton);
    reparentToPanelWrapper(sampleTrimStartLabel);
    reparentToPanelWrapper(sampleTrimStartSlider);
    reparentToPanelWrapper(sampleTrimEndLabel);
    reparentToPanelWrapper(sampleTrimEndSlider);
    reparentToPanelWrapper(sampleTrimApplyButton);
    reparentToPanelWrapper(sampleTrimReloadButton);
    reparentToPanelWrapper(sampleNormalizeButton);
    reparentToPanelWrapper(sampleFadeInButton);
    reparentToPanelWrapper(sampleFadeOutButton);
    reparentToPanelWrapper(sampleReverseButton);
    reparentToPanelWrapper(sampleRateSelector);
    reparentToPanelWrapper(sampleResampleButton);
    reparentToPanelWrapper(sampleBitDepthSelector);
    reparentToPanelWrapper(sampleBitDepthButton);
    reparentToPanelWrapper(sampleLoopXfadeButton);
    reparentToPanelWrapper(sampleWaveformView);
    reparentToPanelWrapper(samplePathLabel);
    reparentToPanelWrapper(sampleVolumeLabel);
    reparentToPanelWrapper(sampleVolumeSlider);
    reparentToPanelWrapper(samplePanLabel);
    reparentToPanelWrapper(samplePanSlider);
    reparentToPanelWrapper(sampleTransposeLabel);
    reparentToPanelWrapper(sampleTransposeSlider);
    reparentToPanelWrapper(sampleLoopModeLabel);
    reparentToPanelWrapper(sampleLoopModeBox);
    reparentToPanelWrapper(stepEditorTitle);
    reparentToPanelWrapper(selectedStepLabel);
    reparentToPanelWrapper(patternSearchTitle);
    reparentToPanelWrapper(patternSearchMode);
    reparentToPanelWrapper(patternSearchValue);
    reparentToPanelWrapper(patternSearchPrevButton);
    reparentToPanelWrapper(patternSearchNextButton);
    reparentToPanelWrapper(patternSearchStatusLabel);
    reparentToPanelWrapper(patternCompareTitle);
    reparentToPanelWrapper(patternCompareCaptureButton);
    reparentToPanelWrapper(patternCompareToggleButton);
    reparentToPanelWrapper(patternCompareStatusLabel);
    reparentToPanelWrapper(copyBlockButton);
    reparentToPanelWrapper(cutBlockButton);
    reparentToPanelWrapper(pasteBlockButton);
    reparentToPanelWrapper(transposeDownButton);
    reparentToPanelWrapper(transposeUpButton);
    reparentToPanelWrapper(applyFxToBlockButton);
    reparentToPanelWrapper(patternMacroTitle);
    reparentToPanelWrapper(patternMacroFillHatsButton);
    reparentToPanelWrapper(patternMacroAccent4Button);
    reparentToPanelWrapper(patternMacroInvertVelocityButton);
    reparentToPanelWrapper(undoHistoryTitle);
    reparentToPanelWrapper(undoHistoryTimelineLabel);
    reparentToPanelWrapper(undoHistorySelector);
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
    reparentToPanelWrapper(midiLearnTitle);
    reparentToPanelWrapper(midiLearnVelocityButton);
    reparentToPanelWrapper(midiLearnGateButton);
    reparentToPanelWrapper(midiLearnEffectCommandButton);
    reparentToPanelWrapper(midiLearnEffectValueButton);
    reparentToPanelWrapper(midiLearnClearButton);
    reparentToPanelWrapper(midiLearnStatusLabel);
    reparentToPanelWrapper(keyboardOctaveLabel);
    reparentToPanelWrapper(keyboardOctaveSlider);
    reparentToPanelWrapper(editStepLabel);
    reparentToPanelWrapper(editStepSlider);
    reparentToPanelWrapper(notePreviewMsLabel);
    reparentToPanelWrapper(notePreviewMsSlider);
    reparentToPanelWrapper(followPreviewToggle);
    reparentToPanelWrapper(gainLabel);
    reparentToPanelWrapper(gainSlider);
    reparentToPanelWrapper(attackLabel);
    reparentToPanelWrapper(attackSlider);
    reparentToPanelWrapper(releaseLabel);
    reparentToPanelWrapper(releaseSlider);
    reparentToPanelWrapper(pitchLabel);
    reparentToPanelWrapper(pitchSlider);
    reparentToPanelWrapper(depthLabel);
    reparentToPanelWrapper(depthSlider);
    reparentToPanelWrapper(instrumentRootLabel);
    reparentToPanelWrapper(instrumentRootSlider);
    reparentToPanelWrapper(instrumentPanLabel);
    reparentToPanelWrapper(instrumentPanSlider);
    reparentToPanelWrapper(instrumentLoopModeLabel);
    reparentToPanelWrapper(instrumentLoopModeBox);
    reparentToPanelWrapper(instrumentLoopStartLabel);
    reparentToPanelWrapper(instrumentLoopStartSlider);
    reparentToPanelWrapper(instrumentLoopEndLabel);
    reparentToPanelWrapper(instrumentLoopEndSlider);
    reparentToPanelWrapper(fxResetButton);
    reparentToPanelWrapper(fxSectionLabel);
    reparentToPanelWrapper(fxDelayLabel);
    reparentToPanelWrapper(fxDelayTimeSlider);
    reparentToPanelWrapper(fxDelayFeedbackSlider);
    reparentToPanelWrapper(fxDelayWetSlider);
    reparentToPanelWrapper(fxDistLabel);
    reparentToPanelWrapper(fxDistTypeBox);
    reparentToPanelWrapper(fxDistDriveSlider);
    reparentToPanelWrapper(fxChorusLabel);
    reparentToPanelWrapper(fxChorusRateSlider);
    reparentToPanelWrapper(fxChorusDepthSlider);
    reparentToPanelWrapper(fxChorusWetSlider);
    reparentToPanelWrapper(fxReverbSendLabel);
    reparentToPanelWrapper(fxReverbSendSlider);
    reparentToPanelWrapper(reverbSectionLabel);
    reparentToPanelWrapper(reverbRoomSlider);
    reparentToPanelWrapper(reverbDampSlider);
    reparentToPanelWrapper(reverbWetSlider);
    reparentToPanelWrapper(reverbWidthSlider);
    reparentToPanelWrapper(filterSectionLabel);
    reparentToPanelWrapper(filterChannelLabel);
    reparentToPanelWrapper(filterTypeBox);
    reparentToPanelWrapper(filterCutoffLabel);
    reparentToPanelWrapper(filterCutoffSlider);
    reparentToPanelWrapper(filterResonanceLabel);
    reparentToPanelWrapper(filterResonanceSlider);
    reparentToPanelWrapper(controlPortSectionTitle);
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
    patternGrid.setFocusModuleMessageCallback([this]() {
      moduleMessageEditor.grabKeyboardFocus();
      moduleMessageEditor.setCaretPosition(moduleMessageEditor.getText().length());
      const int targetY = std::max(0, moduleMessageEditor.getY() - 24);
      panelViewport.setViewPosition(0, targetY);
    });
    patternGrid.setSearchNavigationCallback([this](bool forward) {
      findPatternMatch(forward);
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
    patternGrid.setPreviewDurationMs(static_cast<std::uint32_t>(std::lround(notePreviewMsSlider.getValue())));

    startupTemplateSelector.clear(juce::dontSendNotification);
    startupTemplateSelector.addItem("Blank", 1);
    startupTemplateSelector.addItem("House", 2);
    startupTemplateSelector.addItem("Electro", 3);
    const std::string startupTemplate = loadStartupTemplatePreference();
    if (startupTemplate == "house") {
      startupTemplateSelector.setSelectedId(2, juce::dontSendNotification);
    } else if (startupTemplate == "electro") {
      startupTemplateSelector.setSelectedId(3, juce::dontSendNotification);
    } else {
      startupTemplateSelector.setSelectedId(1, juce::dontSendNotification);
    }
    startupTemplateSelector.onChange = [this]() {
      const int selectedId = startupTemplateSelector.getSelectedId();
      std::string templateName = "blank";
      if (selectedId == 2) {
        templateName = "house";
      } else if (selectedId == 3) {
        templateName = "electro";
      }
      pluginStatusLabel.setText("Selected startup template: " + displayTemplateName(templateName) + " (preview or set default)",
                                juce::dontSendNotification);
    };

    startupTemplatePreviewButton.onClick = [this]() {
      const int selectedId = startupTemplateSelector.getSelectedId();
      std::string templateName = "blank";
      if (selectedId == 2) {
        templateName = "house";
      } else if (selectedId == 3) {
        templateName = "electro";
      }

      {
        std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
          pluginStatusLabel.setText("Template preview skipped (engine busy)", juce::dontSendNotification);
          return;
        }
        extracker::applyPatternTemplate(app.module.currentEditor(), templateName);
      }

      refreshPatternView();
      pluginStatusLabel.setText("Previewed template: " + displayTemplateName(templateName), juce::dontSendNotification);
    };

    startupTemplateSetDefaultButton.onClick = [this]() {
      const int selectedId = startupTemplateSelector.getSelectedId();
      std::string templateName = "blank";
      if (selectedId == 2) {
        templateName = "house";
      } else if (selectedId == 3) {
        templateName = "electro";
      }
      saveStartupTemplatePreference(templateName);
      pluginStatusLabel.setText("Startup default set to " + displayTemplateName(templateName), juce::dontSendNotification);
    };

    if (startupTemplate != "blank") {
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        extracker::applyPatternTemplate(app.module.currentEditor(), startupTemplate);
      }
      pluginStatusLabel.setText("Applied startup template: " + displayTemplateName(startupTemplate), juce::dontSendNotification);
    }

    recentSampleLoadFolders = loadSampleFolderPreferences();
    if (!recentSampleLoadFolders.empty() && recentSampleLoadFolders.front().isDirectory()) {
      lastSampleLoadFolder = recentSampleLoadFolders.front();
    }
    lastSongFile = loadLastSongFilePreference();

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

    bounceWavButton.onClick = [this]() { handleBounceWav(); };

    recordButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.recordState.enabled = !app.recordState.enabled;
      const bool on = app.recordState.enabled;
      recordButton.setButtonText(on ? "Rec: On" : "Record");
      recordButton.setColour(juce::TextButton::buttonColourId,
                             on ? juce::Colour(0xFFCC2222) : getLookAndFeel().findColour(juce::TextButton::buttonColourId));
    };

    playFromCursorButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.transport.jumpToRow(app.recordState.cursorRow);
      app.transport.play();
      app.recordState.enabled = true;
      recordButton.setButtonText("Rec: On");
      recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFCC2222));
    };

    overdubButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.recordState.overdubEnabled = !app.recordState.overdubEnabled;
      const bool on = app.recordState.overdubEnabled;
      overdubButton.setButtonText(on ? "Overdub: On" : "Overdub: Off");
      overdubButton.setColour(juce::TextButton::buttonColourId,
                              on ? juce::Colour(0xFF226622) : getLookAndFeel().findColour(juce::TextButton::buttonColourId));
    };

    punchButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.recordState.punchEnabled = !app.recordState.punchEnabled;
      const bool on = app.recordState.punchEnabled;
      if (on) {
        const int curRow = static_cast<int>(app.transport.currentRow());
        const int rows = static_cast<int>(app.module.currentEditor().rows());
        if (app.recordState.punchIn == 0 && app.recordState.punchOut == 0) {
          app.recordState.punchIn = curRow;
          app.recordState.punchOut = std::min(curRow + 15, rows - 1);
        }
        punchButton.setButtonText("Punch " + juce::String(app.recordState.punchIn) +
                                  "-" + juce::String(app.recordState.punchOut));
      } else {
        punchButton.setButtonText("Punch: Off");
      }
      punchButton.setColour(juce::TextButton::buttonColourId,
                            on ? juce::Colour(0xFF883300) : getLookAndFeel().findColour(juce::TextButton::buttonColourId));
    };

    recordStartRowLabel.setText("Row:", juce::dontSendNotification);
    recordStartRowLabel.setJustificationType(juce::Justification::centredRight);
    recordStartRowSlider.setSliderStyle(juce::Slider::IncDecButtons);
    recordStartRowSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, true, 36, 20);
    recordStartRowSlider.setWantsKeyboardFocus(false);
    recordStartRowSlider.setRange(0.0, 63.0, 1.0);
    recordStartRowSlider.setValue(0.0, juce::dontSendNotification);
    recordStartRowSlider.onValueChange = [this]() {
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.recordState.cursorRow = static_cast<int>(recordStartRowSlider.getValue());
      }
      patternGrid.grabKeyboardFocus();
    };

    recordStepLabel.setText("Step:", juce::dontSendNotification);
    recordStepLabel.setJustificationType(juce::Justification::centredRight);
    recordStepSlider.setSliderStyle(juce::Slider::IncDecButtons);
    recordStepSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, true, 28, 20);
    recordStepSlider.setWantsKeyboardFocus(false);
    recordStepSlider.setRange(1.0, 16.0, 1.0);
    recordStepSlider.setValue(1.0, juce::dontSendNotification);
    recordStepSlider.onValueChange = [this]() {
      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.recordState.insertJump = static_cast<int>(recordStepSlider.getValue());
      }
      patternGrid.grabKeyboardFocus();
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
      // Anchor the pattern-advance baseline at the current position so we
      // don't immediately fire a spurious advance if rowAdvanceCount is large
      // from prior pattern-mode playback.
      app.songModePatternAdvanceBaseline = app.transport.rowAdvanceCount();
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
      selectSongOrderEntry(songOrderEntrySelector.getSelectedItemIndex());
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

    songArrangerBarsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    songArrangerBarsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
    songArrangerBarsSlider.setRange(1.0, 16.0, 1.0);
    songArrangerBarsSlider.setNumDecimalPlacesToDisplay(0);
    songArrangerBarsSlider.setValue(4.0, juce::dontSendNotification);
    songArrangerBarsSlider.setTextValueSuffix(" bars");

    songArrangerDuplicateButton.onClick = [this]() { applySongArrangerDuplicateSection(); };
    songArrangerInsertBarsButton.onClick = [this]() { applySongArrangerInsertBars(); };
    songArrangerRippleLeftButton.onClick = [this]() { applySongArrangerRippleMove(-1); };
    songArrangerRippleRightButton.onClick = [this]() { applySongArrangerRippleMove(1); };

    auto applyModuleMessage = [this]() {
      const juce::String text = moduleMessageEditor.getText();
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.module.setMessage(text.toStdString());
      moduleMessageSavedSnapshot = text;
      updateModuleMessageStateIndicator();
    };
    moduleMessageApplyButton.onClick = applyModuleMessage;
    moduleMessageEditor.onFocusLost = applyModuleMessage;
    moduleMessageEditor.onTextChange = [this]() {
      updateModuleMessageCounter();
      updateModuleMessageStateIndicator();
    };

    helpButton.onClick = [this]() {
      juce::String msg =
        "=== TRANSPORT ===\n"
        "Play / Stop  - toolbar buttons or Space (grid focused)\n"
        "Loop         - toggle pattern looping\n"
        "Tempo slider - BPM (40-240)\n"
        "Swing slider - per-pattern groove (50-75%)\n"
        "Ticks/Beat   - rows per beat (resolution)\n"
        "Ticks/Row    - transport tick length per row\n"
        "Effect 0Fxx  - set tempo to xx BPM at any row\n"
        "\n"
        "=== PATTERN GRID - NAVIGATION ===\n"
        "Click         - select cell\n"
        "Alt+Drag      - audition scrub rows without writing notes\n"
        "Arrow keys    - move selection\n"
        "Tab / Shift+Tab - next / previous channel\n"
        "Return        - advance by step\n"
        "Home / End    - first / last row\n"
        "Page Up/Down  - jump 16 rows\n"
        "\n"
        "=== NOTE ENTRY ===\n"
        "1-9     high semitones (oct+1, semitones 0-8)\n"
        "Q-P     high whole tones (oct+1, white-key offsets)\n"
        "A-L     low semitones (oct, semitones 0-8)\n"
        "Z-.     low whole tones (oct, white-key offsets)\n"
        "+/-     octave up / down\n"
        "[ / ]   step size down / up\n"
        "Shift+O set note-off fadeout (uses current Gate ticks)\n"
        "Del/Bsp clear selected step\n"
        "Right-click  - clear step\n"
        "\n"
        "=== FX / VOL / SMP DIRECT ENTRY ===\n"
        "F2      cycle modes: Normal -> FX -> Volume -> Sample -> Normal\n"
        "`/|     toggle FX mode directly (header shows [FX])\n"
        "'       toggle Volume mode directly (header shows [VOL])\n"
        ";       toggle Sample mode directly (header shows [SMP])\n"
        "F3/F4   direct Volume / Sample mode (same as ' and ;)\n"
        "\n"
        "=== FX DIRECT ENTRY ===\n"
        "        Type 3 hex chars: 1 command digit + 2 value digits\n"
        "        e.g. F80 -> cmd=0F, val=80 (set tempo 128 BPM)\n"
        "        Row auto-advances after 3rd digit.\n"
        "Enter   commit partial buffer (zero-pads remaining digits)\n"
        "Bsp     remove last typed digit (or clear effect if empty)\n"
        "Esc     exit FX mode\n"
        "\n"
        "=== VOLUME DIRECT ENTRY ===\n"
        "        Type 2 hex chars: velocity byte in volume column\n"
        "        e.g. 40 = 64, 7F = 127 (clamped to 1-127)\n"
        "        On note rows: sets note velocity; on empty rows: writes Cxx volume FX\n"
        "        Auto-advances by step\n"
        "Enter   commit partial buffer (zero-pads remaining digit)\n"
        "Bsp     remove last typed digit\n"
        "Esc     exit volume mode\n"
        "\n"
        "=== SAMPLE DIRECT ENTRY ===\n"
        ";/F4    toggle sample mode (header shows [SMP])\n"
        "        Type 3 hex chars: sample slot 000-0FF\n"
        "        e.g. 00A = sample slot 10\n"
        "        Auto-advances by step\n"
        "Enter   commit partial buffer (zero-pads remaining digits)\n"
        "Bsp     remove last typed digit (or clear sample if empty)\n"
        "Esc     exit sample mode\n"
        "\n"
        "=== BLOCK EDITING ===\n"
        "Shift+Arrows / Click+Drag  - mark block\n"
        "Ctrl+C / X / V             - copy / cut / paste block\n"
        "Ctrl+Up / Down             - transpose +/-1 semitone\n"
        "Ctrl+Shift+Up / Down       - transpose +/-1 octave\n"
        "Alt+Up / Down              - selected step velocity +/-1\n"
        "Alt+Left / Right           - selected step gate +/-1\n"
        "Alt+Shift+Arrows           - larger velocity/gate nudges\n"
        "Panel: Copy / Cut / Paste / Transpose+/- buttons\n"
        "Panel: Apply FX to Block   - write current FX cmd/val to entire selection\n"
        "Pattern Macros - Fill Hats, Accent 4th, Invert Velocities\n"
        "Undo History - visual timeline with click-to-restore snapshots\n"
        "Song Arranger - duplicate section, insert bars, ripple move in song order\n"
        "\n"
        "=== STEP EDITOR (right panel) ===\n"
        "Velocity    - note velocity (1-127)\n"
        "Gate        - note length in ticks (0 = sustain)\n"
        "FX Cmd      - effect command (slider or hex box)\n"
        "FX Val      - effect value   (slider or hex box)\n"
        "Pattern Search - find next/prev Note, Instrument, or FX Value (Ctrl/Cmd+F4 / +Shift)\n"
        "A/B Compare - capture A and toggle between A snapshot and latest edits\n"
        "MIDI Learn  - map external CC to Velocity/Gate/FX Cmd/FX Val\n"
        "Ctrl+M      - focus Song/Module Message editor\n"
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
        "Per-channel instrument selector - fallback target when no sample slot is armed\n"
        "Sample bank selector - arms the selected sample slot for new notes\n"
        "Waveform editor - drag green handles for trim start/end selection\n"
        "Apply Trim crops to selection (non-destructive source retained in memory)\n"
        "Normalize / Fade In / Fade Out process the current selection\n"
        "Reload Source restores original loaded sample from memory\n"
        "Crop shortcut - Ctrl/Cmd+K while waveform has focus\n"
        "Mute toggle - mute channel\n"
        "Slot Editor - load WAV, adjust gain / attack / release\n"
        "Assign Plugin - load CLAP/LV2/internal plugin to slot\n"
        "\n"
        "=== STARTUP TEMPLATE ===\n"
        "Toolbar selector chooses startup pattern template (Blank/House/Electro)\n"
        "Preview applies template to current pattern without saving default\n"
        "Set Default saves selected template for next launch\n";

      juce::AlertWindow::showMessageBoxAsync(
          juce::MessageBoxIconType::InfoIcon,
          "exTracker - Usage Reference",
          msg + "\n\nTip: Press F1 anytime to save this help text to a file.",
          "OK",
          this);
    };

    darkModeButton.onClick = [this]() {
      app.darkMode = !app.darkMode;
      darkModeButton.setButtonText(app.darkMode ? "Dark: On" : "Dark: Off");
      repaint();
      patternGrid.repaint();
      mixerStrip.setDarkMode(app.darkMode);
    };

    pianoToggleButton.onClick = [this]() {
      pianoVisible = !pianoVisible;
      pianoKeyboard.setVisible(pianoVisible);
      pianoToggleButton.setButtonText(pianoVisible ? "Keys: On" : "Keys: Off");
      resized();
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

    ticksPerRowSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    ticksPerRowSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
    ticksPerRowSlider.setRange(1.0, 32.0, 1.0);
    ticksPerRowSlider.setValue(static_cast<double>(app.transport.ticksPerRow()), juce::dontSendNotification);
    ticksPerRowSlider.onValueChange = [this]() {
      app.transport.setTicksPerRow(static_cast<std::uint32_t>(std::lround(ticksPerRowSlider.getValue())));
      updateStatusLabels();
    };

    swingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 22);
    swingSlider.setRange(50.0, 75.0, 1.0);
    swingSlider.setNumDecimalPlacesToDisplay(0);
    swingSlider.setTextValueSuffix("%");
    {
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (lock.owns_lock()) {
        swingSlider.setValue(static_cast<double>(app.module.currentPatternSwing()), juce::dontSendNotification);
      } else {
        swingSlider.setValue(50.0, juce::dontSendNotification);
      }
    }
    swingSlider.onValueChange = [this]() {
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        return;
      }
      const auto swing = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(swingSlider.getValue())), 50, 75));
      app.module.setCurrentPatternSwing(swing);
      app.transport.setSwingPercent(swing);
      updateStatusLabels();
    };

    volumeLabel.setText("Vol", juce::dontSendNotification);
    volumeLabel.setJustificationType(juce::Justification::centredLeft);
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 22);
    volumeSlider.setRange(0.0, 200.0, 1.0);
    volumeSlider.setValue(static_cast<double>(std::lround(app.audio.getGlobalVolume() * 100.0f)),
                          juce::dontSendNotification);
    volumeSlider.setTextValueSuffix("%");
    volumeSlider.onValueChange = [this]() {
      app.audio.setGlobalVolume(static_cast<float>(volumeSlider.getValue()) / 100.0f);
    };

    ticksPerBeatLabel.setText("TPB", juce::dontSendNotification);
    ticksPerBeatLabel.setJustificationType(juce::Justification::centredLeft);
    ticksPerRowLabel.setText("TPR", juce::dontSendNotification);
    ticksPerRowLabel.setJustificationType(juce::Justification::centredLeft);

    expandPatternButton.onClick = [this]() { expandPattern(); patternGrid.grabKeyboardFocus(); };
    shrinkPatternButton.onClick = [this]() { shrinkPattern(); patternGrid.grabKeyboardFocus(); };
    expandChannelButton.onClick = [this]() { expandChannel(); patternGrid.grabKeyboardFocus(); };
    shrinkChannelButton.onClick = [this]() { shrinkChannel(); patternGrid.grabKeyboardFocus(); };
    newModuleButton.onClick = [this]() { newModule(); };
    newModuleButton.setTooltip("Create a blank new song (resets module, clears samples and instruments)");
    savePatternButton.onClick = [this]() { savePattern(); };
    loadPatternButton.onClick = [this]() { loadPattern(); };
    insertRowButton.onClick = [this]() { insertRowAtSelection(); patternGrid.grabKeyboardFocus(); };
    removeRowButton.onClick = [this]() { removeRowAtSelection(); patternGrid.grabKeyboardFocus(); };
    rowEditScopeButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      app.module.setRowEditAllChannels(!app.module.rowEditAllChannels());
      updateRowEditScopeButtonText();
      pluginStatusLabel.setText(
          app.module.rowEditAllChannels() ? "Row edit mode: all channels" : "Row edit mode: current channel",
          juce::dontSendNotification);
      updateStatusLabels();
    };
    insertSwingModeButton.onClick = [this]() {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      const bool next = !app.module.inheritSwingOnInsert();
      app.module.setInheritSwingOnInsert(next);
      updateInsertSwingModeButtonText();
      pluginStatusLabel.setText(
          next ? "Pattern insert swing inheritance: on" : "Pattern insert swing inheritance: off",
          juce::dontSendNotification);
      updateStatusLabels();
    };
    copyBlockButton.onClick = [this]() { patternGrid.copySelection(); patternGrid.grabKeyboardFocus(); };
    cutBlockButton.onClick = [this]() { patternGrid.cutSelection(); patternGrid.grabKeyboardFocus(); };
    pasteBlockButton.onClick = [this]() { patternGrid.pasteSelection(); patternGrid.grabKeyboardFocus(); };
    transposeDownButton.onClick = [this]() { patternGrid.transposeSelectionDown(false); patternGrid.grabKeyboardFocus(); };
    transposeUpButton.onClick = [this]() { patternGrid.transposeSelectionUp(false); patternGrid.grabKeyboardFocus(); };
    applyFxToBlockButton.onClick = [this]() {
      auto cmd = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(stepEffectCommandSlider.getValue())), 0, 255));
      auto val = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(stepEffectValueSlider.getValue())), 0, 255));
      patternGrid.applyEffectToSelection(cmd, val);
      patternGrid.grabKeyboardFocus();
    };
    patternMacroFillHatsButton.onClick = [this]() { applyPatternMacroFillHats(); patternGrid.grabKeyboardFocus(); };
    patternMacroAccent4Button.onClick = [this]() { applyPatternMacroAccentEveryFourth(); patternGrid.grabKeyboardFocus(); };
    patternMacroInvertVelocityButton.onClick = [this]() { applyPatternMacroInvertVelocities(); patternGrid.grabKeyboardFocus(); };
    undoHistorySelector.onChange = [this]() {
      if (!suppressUndoHistorySelectionCallback) {
        restoreUndoHistorySelection();
      }
    };

    tempoLabel.setText("Tempo", juce::dontSendNotification);
    tempoLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setJustificationType(juce::Justification::centredRight);
    updateGridDensityButtonText();
    updateRowEditScopeButtonText();
    updateInsertSwingModeButtonText();

    slotSelector.onChange = [this]() {
      const int slot = getSelectedSlot();
      if (slot >= 0) app.midiInstrument = slot;
      refreshParameterSlidersFromSlot();
      updateStatusLabels();
    };

    // Re-scan is safe only before audio starts.  Plugins are auto-scanned at
    // startup in ExTrackerApp::initialise() before PipeWire launches; this
    // button re-runs the LV2 TTL discovery (no fork/dlopen) to pick up
    // newly installed plugins without restarting.
    scanPluginsButton.onClick = [this]() {
      scanPluginsButton.setEnabled(false);
      scanPluginsButton.setButtonText("Scanning...");
      const std::size_t found = app.plugins.rescanLv2Only();
      refreshPluginChoices();
      pluginStatusLabel.setText(
          juce::String(static_cast<int>(found)) + " new plugin(s) found, " +
          juce::String(pluginSelector.getNumItems()) + " total available",
          juce::dontSendNotification);
      scanPluginsButton.setButtonText("Scan LV2/VST3");
      scanPluginsButton.setEnabled(true);
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
      if (loaded && pluginId == "builtin.sample") {
        const int selectedSampleSlot = getSelectedSampleSlot();
        if (selectedSampleSlot >= 0 &&
            !app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot)).empty()) {
          assigned = app.plugins.assignSampleSlotToInstrument(
              static_cast<std::uint16_t>(selectedSampleSlot),
              static_cast<std::uint8_t>(slot));
        } else {
          assigned = app.plugins.assignInstrument(static_cast<std::uint8_t>(slot), pluginId);
        }
      } else {
        assigned = loaded && app.plugins.assignInstrument(static_cast<std::uint8_t>(slot), pluginId);
      }

      pluginStatusLabel.setText(
          assigned ? "Assigned: " + juce::String(pluginId)
                   : "Assign failed for: " + juce::String(pluginId),
          juce::dontSendNotification);
      if (assigned) {
        app.midiInstrument = slot;  // route MIDI to the just-assigned slot
      }
      refreshSlotSelector();
        refreshChannelPluginLabels();
      refreshParameterSlidersFromSlot();
      assignPluginButton.setButtonText("Assign Plugin To Slot");
      assignPluginButton.setEnabled(true);
      slotSelector.setEnabled(true);
      pluginSelector.setEnabled(true);
    };

    loadInstrumentFileButton.onClick = [this]() {
      const int slot = getSelectedSlot();
      if (slot < 0) {
        pluginStatusLabel.setText("Select an instrument slot first", juce::dontSendNotification);
        return;
      }
      loadInstrumentFileChooser = std::make_unique<juce::FileChooser>(
          "Load Instrument File",
          juce::File::getSpecialLocation(juce::File::userHomeDirectory),
          "*.xpm;*.XPM;*.s3i;*.S3I;*.xi;*.XI;*.iff;*.IFF;*.8svx;*.8SVX;*.sf2;*.SF2");
      loadInstrumentFileChooser->launchAsync(
          juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
          [this, slot](const juce::FileChooser& chooser) {
            const juce::File file = chooser.getResult();
            loadInstrumentFileChooser.reset();
            if (!file.existsAsFile()) {
              return;
            }
            const std::string path = file.getFullPathName().toStdString();
            const std::string ext = file.getFileExtension().toLowerCase().toStdString();
            const auto instr = static_cast<std::uint8_t>(slot);
            bool loaded = false;
            juce::String loadError;
            if (ext == ".xpm") {
              loaded = app.plugins.loadXpmInstrument(path, instr);
            } else if (ext == ".s3i") {
              loaded = app.plugins.loadS3iInstrument(path, instr);
              if (!loaded) {
                std::uint8_t s3iType = 0;
                if (std::ifstream f(path, std::ios::binary); f)
                  f.read(reinterpret_cast<char*>(&s3iType), 1);
                if (s3iType >= 3 && s3iType <= 7)
                  loadError = "Adlib rhythm instrument (not supported)";
              }
            } else if (ext == ".xi") {
              loaded = app.plugins.loadXiInstrument(path, instr);
            } else if (ext == ".iff" || ext == ".8svx") {
              loaded = app.plugins.loadIffSvxInstrument(path, instr);
            } else if (ext == ".sf2") {
              showSF2KeyPickerDialog(file, slot);
              return;  // dialog handles the rest asynchronously
            }
            const juce::String name = file.getFileNameWithoutExtension();
            pluginStatusLabel.setText(
                loaded ? "Loaded: " + name + " -> I" + juce::String(slot)
                       : "Failed to load " + name + (loadError.isEmpty() ? "" : ": " + loadError),
                juce::dontSendNotification);
            if (loaded) {
              app.midiInstrument = slot;
            }
            refreshSlotSelector();
            refreshChannelPluginLabels();
            refreshParameterSlidersFromSlot();
          });
    };

    openPluginEditorButton.onClick = [this]() {
      const int slot = getSelectedSlot();
      if (slot < 0) {
        pluginStatusLabel.setText("Select an instrument slot first", juce::dontSendNotification);
        return;
      }
      const auto instrument = static_cast<std::uint8_t>(slot);
      if (!app.plugins.hasInstrumentAssignment(instrument)) {
        pluginStatusLabel.setText("No plugin assigned to slot " + juce::String(slot), juce::dontSendNotification);
        return;
      }
      if (!app.plugins.openPluginEditor(instrument)) {
        extracker::PluginPortInfo portInfo;
        const std::string pid = app.plugins.pluginForInstrument(instrument);
        const bool hasParams = app.plugins.getPluginPortInfo(pid, portInfo) && !portInfo.controlInMeta.empty();
        pluginStatusLabel.setText(
            hasParams ? "No native editor — scroll down in this panel to find the Parameters sliders"
                      : "Plugin editor not supported (VST3 only, or plugin has no GUI)",
            juce::dontSendNotification);
        return;
      }
      // Create a native floating window and attach the VST3 IPlugView to it.
      auto* editorWindow = new juce::DocumentWindow(
          "Plugin Editor — " + juce::String(app.plugins.pluginForInstrument(instrument)),
          juce::Desktop::getInstance().getDefaultLookAndFeel()
              .findColour(juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons);
      editorWindow->setUsingNativeTitleBar(true);
      editorWindow->setResizable(true, false);
      editorWindow->centreWithSize(640, 480);
      editorWindow->addToDesktop();
      editorWindow->setVisible(true);

      if (auto* peer = editorWindow->getPeer()) {
        void* nativeHandle = peer->getNativeHandle();
#ifdef _WIN32
        const char* platformType = "HWND";
#elif defined(__APPLE__)
        const char* platformType = "NSView";
#else
        const char* platformType = "X11EmbedWindowID";
#endif
        if (app.plugins.attachPluginEditorToWindow(instrument, nativeHandle, platformType)) {
          int w = 640, h = 480;
          app.plugins.getPluginEditorPreferredSize(instrument, w, h);
          editorWindow->setSize(w, h);
          pluginStatusLabel.setText("Plugin editor opened for slot " + juce::String(slot), juce::dontSendNotification);
        } else {
          app.plugins.closePluginEditor(instrument);
          delete editorWindow;
          pluginStatusLabel.setText("Failed to attach plugin editor window", juce::dontSendNotification);
        }
      } else {
        app.plugins.closePluginEditor(instrument);
        delete editorWindow;
        pluginStatusLabel.setText("Failed to create native editor window", juce::dontSendNotification);
      }
    };

    sampleSlotSelector.onChange = [this]() {
      // Do NOT auto-arm here — browsing slots shouldn't silently corrupt
      // notes typed while the user is on a different panel.
      // activeSampleSlot is updated only after an explicit load or clear.
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

    configureParameterSlider(sampleTrimStartSlider, 0.0, 99.0, 1.0);
    configureParameterSlider(sampleTrimEndSlider, 1.0, 100.0, 1.0);
    sampleTrimStartSlider.setNumDecimalPlacesToDisplay(0);
    sampleTrimEndSlider.setNumDecimalPlacesToDisplay(0);
    sampleTrimStartSlider.setTextValueSuffix("%");
    sampleTrimEndSlider.setTextValueSuffix("%");
    sampleTrimStartSlider.setValue(0.0, juce::dontSendNotification);
    sampleTrimEndSlider.setValue(100.0, juce::dontSendNotification);
    sampleWaveformView.setWantsKeyboardFocus(true);
    sampleWaveformView.setSelectionChangedCallback([this](double startNorm, double endNorm) {
      sampleTrimStartSlider.setValue(std::clamp(startNorm * 100.0, 0.0, 99.0), juce::dontSendNotification);
      sampleTrimEndSlider.setValue(std::clamp(endNorm * 100.0, 1.0, 100.0), juce::dontSendNotification);
    });
    sampleWaveformView.setSelectionDragStateCallback([this](bool active) {
      isEditingSampleTrimSelection = active;
    });
    sampleWaveformView.setCropShortcutCallback([this]() {
      applySampleTrimSelection();
    });
    sampleTrimStartSlider.onDragStart = [this]() {
      isEditingSampleTrimSelection = true;
    };
    sampleTrimStartSlider.onDragEnd = [this]() {
      isEditingSampleTrimSelection = false;
    };
    sampleTrimEndSlider.onDragStart = [this]() {
      isEditingSampleTrimSelection = true;
    };
    sampleTrimEndSlider.onDragEnd = [this]() {
      isEditingSampleTrimSelection = false;
    };
    sampleTrimStartSlider.onValueChange = [this]() {
      if (sampleTrimStartSlider.getValue() >= sampleTrimEndSlider.getValue()) {
        sampleTrimEndSlider.setValue(std::min(100.0, sampleTrimStartSlider.getValue() + 1.0), juce::dontSendNotification);
      }
      sampleWaveformView.setSelection(sampleTrimStartSlider.getValue() / 100.0, sampleTrimEndSlider.getValue() / 100.0);
    };
    sampleTrimEndSlider.onValueChange = [this]() {
      if (sampleTrimEndSlider.getValue() <= sampleTrimStartSlider.getValue()) {
        sampleTrimStartSlider.setValue(std::max(0.0, sampleTrimEndSlider.getValue() - 1.0), juce::dontSendNotification);
      }
      sampleWaveformView.setSelection(sampleTrimStartSlider.getValue() / 100.0, sampleTrimEndSlider.getValue() / 100.0);
    };

    sampleTrimApplyButton.onClick = [this]() {
      applySampleTrimSelection();
    };

    sampleTrimReloadButton.onClick = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0) {
        pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
        return;
      }

      const std::string path = app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
      if (path.empty()) {
        pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
        return;
      }

      const bool reloaded = app.plugins.restoreSampleSlotSource(static_cast<std::uint16_t>(selectedSampleSlot));
      pluginStatusLabel.setText(
          reloaded ? "Reloaded source sample for " + formatSampleSlotHex(selectedSampleSlot)
                   : "Failed to reload source sample for " + formatSampleSlotHex(selectedSampleSlot),
          juce::dontSendNotification);
      refreshSampleSlotDetails();
      patternGrid.repaint();
    };

    sampleNormalizeButton.onClick = [this]() { applySampleRangeNormalize(); };
    sampleFadeInButton.onClick = [this]() { applySampleRangeFade(true); };
    sampleFadeOutButton.onClick = [this]() { applySampleRangeFade(false); };
    sampleReverseButton.onClick = [this]() { applySampleRangeReverse(); };

    // Rate / bit-depth pickers use the value itself as the item id.
    for (int hz : {8000, 11025, 22050, 44100, 48000}) {
      sampleRateSelector.addItem(juce::String(hz) + " Hz", hz);
    }
    sampleRateSelector.setSelectedId(44100, juce::dontSendNotification);
    sampleResampleButton.onClick = [this]() { applySampleResample(); };

    for (int bits : {16, 12, 8, 6, 4, 2}) {
      sampleBitDepthSelector.addItem(juce::String(bits) + "-bit", bits);
    }
    sampleBitDepthSelector.setSelectedId(8, juce::dontSendNotification);
    sampleBitDepthButton.onClick = [this]() { applySampleBitDepth(); };
    sampleLoopXfadeButton.onClick = [this]() { applySampleLoopCrossfade(); };

    configureParameterSlider(sampleVolumeSlider, 0.0, 2.0, 0.01);
    sampleVolumeSlider.setNumDecimalPlacesToDisplay(2);
    sampleVolumeSlider.setValue(1.0, juce::dontSendNotification);
    sampleVolumeSlider.onValueChange = [this]() {
      const int slot = getSelectedSampleSlot();
      if (slot >= 0) {
        app.plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "gain", sampleVolumeSlider.getValue());
      }
    };

    configureParameterSlider(samplePanSlider, 0.0, 255.0, 1.0);
    samplePanSlider.setNumDecimalPlacesToDisplay(0);
    samplePanSlider.setValue(128.0, juce::dontSendNotification);
    samplePanSlider.onValueChange = [this]() {
      const int slot = getSelectedSampleSlot();
      if (slot >= 0) {
        app.plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "pan",
            samplePanSlider.getValue() / 255.0);
      }
    };

    configureParameterSlider(sampleTransposeSlider, -60.0, 60.0, 1.0);
    sampleTransposeSlider.setNumDecimalPlacesToDisplay(0);
    sampleTransposeSlider.setTextValueSuffix(" st");
    sampleTransposeSlider.setValue(0.0, juce::dontSendNotification);
    sampleTransposeSlider.onValueChange = [this]() {
      const int slot = getSelectedSampleSlot();
      if (slot >= 0) {
        const int newRoot = std::clamp(60 + static_cast<int>(sampleTransposeSlider.getValue()), 0, 127);
        app.plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "sample_root",
            static_cast<double>(newRoot));
      }
    };

    sampleLoopModeBox.addItem("Off",     1);
    sampleLoopModeBox.addItem("Forward", 2);
    sampleLoopModeBox.addItem("Bidi",    3);
    sampleLoopModeBox.addItem("Sustain", 4);
    sampleLoopModeBox.setSelectedId(1, juce::dontSendNotification);
    sampleLoopModeBox.onChange = [this]() {
      const int slot = getSelectedSampleSlot();
      if (slot >= 0) {
        app.plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_mode",
            static_cast<double>(sampleLoopModeBox.getSelectedId() - 1));
      }
    };

    sampleLoadButton.onClick = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0) {
        return;
      }

      juce::File initialFolder = lastSampleLoadFolder;
      if (!initialFolder.isDirectory()) {
        for (const auto& folder : recentSampleLoadFolders) {
          if (folder.isDirectory()) {
            initialFolder = folder;
            break;
          }
        }
      }
      if (!initialFolder.isDirectory()) {
        initialFolder = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
      }
      sampleFileChooser = std::make_unique<juce::FileChooser>("Load WAV Sample", initialFolder, "*.wav;*.wave");
      constexpr int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
      sampleFileChooser->launchAsync(flags, [this, selectedSampleSlot](const juce::FileChooser& chooser) {
        const juce::File file = chooser.getResult();
        sampleFileChooser.reset();

        if (!file.existsAsFile()) {
          return;
        }

        lastSampleLoadFolder = file.getParentDirectory();
    rememberSampleFolderPreference(recentSampleLoadFolders, lastSampleLoadFolder);

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

        // Don't auto-arm on load: arming must be explicit (Arm for Notes button).
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

    sampleRouteKeystationButton.onClick = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0) {
        pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
        return;
      }
      if (selectedSampleSlot > 255) {
        pluginStatusLabel.setText("Keystation routing supports sample slots 0-255", juce::dontSendNotification);
        return;
      }
      const auto instrSlot = static_cast<std::uint8_t>(selectedSampleSlot);
      const bool ok = app.plugins.assignSampleSlotToInstrument(
          static_cast<std::uint16_t>(selectedSampleSlot), instrSlot);
      if (ok) {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.midiInstrument = instrSlot;
        app.midiThruEnabled = true;
        pluginStatusLabel.setText(
            "Keystation -> sample slot " + formatSampleSlotHex(selectedSampleSlot),
            juce::dontSendNotification);
      } else {
        pluginStatusLabel.setText("Route failed: slot empty?", juce::dontSendNotification);
      }
    };

    sampleArmButton.onClick = [this]() {
      const int selectedSampleSlot = getSelectedSampleSlot();
      if (selectedSampleSlot < 0 || selectedSampleSlot > 255) {
        return;
      }
      if (app.activeSampleSlot == selectedSampleSlot) {
        app.activeSampleSlot = -1;
      } else if (!app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot)).empty()) {
        app.activeSampleSlot = selectedSampleSlot;
      }
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

    patternCompareCaptureButton.onClick = [this]() { capturePatternCompareA(); };
    patternCompareToggleButton.onClick = [this]() { togglePatternCompareAB(); };

    patternSearchMode.clear(juce::dontSendNotification);
    patternSearchMode.addItem("Note", 1);
    patternSearchMode.addItem("Instrument", 2);
    patternSearchMode.addItem("FX Value", 3);
    patternSearchMode.setSelectedId(1, juce::dontSendNotification);
    patternSearchMode.onChange = [this]() {
      if (patternSearchMode.getSelectedId() == 1) {
        patternSearchValue.setTextToShowWhenEmpty("C#4 or 61", juce::Colour(0xFF6A737D));
      } else if (patternSearchMode.getSelectedId() == 2) {
        patternSearchValue.setTextToShowWhenEmpty("hex/dec (0A or 10)", juce::Colour(0xFF6A737D));
      } else {
        patternSearchValue.setTextToShowWhenEmpty("hex/dec byte (7F)", juce::Colour(0xFF6A737D));
      }
    };
    patternSearchMode.onChange();
    patternSearchPrevButton.onClick = [this]() { findPatternMatch(false); };
    patternSearchNextButton.onClick = [this]() { findPatternMatch(true); };
    patternSearchValue.onReturnKey = [this]() { findPatternMatch(true); };

    gainSlider.onValueChange = [this]() { setSelectedSlotParameter("gain", gainSlider.getValue()); };
    attackSlider.onValueChange = [this]() { setSelectedSlotParameter("attack_ms", attackSlider.getValue()); };
    releaseSlider.onValueChange = [this]() { setSelectedSlotParameter("release_ms", releaseSlider.getValue()); };
    instrumentRootSlider.onValueChange = [this]() {
      if (suppressInstrumentSampleEditorCallbacks) {
        return;
      }
      const int root = std::clamp(60 + static_cast<int>(std::lround(instrumentRootSlider.getValue())), 0, 127);
      setSelectedSlotParameter("sample_root", static_cast<double>(root));
    };
    instrumentPanSlider.onValueChange = [this]() {
      if (suppressInstrumentSampleEditorCallbacks) {
        return;
      }
      setSelectedSlotParameter("pan", instrumentPanSlider.getValue() / 255.0);
    };
    instrumentLoopModeBox.onChange = [this]() {
      if (suppressInstrumentSampleEditorCallbacks) {
        return;
      }
      const int selectedId = instrumentLoopModeBox.getSelectedId();
      if (selectedId <= 0) {
        return;
      }
      setSelectedSlotParameter("loop_mode", static_cast<double>(selectedId - 1));
    };
    instrumentLoopStartSlider.onValueChange = [this]() {
      if (suppressInstrumentSampleEditorCallbacks) {
        return;
      }
      setSelectedSlotParameter("loop_start", std::max(0.0, instrumentLoopStartSlider.getValue()));
    };
    instrumentLoopEndSlider.onValueChange = [this]() {
      if (suppressInstrumentSampleEditorCallbacks) {
        return;
      }
      setSelectedSlotParameter("loop_end", std::max(0.0, instrumentLoopEndSlider.getValue()));
    };
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
      // Flush immediately so the edit is written to the current cell before any
      // selection change moves selectedStepRow/Channel to a different cell.
      flushPendingStepEdit();
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
      // Flush immediately so the edit is written to the current cell before any
      // selection change moves selectedStepRow/Channel to a different cell.
      flushPendingStepEdit();
    };
    stepEffectCommandHexEditor.onReturnKey = applyEffectCommandHexText;
    stepEffectCommandHexEditor.onFocusLost = applyEffectCommandHexText;
    stepEffectCommandHexEditor.onTextChange = [this, applyEffectCommandHexText]() {
      if (!suppressStepEffectTextCallbacks && stepEffectCommandHexEditor.getText().length() == 2) {
        applyEffectCommandHexText();
      }
    };
    stepEffectValueHexEditor.onReturnKey = applyEffectValueHexText;
    stepEffectValueHexEditor.onFocusLost = applyEffectValueHexText;
    stepEffectValueHexEditor.onTextChange = [this, applyEffectValueHexText]() {
      if (!suppressStepEffectTextCallbacks && stepEffectValueHexEditor.getText().length() == 2) {
        applyEffectValueHexText();
      }
    };
    keyboardOctaveSlider.onValueChange = [this]() {
      pianoKeyboard.setLowestOctave(static_cast<int>(std::lround(keyboardOctaveSlider.getValue())));
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
    notePreviewMsSlider.onValueChange = [this]() {
      patternGrid.setPreviewDurationMs(static_cast<std::uint32_t>(std::lround(notePreviewMsSlider.getValue())));
    };
    followPreviewToggle.onClick = [this]() {
      patternGrid.setFollowPreviewOnSelect(followPreviewToggle.getToggleState());
    };

    midiLearnVelocityButton.onClick = [this]() {
      app.armMidiEditorLearn(ExTrackerApp::MidiEditorAction::Velocity);
      midiLearnStatusLabel.setText("Learning Velocity: move a MIDI CC control", juce::dontSendNotification);
      refreshMidiLearnStatus();
    };
    midiLearnGateButton.onClick = [this]() {
      app.armMidiEditorLearn(ExTrackerApp::MidiEditorAction::Gate);
      midiLearnStatusLabel.setText("Learning Gate: move a MIDI CC control", juce::dontSendNotification);
      refreshMidiLearnStatus();
    };
    midiLearnEffectCommandButton.onClick = [this]() {
      app.armMidiEditorLearn(ExTrackerApp::MidiEditorAction::EffectCommand);
      midiLearnStatusLabel.setText("Learning FX Cmd: move a MIDI CC control", juce::dontSendNotification);
      refreshMidiLearnStatus();
    };
    midiLearnEffectValueButton.onClick = [this]() {
      app.armMidiEditorLearn(ExTrackerApp::MidiEditorAction::EffectValue);
      midiLearnStatusLabel.setText("Learning FX Val: move a MIDI CC control", juce::dontSendNotification);
      refreshMidiLearnStatus();
    };
    midiLearnClearButton.onClick = [this]() {
      app.clearMidiEditorCcMappings();
      refreshMidiLearnStatus();
      midiLearnStatusLabel.setText("Cleared editor CC mappings", juce::dontSendNotification);
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
    refreshMidiLearnStatus();
    refreshPatternView();
    startTimerHz(60);
  }

  void refreshPatternView() {
    updatePatternSelector();
    refreshSwingForCurrentPattern();
    refreshSongOrderEditor();
    refreshModuleMessageEditor();
    patternGrid.recalculateGridSize();
    patternGrid.refreshSnapshotForPatternChange();
    patternGrid.repaint();
    syncPatternRowSliderFromViewport();
    captureUndoHistoryIfPatternChanged();
    refreshUndoHistoryPanel();
  }

  void handlePatternChangedFromPlayback() {
    refreshPatternView();
  }

  void autoLoadLastSong() {
    if (lastSongFile == juce::File() || !lastSongFile.existsAsFile()) {
      return;
    }
    const std::string filePath = lastSongFile.getFullPathName().toStdString();
    if (app.loadPatternFromFile(filePath, /*blocking=*/true)) {
      tempoSlider.setValue(app.transport.tempoBpm(), juce::dontSendNotification);
      ticksPerBeatSlider.setValue(static_cast<double>(app.transport.ticksPerBeat()), juce::dontSendNotification);
      ticksPerRowSlider.setValue(static_cast<double>(app.transport.ticksPerRow()), juce::dontSendNotification);
      refreshPatternView();
      updateRowEditScopeButtonText();
      updateInsertSwingModeButtonText();
      patternGrid.clampSelectionToBounds();
      updateStepEditorFromSelection();
      refreshSlotSelector();
      refreshParameterSlidersFromSlot();
      refreshChannelRows();
      refreshSampleSlotSelector();
      refreshSampleSlotDetails();
      moduleMessageEditor.setText(juce::String(app.module.message()), juce::dontSendNotification);
      moduleMessageSavedSnapshot = juce::String(app.module.message());
      updateModuleMessageStateIndicator();
      isSongDirty = false;
      pluginStatusLabel.setText("Restored: " + lastSongFile.getFileName(), juce::dontSendNotification);
    }
  }

  void mouseDoubleClick(const juce::MouseEvent& event) override {
    auto* source = event.eventComponent;
    if (source != &songOrderListView &&
        (source == nullptr || !songOrderListView.isParentOf(source))) {
      return;
    }

    const auto relative = event.getEventRelativeTo(&songOrderListView);
    const float y = relative.position.y - 6.0f;
    const float lineHeight = std::max(1.0f, songOrderListView.getFont().getHeight());
    const int index = static_cast<int>(std::floor(y / lineHeight));
    if (index < 0) {
      return;
    }
    selectSongOrderEntry(index);
  }

  void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
    auto* source = event.eventComponent;
    if (source != &patternSelector &&
        (source == nullptr || !patternSelector.isParentOf(source))) {
      return;
    }

    const float primaryDelta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX)
        ? wheel.deltaY
        : wheel.deltaX;
    if (std::abs(primaryDelta) < 0.0001f) {
      return;
    }

    stepPatternSelection(primaryDelta > 0.0f ? -1 : 1);
  }

  void resized() override {
    auto area = getLocalBounds();
    auto toolbar1 = area.removeFromTop(28).reduced(8, 3);
    auto toolbar2 = area.removeFromTop(28).reduced(8, 3);
    auto toolbar3 = area.removeFromTop(28).reduced(8, 3);
    area.removeFromTop(4);

    // Toolbar 1: Transport + Pattern navigation (~975px)
    playButton.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(8);
    stopButton.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(8);
    recordButton.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(4);
    playFromCursorButton.setBounds(toolbar1.removeFromLeft(70));
    toolbar1.removeFromLeft(4);
    overdubButton.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(4);
    punchButton.setBounds(toolbar1.removeFromLeft(110));
    toolbar1.removeFromLeft(6);
    recordStartRowLabel.setBounds(toolbar1.removeFromLeft(32));
    recordStartRowSlider.setBounds(toolbar1.removeFromLeft(84));
    toolbar1.removeFromLeft(6);
    recordStepLabel.setBounds(toolbar1.removeFromLeft(36));
    recordStepSlider.setBounds(toolbar1.removeFromLeft(76));
    toolbar1.removeFromLeft(8);
    loopButton.setBounds(toolbar1.removeFromLeft(110));
    toolbar1.removeFromLeft(8);
    playModePatternButton.setBounds(toolbar1.removeFromLeft(75));
    toolbar1.removeFromLeft(4);
    playModeSongButton.setBounds(toolbar1.removeFromLeft(60));
    toolbar1.removeFromLeft(8);
    statusLabel.setBounds(toolbar1);

    // Toolbar 2: Timing parameters (~890px)
    toolbar2.removeFromLeft(12);
    tempoLabel.setBounds(toolbar2.removeFromLeft(55));
    tempoSlider.setBounds(toolbar2.removeFromLeft(200));
    toolbar2.removeFromLeft(10);
    swingLabel.setBounds(toolbar2.removeFromLeft(48));
    swingSlider.setBounds(toolbar2.removeFromLeft(120));
    toolbar2.removeFromLeft(10);
    ticksPerBeatLabel.setBounds(toolbar2.removeFromLeft(36));
    ticksPerBeatSlider.setBounds(toolbar2.removeFromLeft(110));
    toolbar2.removeFromLeft(8);
    ticksPerRowLabel.setBounds(toolbar2.removeFromLeft(36));
    ticksPerRowSlider.setBounds(toolbar2.removeFromLeft(110));
    toolbar2.removeFromLeft(12);
    patternLabel.setBounds(toolbar2.removeFromLeft(120));
    toolbar2.removeFromLeft(6);
    patternSelector.setBounds(toolbar2.removeFromLeft(90));
    toolbar2.removeFromLeft(8);
    insertPatternBeforeButton.setBounds(toolbar2.removeFromLeft(90));
    toolbar2.removeFromLeft(4);
    insertPatternAfterButton.setBounds(toolbar2.removeFromLeft(90));
    toolbar2.removeFromLeft(4);
    removePatternButton.setBounds(toolbar2.removeFromLeft(90));
    toolbar2.removeFromLeft(12);
    volumeLabel.setBounds(toolbar2.removeFromLeft(28));
    volumeSlider.setBounds(toolbar2.removeFromLeft(160));

    // Toolbar 3: File, size, row editing, UI toggles (~1230px)
    newModuleButton.setBounds(toolbar3.removeFromLeft(90));
    toolbar3.removeFromLeft(4);
    savePatternButton.setBounds(toolbar3.removeFromLeft(90));
    toolbar3.removeFromLeft(4);
    loadPatternButton.setBounds(toolbar3.removeFromLeft(90));
    toolbar3.removeFromLeft(10);
    expandPatternButton.setBounds(toolbar3.removeFromLeft(86));
    toolbar3.removeFromLeft(4);
    shrinkPatternButton.setBounds(toolbar3.removeFromLeft(86));
    toolbar3.removeFromLeft(4);
    expandChannelButton.setBounds(toolbar3.removeFromLeft(46));
    toolbar3.removeFromLeft(4);
    shrinkChannelButton.setBounds(toolbar3.removeFromLeft(46));
    toolbar3.removeFromLeft(10);
    insertRowButton.setBounds(toolbar3.removeFromLeft(86));
    toolbar3.removeFromLeft(4);
    removeRowButton.setBounds(toolbar3.removeFromLeft(90));
    toolbar3.removeFromLeft(4);
    rowEditScopeButton.setBounds(toolbar3.removeFromLeft(110));
    toolbar3.removeFromLeft(4);
    insertSwingModeButton.setBounds(toolbar3.removeFromLeft(120));
    toolbar3.removeFromLeft(10);
    helpButton.setBounds(toolbar3.removeFromLeft(60));
    toolbar3.removeFromLeft(4);
    gridDensityButton.setBounds(toolbar3.removeFromLeft(132));
    toolbar3.removeFromLeft(4);
    darkModeButton.setBounds(toolbar3.removeFromLeft(80));
    toolbar3.removeFromLeft(4);
    pianoToggleButton.setBounds(toolbar3.removeFromLeft(80));
    toolbar3.removeFromLeft(4);
    bounceWavButton.setBounds(toolbar3.removeFromLeft(100));

    auto contentArea = area.reduced(8, 8);
    if (pianoVisible) {
      pianoKeyboard.setBounds(contentArea.removeFromBottom(80));
      contentArea.removeFromBottom(4);
    }
    // Mixer strip — 60px at the bottom of the pattern area (above piano if visible)
    constexpr int kMixerHeight = 60;
    auto mixerAreaFull = contentArea.removeFromBottom(kMixerHeight);
    contentArea.removeFromBottom(2);  // gap between mixer and grid

    // Make panel flexible: min 280px, max 360px, or 25% of available space
    int panelWidth = std::clamp(contentArea.getWidth() / 4, 280, 360);
    auto panelViewportBounds = contentArea.removeFromRight(panelWidth);
    auto patternArea = contentArea;
    auto patternSliderArea = patternArea.removeFromRight(16);
    mixerAreaFull.removeFromRight(panelWidth + 16 + 8);  // align with pattern area (excl. panel + scrollbar + gap)
    mixerViewport.setBounds(mixerAreaFull);
    patternViewport.setBounds(patternArea);
    patternRowSlider.setBounds(patternSliderArea.reduced(2, 2));
    panelViewport.setBounds(panelViewportBounds);
    panelViewport.setViewPosition(0, panelViewport.getViewPositionY());

    // Lay out into a temporary tall area, then compute the exact content height needed.
    auto panelLayoutBounds = juce::Rectangle<int>(0, 0, panelViewportBounds.getWidth(), 5000);
    auto panelArea = panelLayoutBounds;

    const int gridColumns = static_cast<int>(app.module.currentEditor().channels());
    const int gridRows = static_cast<int>(app.module.currentEditor().rows());
    const int gridWidth = std::max(patternArea.getWidth(),
                     patternGrid.preferredLabelWidth() +
                     gridColumns * patternGrid.preferredCellWidth());
    const int gridHeight = std::max(patternArea.getHeight(),
                    patternGrid.preferredHeaderHeight() +
                    gridRows * (patternGrid.isCompactDensity() ? 16 : 20));
    patternGrid.setBounds(0, 0, gridWidth, gridHeight);
    syncPatternRowSliderFromViewport();
    syncMixerLayout();

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

    panelArea.removeFromTop(6);
    songArrangerTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(24);
      songArrangerBarsLabel.setBounds(row.removeFromLeft(56));
      songArrangerBarsSlider.setBounds(row);
    }
    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      songArrangerDuplicateButton.setBounds(row.removeFromLeft(152));
      row.removeFromLeft(6);
      songArrangerInsertBarsButton.setBounds(row.removeFromLeft(152));
    }
    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      songArrangerRippleLeftButton.setBounds(row.removeFromLeft(152));
      row.removeFromLeft(6);
      songArrangerRippleRightButton.setBounds(row.removeFromLeft(152));
    }

    panelArea.removeFromTop(10);
    startupTemplateLabel.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    startupTemplateSelector.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    {
      auto tmplRow = panelArea.removeFromTop(26);
      const int half = (tmplRow.getWidth() - 4) / 2;
      startupTemplatePreviewButton.setBounds(tmplRow.removeFromLeft(half));
      tmplRow.removeFromLeft(4);
      startupTemplateSetDefaultButton.setBounds(tmplRow);
    }
    panelArea.removeFromTop(10);
    moduleMessageTitle.setBounds(panelArea.removeFromTop(22));
    panelArea.removeFromTop(4);
    moduleMessageEditor.setBounds(panelArea.removeFromTop(86));
    panelArea.removeFromTop(4);
    moduleMessageCounterLabel.setBounds(panelArea.removeFromTop(18));
    panelArea.removeFromTop(2);
    moduleMessageStateLabel.setBounds(panelArea.removeFromTop(18));
    panelArea.removeFromTop(4);
    moduleMessageApplyButton.setBounds(panelArea.removeFromTop(26));

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

    scanPluginsButton.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    pluginSelector.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    assignPluginButton.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    loadInstrumentFileButton.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    openPluginEditorButton.setBounds(panelArea.removeFromTop(26));
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
    auto sampleKeystationRow = panelArea.removeFromTop(26);
    sampleRouteKeystationButton.setBounds(sampleKeystationRow.removeFromLeft(160));
    sampleKeystationRow.removeFromLeft(4);
    sampleArmButton.setBounds(sampleKeystationRow);

    panelArea.removeFromTop(4);
    auto sampleArmChannelRow = panelArea.removeFromTop(24);
    sampleArmChannelLabel.setBounds(sampleArmChannelRow.removeFromLeft(64));
    sampleArmChannelRow.removeFromLeft(4);
    sampleArmChannelSelector.setBounds(sampleArmChannelRow);

    panelArea.removeFromTop(4);
    auto sampleRenameRow = panelArea.removeFromTop(26);
    sampleRenameEditor.setBounds(sampleRenameRow.removeFromLeft(206));
    sampleRenameRow.removeFromLeft(4);
    sampleRenameButton.setBounds(sampleRenameRow);

    panelArea.removeFromTop(4);
    auto sampleButtonRow2 = panelArea.removeFromTop(26);
    sampleClearButton.setBounds(sampleButtonRow2);

    panelArea.removeFromTop(4);
    auto sampleTrimStartRow = panelArea.removeFromTop(26);
    sampleTrimStartLabel.setBounds(sampleTrimStartRow.removeFromLeft(80));
    sampleTrimStartSlider.setBounds(sampleTrimStartRow);

    panelArea.removeFromTop(4);
    auto sampleTrimEndRow = panelArea.removeFromTop(26);
    sampleTrimEndLabel.setBounds(sampleTrimEndRow.removeFromLeft(80));
    sampleTrimEndSlider.setBounds(sampleTrimEndRow);

    panelArea.removeFromTop(4);
    auto sampleTrimButtonRow = panelArea.removeFromTop(26);
    sampleTrimApplyButton.setBounds(sampleTrimButtonRow.removeFromLeft(150));
    sampleTrimButtonRow.removeFromLeft(4);
    sampleTrimReloadButton.setBounds(sampleTrimButtonRow.removeFromLeft(150));

    panelArea.removeFromTop(4);
    auto sampleEditButtonRow = panelArea.removeFromTop(26);
    sampleNormalizeButton.setBounds(sampleEditButtonRow.removeFromLeft(100));
    sampleEditButtonRow.removeFromLeft(4);
    sampleFadeInButton.setBounds(sampleEditButtonRow.removeFromLeft(100));
    sampleEditButtonRow.removeFromLeft(4);
    sampleFadeOutButton.setBounds(sampleEditButtonRow.removeFromLeft(100));

    panelArea.removeFromTop(4);
    auto sampleEditButtonRow2 = panelArea.removeFromTop(26);
    sampleReverseButton.setBounds(sampleEditButtonRow2.removeFromLeft(100));

    panelArea.removeFromTop(4);
    auto sampleResampleRow = panelArea.removeFromTop(26);
    sampleRateSelector.setBounds(sampleResampleRow.removeFromLeft(110));
    sampleResampleRow.removeFromLeft(4);
    sampleResampleButton.setBounds(sampleResampleRow.removeFromLeft(100));

    panelArea.removeFromTop(4);
    auto sampleBitDepthRow = panelArea.removeFromTop(26);
    sampleBitDepthSelector.setBounds(sampleBitDepthRow.removeFromLeft(110));
    sampleBitDepthRow.removeFromLeft(4);
    sampleBitDepthButton.setBounds(sampleBitDepthRow.removeFromLeft(100));

    panelArea.removeFromTop(4);
    auto sampleLoopXfadeRow = panelArea.removeFromTop(26);
    sampleLoopXfadeButton.setBounds(sampleLoopXfadeRow.removeFromLeft(110));

    panelArea.removeFromTop(4);
    sampleWaveformView.setBounds(panelArea.removeFromTop(92));

    panelArea.removeFromTop(4);
    samplePathLabel.setBounds(panelArea.removeFromTop(48));

    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      sampleVolumeLabel.setBounds(row.removeFromLeft(80));
      sampleVolumeSlider.setBounds(row);
    }

    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      samplePanLabel.setBounds(row.removeFromLeft(80));
      samplePanSlider.setBounds(row);
    }

    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      sampleTransposeLabel.setBounds(row.removeFromLeft(80));
      sampleTransposeSlider.setBounds(row);
    }

    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      sampleLoopModeLabel.setBounds(row.removeFromLeft(80));
      sampleLoopModeBox.setBounds(row.removeFromLeft(130));
    }

    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      instrumentLoopModeLabel.setBounds(row.removeFromLeft(80));
      instrumentLoopModeBox.setBounds(row.removeFromLeft(130));
    }

    panelArea.removeFromTop(4);
    instrumentLoopStartLabel.setBounds(panelArea.removeFromTop(20));
    instrumentLoopStartSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(4);
    instrumentLoopEndLabel.setBounds(panelArea.removeFromTop(20));
    instrumentLoopEndSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(8);
    stepEditorTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    selectedStepLabel.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);

    patternSearchTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      patternSearchMode.setBounds(row.removeFromLeft(108));
      row.removeFromLeft(6);
      patternSearchValue.setBounds(row);
    }
    panelArea.removeFromTop(4);
    {
      auto row = panelArea.removeFromTop(26);
      patternSearchPrevButton.setBounds(row.removeFromLeft(152));
      row.removeFromLeft(6);
      patternSearchNextButton.setBounds(row.removeFromLeft(152));
    }
    panelArea.removeFromTop(4);
    patternSearchStatusLabel.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);

    patternCompareTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    auto compareRow = panelArea.removeFromTop(26);
    patternCompareCaptureButton.setBounds(compareRow.removeFromLeft(152));
    compareRow.removeFromLeft(6);
    patternCompareToggleButton.setBounds(compareRow.removeFromLeft(152));
    panelArea.removeFromTop(4);
    patternCompareStatusLabel.setBounds(panelArea.removeFromTop(20));
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

    panelArea.removeFromTop(6);
    midiLearnTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    auto midiLearnRow1 = panelArea.removeFromTop(26);
    midiLearnVelocityButton.setBounds(midiLearnRow1.removeFromLeft(152));
    midiLearnRow1.removeFromLeft(6);
    midiLearnGateButton.setBounds(midiLearnRow1.removeFromLeft(152));
    panelArea.removeFromTop(4);
    auto midiLearnRow2 = panelArea.removeFromTop(26);
    midiLearnEffectCommandButton.setBounds(midiLearnRow2.removeFromLeft(152));
    midiLearnRow2.removeFromLeft(6);
    midiLearnEffectValueButton.setBounds(midiLearnRow2.removeFromLeft(152));
    panelArea.removeFromTop(4);
    midiLearnClearButton.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    midiLearnStatusLabel.setBounds(panelArea.removeFromTop(36));

    panelArea.removeFromTop(4);
    applyFxToBlockButton.setBounds(panelArea.removeFromTop(26));

    panelArea.removeFromTop(6);
    patternMacroTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    patternMacroFillHatsButton.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    patternMacroAccent4Button.setBounds(panelArea.removeFromTop(26));
    panelArea.removeFromTop(4);
    patternMacroInvertVelocityButton.setBounds(panelArea.removeFromTop(26));

    panelArea.removeFromTop(6);
    undoHistoryTitle.setBounds(panelArea.removeFromTop(20));
    panelArea.removeFromTop(4);
    undoHistoryTimelineLabel.setBounds(panelArea.removeFromTop(76));
    panelArea.removeFromTop(4);
    undoHistorySelector.setBounds(panelArea.removeFromTop(26));

    panelArea.removeFromTop(6);
    keyboardOctaveLabel.setBounds(panelArea.removeFromTop(20));
    keyboardOctaveSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(6);
    editStepLabel.setBounds(panelArea.removeFromTop(20));
    editStepSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(6);
    notePreviewMsLabel.setBounds(panelArea.removeFromTop(20));
    notePreviewMsSlider.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(4);
    followPreviewToggle.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(12);
    gainLabel.setBounds(panelArea.removeFromTop(20));
    gainSlider.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(6);
    attackLabel.setBounds(panelArea.removeFromTop(20));
    attackSlider.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(6);
    releaseLabel.setBounds(panelArea.removeFromTop(20));
    releaseSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(6);
    instrumentRootLabel.setBounds(panelArea.removeFromTop(20));
    instrumentRootSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(6);
    instrumentPanLabel.setBounds(panelArea.removeFromTop(20));
    instrumentPanSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(6);
    pitchLabel.setBounds(panelArea.removeFromTop(20));
    pitchSlider.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(4);
    depthLabel.setBounds(panelArea.removeFromTop(20));
    depthSlider.setBounds(panelArea.removeFromTop(24));

    panelArea.removeFromTop(10);
    {
      auto headerRow = panelArea.removeFromTop(20);
      fxResetButton.setBounds(headerRow.removeFromRight(68));
      fxSectionLabel.setBounds(headerRow);
    }

    panelArea.removeFromTop(4);
    fxDelayLabel.setBounds(panelArea.removeFromTop(18));
    fxDelayTimeSlider.setBounds(panelArea.removeFromTop(22));
    fxDelayFeedbackSlider.setBounds(panelArea.removeFromTop(22));
    fxDelayWetSlider.setBounds(panelArea.removeFromTop(22));

    panelArea.removeFromTop(4);
    fxDistLabel.setBounds(panelArea.removeFromTop(18));
    fxDistTypeBox.setBounds(panelArea.removeFromTop(24));
    fxDistDriveSlider.setBounds(panelArea.removeFromTop(22));

    panelArea.removeFromTop(4);
    fxChorusLabel.setBounds(panelArea.removeFromTop(18));
    fxChorusRateSlider.setBounds(panelArea.removeFromTop(22));
    fxChorusDepthSlider.setBounds(panelArea.removeFromTop(22));
    fxChorusWetSlider.setBounds(panelArea.removeFromTop(22));

    panelArea.removeFromTop(4);
    fxReverbSendLabel.setBounds(panelArea.removeFromTop(18));
    fxReverbSendSlider.setBounds(panelArea.removeFromTop(22));

    panelArea.removeFromTop(10);
    reverbSectionLabel.setBounds(panelArea.removeFromTop(20));
    reverbRoomSlider.setBounds(panelArea.removeFromTop(22));
    reverbDampSlider.setBounds(panelArea.removeFromTop(22));
    reverbWetSlider.setBounds(panelArea.removeFromTop(22));
    reverbWidthSlider.setBounds(panelArea.removeFromTop(22));

    panelArea.removeFromTop(10);
    filterSectionLabel.setBounds(panelArea.removeFromTop(20));
    if (!filterChannelToggles.empty()) {
      filterChannelLabel.setBounds(panelArea.removeFromTop(18));
      const int toggleW = std::max(22, std::min(36, panelArea.getWidth() /
                                                static_cast<int>(filterChannelToggles.size())));
      const int perRow = std::max(1, panelArea.getWidth() / toggleW);
      for (std::size_t ti = 0; ti < filterChannelToggles.size(); ) {
        auto row = panelArea.removeFromTop(22);
        for (int col = 0; col < perRow && ti < filterChannelToggles.size(); ++col, ++ti)
          filterChannelToggles[ti]->setBounds(row.removeFromLeft(toggleW));
      }
      panelArea.removeFromTop(4);
    }
    filterTypeBox.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(4);
    filterCutoffLabel.setBounds(panelArea.removeFromTop(18));
    filterCutoffSlider.setBounds(panelArea.removeFromTop(24));
    panelArea.removeFromTop(4);
    filterResonanceLabel.setBounds(panelArea.removeFromTop(18));
    filterResonanceSlider.setBounds(panelArea.removeFromTop(24));

    if (!controlPortRows.empty()) {
      panelArea.removeFromTop(8);
      controlPortSectionTitle.setBounds(panelArea.removeFromTop(20));
      for (auto& row : controlPortRows) {
        panelArea.removeFromTop(4);
        row->label.setBounds(panelArea.removeFromTop(18));
        row->slider.setBounds(panelArea.removeFromTop(22));
      }
    }

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

    const int usedHeight = panelLayoutBounds.getHeight() - panelArea.getHeight();
    const int contentHeight = std::max(usedHeight + 8, panelViewportBounds.getHeight());
    panelWrapper->setBounds(0, 0, panelViewportBounds.getWidth(), contentHeight);
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
  void saveHelpToFile();

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

  void newModule() {
    auto doReset = [this]() {
      app.transport.stop();
      app.sequencer.reset();
      app.plugins.allNotesOff();
      app.audio.allNotesOff();

      const std::string tmpl = loadStartupTemplatePreference();

      {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        const std::size_t rows = app.module.currentEditor().rows();
        const std::size_t channels = app.module.currentEditor().channels();
        app.module.reset(rows, channels, 1);
        app.module.setMessage("");

        // Reset transport settings
        app.transport.setTempoBpm(125.0);
        app.transport.setTicksPerBeat(6);
        app.transport.setTicksPerRow(1);
        app.transport.resetTickCount();

        // Reset instruments back to built-in defaults
        app.plugins.clearInstrumentSlots();
        app.plugins.assignInstrument(0, "builtin.sine");
        app.plugins.assignInstrument(1, "builtin.square");

        // Clear the sample bank -- otherwise previously loaded samples'
        // names/paths linger in the sample slot dropdown after New Song.
        for (std::size_t sampleSlot = 0; sampleSlot < extracker::PluginHost::kMaxSampleSlots; ++sampleSlot) {
          app.plugins.clearSampleSlot(static_cast<std::uint16_t>(sampleSlot));
        }

        // Apply startup template if set
        if (tmpl != "blank") {
          extracker::applyPatternTemplate(app.module.currentEditor(), tmpl);
        }
      }

      // updatePatternSelector() reads these caches rather than app.module
      // directly (kept in sync elsewhere on every pattern/song mutation) --
      // without this, the pattern dropdown keeps showing the old song's
      // pattern count/selection until the next sequencer tick updates it.
      app.currentPatternCache.store(app.module.currentPattern());
      app.patternCountCache.store(app.module.patternCount());

      lastSongFile = juce::File();
      isSongDirty = false;
      moduleMessageEditor.setText("", false);
      moduleMessageSavedSnapshot = "";
      updateModuleMessageStateIndicator();
      refreshPatternView();
      updateRowEditScopeButtonText();
      updateInsertSwingModeButtonText();
      patternGrid.clampSelectionToBounds();
      updateStepEditorFromSelection();
      refreshSlotSelector();
      refreshChannelRows();
      refreshSampleSlotSelector();
      refreshSampleSlotDetails();
      tempoSlider.setValue(app.transport.tempoBpm(), juce::dontSendNotification);
      ticksPerBeatSlider.setValue(static_cast<double>(app.transport.ticksPerBeat()), juce::dontSendNotification);
      ticksPerRowSlider.setValue(static_cast<double>(app.transport.ticksPerRow()), juce::dontSendNotification);
      pluginStatusLabel.setText(
          tmpl != "blank" ? "New song — template: " + displayTemplateName(tmpl)
                          : "New song created",
          juce::dontSendNotification);
    };

    if (isSongDirty) {
      juce::AlertWindow::showYesNoCancelBox(
          juce::AlertWindow::WarningIcon,
          "Unsaved Changes",
          "The current song has unsaved changes. Do you want to save before creating a new song?",
          "Save",
          "Discard",
          "Cancel",
          nullptr,
          juce::ModalCallbackFunction::create([this, doReset](int result) {
            if (result == 1) {
              // Save first, then reset after save completes
              savePattern();
              // savePattern is async; user will need to click New Song again after save
              // OR set a flag — for simplicity, just save; the song will still be dirty
              // so a second click will land in "discard" or user cancels. Mark clean now:
              isSongDirty = false;
            } else if (result == 2) {
              doReset();
            }
            // result == 0 means Cancel — do nothing
          }));
    } else {
      doReset();
    }
  }

  void savePattern() {
    juce::File initialTarget = lastSongFile;
    if (initialTarget == juce::File()) {
      initialTarget = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                          .getChildFile("song.xtd");
    }

    savePatternFileChooser = std::make_unique<juce::FileChooser>("Save song/module file",
        initialTarget, "*.xtp;*.xtd");
    savePatternFileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser) {
          const juce::File selectedFile = normalizeSongSaveTarget(chooser.getResult());
          savePatternFileChooser.reset();
          if (selectedFile != juce::File()) {
            const std::string filePath = selectedFile.getFullPathName().toStdString();
            // Apply any pending message changes before saving
            app.module.setMessage(moduleMessageEditor.getText().toStdString());
            
            // Create backup of previous version
            juce::File backupFile(filePath + ".backup");
            if (selectedFile.existsAsFile()) {
              selectedFile.copyFileTo(backupFile);
            }
            
            if (app.savePatternToFile(filePath, /*blocking=*/true)) {
              lastSongFile = selectedFile;
              saveLastSongFilePreference(lastSongFile);
              moduleMessageSavedSnapshot = moduleMessageEditor.getText();
              updateModuleMessageStateIndicator();
              isSongDirty = false;
              pluginStatusLabel.setText("Song saved: " + selectedFile.getFileName(), juce::dontSendNotification);
              
              if (needsToSaveBeforeQuit) {
                needsToSaveBeforeQuit = false;
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
              }
            } else {
              pluginStatusLabel.setText("Failed to save song", juce::dontSendNotification);
            }
          }
        });
  }

  void loadPattern() {
    juce::File initialTarget = lastSongFile;
    if (initialTarget != juce::File()) {
      juce::File parent = initialTarget.getParentDirectory();
      if (parent.isDirectory()) {
        initialTarget = parent;
      }
    }
    if (initialTarget == juce::File() || !initialTarget.isDirectory()) {
      initialTarget = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    }

    loadPatternFileChooser = std::make_unique<juce::FileChooser>("Load song/module file",
        initialTarget, "*.xtp;*.xtd");
    loadPatternFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode,
        [this](const juce::FileChooser& chooser) {
          const juce::File selectedFile = chooser.getResult();
          loadPatternFileChooser.reset();
          if (selectedFile != juce::File()) {
            const std::string filePath = selectedFile.getFullPathName().toStdString();
            app.transport.stop();
            if (app.loadPatternFromFile(filePath, /*blocking=*/true)) {
              lastSongFile = selectedFile;
              saveLastSongFilePreference(lastSongFile);
              tempoSlider.setValue(app.transport.tempoBpm(), juce::dontSendNotification);
              ticksPerBeatSlider.setValue(static_cast<double>(app.transport.ticksPerBeat()), juce::dontSendNotification);
              ticksPerRowSlider.setValue(static_cast<double>(app.transport.ticksPerRow()), juce::dontSendNotification);
              refreshPatternView();
              updateRowEditScopeButtonText();
              updateInsertSwingModeButtonText();
              patternGrid.clampSelectionToBounds();
              updateStepEditorFromSelection();
              refreshSlotSelector();
              refreshParameterSlidersFromSlot();
              refreshChannelRows();
              refreshSampleSlotSelector();
              refreshSampleSlotDetails();
              moduleMessageEditor.setText(juce::String(app.module.message()), juce::dontSendNotification);
              moduleMessageSavedSnapshot = juce::String(app.module.message());
              updateModuleMessageStateIndicator();
              isSongDirty = false;
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
    const int insertChannel = std::clamp(selectedStepChannel, 0, std::max(channels - 1, 0));

    if (app.module.rowEditAllChannels()) {
      for (int row = rows - 1; row > insertRow; --row) {
        for (int channel = 0; channel < channels; ++channel) {
          writeStep(row, channel, readStep(row - 1, channel));
        }
      }

      for (int channel = 0; channel < channels; ++channel) {
        app.module.currentEditor().clearStep(insertRow, channel);
      }
    } else {
      for (int row = rows - 1; row > insertRow; --row) {
        writeStep(row, insertChannel, readStep(row - 1, insertChannel));
      }

      app.module.currentEditor().clearStep(insertRow, insertChannel);
    }

    lock.unlock();
    app.transport.resetTickCount();
    app.sequencer.reset();
    updateStepEditorFromSelection();
    patternGrid.repaint();
    if (app.module.rowEditAllChannels()) {
      pluginStatusLabel.setText("Inserted row at " + juce::String(insertRow) + " (all channels)",
                                juce::dontSendNotification);
    } else {
      pluginStatusLabel.setText(
          "Inserted row at " + juce::String(insertRow) + " (ch " + juce::String(insertChannel) + ")",
          juce::dontSendNotification);
    }
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
    const int removeChannel = std::clamp(selectedStepChannel, 0, std::max(channels - 1, 0));

    if (app.module.rowEditAllChannels()) {
      for (int row = removeRow; row < rows - 1; ++row) {
        for (int channel = 0; channel < channels; ++channel) {
          writeStep(row, channel, readStep(row + 1, channel));
        }
      }
      for (int channel = 0; channel < channels; ++channel) {
        app.module.currentEditor().clearStep(rows - 1, channel);
      }
    } else {
      for (int row = removeRow; row < rows - 1; ++row) {
        writeStep(row, removeChannel, readStep(row + 1, removeChannel));
      }
      app.module.currentEditor().clearStep(rows - 1, removeChannel);
    }

    lock.unlock();
    app.transport.resetTickCount();
    app.sequencer.reset();
    updateStepEditorFromSelection();
    patternGrid.repaint();
    if (app.module.rowEditAllChannels()) {
      pluginStatusLabel.setText("Removed row at " + juce::String(removeRow) + " (all channels)",
                                juce::dontSendNotification);
    } else {
      pluginStatusLabel.setText("Removed row at " + juce::String(removeRow) + " (ch " +
                                    juce::String(removeChannel) + ")",
                                juce::dontSendNotification);
    }
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
    consumeMidiEditorCcUpdates();
    flushPendingStepEdit();
    captureUndoHistoryIfPatternChanged();

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
      refreshMidiLearnStatus();
    }
    if ((refreshTickCounter % 4) == 0) {
      refreshSlotActivityLabels();
    }
    if ((refreshTickCounter % 8) == 0 && !isEditingSampleTrimSelection) {
      refreshSampleSlotDetails();
    }

    if (app.recordDirty.exchange(false, std::memory_order_relaxed)) {
      patternGrid.repaint();
    }

    bool isPlayingNow = app.transport.isPlaying();
    int currentRow = static_cast<int>(app.transport.currentRow());

    if (isPlayingNow != lastTransportPlaying || currentRow != lastTransportRow) {
      patternGrid.repaintPlaybackRows(lastTransportRow, currentRow);
      if (isPlayingNow && !lastTransportPlaying)
        playbackStartTime = std::chrono::steady_clock::now();
      lastTransportPlaying = isPlayingNow;
      lastTransportRow = currentRow;
    }

    syncPatternRowSliderFromViewport();
    syncMixerFromPatternViewport();

    {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      const int rows = static_cast<int>(app.module.currentEditor().rows());
      recordStartRowSlider.setRange(0.0, rows - 1, 1.0);
      const double cursorVal = static_cast<double>(app.recordState.cursorRow);
      if (recordStartRowSlider.getValue() != cursorVal)
        recordStartRowSlider.setValue(cursorVal, juce::dontSendNotification);
      const double stepVal = static_cast<double>(app.recordState.insertJump);
      if (recordStepSlider.getValue() != stepVal)
        recordStepSlider.setValue(stepVal, juce::dontSendNotification);
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
    songArrangerTitle.setText("Song Arranger Helpers", juce::dontSendNotification);
    songArrangerTitle.setJustificationType(juce::Justification::centredLeft);
    songArrangerBarsLabel.setText("Span", juce::dontSendNotification);
    songArrangerBarsLabel.setJustificationType(juce::Justification::centredLeft);
    songArrangerDuplicateButton.setTooltip("Duplicate selected section using span bars");
    songArrangerInsertBarsButton.setTooltip("Insert span bars after selection using selected pattern");
    songArrangerRippleLeftButton.setTooltip("Ripple move selected section left by span bars");
    songArrangerRippleRightButton.setTooltip("Ripple move selected section right by span bars");

    startupTemplateLabel.setText("Startup Template", juce::dontSendNotification);
    startupTemplateLabel.setJustificationType(juce::Justification::centredLeft);
    startupTemplateSelector.setTooltip("Choose startup template candidate");
    startupTemplatePreviewButton.setTooltip("Preview selected startup template on current pattern");
    startupTemplateSetDefaultButton.setTooltip("Save selected template as startup default");
    swingLabel.setText("Swing", juce::dontSendNotification);
    swingLabel.setJustificationType(juce::Justification::centredLeft);

    moduleMessageTitle.setText("Song / Module Message", juce::dontSendNotification);
    moduleMessageTitle.setJustificationType(juce::Justification::centredLeft);

    moduleMessageEditor.setMultiLine(true);
    moduleMessageEditor.setReturnKeyStartsNewLine(true);
    moduleMessageEditor.setScrollbarsShown(true);
    moduleMessageEditor.setInputRestrictions(kModuleMessageMaxChars);
    moduleMessageEditor.setTextToShowWhenEmpty("Write a module note, credits, or arrangement hint...", juce::Colour(0xFF6A737D));
    moduleMessageEditor.setTooltip("Stored inside the song file as module metadata");
    moduleMessageCounterLabel.setJustificationType(juce::Justification::centredRight);
    moduleMessageCounterLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA4AE));
    moduleMessageCounterLabel.setText("0 / " + juce::String(kModuleMessageMaxChars), juce::dontSendNotification);
    moduleMessageStateLabel.setJustificationType(juce::Justification::centredRight);
    moduleMessageStateLabel.setText("Saved", juce::dontSendNotification);
    moduleMessageStateLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF2EA043));
    moduleMessageApplyButton.setTooltip("Apply message text to current module");

    channelPanelTitle.setText("Per-Channel Instrument + Mute", juce::dontSendNotification);
    channelPanelTitle.setJustificationType(juce::Justification::centredLeft);

    slotPanelTitle.setText("Slot Editor", juce::dontSendNotification);
    slotPanelTitle.setJustificationType(juce::Justification::centredLeft);

    pluginPanelTitle.setText("Plugin Assignment", juce::dontSendNotification);
    pluginPanelTitle.setJustificationType(juce::Justification::centredLeft);
    scanPluginsButton.setTooltip("Scan LV2_PATH / VST3_PATH and ~/.vst3 for external plugins");

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

    sampleArmChannelLabel.setText("Place on ch", juce::dontSendNotification);
    sampleArmChannelLabel.setJustificationType(juce::Justification::centredLeft);
    sampleArmChannelLabel.setTooltip("Channel to write notes to when this sample is armed (-- = follow cursor)");
    sampleArmChannelSelector.setTooltip("When armed, new notes go to this channel regardless of cursor column");
    sampleArmChannelSelector.onChange = [this]() {
      const int selectedId = sampleArmChannelSelector.getSelectedId();
      app.sampleTargetChannel = selectedId - 2;  // id 1 = "— any —" (-1), id 2 = ch 0, id 3 = ch 1, ...
      patternGrid.grabKeyboardFocus();
    };

    sampleTrimStartLabel.setText("Trim Start", juce::dontSendNotification);
    sampleTrimStartLabel.setJustificationType(juce::Justification::centredLeft);
    sampleTrimStartSlider.setTooltip("Start point for sample trim (percent)");
    sampleTrimEndLabel.setText("Trim End", juce::dontSendNotification);
    sampleTrimEndLabel.setJustificationType(juce::Justification::centredLeft);
    sampleTrimEndSlider.setTooltip("End point for sample trim (percent)");
    sampleTrimApplyButton.setTooltip("Apply trim range to selected sample slot");
    sampleTrimReloadButton.setTooltip("Reload sample from source file path (undo trim)");
    sampleNormalizeButton.setTooltip("Normalize peak level inside trim selection");
    sampleFadeInButton.setTooltip("Apply fade in inside trim selection");
    sampleFadeOutButton.setTooltip("Apply fade out inside trim selection");
    sampleReverseButton.setTooltip("Reverse audio inside trim selection");
    sampleRateSelector.setTooltip("Target sample rate for resampling");
    sampleResampleButton.setTooltip("Resample whole sample to target rate (Lanczos, pitch-preserving)");
    sampleBitDepthSelector.setTooltip("Target bit depth for crushing");
    sampleBitDepthButton.setTooltip("Reduce bit depth inside trim selection");
    sampleLoopXfadeButton.setTooltip("Forward-loop the trim selection and crossfade its seam (selection start must be > 0)");
    sampleVolumeLabel.setText("Volume", juce::dontSendNotification);
    sampleVolumeLabel.setJustificationType(juce::Justification::centredLeft);
    sampleVolumeSlider.setTooltip("Playback volume multiplier (0.0-2.0; 1.0 = unity)");
    samplePanLabel.setText("Pan", juce::dontSendNotification);
    samplePanLabel.setJustificationType(juce::Justification::centredLeft);
    samplePanSlider.setTooltip("Stereo pan position (0 = L, 128 = C, 255 = R)");
    sampleTransposeLabel.setText("Transpose", juce::dontSendNotification);
    sampleTransposeLabel.setJustificationType(juce::Justification::centredLeft);
    sampleTransposeSlider.setTooltip("Root note offset from C4 in semitones (0 = no shift)");
    sampleLoopModeLabel.setText("Loop", juce::dontSendNotification);
    sampleLoopModeLabel.setJustificationType(juce::Justification::centredLeft);
    sampleLoopModeBox.setTooltip("Sample loop mode during playback");

    pluginStatusLabel.setText("", juce::dontSendNotification);
    pluginStatusLabel.setJustificationType(juce::Justification::centredLeft);

    stepEditorTitle.setText("Step Editor", juce::dontSendNotification);
    stepEditorTitle.setJustificationType(juce::Justification::centredLeft);
    selectedStepLabel.setText("No step selected", juce::dontSendNotification);
    selectedStepLabel.setJustificationType(juce::Justification::centredLeft);
    patternSearchTitle.setText("Pattern Search", juce::dontSendNotification);
    patternSearchTitle.setJustificationType(juce::Justification::centredLeft);
    patternSearchValue.setTextToShowWhenEmpty("C#4 or 61", juce::Colour(0xFF6A737D));
    patternSearchPrevButton.setTooltip("Find previous match in current pattern");
    patternSearchNextButton.setTooltip("Find next match in current pattern");
    patternSearchStatusLabel.setText("", juce::dontSendNotification);
    patternSearchStatusLabel.setJustificationType(juce::Justification::centredLeft);
    patternSearchStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA4AE));
    patternMacroTitle.setText("Pattern Macros", juce::dontSendNotification);
    patternMacroTitle.setJustificationType(juce::Justification::centredLeft);
    patternMacroFillHatsButton.setTooltip("Write closed hats on every 2nd row in selected channel");
    patternMacroAccent4Button.setTooltip("Boost velocity on every 4th row for existing notes");
    patternMacroInvertVelocityButton.setTooltip("Invert velocity values for existing notes");
    undoHistoryTitle.setText("Undo History", juce::dontSendNotification);
    undoHistoryTitle.setJustificationType(juce::Justification::centredLeft);
    undoHistoryTimelineLabel.setText("No edits yet", juce::dontSendNotification);
    undoHistoryTimelineLabel.setJustificationType(juce::Justification::topLeft);
    undoHistoryTimelineLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA4AE));
    undoHistoryTimelineLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xFF121518));
    undoHistoryTimelineLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xFF2A2F34));
    undoHistoryTimelineLabel.setBorderSize(juce::BorderSize<int>(4));
    undoHistorySelector.setTextWhenNothingSelected("Click to restore snapshot...");
    undoHistorySelector.setTooltip("Select a history snapshot to restore pattern state");
    patternCompareTitle.setText("A/B Pattern Compare", juce::dontSendNotification);
    patternCompareTitle.setJustificationType(juce::Justification::centredLeft);
    patternCompareCaptureButton.setTooltip("Capture current pattern as A compare snapshot");
    patternCompareToggleButton.setTooltip("Swap current pattern with captured A/B snapshot");
    patternCompareToggleButton.setEnabled(false);
    patternCompareStatusLabel.setText("No A snapshot captured", juce::dontSendNotification);
    patternCompareStatusLabel.setJustificationType(juce::Justification::centredLeft);
    patternCompareStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA4AE));
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
    notePreviewMsLabel.setText("Note Preview (ms)", juce::dontSendNotification);
    notePreviewMsLabel.setJustificationType(juce::Justification::centredLeft);
    followPreviewToggle.setButtonText("Follow Preview On Select");
    followPreviewToggle.setToggleState(false, juce::dontSendNotification);
    followPreviewToggle.setTooltip("When enabled, selecting a step previews its note/sample");

    slotLabel.setText("Slot", juce::dontSendNotification);
    slotLabel.setJustificationType(juce::Justification::centredLeft);

    gainLabel.setText("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType(juce::Justification::centredLeft);
    attackLabel.setText("Attack (ms)", juce::dontSendNotification);
    attackLabel.setJustificationType(juce::Justification::centredLeft);
    releaseLabel.setText("Release (ms)", juce::dontSendNotification);
    releaseLabel.setJustificationType(juce::Justification::centredLeft);

    pitchLabel.setText("Pitch (semitones)", juce::dontSendNotification);
    pitchLabel.setJustificationType(juce::Justification::centredLeft);
    configureParameterSlider(pitchSlider, -24.0, 24.0, 0.01);
    pitchSlider.setNumDecimalPlacesToDisplay(2);
    pitchSlider.setValue(0.0, juce::dontSendNotification);
    pitchSlider.setTooltip("Transpose this instrument up/down (semitones, fractional for fine-tune)");
    pitchSlider.onValueChange = [this]() {
        const int slot = getSelectedSlot();
        if (slot < 0) return;
        const auto u8 = static_cast<std::uint8_t>(slot);
        app.audio.setInstrumentPitch(u8, static_cast<float>(pitchSlider.getValue()));
        app.plugins.setInstrumentPitch(u8, static_cast<float>(pitchSlider.getValue()));
    };

    depthLabel.setText("Depth (F/R)", juce::dontSendNotification);
    depthLabel.setJustificationType(juce::Justification::centredLeft);
    configureParameterSlider(depthSlider, 0.0, 255.0, 1.0);
    depthSlider.setNumDecimalPlacesToDisplay(0);
    depthSlider.setValue(0.0, juce::dontSendNotification);
    depthSlider.setTooltip("Front/Rear position: 0=full front, 255=full rear");
    depthSlider.onValueChange = [this]() {
        const int slot = getSelectedSlot();
        if (slot < 0) return;
        const auto u8 = static_cast<std::uint8_t>(slot);
        const float d = static_cast<float>(depthSlider.getValue()) / 255.0f;
        app.audio.setInstrumentDepth(u8, d);
        app.plugins.setInstrumentDepth(u8, d);
    };

    instrumentRootLabel.setText("Root", juce::dontSendNotification);
    instrumentRootLabel.setJustificationType(juce::Justification::centredLeft);
    instrumentPanLabel.setText("Pan", juce::dontSendNotification);
    instrumentPanLabel.setJustificationType(juce::Justification::centredLeft);
    instrumentLoopModeLabel.setText("Loop", juce::dontSendNotification);
    instrumentLoopModeLabel.setJustificationType(juce::Justification::centredLeft);
    instrumentLoopStartLabel.setText("Loop Start", juce::dontSendNotification);
    instrumentLoopStartLabel.setJustificationType(juce::Justification::centredLeft);
    instrumentLoopEndLabel.setText("Loop End", juce::dontSendNotification);
    instrumentLoopEndLabel.setJustificationType(juce::Justification::centredLeft);

    instrumentRootSlider.setTooltip("Sample root note offset from C4 (requires builtin.sample)");
    instrumentPanSlider.setTooltip("Sample pan (0 = L, 128 = C, 255 = R; requires builtin.sample)");
    instrumentLoopModeBox.setTooltip("Instrument sample loop mode (requires builtin.sample)");
    instrumentLoopStartSlider.setTooltip("Instrument sample loop start frame (requires builtin.sample)");
    instrumentLoopEndSlider.setTooltip("Instrument sample loop end frame (requires builtin.sample)");

    auto initFxSlider = [this](juce::Slider& s, const char* tip) {
        configureParameterSlider(s, 0.0, 255.0, 1.0);
        s.setNumDecimalPlacesToDisplay(0);
        s.setValue(0.0, juce::dontSendNotification);
        s.setTooltip(tip);
    };
    auto applyFxParams = [this]() {
        const int slot = getSelectedSlot();
        if (slot < 0) return;
        const auto u8 = static_cast<std::uint8_t>(slot);
        extracker::InstrumentEffectParams p;
        p.delay.timeMs   = static_cast<float>(fxDelayTimeSlider.getValue()) * (1000.0f / 255.0f);
        p.delay.feedback = static_cast<float>(fxDelayFeedbackSlider.getValue()) / 255.0f * 0.98f;
        p.delay.wet      = static_cast<float>(fxDelayWetSlider.getValue())      / 255.0f;
        p.distortion.type  = static_cast<extracker::DistortionType>(
            std::max(0, fxDistTypeBox.getSelectedId() - 1));
        p.distortion.drive = static_cast<float>(fxDistDriveSlider.getValue()) / 255.0f;
        p.distortion.mix   = 1.0f;
        p.chorus.rate  = 0.05f + static_cast<float>(fxChorusRateSlider.getValue())  / 255.0f * 4.95f;
        p.chorus.depth = static_cast<float>(fxChorusDepthSlider.getValue()) / 255.0f;
        p.chorus.wet   = static_cast<float>(fxChorusWetSlider.getValue())   / 255.0f;
        app.audio.setInstrumentEffects(u8, p);
        app.plugins.setInstrumentEffects(u8, p);
    };

    fxResetButton.setButtonText("Reset FX");
    fxResetButton.setTooltip("Reset effects, filter and pitch for this instrument to defaults");
    fxResetButton.onClick = [this]() {
        const int slot = getSelectedSlot();
        if (slot < 0) return;
        const auto u8 = static_cast<std::uint8_t>(slot);
        app.audio.clearInstrumentEffects(u8);
        app.plugins.clearInstrumentEffects(u8);
        app.audio.clearInstrumentFilter(u8);
        app.plugins.clearInstrumentFilter(u8);
        app.audio.setInstrumentPitch(u8, 0.0f);
        app.plugins.setInstrumentPitch(u8, 0.0f);
        app.audio.setInstrumentReverbSend(u8, 0.0f);
        app.plugins.setInstrumentReverbSend(u8, 0.0f);
        app.audio.setInstrumentDepth(u8, 0.0f);
        app.plugins.setInstrumentDepth(u8, 0.0f);
        refreshParameterSlidersFromSlot();
    };

    fxSectionLabel.setText("Effects", juce::dontSendNotification);
    fxSectionLabel.setJustificationType(juce::Justification::centredLeft);
    fxSectionLabel.setFont(juce::Font(13.0f, juce::Font::bold));

    fxDelayLabel.setText("Delay", juce::dontSendNotification);
    fxDelayLabel.setJustificationType(juce::Justification::centredLeft);
    initFxSlider(fxDelayTimeSlider,     "Delay time: 0=0ms 255=1000ms");
    initFxSlider(fxDelayFeedbackSlider, "Delay feedback: 0=none 255=max");
    initFxSlider(fxDelayWetSlider,      "Delay wet/dry: 0=dry 255=wet");
    fxDelayTimeSlider.onValueChange     = [applyFxParams]() { applyFxParams(); };
    fxDelayFeedbackSlider.onValueChange = [applyFxParams]() { applyFxParams(); };
    fxDelayWetSlider.onValueChange      = [applyFxParams]() { applyFxParams(); };

    fxDistLabel.setText("Distortion", juce::dontSendNotification);
    fxDistLabel.setJustificationType(juce::Justification::centredLeft);
    fxDistTypeBox.addItem("Off",  1);
    fxDistTypeBox.addItem("Soft", 2);
    fxDistTypeBox.addItem("Hard", 3);
    fxDistTypeBox.addItem("Fuzz", 4);
    fxDistTypeBox.setSelectedId(1, juce::dontSendNotification);
    fxDistTypeBox.onChange = [applyFxParams]() { applyFxParams(); };
    initFxSlider(fxDistDriveSlider, "Distortion drive: 0=clean 255=max");
    fxDistDriveSlider.onValueChange = [applyFxParams]() { applyFxParams(); };

    fxChorusLabel.setText("Chorus", juce::dontSendNotification);
    fxChorusLabel.setJustificationType(juce::Justification::centredLeft);
    initFxSlider(fxChorusRateSlider,  "Chorus LFO rate: 0=0.05Hz 255=5Hz");
    initFxSlider(fxChorusDepthSlider, "Chorus depth: 0=none 255=20ms");
    initFxSlider(fxChorusWetSlider,   "Chorus wet/dry: 0=dry 255=wet");
    fxChorusRateSlider.onValueChange  = [applyFxParams]() { applyFxParams(); };
    fxChorusDepthSlider.onValueChange = [applyFxParams]() { applyFxParams(); };
    fxChorusWetSlider.onValueChange   = [applyFxParams]() { applyFxParams(); };

    fxReverbSendLabel.setText("Reverb Send", juce::dontSendNotification);
    fxReverbSendLabel.setJustificationType(juce::Justification::centredLeft);
    initFxSlider(fxReverbSendSlider, "Reverb send level: 0=dry 255=max send");
    fxReverbSendSlider.onValueChange = [this]() {
        const int slot = getSelectedSlot();
        if (slot < 0) return;
        const auto u8 = static_cast<std::uint8_t>(slot);
        const float send = static_cast<float>(fxReverbSendSlider.getValue()) / 255.0f;
        app.audio.setInstrumentReverbSend(u8, send);
        app.plugins.setInstrumentReverbSend(u8, send);
    };

    auto initReverbSlider = [this](juce::Slider& s, const char* tip) {
        configureParameterSlider(s, 0.0, 255.0, 1.0);
        s.setNumDecimalPlacesToDisplay(0);
        s.setValue(0.0, juce::dontSendNotification);
        s.setTooltip(tip);
    };
    auto applyReverbParams = [this]() {
        extracker::ReverbParams p;
        p.roomSize = static_cast<float>(reverbRoomSlider.getValue())  / 255.0f;
        p.damping  = static_cast<float>(reverbDampSlider.getValue())  / 255.0f;
        p.wet      = static_cast<float>(reverbWetSlider.getValue())   / 255.0f;
        p.width    = static_cast<float>(reverbWidthSlider.getValue()) / 255.0f;
        app.audio.setReverbParams(p);
    };

    reverbSectionLabel.setText("Reverb (global)", juce::dontSendNotification);
    reverbSectionLabel.setJustificationType(juce::Justification::centredLeft);
    reverbSectionLabel.setFont(juce::Font(13.0f, juce::Font::bold));

    initReverbSlider(reverbRoomSlider,  "Room size: 0=small 255=large");
    initReverbSlider(reverbDampSlider,  "Damping: 0=bright 255=dark");
    initReverbSlider(reverbWetSlider,   "Wet level: 0=off 255=max");
    initReverbSlider(reverbWidthSlider, "Stereo width: 0=mono 255=full");
    reverbWidthSlider.setValue(255.0, juce::dontSendNotification);  // default full width
    reverbRoomSlider.onValueChange  = [applyReverbParams]() { applyReverbParams(); };
    reverbDampSlider.onValueChange  = [applyReverbParams]() { applyReverbParams(); };
    reverbWetSlider.onValueChange   = [applyReverbParams]() { applyReverbParams(); };
    reverbWidthSlider.onValueChange = [applyReverbParams]() { applyReverbParams(); };

    filterSectionLabel.setText("Filter", juce::dontSendNotification);
    filterSectionLabel.setJustificationType(juce::Justification::centredLeft);
    filterSectionLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    filterChannelLabel.setText("Channels", juce::dontSendNotification);
    filterChannelLabel.setJustificationType(juce::Justification::centredLeft);
    filterTypeBox.addItem("Off",       1);
    filterTypeBox.addItem("Low-pass",  2);
    filterTypeBox.addItem("High-pass", 3);
    filterTypeBox.addItem("Band-pass", 4);
    filterTypeBox.addItem("Notch",     5);
    filterTypeBox.setSelectedId(1, juce::dontSendNotification);
    filterTypeBox.onChange = [this]() {
        const auto t = static_cast<extracker::BiquadType>(
            std::max(0, filterTypeBox.getSelectedId() - 1));
        const auto c = static_cast<float>(filterCutoffSlider.getValue() / 255.0);
        const auto r = static_cast<float>(filterResonanceSlider.getValue() / 255.0);
        for (const std::size_t ch : getFilterChannels())
            app.sequencer.setChannelFilter(ch, t, c, r, app.audio, app.plugins);
    };
    filterCutoffLabel.setText("Cutoff", juce::dontSendNotification);
    filterCutoffLabel.setJustificationType(juce::Justification::centredLeft);
    filterResonanceLabel.setText("Resonance", juce::dontSendNotification);
    filterResonanceLabel.setJustificationType(juce::Justification::centredLeft);
    configureParameterSlider(filterCutoffSlider, 0.0, 255.0, 1.0);
    filterCutoffSlider.setNumDecimalPlacesToDisplay(0);
    filterCutoffSlider.setValue(128.0, juce::dontSendNotification);
    filterCutoffSlider.setTooltip("Filter cutoff: 0=20 Hz, 255=20 kHz (exponential)");
    filterCutoffSlider.onValueChange = [this]() {
        const auto t = static_cast<extracker::BiquadType>(
            std::max(0, filterTypeBox.getSelectedId() - 1));
        if (t == extracker::BiquadType::Off) return;
        const auto c = static_cast<float>(filterCutoffSlider.getValue() / 255.0);
        const auto r = static_cast<float>(filterResonanceSlider.getValue() / 255.0);
        for (const std::size_t ch : getFilterChannels())
            app.sequencer.setChannelFilter(ch, t, c, r, app.audio, app.plugins);
    };
    configureParameterSlider(filterResonanceSlider, 0.0, 255.0, 1.0);
    filterResonanceSlider.setNumDecimalPlacesToDisplay(0);
    filterResonanceSlider.setValue(0.0, juce::dontSendNotification);
    filterResonanceSlider.setTooltip("Filter resonance: 0=flat, 255=max Q");
    filterResonanceSlider.onValueChange = [this]() {
        const auto t = static_cast<extracker::BiquadType>(
            std::max(0, filterTypeBox.getSelectedId() - 1));
        if (t == extracker::BiquadType::Off) return;
        const auto c = static_cast<float>(filterCutoffSlider.getValue() / 255.0);
        const auto r = static_cast<float>(filterResonanceSlider.getValue() / 255.0);
        for (const std::size_t ch : getFilterChannels())
            app.sequencer.setChannelFilter(ch, t, c, r, app.audio, app.plugins);
    };

    controlPortSectionTitle.setText("Parameters", juce::dontSendNotification);
    controlPortSectionTitle.setVisible(false);
    slotActivityTitle.setText("Slot Activity", juce::dontSendNotification);
    slotActivityTitle.setJustificationType(juce::Justification::centredLeft);

    configureParameterSlider(gainSlider, 0.0, 1.0, 0.01);
    configureParameterSlider(attackSlider, 1.0, 500.0, 1.0);
    configureParameterSlider(releaseSlider, 1.0, 1000.0, 1.0);
    configureParameterSlider(instrumentRootSlider, -60.0, 60.0, 1.0);
    instrumentRootSlider.setNumDecimalPlacesToDisplay(0);
    instrumentRootSlider.setTextValueSuffix(" st");
    configureParameterSlider(instrumentPanSlider, 0.0, 255.0, 1.0);
    instrumentPanSlider.setNumDecimalPlacesToDisplay(0);
    configureParameterSlider(instrumentLoopStartSlider, 0.0, 262143.0, 1.0);
    instrumentLoopStartSlider.setNumDecimalPlacesToDisplay(0);
    configureParameterSlider(instrumentLoopEndSlider, 0.0, 262143.0, 1.0);
    instrumentLoopEndSlider.setNumDecimalPlacesToDisplay(0);
    instrumentLoopModeBox.addItem("Off", 1);
    instrumentLoopModeBox.addItem("Forward", 2);
    instrumentLoopModeBox.addItem("Bidi", 3);
    instrumentLoopModeBox.addItem("Sustain", 4);
    instrumentLoopModeBox.setSelectedId(1, juce::dontSendNotification);
    configureParameterSlider(stepVelocitySlider, 1.0, 127.0, 1.0);
    stepVelocitySlider.setValue(100.0, juce::dontSendNotification);
    configureParameterSlider(stepGateSlider, 0.0, 32.0, 1.0);
    configureParameterSlider(stepEffectCommandSlider, 0.0, 255.0, 1.0);
    configureParameterSlider(stepEffectValueSlider, 0.0, 255.0, 1.0);
    configureParameterSlider(keyboardOctaveSlider, 0.0, 8.0, 1.0);
    configureParameterSlider(editStepSlider, 1.0, 16.0, 1.0);
    configureParameterSlider(notePreviewMsSlider, 40.0, 500.0, 10.0);
    notePreviewMsSlider.setNumDecimalPlacesToDisplay(0);
    notePreviewMsSlider.setTextValueSuffix(" ms");
    keyboardOctaveSlider.setValue(4.0, juce::dontSendNotification);
    editStepSlider.setValue(1.0, juce::dontSendNotification);
    notePreviewMsSlider.setValue(160.0, juce::dontSendNotification);
    patternGrid.setInsertDefaults(static_cast<std::uint32_t>(stepGateSlider.getValue()),
                                  static_cast<std::uint8_t>(stepVelocitySlider.getValue()));
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

    midiLearnTitle.setText("MIDI Learn (Editor CC)", juce::dontSendNotification);
    midiLearnTitle.setJustificationType(juce::Justification::centredLeft);
    midiLearnVelocityButton.setTooltip("Arm MIDI learn for step Velocity slider");
    midiLearnGateButton.setTooltip("Arm MIDI learn for step Gate slider");
    midiLearnEffectCommandButton.setTooltip("Arm MIDI learn for FX Command value");
    midiLearnEffectValueButton.setTooltip("Arm MIDI learn for FX Value value");
    midiLearnClearButton.setTooltip("Clear all editor action MIDI CC mappings");
    midiLearnStatusLabel.setText("", juce::dontSendNotification);
    midiLearnStatusLabel.setJustificationType(juce::Justification::centredLeft);
    midiLearnStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA4AE));
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
        patternGrid.grabKeyboardFocus();
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
        patternGrid.grabKeyboardFocus();
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
    populateSampleArmChannelSelector();
    reinitFilterChannelBox();
  }

  void populateSampleArmChannelSelector() {
    const int numChannels = static_cast<int>(app.module.currentEditor().channels());
    const int prevSelection = app.sampleTargetChannel;
    sampleArmChannelSelector.clear(juce::dontSendNotification);
    sampleArmChannelSelector.addItem("-- any --", 1);
    for (int ch = 0; ch < numChannels; ++ch) {
      sampleArmChannelSelector.addItem("ch " + juce::String(ch), ch + 2);
    }
    const int restoredId = (prevSelection >= 0 && prevSelection < numChannels) ? prevSelection + 2 : 1;
    sampleArmChannelSelector.setSelectedId(restoredId, juce::dontSendNotification);
    app.sampleTargetChannel = restoredId - 2;
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
        patternGrid.grabKeyboardFocus();
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
        patternGrid.grabKeyboardFocus();
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

    populateSampleArmChannelSelector();
    reinitFilterChannelBox();
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

  void showSF2KeyPickerDialog(const juce::File& sf2File, int instrumentSlot) {
    const std::string path = sf2File.getFullPathName().toStdString();
    const juce::String name = sf2File.getFileNameWithoutExtension();

    pluginStatusLabel.setText("Probing " + name + " for drum keys...", juce::dontSendNotification);

    // Probe runs on a background thread so the UI stays responsive.
    juce::Thread::launch([this, path, name, instrumentSlot]() {
      const std::vector<int> keys = app.plugins.enumSF2DrumKeys(path);

      juce::MessageManager::callAsync([this, path, name, instrumentSlot, keys]() {
        if (keys.empty()) {
          pluginStatusLabel.setText("No drum keys found in " + name, juce::dontSendNotification);
          return;
        }

        static const char* noteNames[] = {
          "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
        };

        auto* aw = new juce::AlertWindow(
            "SF2 Melodic Mode — " + name,
            "Each key in this soundbank is a different sound.\n"
            "Select one to play chromatically across the keyboard:",
            juce::AlertWindow::QuestionIcon);

        juce::StringArray keyLabels;
        for (const int k : keys) {
          const int oct  = k / 12 - 1;
          const int note = k % 12;
          keyLabels.add("Key " + juce::String(k) + "  (" +
                        juce::String(noteNames[note]) + juce::String(oct) + ")");
        }
        aw->addComboBox("keySelector", keyLabels, "Drum Key");
        aw->addButton("Load Melodic", 1);
        aw->addButton("Cancel", 0);

        aw->enterModalState(true,
            juce::ModalCallbackFunction::create([this, aw, path, name, instrumentSlot, keys](int result) {
              if (result == 1) {
                auto* combo = aw->getComboBoxComponent("keySelector");
                const int idx = combo ? combo->getSelectedItemIndex() : 0;
                const int lockKey = (idx >= 0 && idx < static_cast<int>(keys.size()))
                                    ? keys[idx] : keys[0];
                const std::string pluginId = "sf2:" + path + ":melodic:" + std::to_string(lockKey);
                const auto instr = static_cast<std::uint8_t>(instrumentSlot);
                const bool loaded = app.plugins.loadInstrumentAuto(pluginId, instr);
                pluginStatusLabel.setText(
                    loaded ? "Loaded melodic: " + name + " key " + juce::String(lockKey) +
                             " -> I" + juce::String(instrumentSlot)
                           : "Failed to load melodic SF2: " + name,
                    juce::dontSendNotification);
                if (loaded) {
                  app.midiInstrument = instrumentSlot;
                  refreshSlotSelector();
                  refreshChannelPluginLabels();
                  refreshParameterSlidersFromSlot();
                }
              }
              delete aw;
            }),
            true);
      });
    });
  }

  void refreshSlotSelector() {
    const int previousSelectedId = slotSelector.getSelectedId();
    slotSelector.clear(juce::dontSendNotification);

    for (int slot = 0; slot < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots); ++slot) {
      std::string pluginId = app.plugins.pluginForInstrument(static_cast<std::uint8_t>(slot));

      juce::String text = "I" + juce::String(slot);
      if (!pluginId.empty()) {
        const juce::String juceId(pluginId);
        juce::String displayName = juce::File::isAbsolutePath(juceId)
            ? juce::File(juceId).getFileNameWithoutExtension()
            : juceId;
        text += " -> " + displayName;
      }
      slotSelector.addItem(text, slot + 1);
    }

    const int restoreId = previousSelectedId > 0 ? previousSelectedId : 1;
    slotSelector.setSelectedId(restoreId, juce::dontSendNotification);
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

  bool selectionFramesFromPercent(std::size_t totalFrames,
                                  std::size_t& startFrame,
                                  std::size_t& endFrameExclusive) const {
    if (totalFrames < 2) {
      return false;
    }

    const double startPercent = std::clamp(sampleTrimStartSlider.getValue() / 100.0, 0.0, 0.99);
    const double endPercent = std::clamp(sampleTrimEndSlider.getValue() / 100.0, 0.01, 1.0);
    startFrame = static_cast<std::size_t>(std::floor(startPercent * static_cast<double>(totalFrames)));
    endFrameExclusive = static_cast<std::size_t>(std::ceil(endPercent * static_cast<double>(totalFrames)));
    startFrame = std::min(startFrame, totalFrames - 1);
    endFrameExclusive = std::clamp(endFrameExclusive, startFrame + static_cast<std::size_t>(1), totalFrames);
    return true;
  }

  void applySampleTrimSelection() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
      return;
    }

    const auto sourceFrames = app.plugins.sampleSourceFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    std::size_t startFrame = 0;
    std::size_t endFrameExclusive = 0;
    if (!selectionFramesFromPercent(sourceFrames, startFrame, endFrameExclusive)) {
      pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
      return;
    }

    const bool trimmed = app.plugins.trimSampleSlot(
        static_cast<std::uint16_t>(selectedSampleSlot),
        startFrame,
        endFrameExclusive);

    pluginStatusLabel.setText(
        trimmed ? "Trimmed " + formatSampleSlotHex(selectedSampleSlot) +
                      " to " + juce::String(static_cast<int>(endFrameExclusive - startFrame)) + " frames"
                : "Failed to trim " + formatSampleSlotHex(selectedSampleSlot),
        juce::dontSendNotification);
    refreshSampleSlotDetails();
    patternGrid.repaint();
  }

  void applySampleRangeNormalize() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
      return;
    }

    const auto frames = app.plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    std::size_t startFrame = 0;
    std::size_t endFrameExclusive = 0;
    if (!selectionFramesFromPercent(frames, startFrame, endFrameExclusive)) {
      pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
      return;
    }

    const bool ok = app.plugins.normalizeSampleSlot(
        static_cast<std::uint16_t>(selectedSampleSlot),
        startFrame,
        endFrameExclusive);
    pluginStatusLabel.setText(
        ok ? "Normalized selection in " + formatSampleSlotHex(selectedSampleSlot)
           : "Normalize failed for " + formatSampleSlotHex(selectedSampleSlot),
        juce::dontSendNotification);
    refreshSampleSlotDetails();
    patternGrid.repaint();
  }

  void applySampleRangeFade(bool fadeIn) {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
      return;
    }

    const auto frames = app.plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    std::size_t startFrame = 0;
    std::size_t endFrameExclusive = 0;
    if (!selectionFramesFromPercent(frames, startFrame, endFrameExclusive)) {
      pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
      return;
    }

    const bool ok = fadeIn
        ? app.plugins.fadeInSampleSlot(static_cast<std::uint16_t>(selectedSampleSlot), startFrame, endFrameExclusive)
        : app.plugins.fadeOutSampleSlot(static_cast<std::uint16_t>(selectedSampleSlot), startFrame, endFrameExclusive);
    pluginStatusLabel.setText(
        ok ? (fadeIn ? "Applied fade in to " : "Applied fade out to ") + formatSampleSlotHex(selectedSampleSlot)
           : (fadeIn ? "Fade in failed for " : "Fade out failed for ") + formatSampleSlotHex(selectedSampleSlot),
        juce::dontSendNotification);
    refreshSampleSlotDetails();
    patternGrid.repaint();
  }

  void applySampleRangeReverse() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
      return;
    }

    const auto frames = app.plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    std::size_t startFrame = 0;
    std::size_t endFrameExclusive = 0;
    if (!selectionFramesFromPercent(frames, startFrame, endFrameExclusive)) {
      pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
      return;
    }

    const bool ok = app.plugins.reverseSampleSlot(
        static_cast<std::uint16_t>(selectedSampleSlot),
        startFrame,
        endFrameExclusive);
    pluginStatusLabel.setText(
        ok ? "Reversed selection in " + formatSampleSlotHex(selectedSampleSlot)
           : "Reverse failed for " + formatSampleSlotHex(selectedSampleSlot),
        juce::dontSendNotification);
    refreshSampleSlotDetails();
    patternGrid.repaint();
  }

  void applySampleResample() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
      return;
    }
    if (app.plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot)) == 0) {
      pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
      return;
    }

    const int targetRate = sampleRateSelector.getSelectedId();
    if (targetRate <= 0) {
      pluginStatusLabel.setText("Select a target rate first", juce::dontSendNotification);
      return;
    }

    const bool ok = app.plugins.resampleSampleSlot(
        static_cast<std::uint16_t>(selectedSampleSlot),
        static_cast<std::uint32_t>(targetRate));
    pluginStatusLabel.setText(
        ok ? "Resampled " + formatSampleSlotHex(selectedSampleSlot) + " to " + juce::String(targetRate) + " Hz"
           : "Resample failed for " + formatSampleSlotHex(selectedSampleSlot) + " (same rate?)",
        juce::dontSendNotification);
    refreshSampleSlotDetails();
    patternGrid.repaint();
  }

  void applySampleBitDepth() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
      return;
    }

    const auto frames = app.plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    std::size_t startFrame = 0;
    std::size_t endFrameExclusive = 0;
    if (!selectionFramesFromPercent(frames, startFrame, endFrameExclusive)) {
      pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
      return;
    }

    const int bits = sampleBitDepthSelector.getSelectedId();
    if (bits <= 0) {
      pluginStatusLabel.setText("Select a bit depth first", juce::dontSendNotification);
      return;
    }

    const bool ok = app.plugins.bitDepthSampleSlot(
        static_cast<std::uint16_t>(selectedSampleSlot), bits, startFrame, endFrameExclusive);
    pluginStatusLabel.setText(
        ok ? "Reduced " + formatSampleSlotHex(selectedSampleSlot) + " to " + juce::String(bits) + "-bit"
           : "Bit crush failed for " + formatSampleSlotHex(selectedSampleSlot),
        juce::dontSendNotification);
    refreshSampleSlotDetails();
    patternGrid.repaint();
  }

  void applySampleLoopCrossfade() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      pluginStatusLabel.setText("Select a sample slot first", juce::dontSendNotification);
      return;
    }

    const auto frames = app.plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    std::size_t startFrame = 0;
    std::size_t endFrameExclusive = 0;
    if (!selectionFramesFromPercent(frames, startFrame, endFrameExclusive)) {
      pluginStatusLabel.setText("Selected sample slot is empty", juce::dontSendNotification);
      return;
    }
    if (startFrame < 1) {
      pluginStatusLabel.setText("Move selection start above 0 for crossfade pre-roll", juce::dontSendNotification);
      return;
    }

    // Make the trim selection a forward loop, then crossfade its seam.
    app.plugins.setSampleSlotParameter(static_cast<std::uint16_t>(selectedSampleSlot), "loop_mode", 1.0);
    app.plugins.setSampleSlotParameter(static_cast<std::uint16_t>(selectedSampleSlot), "loop_start", static_cast<double>(startFrame));
    app.plugins.setSampleSlotParameter(static_cast<std::uint16_t>(selectedSampleSlot), "loop_end", static_cast<double>(endFrameExclusive));
    sampleLoopModeBox.setSelectedId(2, juce::dontSendNotification);  // Forward

    const std::size_t len = std::min<std::size_t>({std::size_t{256}, startFrame, endFrameExclusive - startFrame});
    const bool ok = app.plugins.crossfadeLoopSampleSlot(static_cast<std::uint16_t>(selectedSampleSlot), len);
    pluginStatusLabel.setText(
        ok ? "Crossfaded loop seam in " + formatSampleSlotHex(selectedSampleSlot) + " (forward loop over selection)"
           : "Loop crossfade failed for " + formatSampleSlotHex(selectedSampleSlot),
        juce::dontSendNotification);
    refreshSampleSlotDetails();
    patternGrid.repaint();
  }

  void refreshSampleSlotDetails() {
    const int selectedSampleSlot = getSelectedSampleSlot();
    if (selectedSampleSlot < 0) {
      samplePathLabel.setText("No sample slot selected", juce::dontSendNotification);
      sampleRenameEditor.setText("", false);
      lastTrimSelectionSampleSlot = -1;
      lastTrimSelectionSamplePath.clear();
      return;
    }

    const std::string path = app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    const std::string name = app.plugins.sampleNameForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    const std::size_t frameCount = app.plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    const std::size_t sourceFrameCount = app.plugins.sampleSourceFrameCountForSlot(static_cast<std::uint16_t>(selectedSampleSlot));
    const bool sampleContextChanged = selectedSampleSlot != lastTrimSelectionSampleSlot || path != lastTrimSelectionSamplePath;
    sampleWaveformView.setWaveform(app.plugins.sampleWaveformForSlot(static_cast<std::uint16_t>(selectedSampleSlot), 2048));
    sampleRenameEditor.setText(path.empty() ? juce::String() : juce::String(name), false);
    if (sampleContextChanged) {
      sampleTrimStartSlider.setValue(0.0, juce::dontSendNotification);
      sampleTrimEndSlider.setValue(100.0, juce::dontSendNotification);

      const auto uslot = static_cast<std::uint16_t>(selectedSampleSlot);
      sampleVolumeSlider.setValue(
          app.plugins.getSampleSlotParameter(uslot, "gain"), juce::dontSendNotification);
      samplePanSlider.setValue(
          app.plugins.getSampleSlotParameter(uslot, "pan") * 255.0, juce::dontSendNotification);
      sampleTransposeSlider.setValue(
          app.plugins.getSampleSlotParameter(uslot, "sample_root") - 60.0, juce::dontSendNotification);
      const int loopMode = static_cast<int>(app.plugins.getSampleSlotParameter(uslot, "loop_mode"));
      sampleLoopModeBox.setSelectedId(loopMode + 1, juce::dontSendNotification);
    }
    sampleWaveformView.setSelection(sampleTrimStartSlider.getValue() / 100.0, sampleTrimEndSlider.getValue() / 100.0);
    juce::String text;
    if (path.empty()) {
      text = formatSampleSlotHex(selectedSampleSlot) + ": empty";
    } else {
      text = formatSampleSlotHex(selectedSampleSlot);
      if (!name.empty()) {
        text += " \"" + juce::String(name) + "\"";
      }
      text += ": " + juce::String(path);
      if (frameCount > 0) {
        text += "  |  " + juce::String(static_cast<int>(frameCount)) + " frames";
        if (sourceFrameCount > 0 && sourceFrameCount != frameCount) {
          text += " (source " + juce::String(static_cast<int>(sourceFrameCount)) + ")";
        }
      }
      if (selectedSampleSlot <= 255) {
        text += app.activeSampleSlot == selectedSampleSlot
            ? "  |  ARMED: placed notes will carry this sample slot"
            : "  |  not armed";
      } else {
        text += "  |  slot 256 is bank-only and not pattern-addressable";
      }
    }

    samplePathLabel.setText(text, juce::dontSendNotification);

    // Keep arm button label in sync with actual arm state
    const bool slotHasSample = selectedSampleSlot >= 0 && selectedSampleSlot <= 255 &&
        !app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot)).empty();
    const bool isArmed = (app.activeSampleSlot >= 0 && app.activeSampleSlot == selectedSampleSlot);
    sampleArmButton.setButtonText(isArmed ? "Disarm" : "Arm for Notes");
    sampleArmButton.setEnabled(slotHasSample || isArmed);
    lastTrimSelectionSampleSlot = selectedSampleSlot;
    lastTrimSelectionSamplePath = path;
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
      juce::String labelText;
      if (pluginId.empty()) {
        labelText = "(unassigned)";
      } else {
        const juce::String juceId(pluginId);
        labelText = juce::File::isAbsolutePath(juceId)
            ? juce::File(juceId).getFileNameWithoutExtension()
            : juceId;
      }
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

  // Returns instrument for the first active filter-channel toggle (for display).
  int getFilterInstrument() const {
    for (std::size_t i = 0; i < filterChannelToggles.size(); ++i) {
      if (filterChannelToggles[i]->getToggleState()) {
        const int instr =
            channelInstrumentSelectors[i]->getSelectedId() - 1;
        if (instr >= 0 && instr < static_cast<int>(extracker::PluginHost::kMaxInstrumentSlots))
          return instr;
      }
    }
    return getSelectedSlot();
  }

  // Returns all active (toggled) channel indices for filter writes.
  std::vector<std::size_t> getFilterChannels() const {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < filterChannelToggles.size(); ++i) {
      if (filterChannelToggles[i]->getToggleState())
        result.push_back(i);
    }
    return result;
  }

  void reinitFilterChannelBox() {
    // Remove old toggles from their parent
    for (auto& t : filterChannelToggles) {
      if (auto* p = t->getParentComponent())
        p->removeChildComponent(t.get());
    }
    filterChannelToggles.clear();

    const int n = static_cast<int>(app.module.currentEditor().channels());
    filterChannelToggles.reserve(static_cast<std::size_t>(n));
    for (int ch = 0; ch < n; ++ch) {
      auto btn = std::make_unique<juce::ToggleButton>(juce::String(ch + 1));
      btn->onClick = [this]() {
        // Refresh display from first active channel
        const int fi = getFilterInstrument();
        if (fi < 0) return;
        const extracker::BiquadParams fp =
            app.plugins.getInstrumentFilterParams(static_cast<std::uint8_t>(fi));
        filterTypeBox.setSelectedId(static_cast<int>(fp.type) + 1, juce::dontSendNotification);
        filterCutoffSlider.setValue(fp.cutoffNorm * 255.0, juce::dontSendNotification);
        filterResonanceSlider.setValue(fp.resonanceNorm * 255.0, juce::dontSendNotification);
      };
      if (panelWrapper) panelWrapper->addAndMakeVisible(*btn);
      else             addAndMakeVisible(*btn);
      filterChannelToggles.push_back(std::move(btn));
    }
    // Default: select channel 1 if nothing was selected
    if (!filterChannelToggles.empty())
      filterChannelToggles[0]->setToggleState(true, juce::dontSendNotification);
    if (isShowing()) resized();
  }

  void refreshParameterSlidersFromSlot() {
    int slot = getSelectedSlot();
    if (slot < 0) {
      return;
    }

    const std::uint8_t instrument = static_cast<std::uint8_t>(slot);
    const std::string pluginId = app.plugins.pluginForInstrument(instrument);
    const bool sampleInstrument = pluginId == "builtin.sample";

    double gain = app.plugins.getInstrumentParameter(instrument, "gain");
    double attack = app.plugins.getInstrumentParameter(instrument, "attack_ms");
    double release = app.plugins.getInstrumentParameter(instrument, "release_ms");

    suppressInstrumentSampleEditorCallbacks = true;
    gainSlider.setValue(gain, juce::dontSendNotification);
    attackSlider.setValue(attack, juce::dontSendNotification);
    releaseSlider.setValue(release, juce::dontSendNotification);

    double loopRangeMax = 262143.0;
    if (sampleInstrument) {
      const int sampleSlot = app.plugins.sampleSlotForInstrument(instrument);
      if (sampleSlot >= 0) {
        const std::size_t frameCount = app.plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(sampleSlot));
        if (frameCount > 0) {
          loopRangeMax = static_cast<double>(frameCount - 1);
        }
      }

      instrumentRootSlider.setValue(
          app.plugins.getInstrumentParameter(instrument, "sample_root") - 60.0,
          juce::dontSendNotification);
      instrumentPanSlider.setValue(
          app.plugins.getInstrumentParameter(instrument, "pan") * 255.0,
          juce::dontSendNotification);
      const int loopMode = std::clamp(
          static_cast<int>(std::lround(app.plugins.getInstrumentParameter(instrument, "loop_mode"))),
          0,
          3);
      instrumentLoopModeBox.setSelectedId(loopMode + 1, juce::dontSendNotification);

      const double loopStart = std::clamp(
          app.plugins.getInstrumentParameter(instrument, "loop_start"),
          0.0,
          loopRangeMax);
      const double loopEndRaw = app.plugins.getInstrumentParameter(instrument, "loop_end");
      const double loopEnd = std::clamp(loopEndRaw > 0.0 ? loopEndRaw : loopRangeMax, 0.0, loopRangeMax);
      instrumentLoopStartSlider.setValue(loopStart, juce::dontSendNotification);
      instrumentLoopEndSlider.setValue(loopEnd, juce::dontSendNotification);
    } else {
      instrumentRootSlider.setValue(0.0, juce::dontSendNotification);
      instrumentPanSlider.setValue(128.0, juce::dontSendNotification);
      instrumentLoopModeBox.setSelectedId(1, juce::dontSendNotification);
      instrumentLoopStartSlider.setValue(0.0, juce::dontSendNotification);
      instrumentLoopEndSlider.setValue(0.0, juce::dontSendNotification);
    }

    instrumentRootSlider.setRange(-60.0, 60.0, 1.0);
    instrumentPanSlider.setRange(0.0, 255.0, 1.0);
    instrumentLoopStartSlider.setRange(0.0, loopRangeMax, 1.0);
    instrumentLoopEndSlider.setRange(0.0, loopRangeMax, 1.0);

    instrumentRootSlider.setEnabled(sampleInstrument);
    instrumentPanSlider.setEnabled(sampleInstrument);
    instrumentLoopModeBox.setEnabled(sampleInstrument);
    instrumentLoopStartSlider.setEnabled(sampleInstrument);
    instrumentLoopEndSlider.setEnabled(sampleInstrument);
    // Pitch and depth
    pitchSlider.setValue(
        static_cast<double>(app.plugins.getInstrumentPitch(instrument)),
        juce::dontSendNotification);
    depthSlider.setValue(
        static_cast<double>(app.plugins.getInstrumentDepth(instrument) * 255.0f),
        juce::dontSendNotification);

    // Per-instrument FX
    {
        const extracker::InstrumentEffectParams ep =
            app.plugins.getInstrumentEffectParams(instrument);
        // Delay: convert back from internal representation
        fxDelayTimeSlider.setValue(
            static_cast<double>(ep.delay.timeMs / (1000.0f / 255.0f)),
            juce::dontSendNotification);
        fxDelayFeedbackSlider.setValue(
            static_cast<double>(ep.delay.feedback / 0.98f * 255.0f),
            juce::dontSendNotification);
        fxDelayWetSlider.setValue(
            static_cast<double>(ep.delay.wet * 255.0f),
            juce::dontSendNotification);
        // Distortion
        fxDistTypeBox.setSelectedId(
            static_cast<int>(ep.distortion.type) + 1,
            juce::dontSendNotification);
        fxDistDriveSlider.setValue(
            static_cast<double>(ep.distortion.drive * 255.0f),
            juce::dontSendNotification);
        // Chorus
        fxChorusRateSlider.setValue(
            static_cast<double>((ep.chorus.rate - 0.05f) / 4.95f * 255.0f),
            juce::dontSendNotification);
        fxChorusDepthSlider.setValue(
            static_cast<double>(ep.chorus.depth * 255.0f),
            juce::dontSendNotification);
        fxChorusWetSlider.setValue(
            static_cast<double>(ep.chorus.wet * 255.0f),
            juce::dontSendNotification);
    }

    // Reverb send (per-instrument)
    fxReverbSendSlider.setValue(
        static_cast<double>(app.plugins.getInstrumentReverbSend(instrument) * 255.0f),
        juce::dontSendNotification);

    // Global reverb params
    {
        const extracker::ReverbParams rp = app.audio.getReverbParams();
        reverbRoomSlider.setValue( static_cast<double>(rp.roomSize * 255.0f), juce::dontSendNotification);
        reverbDampSlider.setValue( static_cast<double>(rp.damping  * 255.0f), juce::dontSendNotification);
        reverbWetSlider.setValue(  static_cast<double>(rp.wet      * 255.0f), juce::dontSendNotification);
        reverbWidthSlider.setValue(static_cast<double>(rp.width    * 255.0f), juce::dontSendNotification);
    }

    // Filter
    {
        const int fi = getFilterInstrument();
        if (fi >= 0) {
            const extracker::BiquadParams fp =
                app.plugins.getInstrumentFilterParams(static_cast<std::uint8_t>(fi));
            filterTypeBox.setSelectedId(static_cast<int>(fp.type) + 1, juce::dontSendNotification);
            filterCutoffSlider.setValue(fp.cutoffNorm * 255.0, juce::dontSendNotification);
            filterResonanceSlider.setValue(fp.resonanceNorm * 255.0, juce::dontSendNotification);
        }
    }

    suppressInstrumentSampleEditorCallbacks = false;

    // Rebuild generic control-port sliders for LV2 / non-native-UI plugins
    controlPortRows.clear();
    extracker::PluginPortInfo portInfo;
    if (app.plugins.getPluginPortInfo(pluginId, portInfo) && !portInfo.controlInMeta.empty()) {
      for (std::size_t i = 0; i < portInfo.controlInMeta.size(); ++i) {
        const auto& meta = portInfo.controlInMeta[i];
        auto row = std::make_unique<ControlPortRow>();
        row->paramName = "lv2_control_in_" + std::to_string(i);
        const float lo = meta.hasMin ? meta.minVal : 0.0f;
        const float hi = meta.hasMax ? meta.maxVal : 1.0f;
        const std::string labelText = meta.label.empty() ? meta.symbol : meta.label;
        const double curVal = app.plugins.getInstrumentParameter(instrument, row->paramName);

        row->label.setText(labelText, juce::dontSendNotification);
        row->label.setJustificationType(juce::Justification::centredLeft);
        row->slider.setRange(static_cast<double>(lo), static_cast<double>(hi));
        row->slider.setValue(curVal, juce::dontSendNotification);
        row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);

        juce::Slider* sliderPtr = &row->slider;
        const std::string paramCapture = row->paramName;
        row->slider.onValueChange = [this, paramCapture, sliderPtr]() {
          setSelectedSlotParameter(paramCapture, sliderPtr->getValue());
        };

        panelWrapper->addAndMakeVisible(row->label);
        panelWrapper->addAndMakeVisible(row->slider);
        controlPortRows.push_back(std::move(row));
      }
    }
    controlPortSectionTitle.setVisible(!controlPortRows.empty());
    resized();
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

  void updateRowEditScopeButtonText() {
    rowEditScopeButton.setButtonText(app.module.rowEditAllChannels() ? "Rows: All Ch" : "Rows: One Ch");
  }

  void updateInsertSwingModeButtonText() {
    insertSwingModeButton.setButtonText(
        app.module.inheritSwingOnInsert() ? "Insert Swing: On" : "Insert Swing: Off");
  }

  void handleBounceWav() {
    if (isBouncing) {
      // Stop capture and write file.
      isBouncing = false;
      app.audio.clearCaptureCallback();
      bounceWavButton.setButtonText("Bounce WAV");
      bounceWavButton.setColour(juce::TextButton::buttonColourId,
                                getLookAndFeel().findColour(juce::TextButton::buttonColourId));
      writeBouncedWav();
      return;
    }

    // Pick output file, then arm capture.
    bounceFileChooser = std::make_shared<juce::FileChooser>(
        "Save bounce as WAV...",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory)
            .getChildFile("bounce.wav"),
        "*.wav");

    const int flags = juce::FileBrowserComponent::saveMode |
                      juce::FileBrowserComponent::canSelectFiles;
    bounceFileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
      const juce::File result = fc.getResult();
      if (result == juce::File{}) {
        return;  // cancelled
      }
      bounceOutputFile  = result.withFileExtension("wav");
      bounceSampleRate  = app.audio.currentSampleRate();

      {
        std::lock_guard<std::mutex> lock(bounceMutex);
        bounceBufferL.clear();
        bounceBufferR.clear();
      }

      app.audio.setCaptureCallback([this](const float* left, const float* right, std::uint32_t frames) {
        std::lock_guard<std::mutex> lock(bounceMutex);
        bounceBufferL.insert(bounceBufferL.end(), left, left + frames);
        bounceBufferR.insert(bounceBufferR.end(), right, right + frames);
      });

      isBouncing = true;
      bounceWavButton.setButtonText("Stop Bounce");
      bounceWavButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFCC4400));
    });
  }

  void writeBouncedWav() {
    std::vector<float> capL, capR;
    {
      std::lock_guard<std::mutex> lock(bounceMutex);
      capL = std::move(bounceBufferL);
      capR = std::move(bounceBufferR);
    }

    if (capL.empty()) {
      juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
          "Bounce", "Nothing was captured — press Play first, then Stop Bounce.");
      return;
    }

    const std::int32_t numSamples   = static_cast<std::int32_t>(capL.size());
    const std::int16_t numChannels  = 2;
    const std::int16_t bitsPerSample = 16;
    const std::int32_t sr           = static_cast<std::int32_t>(bounceSampleRate);
    const std::int16_t blockAlign   = numChannels * (bitsPerSample / 8);
    const std::int32_t byteRate     = sr * blockAlign;
    const std::int32_t dataBytes    = numSamples * blockAlign;

    // Write a minimal stereo 16-bit PCM WAV file without juce_audio_formats.
    if (bounceOutputFile.existsAsFile()) {
      bounceOutputFile.deleteFile();
    }
    std::unique_ptr<juce::FileOutputStream> out(bounceOutputFile.createOutputStream());
    if (!out || !out->openedOk()) {
      juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
          "Bounce", "Could not open output file:\n" + bounceOutputFile.getFullPathName());
      return;
    }

    // RIFF header
    out->write("RIFF", 4);
    out->writeInt(36 + dataBytes);
    out->write("WAVE", 4);
    // fmt chunk
    out->write("fmt ", 4);
    out->writeInt(16);
    out->writeShort(1);            // PCM
    out->writeShort(numChannels);
    out->writeInt(sr);
    out->writeInt(byteRate);
    out->writeShort(blockAlign);
    out->writeShort(bitsPerSample);
    // data chunk
    out->write("data", 4);
    out->writeInt(dataBytes);
    // PCM samples — interleaved L/R, 16-bit signed little-endian
    for (std::int32_t i = 0; i < numSamples; ++i) {
      out->writeShort(static_cast<short>(std::clamp(capL[static_cast<std::size_t>(i)] * 32767.0f, -32767.0f, 32767.0f)));
      out->writeShort(static_cast<short>(std::clamp(capR[static_cast<std::size_t>(i)] * 32767.0f, -32767.0f, 32767.0f)));
    }

    const double seconds = static_cast<double>(numSamples) / static_cast<double>(bounceSampleRate);
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        "Bounce Complete",
        juce::String::formatted("Saved %.1f s to:\n", seconds) + bounceOutputFile.getFullPathName());
  }

  void updateStatusLabels() {
    bool playing = app.transport.isPlaying();
    int row = static_cast<int>(app.transport.currentRow());
    double bpm = app.transport.tempoBpm();
    int ticksPerBeat = static_cast<int>(app.transport.ticksPerBeat());
    int ticksPerRow = static_cast<int>(app.transport.ticksPerRow());
    std::uint64_t dispatchCount = app.sequencer.dispatchCount();
    std::size_t activeVoices = app.sequencer.activeVoiceCount();
    std::size_t pluginNoteOn = app.plugins.noteOnEventCount();
    std::size_t pluginNoteOff = app.plugins.noteOffEventCount();
    const bool rowEditAllChannels = app.module.rowEditAllChannels();
    const bool inheritSwingOnInsert = app.module.inheritSwingOnInsert();

    juce::String autoSaveText = "AS off";
    if (app.recoveryAutoSaveEnabled.load()) {
      const auto stampMs = static_cast<juce::int64>(app.lastRecoverySnapshotEpochMs.load());
      if (stampMs > 0) {
        autoSaveText = "AS " + juce::Time(stampMs).formatted("%H:%M:%S");
      } else {
        autoSaveText = "AS waiting";
      }
    }

    juce::String elapsedText = "0:00";
    if (playing) {
      const auto elapsed = std::chrono::steady_clock::now() - playbackStartTime;
      const auto totalSecs = static_cast<int>(
          std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
      const int mins = totalSecs / 60;
      const int secs = totalSecs % 60;
      elapsedText = juce::String(mins) + ":" +
                    juce::String(secs).paddedLeft('0', 2);
    }

    juce::String text = juce::String(playing ? "Play" : "Stop") +
                        " | " + elapsedText +
                        " | R" + juce::String(row) +
                        " | " + juce::String(bpm, 1) + "b" +
                        " | TPB" + juce::String(ticksPerBeat) +
                        " | TPR" + juce::String(ticksPerRow) +
                        " | V" + juce::String(static_cast<juce::int64>(activeVoices)) +
                        " | S" + juce::String(static_cast<juce::int64>(dispatchCount)) +
                        " | " + autoSaveText;
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

  void stepPatternSelection(int delta) {
    const int count = patternSelector.getNumItems();
    if (count <= 0 || delta == 0) {
      return;
    }

    int current = patternSelector.getSelectedItemIndex();
    if (current < 0) {
      current = std::clamp(static_cast<int>(app.currentPatternCache.load()), 0, count - 1);
    }

    const int next = std::clamp(current + delta, 0, count - 1);
    if (next == current) {
      return;
    }
    patternSelector.setSelectedItemIndex(next, juce::sendNotificationSync);
  }

  void selectSongOrderEntry(int selectedIndex) {
    if (selectedIndex < 0) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(app.stateMutex);
      if (selectedIndex >= static_cast<int>(app.module.songLength())) {
        return;
      }
      app.currentSongOrderPositionCache.store(static_cast<std::size_t>(selectedIndex));
      app.module.switchToPattern(app.module.songEntryAt(static_cast<std::size_t>(selectedIndex)));
      app.currentPatternCache.store(app.module.currentPattern());
    }
    refreshPatternView();
  }

  void syncPatternRowSliderFromViewport() {
    const int maxScroll = std::max(0, patternGrid.getHeight() - patternViewport.getHeight());
    const int currentY = std::clamp(patternViewport.getViewPositionY(), 0, maxScroll);

    suppressPatternRowSliderCallback = true;
    // JUCE slider ranges require end > start; keep a valid range even when no scrolling is possible.
    const double sliderMax = maxScroll > 0 ? static_cast<double>(maxScroll) : 1.0;
    patternRowSlider.setRange(0.0, sliderMax, 1.0);
    patternRowSlider.setValue(static_cast<double>(currentY), juce::dontSendNotification);
    patternRowSlider.setEnabled(maxScroll > 0);
    suppressPatternRowSliderCallback = false;
  }

  void syncMixerFromPatternViewport() {
    const int x = patternViewport.getViewPositionX();
    if (mixerViewport.getViewPositionX() != x)
      mixerViewport.setViewPosition(x, 0);
  }

  void syncMixerLayout() {
    const int numChannels = static_cast<int>(app.module.currentEditor().channels());
    const int lw = patternGrid.currentLabelWidth();
    const int cw = patternGrid.currentCellWidth();

    if (numChannels != mixerLastNumChannels) {
      mixerLastNumChannels = numChannels;
      mixerStrip.rebuild(numChannels, lw, cw, app.darkMode,
                         [this](int ch) {
                           return app.sequencer.channelVolume(static_cast<std::size_t>(ch));
                         });
    } else {
      mixerStrip.syncLayout(lw, cw);
    }
    // Make the strip as wide as the pattern grid content so scroll sync works
    mixerStrip.setBounds(0, 0, patternGrid.getWidth(), mixerViewport.getHeight());
    syncMixerFromPatternViewport();
  }

  void refreshSwingForCurrentPattern() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }
    const auto swing = static_cast<std::uint32_t>(app.module.currentPatternSwing());
    lock.unlock();

    if (std::abs(swingSlider.getValue() - static_cast<double>(swing)) > 0.1) {
      swingSlider.setValue(static_cast<double>(swing), juce::dontSendNotification);
    }
    app.transport.setSwingPercent(swing);
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

  void applySongArrangerDuplicateSection() {
    const int span = std::clamp(static_cast<int>(std::lround(songArrangerBarsSlider.getValue())), 1, 16);
    const int selected = songOrderEntrySelector.getSelectedItemIndex();
    if (selected < 0) {
      pluginStatusLabel.setText("Select a song entry first", juce::dontSendNotification);
      return;
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Arrange skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    std::vector<std::size_t> order;
    order.reserve(app.module.songLength());
    for (std::size_t i = 0; i < app.module.songLength(); ++i) {
      order.push_back(app.module.songEntryAt(i));
    }
    if (order.empty()) {
      return;
    }

    const int start = std::clamp(selected, 0, static_cast<int>(order.size()) - 1);
    const int sectionLen = std::min(span, static_cast<int>(order.size()) - start);
    const int insertPos = start + sectionLen;
    order.insert(order.begin() + insertPos,
                 order.begin() + start,
                 order.begin() + start + sectionLen);

    app.module.setSongOrder(order);
    const std::size_t newPos = static_cast<std::size_t>(insertPos);
    app.currentSongOrderPositionCache.store(newPos);
    app.module.switchToPattern(app.module.songEntryAt(newPos));
    app.currentPatternCache.store(app.module.currentPattern());
    lock.unlock();

    refreshPatternView();
    pluginStatusLabel.setText("Arranger: duplicated " + juce::String(sectionLen) + " bars", juce::dontSendNotification);
  }

  void applySongArrangerInsertBars() {
    const int span = std::clamp(static_cast<int>(std::lround(songArrangerBarsSlider.getValue())), 1, 16);
    const int selected = songOrderEntrySelector.getSelectedItemIndex();
    const int selectedPattern = songOrderPatternSelector.getSelectedItemIndex();
    if (selected < 0 || selectedPattern < 0) {
      pluginStatusLabel.setText("Select song entry and pattern first", juce::dontSendNotification);
      return;
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Arrange skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    const std::size_t insertBase = static_cast<std::size_t>(selected + 1);
    const std::size_t patternIndex = static_cast<std::size_t>(selectedPattern);
    for (int i = 0; i < span; ++i) {
      app.module.insertSongEntry(insertBase + static_cast<std::size_t>(i), patternIndex);
    }

    app.currentSongOrderPositionCache.store(insertBase);
    app.module.switchToPattern(patternIndex);
    app.currentPatternCache.store(app.module.currentPattern());
    lock.unlock();

    refreshPatternView();
    pluginStatusLabel.setText("Arranger: inserted " + juce::String(span) + " bars", juce::dontSendNotification);
  }

  void applySongArrangerRippleMove(int direction) {
    const int span = std::clamp(static_cast<int>(std::lround(songArrangerBarsSlider.getValue())), 1, 16);
    if (direction != -1 && direction != 1) {
      return;
    }
    const int selected = songOrderEntrySelector.getSelectedItemIndex();
    if (selected < 0) {
      pluginStatusLabel.setText("Select a song entry first", juce::dontSendNotification);
      return;
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Arrange skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    std::vector<std::size_t> order;
    order.reserve(app.module.songLength());
    for (std::size_t i = 0; i < app.module.songLength(); ++i) {
      order.push_back(app.module.songEntryAt(i));
    }
    if (order.size() <= 1) {
      return;
    }

    const int start = std::clamp(selected, 0, static_cast<int>(order.size()) - 1);
    const int sectionLen = std::min(span, static_cast<int>(order.size()) - start);
    std::vector<std::size_t> segment(order.begin() + start, order.begin() + start + sectionLen);
    order.erase(order.begin() + start, order.begin() + start + sectionLen);

    const int delta = direction * span;
    const int target = std::clamp(start + delta, 0, static_cast<int>(order.size()));
    order.insert(order.begin() + target, segment.begin(), segment.end());

    app.module.setSongOrder(order);
    const std::size_t newPos = static_cast<std::size_t>(target);
    app.currentSongOrderPositionCache.store(newPos);
    app.module.switchToPattern(app.module.songEntryAt(newPos));
    app.currentPatternCache.store(app.module.currentPattern());
    lock.unlock();

    refreshPatternView();
    pluginStatusLabel.setText("Arranger: ripple moved section", juce::dontSendNotification);
  }

  void refreshModuleMessageEditor() {
    if (moduleMessageEditor.hasKeyboardFocus(true)) {
      updateModuleMessageCounter();
      return;
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }

    const juce::String message(app.module.message());
    moduleMessageSavedSnapshot = message;
    if (moduleMessageEditor.getText() != message) {
      moduleMessageEditor.setText(message, false);
    }
    updateModuleMessageCounter();
    updateModuleMessageStateIndicator();
  }

  void updateModuleMessageCounter() {
    const int chars = moduleMessageEditor.getText().length();
    const juce::String counter = juce::String(chars) + " / " + juce::String(kModuleMessageMaxChars);
    if (moduleMessageCounterLabel.getText() != counter) {
      moduleMessageCounterLabel.setText(counter, juce::dontSendNotification);
    }
  }

  void updateModuleMessageStateIndicator() {
    const bool saved = (moduleMessageEditor.getText() == moduleMessageSavedSnapshot);
    const juce::String text = saved ? "Saved" : "Unsaved changes";
    const juce::Colour colour = saved ? juce::Colour(0xFF2EA043) : juce::Colour(0xFFE5534B);
    if (moduleMessageStateLabel.getText() != text) {
      moduleMessageStateLabel.setText(text, juce::dontSendNotification);
    }
    moduleMessageStateLabel.setColour(juce::Label::textColourId, colour);
  }

  void updatePatternCompareStatus() {
    if (!patternCompareHasSnapshot) {
      patternCompareStatusLabel.setText("No A snapshot captured", juce::dontSendNotification);
      patternCompareToggleButton.setEnabled(false);
      patternCompareToggleButton.setButtonText("Toggle A/B");
      return;
    }

    patternCompareToggleButton.setEnabled(true);
    patternCompareToggleButton.setButtonText("Toggle A/B");
    const juce::String modeText = patternCompareShowingA
        ? "Showing A snapshot"
        : "Showing B (latest)";
    patternCompareStatusLabel.setText(
        modeText + "  |  P" + juce::String(static_cast<int>(patternComparePatternIndex + 1)),
        juce::dontSendNotification);
  }

  void findPatternMatch(bool forward) {
    if (selectedStepRow < 0 || selectedStepChannel < 0) {
      patternSearchStatusLabel.setText("Select a step first", juce::dontSendNotification);
      return;
    }

    const int mode = patternSearchMode.getSelectedId();
    int queryValue = 0;
    if (mode == 1) {
      if (!parseTrackerNote(patternSearchValue.getText(), queryValue)) {
        patternSearchStatusLabel.setText("Invalid note query", juce::dontSendNotification);
        return;
      }
    } else {
      if (!parseSearchByte(patternSearchValue.getText(), queryValue)) {
        patternSearchStatusLabel.setText("Invalid byte query (00-FF)", juce::dontSendNotification);
        return;
      }
    }

    int foundRow = -1;
    int foundChannel = -1;

    {
      std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        patternSearchStatusLabel.setText("Search skipped (engine busy)", juce::dontSendNotification);
        return;
      }

      auto& editor = app.module.currentEditor();
      const int rows = static_cast<int>(editor.rows());
      const int channels = static_cast<int>(editor.channels());
      const int total = rows * channels;
      if (rows <= 0 || channels <= 0 || total <= 0) {
        patternSearchStatusLabel.setText("Pattern is empty", juce::dontSendNotification);
        return;
      }

      const int clampedRow = std::clamp(selectedStepRow, 0, rows - 1);
      const int clampedChannel = std::clamp(selectedStepChannel, 0, channels - 1);
      const int start = clampedRow * channels + clampedChannel;

      for (int offset = 1; offset <= total; ++offset) {
        const int index = forward
            ? (start + offset) % total
            : (start - offset + total * 4) % total;
        const int row = index / channels;
        const int channel = index % channels;

        bool match = false;
        if (mode == 1) {
          match = editor.hasNoteAt(row, channel) && editor.noteAt(row, channel) == queryValue;
        } else if (mode == 2) {
          match = editor.hasNoteAt(row, channel) && editor.instrumentAt(row, channel) == queryValue;
        } else {
          const int effectCommand = editor.effectCommandAt(row, channel);
          const int effectValue = editor.effectValueAt(row, channel);
          match = effectValue == queryValue && (effectCommand != 0 || effectValue != 0);
        }

        if (match) {
          foundRow = row;
          foundChannel = channel;
          break;
        }
      }
    }

    if (foundRow < 0 || foundChannel < 0) {
      patternSearchStatusLabel.setText("No match found", juce::dontSendNotification);
      return;
    }

    patternGrid.jumpToCell(foundRow, foundChannel);
    patternSearchStatusLabel.setText(
        "Found at R" + juce::String(foundRow) + " C" + juce::String(foundChannel),
        juce::dontSendNotification);
  }

  void applyPatternMacroFillHats() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Macro skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    auto& editor = app.module.currentEditor();
    const int rows = static_cast<int>(editor.rows());
    const int channels = static_cast<int>(editor.channels());
    const int channel = std::clamp(selectedStepChannel >= 0 ? selectedStepChannel : 0, 0, std::max(0, channels - 1));
    const int instrument = (channel >= 0 && static_cast<std::size_t>(channel) < app.channelInstruments.size())
        ? static_cast<int>(app.channelInstruments[static_cast<std::size_t>(channel)])
        : 0;
    const int velocity = std::clamp(static_cast<int>(std::lround(stepVelocitySlider.getValue())), 1, 127);
    const auto gateTicks = static_cast<std::uint32_t>(std::max(0, static_cast<int>(std::lround(stepGateSlider.getValue()))));

    int writes = 0;
    for (int row = 0; row < rows; row += 2) {
      editor.insertNote(row,
                        channel,
                        42,
                        static_cast<std::uint8_t>(std::clamp(instrument, 0, 255)),
                        gateTicks,
                        static_cast<std::uint8_t>(velocity),
                        false,
                        static_cast<std::uint8_t>(editor.effectCommandAt(row, channel)),
                        static_cast<std::uint8_t>(editor.effectValueAt(row, channel)));
      ++writes;
    }

    lock.unlock();
    patternGrid.repaint();
    pluginStatusLabel.setText("Macro: filled hats on CH " + juce::String(channel) + " (" + juce::String(writes) + " rows)",
                              juce::dontSendNotification);
  }

  void applyPatternMacroAccentEveryFourth() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Macro skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    auto& editor = app.module.currentEditor();
    const int rows = static_cast<int>(editor.rows());
    const int channels = static_cast<int>(editor.channels());
    int edits = 0;

    for (int row = 0; row < rows; ++row) {
      if ((row % 4) != 0) {
        continue;
      }
      for (int channel = 0; channel < channels; ++channel) {
        if (!editor.hasNoteAt(row, channel)) {
          continue;
        }
        const int current = static_cast<int>(editor.velocityAt(row, channel));
        const int accented = std::clamp(current + 20, 1, 127);
        if (accented != current) {
          editor.setVelocity(row, channel, static_cast<std::uint8_t>(accented));
          ++edits;
        }
      }
    }

    lock.unlock();
    patternGrid.repaint();
    pluginStatusLabel.setText("Macro: accented every 4th row (" + juce::String(edits) + " notes)",
                              juce::dontSendNotification);
  }

  void applyPatternMacroInvertVelocities() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Macro skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    auto& editor = app.module.currentEditor();
    const int rows = static_cast<int>(editor.rows());
    const int channels = static_cast<int>(editor.channels());
    int edits = 0;

    for (int row = 0; row < rows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        if (!editor.hasNoteAt(row, channel)) {
          continue;
        }
        const int current = static_cast<int>(editor.velocityAt(row, channel));
        const int inverted = std::clamp(128 - current, 1, 127);
        if (inverted != current) {
          editor.setVelocity(row, channel, static_cast<std::uint8_t>(inverted));
          ++edits;
        }
      }
    }

    lock.unlock();
    patternGrid.repaint();
    pluginStatusLabel.setText("Macro: inverted velocities (" + juce::String(edits) + " notes)",
                              juce::dontSendNotification);
  }

  std::uint64_t computePatternHashLocked(const extracker::PatternEditor& editor) const {
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](std::uint64_t v) {
      hash ^= v;
      hash *= 1099511628211ull;
    };

    const int rows = static_cast<int>(editor.rows());
    const int channels = static_cast<int>(editor.channels());
    mix(static_cast<std::uint64_t>(rows));
    mix(static_cast<std::uint64_t>(channels));
    for (int row = 0; row < rows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        mix(static_cast<std::uint64_t>(editor.hasNoteAt(row, channel) ? 1 : 0));
        mix(static_cast<std::uint64_t>(std::max(editor.noteAt(row, channel), -1) + 1));
        mix(static_cast<std::uint64_t>(editor.instrumentAt(row, channel)));
        mix(static_cast<std::uint64_t>(editor.sampleAt(row, channel)));
        mix(static_cast<std::uint64_t>(editor.gateTicksAt(row, channel)));
        mix(static_cast<std::uint64_t>(editor.velocityAt(row, channel)));
        mix(static_cast<std::uint64_t>(editor.retriggerAt(row, channel) ? 1 : 0));
        mix(static_cast<std::uint64_t>(editor.effectCommandAt(row, channel)));
        mix(static_cast<std::uint64_t>(editor.effectValueAt(row, channel)));
      }
    }
    return hash;
  }

  void captureUndoHistoryIfPatternChanged() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }

    const std::size_t patternIndex = app.module.currentPattern();
    auto& editor = app.module.currentEditor();
    const int rows = static_cast<int>(editor.rows());
    const int channels = static_cast<int>(editor.channels());
    const std::uint64_t hash = computePatternHashLocked(editor);

    if (undoHistoryInitialized &&
        patternIndex == lastUndoObservedPatternIndex &&
        hash == lastUndoObservedHash) {
      return;
    }

    UndoHistoryEntry entry;
    entry.patternIndex = patternIndex;
    entry.rows = rows;
    entry.channels = channels;
    entry.hash = hash;
    entry.snapshot.assign(static_cast<std::size_t>(rows * channels), extracker::PatternEditor::Step{});
    for (int row = 0; row < rows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        entry.snapshot[static_cast<std::size_t>(row * channels + channel)] = readStep(row, channel);
      }
    }

    const juce::String stamp = juce::Time::getCurrentTime().formatted("%H:%M:%S");
    entry.label = stamp + "  P" + juce::String(static_cast<int>(patternIndex + 1));
    if (!undoHistoryInitialized) {
      entry.label += "  baseline";
    }

    undoHistoryEntries.push_back(std::move(entry));
    constexpr std::size_t kMaxEntries = 48;
    while (undoHistoryEntries.size() > kMaxEntries) {
      undoHistoryEntries.erase(undoHistoryEntries.begin());
    }

    undoHistoryInitialized = true;
    lastUndoObservedHash = hash;
    lastUndoObservedPatternIndex = patternIndex;

    lock.unlock();
    refreshUndoHistoryPanel();
  }

  void refreshUndoHistoryPanel() {
    juce::String timeline;
    undoHistoryDisplayToIndex.clear();

    if (undoHistoryEntries.empty()) {
      undoHistoryTimelineLabel.setText("No edits yet", juce::dontSendNotification);
      suppressUndoHistorySelectionCallback = true;
      undoHistorySelector.clear(juce::dontSendNotification);
      suppressUndoHistorySelectionCallback = false;
      return;
    }

    const int start = std::max(0, static_cast<int>(undoHistoryEntries.size()) - 6);
    for (int i = static_cast<int>(undoHistoryEntries.size()) - 1; i >= start; --i) {
      timeline += juce::String(static_cast<int>(undoHistoryEntries.size() - static_cast<std::size_t>(i))) + ". " +
                  undoHistoryEntries[static_cast<std::size_t>(i)].label;
      if (i > start) {
        timeline += "\n";
      }
    }
    undoHistoryTimelineLabel.setText(timeline, juce::dontSendNotification);

    suppressUndoHistorySelectionCallback = true;
    undoHistorySelector.clear(juce::dontSendNotification);
    int itemId = 1;
    for (int i = static_cast<int>(undoHistoryEntries.size()) - 1; i >= 0; --i) {
      undoHistorySelector.addItem(undoHistoryEntries[static_cast<std::size_t>(i)].label, itemId++);
      undoHistoryDisplayToIndex.push_back(i);
    }
    if (undoHistorySelector.getNumItems() > 0) {
      undoHistorySelector.setSelectedId(1, juce::dontSendNotification);
    }
    suppressUndoHistorySelectionCallback = false;
  }

  void restoreUndoHistorySelection() {
    const int selectedIdx = undoHistorySelector.getSelectedItemIndex();
    if (selectedIdx < 0 || selectedIdx >= static_cast<int>(undoHistoryDisplayToIndex.size())) {
      return;
    }
    const int historyIndex = undoHistoryDisplayToIndex[static_cast<std::size_t>(selectedIdx)];
    if (historyIndex < 0 || historyIndex >= static_cast<int>(undoHistoryEntries.size())) {
      return;
    }

    const auto entry = undoHistoryEntries[static_cast<std::size_t>(historyIndex)];

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      pluginStatusLabel.setText("Restore skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    if (!app.module.switchToPattern(entry.patternIndex)) {
      pluginStatusLabel.setText("Restore failed (invalid pattern)", juce::dontSendNotification);
      return;
    }
    auto& editor = app.module.currentEditor();
    if (static_cast<int>(editor.rows()) != entry.rows || static_cast<int>(editor.channels()) != entry.channels) {
      pluginStatusLabel.setText("Restore failed (pattern size mismatch)", juce::dontSendNotification);
      return;
    }

    for (int row = 0; row < entry.rows; ++row) {
      for (int channel = 0; channel < entry.channels; ++channel) {
        writeStep(row,
                  channel,
                  entry.snapshot[static_cast<std::size_t>(row * entry.channels + channel)]);
      }
    }

    app.currentPatternCache.store(app.module.currentPattern());
    app.patternCountCache.store(app.module.patternCount());
    app.currentSongOrderPositionCache.store(app.module.firstSongEntryForPattern(app.module.currentPattern()));
    lastUndoObservedPatternIndex = entry.patternIndex;
    lastUndoObservedHash = entry.hash;
    undoHistoryInitialized = true;

    lock.unlock();
    refreshPatternView();
    patternGrid.repaint();
    pluginStatusLabel.setText("Restored snapshot: " + entry.label, juce::dontSendNotification);
  }

  void capturePatternCompareA() {
    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      patternCompareStatusLabel.setText("A capture skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    auto& editor = app.module.currentEditor();
    const int rows = static_cast<int>(editor.rows());
    const int channels = static_cast<int>(editor.channels());
    patternCompareSnapshot.assign(static_cast<std::size_t>(rows * channels), extracker::PatternEditor::Step{});
    patternCompareRows = rows;
    patternCompareChannels = channels;
    patternComparePatternIndex = app.module.currentPattern();

    for (int row = 0; row < rows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        patternCompareSnapshot[static_cast<std::size_t>(row * channels + channel)] = readStep(row, channel);
      }
    }

    patternCompareHasSnapshot = true;
    patternCompareShowingA = true;
    lock.unlock();

    updatePatternCompareStatus();
    pluginStatusLabel.setText("Captured compare A snapshot", juce::dontSendNotification);
  }

  void togglePatternCompareAB() {
    if (!patternCompareHasSnapshot) {
      updatePatternCompareStatus();
      return;
    }

    std::unique_lock<std::mutex> lock(app.stateMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      patternCompareStatusLabel.setText("A/B toggle skipped (engine busy)", juce::dontSendNotification);
      return;
    }

    if (app.module.currentPattern() != patternComparePatternIndex) {
      patternCompareStatusLabel.setText("A/B snapshot is for another pattern", juce::dontSendNotification);
      return;
    }

    auto& editor = app.module.currentEditor();
    const int rows = static_cast<int>(editor.rows());
    const int channels = static_cast<int>(editor.channels());
    if (rows != patternCompareRows || channels != patternCompareChannels) {
      patternCompareStatusLabel.setText("A/B snapshot size mismatch; recapture A", juce::dontSendNotification);
      patternCompareHasSnapshot = false;
      lock.unlock();
      updatePatternCompareStatus();
      return;
    }

    std::vector<extracker::PatternEditor::Step> currentSnapshot(static_cast<std::size_t>(rows * channels));
    for (int row = 0; row < rows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        currentSnapshot[static_cast<std::size_t>(row * channels + channel)] = readStep(row, channel);
      }
    }

    for (int row = 0; row < rows; ++row) {
      for (int channel = 0; channel < channels; ++channel) {
        writeStep(row, channel, patternCompareSnapshot[static_cast<std::size_t>(row * channels + channel)]);
      }
    }

    patternCompareSnapshot.swap(currentSnapshot);
    patternCompareShowingA = !patternCompareShowingA;
    lock.unlock();

    patternGrid.repaint();
    updateStepEditorFromSelection();
    updatePatternCompareStatus();
  }

  void consumeMidiEditorCcUpdates() {
    auto scaleToByte = [](int value0to127) {
      return static_cast<int>(std::lround((std::clamp(value0to127, 0, 127) / 127.0) * 255.0));
    };

    const int velocityValue = app.midiEditorPendingValues[0].exchange(-1);
    const int gateValue = app.midiEditorPendingValues[1].exchange(-1);
    const int effectCommandValue = app.midiEditorPendingValues[2].exchange(-1);
    const int effectValue = app.midiEditorPendingValues[3].exchange(-1);

    if (velocityValue < 0 && gateValue < 0 && effectCommandValue < 0 && effectValue < 0) {
      return;
    }

    if (velocityValue >= 0) {
      const int scaled = std::clamp(velocityValue, 1, 127);
      suppressStepSliderCallbacks = true;
      stepVelocitySlider.setValue(static_cast<double>(scaled), juce::dontSendNotification);
      suppressStepSliderCallbacks = false;
      pendingStepVelocity = scaled;
      patternGrid.setInsertDefaults(static_cast<std::uint32_t>(std::max(stepGateSlider.getValue(), 0.0)),
                                    static_cast<std::uint8_t>(scaled));
    }

    if (gateValue >= 0) {
      const int scaled = static_cast<int>(std::lround((std::clamp(gateValue, 0, 127) / 127.0) * 32.0));
      suppressStepSliderCallbacks = true;
      stepGateSlider.setValue(static_cast<double>(scaled), juce::dontSendNotification);
      suppressStepSliderCallbacks = false;
      pendingStepGate = scaled;
      patternGrid.setInsertDefaults(static_cast<std::uint32_t>(scaled),
                                    static_cast<std::uint8_t>(std::max(stepVelocitySlider.getValue(), 1.0)));
    }

    if (effectCommandValue >= 0) {
      const int scaled = scaleToByte(effectCommandValue);
      suppressStepSliderCallbacks = true;
      stepEffectCommandSlider.setValue(static_cast<double>(scaled), juce::dontSendNotification);
      suppressStepSliderCallbacks = false;
      pendingStepEffectCommand = scaled;
      suppressStepEffectTextCallbacks = true;
      stepEffectCommandHexEditor.setText(formatHexByte(scaled), false);
      suppressStepEffectTextCallbacks = false;
    }

    if (effectValue >= 0) {
      const int scaled = scaleToByte(effectValue);
      suppressStepSliderCallbacks = true;
      stepEffectValueSlider.setValue(static_cast<double>(scaled), juce::dontSendNotification);
      suppressStepSliderCallbacks = false;
      pendingStepEffectValue = scaled;
      suppressStepEffectTextCallbacks = true;
      stepEffectValueHexEditor.setText(formatHexByte(scaled), false);
      suppressStepEffectTextCallbacks = false;
    }
  }

  void refreshMidiLearnStatus() {
    auto formatMapping = [](int code) {
      if (code < 0) {
        return juce::String("--");
      }
      const int channel = std::clamp(code / 128, 0, 15);
      const int controller = std::clamp(code % 128, 0, 127);
      return juce::String("Ch") + juce::String(channel + 1) + " CC" + juce::String(controller);
    };

    const juce::String velocityText = "Learn Velocity [" +
        formatMapping(app.midiEditorCcMappingCode(ExTrackerApp::MidiEditorAction::Velocity)) + "]";
    const juce::String gateText = "Learn Gate [" +
        formatMapping(app.midiEditorCcMappingCode(ExTrackerApp::MidiEditorAction::Gate)) + "]";
    const juce::String effectCommandText = "Learn FX Cmd [" +
        formatMapping(app.midiEditorCcMappingCode(ExTrackerApp::MidiEditorAction::EffectCommand)) + "]";
    const juce::String effectValueText = "Learn FX Val [" +
        formatMapping(app.midiEditorCcMappingCode(ExTrackerApp::MidiEditorAction::EffectValue)) + "]";

    if (midiLearnVelocityButton.getButtonText() != velocityText) {
      midiLearnVelocityButton.setButtonText(velocityText);
    }
    if (midiLearnGateButton.getButtonText() != gateText) {
      midiLearnGateButton.setButtonText(gateText);
    }
    if (midiLearnEffectCommandButton.getButtonText() != effectCommandText) {
      midiLearnEffectCommandButton.setButtonText(effectCommandText);
    }
    if (midiLearnEffectValueButton.getButtonText() != effectValueText) {
      midiLearnEffectValueButton.setButtonText(effectValueText);
    }
  }

  ExTrackerApp& app;
  juce::TextButton playButton;
  juce::TextButton stopButton;
  juce::TextButton recordButton{"Record"};
  juce::TextButton playFromCursorButton{"Rec >"};
  juce::TextButton overdubButton{"Overdub: Off"};
  juce::TextButton punchButton{"Punch: Off"};
  juce::Label recordStartRowLabel;
  juce::Slider recordStartRowSlider;
  juce::Label recordStepLabel;
  juce::Slider recordStepSlider;
  juce::TextButton playModePatternButton{"Pattern"};
  juce::TextButton playModeSongButton{"Song"};
  juce::TextButton loopButton;
  juce::TextButton helpButton{"Help"};
  juce::TextButton darkModeButton{"Dark: Off"};
  juce::TextButton pianoToggleButton{"Keys: On"};
  juce::Label patternLabel;
  juce::ComboBox patternSelector;
  juce::Label startupTemplateLabel;
  juce::ComboBox startupTemplateSelector;
  juce::TextButton startupTemplatePreviewButton{"Preview"};
  juce::TextButton startupTemplateSetDefaultButton{"Set Default"};
  juce::TextButton insertPatternBeforeButton{"Insert Before"};
  juce::TextButton insertPatternAfterButton{"Insert After"};
  juce::TextButton removePatternButton{"Remove Pattern"};
  juce::TextButton gridDensityButton;
  juce::TextButton applyChannelMapButton;
  juce::Slider tempoSlider;
  juce::Label tempoLabel;
  juce::Label swingLabel;
  juce::Slider swingSlider;
  juce::Label volumeLabel;
  juce::Slider volumeSlider;
  juce::Label ticksPerBeatLabel;
  juce::Slider ticksPerBeatSlider;
  juce::Label ticksPerRowLabel;
  juce::Slider ticksPerRowSlider;
  juce::TextButton expandPatternButton{"Expand x2"};
  juce::TextButton shrinkPatternButton{"Shrink x2"};
  juce::TextButton expandChannelButton{"Ch+"};
  juce::TextButton shrinkChannelButton{"Ch-"};
  juce::TextButton newModuleButton{"New Song"};
  juce::TextButton savePatternButton{"Save Song"};
  juce::TextButton loadPatternButton{"Load Song"};
  juce::TextButton insertRowButton{"Insert Row"};
  juce::TextButton removeRowButton{"Remove Row"};
  juce::TextButton rowEditScopeButton;
  juce::TextButton insertSwingModeButton;
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
  juce::Label songArrangerTitle;
  juce::Label songArrangerBarsLabel;
  juce::Slider songArrangerBarsSlider;
  juce::TextButton songArrangerDuplicateButton{"Duplicate Section"};
  juce::TextButton songArrangerInsertBarsButton{"Insert Bars"};
  juce::TextButton songArrangerRippleLeftButton{"Ripple Left"};
  juce::TextButton songArrangerRippleRightButton{"Ripple Right"};
  juce::Label moduleMessageTitle;
  juce::TextEditor moduleMessageEditor;
  juce::Label moduleMessageCounterLabel;
  juce::Label moduleMessageStateLabel;
  juce::TextButton moduleMessageApplyButton{"Apply Message"};
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
  juce::TextButton sampleRouteKeystationButton{"Keystation >"};
  juce::TextButton sampleArmButton{"Arm for Notes"};
  juce::Label sampleArmChannelLabel;
  juce::ComboBox sampleArmChannelSelector;
  juce::TextEditor sampleRenameEditor;
  juce::TextButton sampleRenameButton{"Rename"};
  juce::TextButton sampleClearButton{"Clear"};
  juce::Label sampleTrimStartLabel;
  juce::Slider sampleTrimStartSlider;
  juce::Label sampleTrimEndLabel;
  juce::Slider sampleTrimEndSlider;
  juce::TextButton sampleTrimApplyButton{"Apply Trim"};
  juce::TextButton sampleTrimReloadButton{"Reload Source"};
  juce::TextButton sampleNormalizeButton{"Normalize"};
  juce::TextButton sampleFadeInButton{"Fade In"};
  juce::TextButton sampleFadeOutButton{"Fade Out"};
  juce::TextButton sampleReverseButton{"Reverse"};
  juce::ComboBox sampleRateSelector;
  juce::TextButton sampleResampleButton{"Resample"};
  juce::ComboBox sampleBitDepthSelector;
  juce::TextButton sampleBitDepthButton{"Bit Crush"};
  juce::TextButton sampleLoopXfadeButton{"Loop XFade"};
  SampleWaveformView sampleWaveformView;
  juce::Label samplePathLabel;
  juce::Label sampleVolumeLabel;
  juce::Slider sampleVolumeSlider;
  juce::Label samplePanLabel;
  juce::Slider samplePanSlider;
  juce::Label sampleTransposeLabel;
  juce::Slider sampleTransposeSlider;
  juce::Label sampleLoopModeLabel;
  juce::ComboBox sampleLoopModeBox;
  juce::Label stepEditorTitle;
  juce::Label selectedStepLabel;
  juce::Label patternSearchTitle;
  juce::ComboBox patternSearchMode;
  juce::TextEditor patternSearchValue;
  juce::TextButton patternSearchPrevButton{"Find Prev"};
  juce::TextButton patternSearchNextButton{"Find Next"};
  juce::Label patternSearchStatusLabel;
  juce::Label patternCompareTitle;
  juce::TextButton patternCompareCaptureButton{"Capture A"};
  juce::TextButton patternCompareToggleButton{"Toggle A/B"};
  juce::Label patternCompareStatusLabel;
  juce::TextButton copyBlockButton{"Copy"};
  juce::TextButton cutBlockButton{"Cut"};
  juce::TextButton pasteBlockButton{"Paste"};
  juce::TextButton transposeDownButton{"Transpose -"};
  juce::TextButton transposeUpButton{"Transpose +"};
  juce::TextButton applyFxToBlockButton{"Apply FX to Block"};
  juce::Label patternMacroTitle;
  juce::TextButton patternMacroFillHatsButton{"Fill Hats"};
  juce::TextButton patternMacroAccent4Button{"Accent Every 4th"};
  juce::TextButton patternMacroInvertVelocityButton{"Invert Velocities"};
  juce::Label undoHistoryTitle;
  juce::Label undoHistoryTimelineLabel;
  juce::ComboBox undoHistorySelector;
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
  juce::Label midiLearnTitle;
  juce::TextButton midiLearnVelocityButton{"Learn Velocity"};
  juce::TextButton midiLearnGateButton{"Learn Gate"};
  juce::TextButton midiLearnEffectCommandButton{"Learn FX Cmd"};
  juce::TextButton midiLearnEffectValueButton{"Learn FX Val"};
  juce::TextButton midiLearnClearButton{"Clear MIDI Learn"};
  juce::Label midiLearnStatusLabel;
  juce::Label keyboardOctaveLabel;
  juce::Slider keyboardOctaveSlider;
  juce::Label editStepLabel;
  juce::Slider editStepSlider;
  juce::Label notePreviewMsLabel;
  juce::Slider notePreviewMsSlider;
  juce::ToggleButton followPreviewToggle;
  juce::Label slotLabel;
  juce::ComboBox slotSelector;
  juce::ComboBox pluginSelector;
  juce::TextButton scanPluginsButton{"Scan LV2/VST3"};
  juce::TextButton assignPluginButton{"Assign Plugin To Slot"};
  juce::TextButton loadInstrumentFileButton{"Load File Instrument..."};
  juce::TextButton openPluginEditorButton{"Open Plugin Editor"};
  juce::Label gainLabel;
  juce::Slider gainSlider;
  juce::Label attackLabel;
  juce::Slider attackSlider;
  juce::Label releaseLabel;
  juce::Slider releaseSlider;
  // Per-instrument pitch and depth
  juce::Label pitchLabel;
  juce::Slider pitchSlider;
  juce::Label depthLabel;
  juce::Slider depthSlider;
  juce::Label instrumentRootLabel;
  juce::Slider instrumentRootSlider;
  juce::Label instrumentPanLabel;
  juce::Slider instrumentPanSlider;
  juce::Label instrumentLoopModeLabel;
  juce::ComboBox instrumentLoopModeBox;
  juce::Label instrumentLoopStartLabel;
  juce::Slider instrumentLoopStartSlider;
  juce::Label instrumentLoopEndLabel;
  juce::Slider instrumentLoopEndSlider;
  // Per-instrument effects
  juce::TextButton fxResetButton;
  juce::Label fxSectionLabel;
  juce::Label fxDelayLabel;
  juce::Slider fxDelayTimeSlider;
  juce::Slider fxDelayFeedbackSlider;
  juce::Slider fxDelayWetSlider;
  juce::Label fxDistLabel;
  juce::ComboBox fxDistTypeBox;
  juce::Slider fxDistDriveSlider;
  juce::Label fxChorusLabel;
  juce::Slider fxChorusRateSlider;
  juce::Slider fxChorusDepthSlider;
  juce::Slider fxChorusWetSlider;
  // Per-instrument reverb send
  juce::Label fxReverbSendLabel;
  juce::Slider fxReverbSendSlider;
  // Global reverb parameters
  juce::Label reverbSectionLabel;
  juce::Slider reverbRoomSlider;
  juce::Slider reverbDampSlider;
  juce::Slider reverbWetSlider;
  juce::Slider reverbWidthSlider;
  // Per-instrument filter
  juce::Label filterSectionLabel;
  juce::Label filterChannelLabel;
  std::vector<std::unique_ptr<juce::ToggleButton>> filterChannelToggles;
  juce::ComboBox filterTypeBox;
  juce::Label filterCutoffLabel;
  juce::Slider filterCutoffSlider;
  juce::Label filterResonanceLabel;
  juce::Slider filterResonanceSlider;
  juce::Label controlPortSectionTitle;
  struct ControlPortRow {
    juce::Label label;
    juce::Slider slider;
    std::string paramName;
  };
  std::vector<std::unique_ptr<ControlPortRow>> controlPortRows;
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
  int lastTrimSelectionSampleSlot = -1;
  std::string lastTrimSelectionSamplePath;
  bool isEditingSampleTrimSelection = false;
  std::unique_ptr<juce::FileChooser> sampleFileChooser;
  std::unique_ptr<juce::FileChooser> loadInstrumentFileChooser;
  std::unique_ptr<juce::FileChooser> savePatternFileChooser;
  std::unique_ptr<juce::FileChooser> loadPatternFileChooser;
  juce::File lastSampleLoadFolder;
  std::vector<juce::File> recentSampleLoadFolders;
  juce::File lastSongFile;
  juce::String cachedStatusText;
  juce::String moduleMessageSavedSnapshot;
  std::vector<extracker::PatternEditor::Step> patternCompareSnapshot;
  bool isSongDirty = false;
  bool needsToSaveBeforeQuit = false;
  struct UndoHistoryEntry {
    std::size_t patternIndex = 0;
    int rows = 0;
    int channels = 0;
    std::vector<extracker::PatternEditor::Step> snapshot;
    juce::String label;
    std::uint64_t hash = 0;
  };
  std::vector<UndoHistoryEntry> undoHistoryEntries;
  std::vector<int> undoHistoryDisplayToIndex;
  std::uint64_t lastUndoObservedHash = 0;
  std::size_t lastUndoObservedPatternIndex = 0;
  bool undoHistoryInitialized = false;
  bool suppressUndoHistorySelectionCallback = false;
  int patternCompareRows = 0;
  int patternCompareChannels = 0;
  std::size_t patternComparePatternIndex = 0;
  bool patternCompareHasSnapshot = false;
  bool patternCompareShowingA = false;
  int selectedStepRow = -1;
  int selectedStepChannel = -1;
  int pendingStepVelocity = -1;
  int pendingStepGate = -1;
  int pendingStepEffectCommand = -1;
  int pendingStepEffectValue = -1;
  bool suppressStepSliderCallbacks = false;
  bool suppressStepEffectTextCallbacks = false;
  bool suppressKeyboardStateCallbacks = false;
  bool suppressInstrumentSampleEditorCallbacks = false;
  bool suppressPatternRowSliderCallback = false;
  int refreshTickCounter = 0;
  bool lastTransportPlaying = false;
  int lastTransportRow = -1;
  std::chrono::steady_clock::time_point playbackStartTime;
  juce::Viewport patternViewport;
  juce::Slider patternRowSlider;
  juce::Viewport mixerViewport;
  ChannelMixerStrip mixerStrip;
  int mixerLastNumChannels = -1;
  juce::Viewport panelViewport;
  std::unique_ptr<PanelWrapper> panelWrapper;
  PatternGrid patternGrid;
  bool pianoVisible = true;
  MiniPianoKeyboard pianoKeyboard;

  // ── Bounce-to-WAV ──────────────────────────────────────────────────────────
  juce::TextButton bounceWavButton{"Bounce WAV"};
  bool isBouncing = false;
  std::mutex bounceMutex;
  std::vector<float> bounceBufferL;
  std::vector<float> bounceBufferR;
  std::uint32_t bounceSampleRate = 48000;
  juce::File bounceOutputFile;
  std::shared_ptr<juce::FileChooser> bounceFileChooser;
};

void TrackerMainComponent::saveHelpToFile() {
  juce::String screenMsg =
    "=== exTracker HELP ===\n"
    "\n"
    "=== TRANSPORT ===\n"
    "Play / Stop  - toolbar buttons or Space (grid focused)\n"
    "Loop         - toggle pattern looping\n"
    "Tempo slider - BPM (40-240)\n"
    "Swing slider - per-pattern groove (50-75%)\n"
    "Ticks/Beat   - rows per beat (resolution)\n"
    "Ticks/Row    - transport tick length per row\n"
    "Effect 0Fxx  - set tempo to xx BPM at any row\n"
    "\n"
    "=== PATTERN GRID - NAVIGATION ===\n"
    "Click         - select cell\n"
    "Alt+Drag      - audition scrub rows without writing notes\n"
    "Arrow keys    - move selection\n"
    "Tab / Shift+Tab - next / previous channel\n"
    "Return        - advance by step\n"
    "Home / End    - first / last row\n"
    "Page Up/Down  - jump 16 rows\n"
    "\n"
    "=== NOTE ENTRY ===\n"
    "1-9     high semitones (oct+1, semitones 0-8)\n"
    "Q-P     high whole tones (oct+1, white-key offsets)\n"
    "A-L     low semitones (oct, semitones 0-8)\n"
    "Z-.     low whole tones (oct, white-key offsets)\n"
    "+/-     octave up / down\n"
    "[ / ]   step size down / up\n"
    "Shift+O set note-off fadeout (uses current Gate ticks)\n"
    "Del/Bsp clear selected step\n"
    "Right-click  - clear step\n"
    "\n"
    "=== FX DIRECT ENTRY ===\n"
    "`       toggle FX mode (header shows [FX])\n"
    "        Type 3 hex chars: 1 command digit + 2 value digits\n"
    "        e.g. F80 -> cmd=0F, val=80 (set tempo 128 BPM)\n"
    "        Row auto-advances after 3rd digit.\n"
    "Enter   commit partial buffer (zero-pads remaining digits)\n"
    "Bsp     remove last typed digit (or clear effect if empty)\n"
    "Esc     exit FX mode\n"
    "\n"
    "=== VOLUME DIRECT ENTRY ===\n"
    "'       toggle volume mode (header shows [VOL])\n"
    "        Type 2 hex chars: velocity byte in volume column\n"
    "        e.g. 40 = 64, 7F = 127 (clamped to 1-127)\n"
    "        On note rows: sets note velocity; on empty rows: writes Cxx volume FX\n"
    "        Auto-advances by step\n"
    "Enter   commit partial buffer (zero-pads remaining digit)\n"
    "Bsp     remove last typed digit\n"
    "Esc     exit volume mode\n"
    "\n"
    "=== SAMPLE DIRECT ENTRY ===\n"
    ";/F4    toggle sample mode (header shows [SMP])\n"
    "        Type 3 hex chars: sample slot 000-0FF\n"
    "        e.g. 00A = sample slot 10\n"
    "        Auto-advances by step\n"
    "Enter   commit partial buffer (zero-pads remaining digits)\n"
    "Bsp     remove last typed digit (or clear sample if empty)\n"
    "Esc     exit sample mode\n"
    "\n"
    "=== BLOCK EDITING ===\n"
    "Shift+Arrows / Click+Drag  - mark block\n"
    "Ctrl+C / X / V             - copy / cut / paste block\n"
    "Ctrl+Up / Down             - transpose +/-1 semitone\n"
    "Ctrl+Shift+Up / Down       - transpose +/-1 octave\n"
    "Alt+Up / Down              - selected step velocity +/-1\n"
    "Alt+Left / Right           - selected step gate +/-1\n"
    "Alt+Shift+Arrows           - larger velocity/gate nudges\n"
    "Panel: Copy / Cut / Paste / Transpose+/- buttons\n"
    "Panel: Apply FX to Block   - write current FX cmd/val to entire selection\n"
    "Pattern Macros - Fill Hats, Accent 4th, Invert Velocities\n"
    "Undo History - visual timeline with click-to-restore snapshots\n"
    "Song Arranger - duplicate section, insert bars, ripple move in song order\n"
    "\n"
    "=== STEP EDITOR (right panel) ===\n"
    "Velocity    - note velocity (1-127)\n"
    "Gate        - note length in ticks (0 = sustain)\n"
    "FX Cmd      - effect command (slider or hex box)\n"
    "FX Val      - effect value   (slider or hex box)\n"
    "Pattern Search - find next/prev Note, Instrument, or FX Value (Ctrl/Cmd+F4 / +Shift)\n"
    "A/B Compare - capture A and toggle between A snapshot and latest edits\n"
    "MIDI Learn  - map external CC to Velocity/Gate/FX Cmd/FX Val\n"
    "Ctrl+M      - focus Song/Module Message editor\n"
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
    "Per-channel instrument selector - fallback target when no sample slot is armed\n"
    "Sample bank selector - arms the selected sample slot for new notes\n"
    "Waveform editor - drag green handles for trim start/end selection\n"
    "Apply Trim crops to selection (non-destructive source retained in memory)\n"
    "Normalize / Fade In / Fade Out process the current selection\n"
    "Reload Source restores original loaded sample from memory\n"
    "Crop shortcut - Ctrl/Cmd+K while waveform has focus\n"
    "Mute toggle - mute channel\n"
    "Slot Editor - load WAV, adjust gain / attack / release\n"
    "Assign Plugin - load CLAP/LV2/internal plugin to slot\n"
    "\n"
    "=== STARTUP TEMPLATE ===\n"
    "Toolbar selector chooses startup pattern template (Blank/House/Electro)\n"
    "Preview applies template to current pattern without saving default\n"
    "Set Default saves selected template for next launch\n";

  auto homeDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  auto outputFile = homeDir.getChildFile("exTracker_help.txt");
  outputFile.create();
  outputFile.replaceWithText(screenMsg);
  
  juce::AlertWindow::showMessageBoxAsync(
      juce::MessageBoxIconType::InfoIcon,
      "Help Saved",
      "Help text saved to:\n" + outputFile.getFullPathName(),
      "OK", this);
}

}  // namespace

MainWindow::MainWindow(ExTrackerApp& app)
    : DocumentWindow("exTracker", juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId), juce::DocumentWindow::allButtons),
      app(app) {
  setUsingNativeTitleBar(true);

  content = std::make_unique<TrackerMainComponent>(app);
  content->setSize(1200, 600);
  setContentOwned(content.release(), true);

  // Set window properties - use screen bounds to determine reasonable size
  auto& desktop = juce::Desktop::getInstance();
  auto screenBounds = desktop.getDisplays().getMainDisplay().userArea;
  int initialWidth = std::min(1400, screenBounds.getWidth() - 100);
  int initialHeight = std::min(900, screenBounds.getHeight() - 100);
  setSize(initialWidth, initialHeight);
  
  // Set minimum size to ensure UI remains usable via constrainer
  auto* constrainer = getConstrainer();
  if (constrainer) {
    constrainer->setMinimumSize(800, 600);
  }
  
  centreWithSize(getWidth(), getHeight());
  setVisible(true);
}

MainWindow::~MainWindow() = default;

void MainWindow::notifyPatternChanged() {
  if (auto* tracker = dynamic_cast<TrackerMainComponent*>(getContentComponent())) {
    tracker->handlePatternChangedFromPlayback();
  }
}

void MainWindow::autoLoadLastSong() {
  if (auto* tracker = dynamic_cast<TrackerMainComponent*>(getContentComponent())) {
    tracker->autoLoadLastSong();
  }
}

void MainWindow::closeButtonPressed() {
  juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
