#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "extracker/plugin_host.hpp"

namespace extracker {

class PluginHost;

class AudioEngine {
public:
  enum class BackendKind {
    Auto,
    Alsa,
    Null
  };

  AudioEngine();
  ~AudioEngine();

  void setBackendPreference(BackendKind backend);
  void setSampleRate(std::uint32_t sampleRate);
  void setBufferFrames(std::uint32_t bufferFrames);
  void setPluginHost(PluginHost* pluginHost);
  void noteOn(int midiNote, double frequencyHz, double velocity = 1.0, bool retrigger = false, std::uint8_t instrument = 0);
  void noteOff(int midiNote, std::uint8_t instrument = 0);
  void allNotesOff();
  void setTestToneFrequencyHz(double frequencyHz);
  void setTestToneVoicesHz(const std::vector<double>& frequenciesHz);
  double testToneFrequencyHz() const;
  std::size_t testToneVoiceCount() const;
  double testToneVoiceHz(std::size_t voiceIndex) const;

  std::string status() const;
  std::string backendName() const;
  bool start();
  void stop();
  bool isRunning() const;

private:
  struct Impl;
  Impl* impl_;
};

}  // namespace extracker
