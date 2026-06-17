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

class TrackerMainComponent : public juce::Component,
                             private juce::Timer {
public:
  static constexpr int kModuleMessageMaxChars = 2048;

  void paint(juce::Graphics& g) override {
    g.fillAll(app.darkMode ? juce::Colour(0xFF111111) : juce::Colour(0xFF383838));
  }

  bool keyPressed(const juce::KeyPress& key) override {
    if (key == juce::KeyPress::F1Key) {
      saveHelpToFile();
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
    addAndMakeVisible(playModePatternButton);
    addAndMakeVisible(playModeSongButton);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(helpButton);
    addAndMakeVisible(darkModeButton);
    addAndMakeVisible(patternLabel);
    addAndMakeVisible(patternSelector);
    addAndMakeVisible(startupTemplateLabel);
    addAndMakeVisible(startupTemplateSelector);
    addAndMakeVisible(startupTemplatePreviewButton);
    addAndMakeVisible(startupTemplateSetDefaultButton);
    addAndMakeVisible(insertPatternBeforeButton);
    addAndMakeVisible(insertPatternAfterButton);
    addAndMakeVisible(removePatternButton);
    addAndMakeVisible(gridDensityButton);
    addAndMakeVisible(tempoSlider);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(swingLabel);
    addAndMakeVisible(swingSlider);
    addAndMakeVisible(ticksPerBeatLabel);
    addAndMakeVisible(ticksPerBeatSlider);
    addAndMakeVisible(ticksPerRowLabel);
    addAndMakeVisible(ticksPerRowSlider);
    addAndMakeVisible(expandPatternButton);
    addAndMakeVisible(shrinkPatternButton);
    addAndMakeVisible(expandChannelButton);
    addAndMakeVisible(shrinkChannelButton);
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
    addAndMakeVisible(assignPluginButton);
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
    addAndMakeVisible(slotActivityTitle);
    addAndMakeVisible(applyChannelMapButton);
    addAndMakeVisible(patternViewport);
    addAndMakeVisible(patternRowSlider);
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

    ticksPerBeatLabel.setText("TPB", juce::dontSendNotification);
    ticksPerBeatLabel.setJustificationType(juce::Justification::centredLeft);
    ticksPerRowLabel.setText("TPR", juce::dontSendNotification);
    ticksPerRowLabel.setJustificationType(juce::Justification::centredLeft);

    expandPatternButton.onClick = [this]() { expandPattern(); };
    shrinkPatternButton.onClick = [this]() { shrinkPattern(); };
    expandChannelButton.onClick = [this]() { expandChannel(); };
    shrinkChannelButton.onClick = [this]() { shrinkChannel(); };
    savePatternButton.onClick = [this]() { savePattern(); };
    loadPatternButton.onClick = [this]() { loadPattern(); };
    insertRowButton.onClick = [this]() { insertRowAtSelection(); };
    removeRowButton.onClick = [this]() { removeRowAtSelection(); };
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
    patternMacroFillHatsButton.onClick = [this]() { applyPatternMacroFillHats(); };
    patternMacroAccent4Button.onClick = [this]() { applyPatternMacroAccentEveryFourth(); };
    patternMacroInvertVelocityButton.onClick = [this]() { applyPatternMacroInvertVelocities(); };
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
      if (loaded && pluginId == "builtin.sample") {
        const int selectedSampleSlot = getSelectedSampleSlot();
        if (selectedSampleSlot >= 0 &&
            !app.plugins.samplePathForSlot(static_cast<std::uint16_t>(selectedSampleSlot)).empty()) {
          assigned = app.plugins.assignSampleSlotToInstrument(
              static_cast<std::uint16_t>(selectedSampleSlot),
              static_cast<std::uint8_t>(slot));
        }
      } else {
        assigned = loaded && app.plugins.assignInstrument(static_cast<std::uint8_t>(slot), pluginId);
      }

      pluginStatusLabel.setText(
          assigned ? "Assigned: " + juce::String(pluginId)
                   : "Assign failed for: " + juce::String(pluginId),
          juce::dontSendNotification);
      refreshSlotSelector();
        refreshChannelPluginLabels();
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
    startupTemplateLabel.setBounds(toolbar1.removeFromLeft(64));
    startupTemplateSelector.setBounds(toolbar1.removeFromLeft(106));
    toolbar1.removeFromLeft(6);
    startupTemplatePreviewButton.setBounds(toolbar1.removeFromLeft(72));
    toolbar1.removeFromLeft(4);
    startupTemplateSetDefaultButton.setBounds(toolbar1.removeFromLeft(92));
    toolbar1.removeFromLeft(6);
    insertPatternBeforeButton.setBounds(toolbar1.removeFromLeft(95));
    toolbar1.removeFromLeft(4);
    insertPatternAfterButton.setBounds(toolbar1.removeFromLeft(90));
    toolbar1.removeFromLeft(4);
    removePatternButton.setBounds(toolbar1.removeFromLeft(100));
    toolbar1.removeFromLeft(12);
    helpButton.setBounds(toolbar1.removeFromLeft(60));
    toolbar1.removeFromLeft(4);
    darkModeButton.setBounds(toolbar1.removeFromLeft(80));
    toolbar1.removeFromLeft(8);
    statusLabel.setBounds(toolbar1);

    // Toolbar 2: Grid, Tempo, TPB, Pattern controls, Save/Load, Row controls
    gridDensityButton.setBounds(toolbar2.removeFromLeft(132));
    toolbar2.removeFromLeft(12);
    tempoLabel.setBounds(toolbar2.removeFromLeft(55));
    tempoSlider.setBounds(toolbar2.removeFromLeft(220));
    toolbar2.removeFromLeft(10);
    swingLabel.setBounds(toolbar2.removeFromLeft(48));
    swingSlider.setBounds(toolbar2.removeFromLeft(134));
    toolbar2.removeFromLeft(10);
    ticksPerBeatLabel.setBounds(toolbar2.removeFromLeft(36));
    ticksPerBeatSlider.setBounds(toolbar2.removeFromLeft(120));
    toolbar2.removeFromLeft(8);
    ticksPerRowLabel.setBounds(toolbar2.removeFromLeft(36));
    ticksPerRowSlider.setBounds(toolbar2.removeFromLeft(120));
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
    toolbar2.removeFromLeft(4);
    rowEditScopeButton.setBounds(toolbar2.removeFromLeft(120));
    toolbar2.removeFromLeft(4);
    insertSwingModeButton.setBounds(toolbar2.removeFromLeft(130));

    auto contentArea = area.reduced(8, 8);
    // Make panel flexible: min 280px, max 360px, or 25% of available space
    int panelWidth = std::clamp(contentArea.getWidth() / 4, 280, 360);
    auto panelViewportBounds = contentArea.removeFromRight(panelWidth);
    auto patternArea = contentArea;
    auto patternSliderArea = patternArea.removeFromRight(16);
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
            
            if (app.savePatternToFile(filePath)) {
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
            if (app.loadPatternFromFile(filePath)) {
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

    bool isPlayingNow = app.transport.isPlaying();
    int currentRow = static_cast<int>(app.transport.currentRow());

    if (isPlayingNow != lastTransportPlaying || currentRow != lastTransportRow) {
      patternGrid.repaintPlaybackRows(lastTransportRow, currentRow);
      lastTransportPlaying = isPlayingNow;
      lastTransportRow = currentRow;
    }

    syncPatternRowSliderFromViewport();
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

    startupTemplateLabel.setText("Startup", juce::dontSendNotification);
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
    slotActivityTitle.setText("Slot Activity", juce::dontSendNotification);
    slotActivityTitle.setJustificationType(juce::Justification::centredLeft);

    configureParameterSlider(gainSlider, 0.0, 1.0, 0.01);
    configureParameterSlider(attackSlider, 1.0, 500.0, 1.0);
    configureParameterSlider(releaseSlider, 1.0, 1000.0, 1.0);
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
            ? "  |  active write target for new notes"
            : "  |  select this slot to arm it for new notes";
      } else {
        text += "  |  slot 256 is bank-only and not pattern-addressable";
      }
    }

    samplePathLabel.setText(text, juce::dontSendNotification);
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

  void updateRowEditScopeButtonText() {
    rowEditScopeButton.setButtonText(app.module.rowEditAllChannels() ? "Rows: All Ch" : "Rows: One Ch");
  }

  void updateInsertSwingModeButtonText() {
    insertSwingModeButton.setButtonText(
        app.module.inheritSwingOnInsert() ? "Insert Swing: On" : "Insert Swing: Off");
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

    juce::String text = juce::String(playing ? "Play" : "Stop") +
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
  juce::TextButton playModePatternButton{"Pattern"};
  juce::TextButton playModeSongButton{"Song"};
  juce::TextButton loopButton;
  juce::TextButton helpButton{"Help"};
  juce::TextButton darkModeButton{"Dark: Off"};
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
  juce::Label ticksPerBeatLabel;
  juce::Slider ticksPerBeatSlider;
  juce::Label ticksPerRowLabel;
  juce::Slider ticksPerRowSlider;
  juce::TextButton expandPatternButton{"Expand x2"};
  juce::TextButton shrinkPatternButton{"Shrink x2"};
  juce::TextButton expandChannelButton{"Ch+"};
  juce::TextButton shrinkChannelButton{"Ch-"};
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
  int lastTrimSelectionSampleSlot = -1;
  std::string lastTrimSelectionSamplePath;
  bool isEditingSampleTrimSelection = false;
  std::unique_ptr<juce::FileChooser> sampleFileChooser;
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
  bool suppressPatternRowSliderCallback = false;
  int refreshTickCounter = 0;
  bool lastTransportPlaying = false;
  int lastTransportRow = -1;
  juce::Viewport patternViewport;
  juce::Slider patternRowSlider;
  juce::Viewport panelViewport;
  std::unique_ptr<PanelWrapper> panelWrapper;
  PatternGrid patternGrid;
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

void MainWindow::closeButtonPressed() {
  juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
