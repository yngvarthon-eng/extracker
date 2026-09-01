#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "extracker/biquad_filter.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/reverb.hpp"

namespace extracker {

class PluginHost;

class AudioEngine {
public:
  enum class BackendKind {
    Auto,
    Alsa,
    Jack,
    PipeWire,
    Wasapi,
    Null
  };

  AudioEngine();
  ~AudioEngine();

  void setBackendPreference(BackendKind backend);
  void setSampleRate(std::uint32_t sampleRate);
  void setBufferFrames(std::uint32_t bufferFrames);
  void setPluginHost(PluginHost* pluginHost);
  void noteOn(int midiNote,
              double frequencyHz,
              double velocity = 1.0,
              bool retrigger = false,
              std::uint8_t instrument = 0,
              double pan = 0.5);
  void noteOff(int midiNote, std::uint8_t instrument = 0);
  void allNotesOff();

  void setInstrumentFilter(std::uint8_t instrument, BiquadType type, float cutoffNorm, float resonanceNorm);
  void clearInstrumentFilter(std::uint8_t instrument);
  BiquadParams getInstrumentFilterParams(std::uint8_t instrument) const;

  void setInstrumentEffects(std::uint8_t instrument, const InstrumentEffectParams& p);
  void clearInstrumentEffects(std::uint8_t instrument);
  InstrumentEffectParams getInstrumentEffectParams(std::uint8_t instrument) const;

  void setInstrumentPitch(std::uint8_t instrument, float semitones);
  float getInstrumentPitch(std::uint8_t instrument) const;

  void setInstrumentDepth(std::uint8_t instrument, float depth);
  float getInstrumentDepth(std::uint8_t instrument) const;

  void setReverbParams(const ReverbParams& p);
  ReverbParams getReverbParams() const;
  void setInstrumentReverbSend(std::uint8_t instrument, float send);
  float getInstrumentReverbSend(std::uint8_t instrument) const;
  void clearReverb();

  void setGlobalVolume(float volume);  // 0.0 = silence, 1.0 = unity, 2.0 = max
  float getGlobalVolume() const;

  void setTestToneFrequencyHz(double frequencyHz);
  void setTestToneVoicesHz(const std::vector<double>& frequenciesHz);
  double testToneFrequencyHz() const;
  std::size_t testToneVoiceCount() const;
  double testToneVoiceHz(std::size_t voiceIndex) const;
  double testToneVoicePan(std::size_t voiceIndex) const;
  double testToneVoiceLevel(std::size_t voiceIndex) const;

  // Capture callback: called from the audio render thread each buffer with
  // separate float L/R arrays (values nominally -1..1) and frame count.
  // Safe to set/clear while the engine is running; use clearCaptureCallback()
  // to stop capture.
  using CaptureCallback = std::function<void(const float* left, const float* right, std::uint32_t frames)>;
  void setCaptureCallback(CaptureCallback cb);
  void clearCaptureCallback();
  std::uint32_t currentSampleRate() const;

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
