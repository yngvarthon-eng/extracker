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

namespace extracker {

enum class PluginWaveform {
  Sine,
  Square
};

struct PluginRenderVoice {
  int midiNote = -1;
  std::uint8_t instrument = 0;
  double frequencyHz = 0.0;
  double phase = 0.0;
  double level = 0.0;
  double targetLevel = 1.0;
  bool releasing = false;
  PluginWaveform waveform = PluginWaveform::Sine;
};

struct PluginRenderState {
  std::mutex mutex;
  std::vector<PluginRenderVoice> voices;
};

class IInstrumentPlugin {
public:
  virtual ~IInstrumentPlugin() = default;

  virtual void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) = 0;
  virtual void noteOff(int midiNote) = 0;
  virtual void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) = 0;

  virtual bool setParameter(const std::string& name, double value) = 0;
  virtual double getParameter(const std::string& name) const = 0;

  virtual std::size_t activeVoiceCount() const = 0;
  virtual double activeVoiceFrequencyHz(std::size_t voiceIndex) const = 0;
};

class IExternalPluginAdapter {
public:
  virtual ~IExternalPluginAdapter() = default;

  virtual std::string adapterName() const = 0;
  virtual std::size_t registerDiscoveredPlugins(class PluginHost& host) = 0;
};

struct PluginPortInfo {
  int audioIn = -1;
  int audioOut = -1;
  int controlInCount = 0;
  int controlOutCount = 0;
  int eventInCount = 0;
};

class PluginHost {
public:
  static constexpr std::size_t kMaxInstrumentSlots = 16;
  using PluginFactory = std::function<std::unique_ptr<IInstrumentPlugin>()>;

  PluginHost();

  std::string status() const;
  std::vector<std::string> discoverAvailablePlugins() const;
  void registerExternalAdapter(std::unique_ptr<IExternalPluginAdapter> adapter);
  std::size_t rescanExternalPlugins();
  std::vector<std::string> externalAdapterNames() const;
  bool registerPluginFactory(const std::string& pluginId, PluginFactory factory);
  void registerPluginPortInfo(const std::string& pluginId, PluginPortInfo info);
  bool getPluginPortInfo(const std::string& pluginId, PluginPortInfo& out) const;
  bool loadPlugin(const std::string& id);
  bool assignInstrument(std::uint8_t instrument, const std::string& pluginId);
  bool hasInstrumentAssignment(std::uint8_t instrument) const;
  std::string pluginForInstrument(std::uint8_t instrument) const;
  bool triggerNoteOn(std::uint8_t instrument, int midiNote, std::uint8_t velocity, bool retrigger);
  bool triggerNoteOff(std::uint8_t instrument, int midiNote);
  bool renderInterleaved(std::vector<double>& monoBuffer, std::uint32_t sampleRate);
  bool setInstrumentParameter(std::uint8_t instrument, const std::string& name, double value);
  double getInstrumentParameter(std::uint8_t instrument, const std::string& name) const;
  std::size_t noteOnEventCount() const;
  std::size_t noteOffEventCount() const;
  std::size_t activeRenderVoiceCount() const;
  double activeRenderVoiceFrequencyHz(std::size_t voiceIndex) const;

private:
  bool isValidInstrument(std::uint8_t instrument) const;
  bool hasPluginFactory(const std::string& pluginId) const;
  std::unique_ptr<IInstrumentPlugin> createPluginInstance(const std::string& pluginId) const;

  std::array<std::string, kMaxInstrumentSlots> instrumentSlots_;
  std::array<std::unique_ptr<IInstrumentPlugin>, kMaxInstrumentSlots> instrumentPlugins_;
  std::unordered_set<std::string> loadedPluginIds_;
  std::vector<std::string> availablePluginIds_;
  std::unordered_map<std::string, PluginPortInfo> pluginPortInfoMap_;
  std::unordered_map<std::string, PluginFactory> pluginFactories_;
  std::vector<std::unique_ptr<IExternalPluginAdapter>> externalAdapters_;
  std::size_t loadedPluginCount_;
  std::size_t noteOnEventCount_;
  std::size_t noteOffEventCount_;
};

}  // namespace extracker
