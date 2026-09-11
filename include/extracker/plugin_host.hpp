#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <array>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "extracker/biquad_filter.hpp"
#include "extracker/instrument_effects.hpp"

namespace extracker {

enum class PluginWaveform {
  Sine,
  Square
};

struct PluginRenderVoice {
  int midiNote = -1;
  std::uint8_t instrument = 0;
  double frequencyHz = 0.0;
  double pan = 0.5;
  double phase = 0.0;
  double level = 0.0;
  double targetLevel = 1.0;
  bool releasing = false;
  PluginWaveform waveform = PluginWaveform::Sine;
  BiquadState filterState;
};

static constexpr std::size_t kMaxInstrumentSlotsForFilter = 16;

struct PluginRenderState {
  std::mutex mutex;
  std::vector<PluginRenderVoice> voices;
  std::array<BiquadParams,           kMaxInstrumentSlotsForFilter> filterParams{};
  std::array<BiquadCoeffs,           kMaxInstrumentSlotsForFilter> filterCoeffs{};
  std::array<bool,                   kMaxInstrumentSlotsForFilter> filterDirty{};
  std::array<InstrumentEffectParams, kMaxInstrumentSlotsForFilter> effectParams{};
  std::array<InstrumentEffectState,  kMaxInstrumentSlotsForFilter> effectState{};
  std::array<float,                  kMaxInstrumentSlotsForFilter> pitchSemitones{};
  std::array<float,                  kMaxInstrumentSlotsForFilter> depthOffsets{};  // 0=front 1=rear
};

struct PluginTransportContext {
  bool   isPlaying          = false;
  double tempoBpm           = 120.0;
  int    timeSigNumerator   = 4;
  int    timeSigDenominator = 4;
};

class IInstrumentPlugin {
public:
  virtual ~IInstrumentPlugin() = default;

  virtual void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) = 0;
  virtual void noteOff(int midiNote) = 0;
  virtual void allNotesOff() = 0;
  virtual void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) = 0;
  virtual void setTransportContext(const PluginTransportContext& /*ctx*/, int64_t /*projSamples*/) {}

  virtual bool setParameter(const std::string& name, double value) = 0;
  virtual double getParameter(const std::string& name) const = 0;
  virtual std::vector<std::string> listParameters() const { return {}; }

  virtual std::size_t activeVoiceCount() const = 0;
  virtual double activeVoiceFrequencyHz(std::size_t voiceIndex) const = 0;

  virtual bool savePreset(const std::string& /*path*/) const { return false; }
  virtual bool loadPreset(const std::string& /*path*/) { return false; }
  virtual bool exportSamples(const std::string& /*outputDir*/) const { return false; }
  virtual void* openEditor() { return nullptr; }
  virtual void  closeEditor() {}
  virtual bool attachEditor(void* /*nativeWindowHandle*/, const char* /*platformType*/) { return false; }
  virtual bool getEditorPreferredSize(int& /*w*/, int& /*h*/) { return false; }
};

class IEffectPlugin {
public:
  virtual ~IEffectPlugin() = default;
  virtual void process(std::vector<double>& monoBuffer, std::uint32_t sampleRate) = 0;
  virtual bool setParameter(const std::string& name, double value) = 0;
  virtual double getParameter(const std::string& name) const = 0;
  virtual std::string name() const = 0;
};

class IExternalPluginAdapter {
public:
  virtual ~IExternalPluginAdapter() = default;

  virtual std::string adapterName() const = 0;
  virtual std::size_t registerDiscoveredPlugins(class PluginHost& host) = 0;
};

struct PluginControlPortMeta {
  int index = -1;
  std::string symbol;
  std::string label;
  float minVal = 0.0f;
  float maxVal = 1.0f;
  float defaultVal = 0.0f;
  bool hasMin = false;
  bool hasMax = false;
  bool hasDefault = false;
};

struct PluginPortInfo {
  int audioIn = -1;
  int audioOut = -1;
  int audioOut2 = -1;  // second audio output for stereo plugins
  int controlInCount = 0;
  int controlOutCount = 0;
  int eventInCount = 0;
  std::vector<PluginControlPortMeta> controlInMeta;
  std::vector<PluginControlPortMeta> controlOutMeta;
};

class PluginHost {
public:
  static constexpr std::size_t kMaxInstrumentSlots = 16;
  static constexpr std::size_t kMaxSampleSlots = 257; // Slots 0..256
  static constexpr std::size_t kMaxEffectSlots = 8;
  using PluginFactory = std::function<std::unique_ptr<IInstrumentPlugin>()>;
  using EffectFactory = std::function<std::unique_ptr<IEffectPlugin>()>;

  PluginHost();

  void unloadAll();
  void clearInstrumentSlots();

  std::string status() const;
  std::vector<std::string> discoverAvailablePlugins() const;
  void registerExternalAdapter(std::unique_ptr<IExternalPluginAdapter> adapter);
  std::size_t rescanExternalPlugins();
  std::size_t rescanLv2Only();
  std::vector<std::string> externalAdapterNames() const;
  bool registerPluginFactory(const std::string& pluginId, PluginFactory factory);
  void registerPluginDisplayName(const std::string& pluginId, const std::string& displayName);
  void registerPluginPortInfo(const std::string& pluginId, PluginPortInfo info);
  bool getPluginPortInfo(const std::string& pluginId, PluginPortInfo& out) const;
  bool loadPlugin(const std::string& id);
  bool assignInstrument(std::uint8_t instrument, const std::string& pluginId);
  bool hasInstrumentAssignment(std::uint8_t instrument) const;
  std::string pluginForInstrument(std::uint8_t instrument) const;
  bool triggerNoteOn(std::uint8_t instrument, int midiNote, std::uint8_t velocity, bool retrigger);
  bool triggerNoteOff(std::uint8_t instrument, int midiNote);
  bool triggerNoteOnResolved(std::uint8_t instrument,
                             std::uint16_t sampleSlot,
                             int midiNote,
                             std::uint8_t velocity,
                             bool retrigger);
  bool triggerNoteOffResolved(std::uint8_t instrument, std::uint16_t sampleSlot, int midiNote);
  void allNotesOff();
  void setTransportContext(const PluginTransportContext& ctx);
  bool renderInterleaved(std::vector<double>& monoBuffer, std::uint32_t sampleRate);
  // Fills one mono buffer per instrument slot (filter+effects already applied).
  // Returns true if at least one instrument had active voices.
  bool renderPerInstrument(
      std::array<std::vector<double>, kMaxInstrumentSlots>& instrBuffers,
      std::uint32_t sampleRate);
  bool setInstrumentParameter(std::uint8_t instrument, const std::string& name, double value);
  double getInstrumentParameter(std::uint8_t instrument, const std::string& name) const;

  void setInstrumentFilter(std::uint8_t instrument, BiquadType type, float cutoffNorm, float resonanceNorm);
  void clearInstrumentFilter(std::uint8_t instrument);
  BiquadParams getInstrumentFilterParams(std::uint8_t instrument) const;

  void setInstrumentEffects(std::uint8_t instrument, const InstrumentEffectParams& p);
  void clearInstrumentEffects(std::uint8_t instrument);
  InstrumentEffectParams getInstrumentEffectParams(std::uint8_t instrument) const;

  void setInstrumentPitch(std::uint8_t instrument, float semitones);
  float getInstrumentPitch(std::uint8_t instrument) const;

  void setInstrumentReverbSend(std::uint8_t instrument, float send);
  float getInstrumentReverbSend(std::uint8_t instrument) const;

  void setInstrumentDepth(std::uint8_t instrument, float depth);
  float getInstrumentDepth(std::uint8_t instrument) const;
  std::vector<std::string> listInstrumentParameters(std::uint8_t instrument) const;
  bool saveInstrumentPreset(std::uint8_t instrument, const std::string& path) const;
  bool loadInstrumentPreset(std::uint8_t instrument, const std::string& path);
  bool openPluginEditor(std::uint8_t instrument);
  void closePluginEditor(std::uint8_t instrument);
  bool attachPluginEditorToWindow(std::uint8_t instrument, void* nativeWindowHandle, const char* platformType);
  bool getPluginEditorPreferredSize(std::uint8_t instrument, int& widthOut, int& heightOut);
  bool loadSampleToSlot(std::uint16_t sampleSlot, const std::string& wavPath);
  bool saveSampleFromSlot(std::uint16_t sampleSlot, const std::string& wavPath) const;
  bool clearSampleSlot(std::uint16_t sampleSlot);
  std::string samplePathForSlot(std::uint16_t sampleSlot) const;
  std::size_t sampleFrameCountForSlot(std::uint16_t sampleSlot) const;
  std::uint32_t sampleRateForSlot(std::uint16_t sampleSlot) const;
  std::size_t sampleSourceFrameCountForSlot(std::uint16_t sampleSlot) const;
  bool trimSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive);
  bool restoreSampleSlotSource(std::uint16_t sampleSlot);
  bool normalizeSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive);
  bool fadeInSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive);
  bool fadeOutSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive);
  bool reverseSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive);
  bool resampleSampleSlot(std::uint16_t sampleSlot, std::uint32_t newRate);
  bool bitDepthSampleSlot(std::uint16_t sampleSlot, int bits, std::size_t startFrame, std::size_t endFrameExclusive);
  bool crossfadeLoopSampleSlot(std::uint16_t sampleSlot, std::size_t lengthFrames);
  bool setSampleSlotParameter(std::uint16_t sampleSlot, const std::string& name, double value);
  double getSampleSlotParameter(std::uint16_t sampleSlot, const std::string& name) const;
  std::vector<float> sampleWaveformForSlot(std::uint16_t sampleSlot, std::size_t maxPoints = 2048) const;
  bool setSampleNameForSlot(std::uint16_t sampleSlot, const std::string& name);
  std::string sampleNameForSlot(std::uint16_t sampleSlot) const;
  bool assignSampleSlotToInstrument(std::uint16_t sampleSlot, std::uint8_t instrument);
  int sampleSlotForInstrument(std::uint8_t instrument) const;
  bool loadSampleToInstrument(std::uint8_t instrument, const std::string& wavPath);
  bool loadXpmInstrument(const std::string& xpmPath, std::uint8_t instrument);
  bool loadSfzInstrument(const std::string& sfzPath, std::uint8_t instrument);
  bool loadSf2Instrument(const std::string& sf2Path, std::uint8_t instrument);
  bool loadS3iInstrument(const std::string& path, std::uint8_t instrument);
  bool loadIffSvxInstrument(const std::string& path, std::uint8_t instrument);
  bool loadXiInstrument(const std::string& path, std::uint8_t instrument);
  // Load by plugin ID or file path, auto-detecting type by extension.
  bool loadInstrumentAuto(const std::string& pathOrId, std::uint8_t instrument);
  // Human-readable name for a plugin id (VST3/LV2 display name if known, file
  // stem for path-based instruments, otherwise the id itself).
  std::string pluginDisplayName(const std::string& pluginId) const;
  // Enumerate which MIDI keys have samples in an SF2 drum bank (bank=128, preset=0).
  // Returns empty vector if FluidSynth is unavailable or the file cannot be loaded.
  std::vector<int> enumSF2DrumKeys(const std::string& sf2Path) const;
  bool exportInstrumentSamples(std::uint8_t instrument, const std::string& outputDir);
  bool saveSampleFromInstrument(std::uint8_t instrument, const std::string& wavPath) const;
  bool clearSampleFromInstrument(std::uint8_t instrument);
  std::string samplePathForInstrument(std::uint8_t instrument) const;
  std::size_t activeVoiceCountForInstrument(std::uint8_t instrument) const;
  double activeVoiceFrequencyHzForInstrument(std::uint8_t instrument, std::size_t voiceIndex) const;
  std::size_t noteOnEventCount() const;
  std::size_t noteOffEventCount() const;
  std::size_t activeRenderVoiceCount() const;
  double activeRenderVoiceFrequencyHz(std::size_t voiceIndex) const;

  // Effect chain (global master effects applied after instrument synthesis)
  bool registerEffectFactory(const std::string& pluginId, EffectFactory factory);
  bool assignEffect(std::uint8_t slot, const std::string& pluginId);
  bool removeEffect(std::uint8_t slot);
  bool hasEffectAssignment(std::uint8_t slot) const;
  std::string pluginForEffect(std::uint8_t slot) const;
  bool setEffectParameter(std::uint8_t slot, const std::string& name, double value);
  double getEffectParameter(std::uint8_t slot, const std::string& name) const;
  void renderEffectChain(std::vector<double>& monoBuffer, std::uint32_t sampleRate);

private:
  bool isValidInstrument(std::uint8_t instrument) const;
  bool isValidSampleSlot(std::uint16_t sampleSlot) const;
  bool isValidEffectSlot(std::uint8_t slot) const;
  bool hasPluginFactory(const std::string& pluginId) const;
  std::unique_ptr<IInstrumentPlugin> createPluginInstance(const std::string& pluginId) const;
  std::unique_ptr<IEffectPlugin> createEffectInstance(const std::string& pluginId) const;

  std::array<std::string, kMaxInstrumentSlots> instrumentSlots_;
  std::array<std::unique_ptr<IInstrumentPlugin>, kMaxInstrumentSlots> instrumentPlugins_;
  std::array<int, kMaxInstrumentSlots> instrumentSampleSlots_;
  std::array<std::unique_ptr<IInstrumentPlugin>, kMaxSampleSlots> sampleSlotPlugins_;
  std::array<std::string, kMaxSampleSlots> sampleSlotPaths_;
  std::array<std::string, kMaxSampleSlots> sampleSlotNames_;
  std::unordered_set<std::string> loadedPluginIds_;
  std::vector<std::string> availablePluginIds_;
  std::unordered_map<std::string, PluginPortInfo> pluginPortInfoMap_;
  std::unordered_map<std::string, std::string> pluginDisplayNames_;
  std::unordered_map<std::string, PluginFactory> pluginFactories_;
  std::unordered_map<std::string, EffectFactory> effectFactories_;
  std::array<std::string, kMaxEffectSlots> effectSlots_;
  std::array<std::unique_ptr<IEffectPlugin>, kMaxEffectSlots> effectPlugins_;
  std::vector<std::unique_ptr<IExternalPluginAdapter>> externalAdapters_;
  std::size_t loadedPluginCount_;
  std::size_t noteOnEventCount_;
  std::size_t noteOffEventCount_;
  mutable std::timed_mutex mutex_;
  PluginTransportContext transportCtx_;
  int64_t projectTimeSamples_ = 0;
  std::array<BiquadFilter, kMaxInstrumentSlots> instrumentFilters_;

  struct InstrumentEffectSlot {
    InstrumentEffectParams params;
    InstrumentEffectState  state;
  };
  std::array<InstrumentEffectSlot, kMaxInstrumentSlots> instrumentEffects_{};
  std::array<float, kMaxInstrumentSlotsForFilter> pitchOffsets_{};
  std::array<float, kMaxInstrumentSlotsForFilter> reverbSends_{};
  std::array<float, kMaxInstrumentSlotsForFilter> depthOffsets_{};
};

}  // namespace extracker
