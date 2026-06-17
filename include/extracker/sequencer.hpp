#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

namespace extracker {

class AudioEngine;
class PatternEditor;
class PluginHost;
class Transport;

class Sequencer {
public:
  struct RowNote {
    int midiNote = -1;
    std::size_t channel = 0;
    std::uint8_t instrument = 0;
    std::uint16_t sample = 0xFFFF;  // 0xFFFF means no sample
    std::uint32_t gateTicks = 0;
    std::uint8_t velocity = 100;
    bool retrigger = false;
    bool releasedByGate = false;
    int volumeSlideDelta = 0;
    std::uint8_t arpeggioX = 0;
    std::uint8_t arpeggioY = 0;
    int slideUp = 0;
    int slideDown = 0;
    int tonePortamento = 0;
    std::uint8_t vibratoSpeed = 0;
    std::uint8_t vibratoDepth = 0;
    std::uint8_t tremoloSpeed = 0;
    std::uint8_t tremoloDepth = 0;
    bool legacyFilterEnabled = false;
    bool glissandoEnabled = false;
    std::uint8_t vibratoWaveform = 0;
    std::uint8_t tremoloWaveform = 0;
    std::uint8_t retriggerTicks = 0;
    std::uint8_t noteCutTicks = 0;
    std::uint8_t noteDelayTicks = 0;
    int fineSlideUp = 0;
    int fineSlideDown = 0;
    int fineTuneSemitone = 0;
    std::uint8_t pan = 0x80;
    bool delayedStart = false;
    bool hasStarted = false;
    std::uint32_t lastRetriggerTick = 0;
    double baseFrequencyHz = 0.0;
    double currentFrequencyHz = 0.0;
    double targetFrequencyHz = 0.0;
    double vibratoPhase = 0.0;
    double tremoloPhase = 0.0;
  };

  Sequencer();

  void reset();
  void update(
      const PatternEditor& pattern,
      Transport& transport,
      AudioEngine& audioEngine,
      PluginHost& pluginHost,
      const std::vector<bool>* mutedChannels = nullptr);

  std::uint64_t dispatchCount() const;
  bool consumePatternWrapEvent();
  int activeMidiNote() const;
  std::size_t activeVoiceCount() const;
  int activeMidiNoteAt(std::size_t index) const;
  std::uint8_t panByChannel(std::size_t channel) const;

private:
  static double midiNoteToFrequencyHz(int midiNote);

  std::uint32_t lastObservedRow_;
  std::uint64_t lastObservedTickCount_;
  bool hasObservedRow_;
  std::uint64_t dispatchCount_;
  std::vector<RowNote> activeNotes_;
  std::vector<RowNote> currentRowNotes_;
  std::vector<std::array<std::uint8_t, 16>> effectMemoryByChannel_;
  std::vector<std::uint8_t> lastContinuousEffectCommandByChannel_;
  std::vector<std::uint8_t> panByChannel_;
  std::vector<bool> legacyFilterEnabledByChannel_;
  std::vector<std::uint8_t> funkRepeatTicksByChannel_;
  std::uint32_t rowDelayTargetRow_;
  std::uint8_t rowDelayRowsRemaining_;
  std::vector<std::uint32_t> loopStartRowByChannel_;
  std::vector<std::uint8_t> loopRemainingByChannel_;
  std::vector<std::uint32_t> loopEndRowByChannel_;
  std::vector<bool> glissandoEnabledByChannel_;
  std::vector<std::uint8_t> vibratoWaveformByChannel_;
  std::vector<std::uint8_t> tremoloWaveformByChannel_;
  bool patternWrappedSinceLastQuery_;
  bool suppressNextPatternWrapDetection_;

  static bool sameKey(const RowNote& a, const RowNote& b);
  static bool containsNote(const std::vector<RowNote>& notes, const RowNote& note);
  static int findRowNoteIndex(const std::vector<RowNote>& notes, const RowNote& note);
};

}  // namespace extracker
