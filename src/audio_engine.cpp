#include "extracker/audio_engine.hpp"
#include "extracker/reverb.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef EXTRACKER_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

#ifdef EXTRACKER_HAVE_JACK
#include <jack/jack.h>
#endif

#ifdef EXTRACKER_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <cstring>
#include <mmdeviceapi.h>
#endif

namespace extracker {

namespace {

struct CaptureState {
  std::mutex mutex;
  std::function<void(const float*, const float*, std::uint32_t)> callback;
};

struct AudioConfig {
  std::uint32_t sampleRate = 48000;
  std::uint32_t bufferFrames = 256;

  PluginHost* pluginHost = nullptr;
  std::shared_ptr<PluginRenderState> toneState;
  std::shared_ptr<PluginRenderState> pluginRenderState;
  // Shared across the config copy taken by render threads so the callback can
  // be installed/removed while the engine is running.
  std::shared_ptr<CaptureState>          captureState;
  std::shared_ptr<ReverbState>           reverbState;
  std::shared_ptr<std::atomic<float>>    globalVolume;  // 0.0-2.0, 1.0 = unity
};

class IAudioBackend {
public:
  virtual ~IAudioBackend() = default;
  virtual std::string name() const = 0;
  virtual bool start(const AudioConfig& config) = 0;
  virtual void stop() = 0;
  virtual bool isRunning() const = 0;
};

class NullBackend final : public IAudioBackend {
public:
  std::string name() const override {
    return "Null";
  }

  bool start(const AudioConfig&) override {
    running_ = true;
    return true;
  }

  void stop() override {
    running_ = false;
  }

  bool isRunning() const override {
    return running_;
  }

private:
  bool running_ = false;
};

#ifdef EXTRACKER_HAVE_ALSA
class AlsaBackend final : public IAudioBackend {
public:
  std::string name() const override {
    return "ALSA";
  }

  bool start(const AudioConfig& config) override {
    if (running_.load()) {
      return true;
    }

    if (!openDevice(config)) {
      return false;
    }

    running_.store(true);
    renderThread_ = std::thread([this, config]() { renderLoop(config); });
    return true;
  }

  void stop() override {
    running_.store(false);
    if (renderThread_.joinable()) {
      renderThread_.join();
    }
    if (pcm_ != nullptr) {
      snd_pcm_drop(pcm_);
      snd_pcm_close(pcm_);
      pcm_ = nullptr;
    }
  }

  bool isRunning() const override {
    return running_.load();
  }

  ~AlsaBackend() override {
    stop();
  }

private:
  bool openDevice(const AudioConfig& config) {
    int result = snd_pcm_open(&pcm_, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (result < 0) {
      pcm_ = nullptr;
      return false;
    }

    result = snd_pcm_set_params(
        pcm_,
        SND_PCM_FORMAT_S16_LE,
        SND_PCM_ACCESS_RW_INTERLEAVED,
        2,
        config.sampleRate,
        1,
        10000);

    if (result < 0) {
      snd_pcm_close(pcm_);
      pcm_ = nullptr;
      return false;
    }

    return true;
  }

  void renderLoop(const AudioConfig& config) {
    std::vector<std::int16_t> buffer(config.bufferFrames * 2, 0);
    std::vector<float> captureL(config.bufferFrames);
    std::vector<float> captureR(config.bufferFrames);
    const std::int32_t amplitude = 5000;
    const double twoPi = 6.28318530717958647692;
    const double attackStep = 1.0 / std::max<double>(config.sampleRate * 0.005, 1.0);
    const double releaseStep = 1.0 / std::max<double>(config.sampleRate * 0.060, 1.0);

    while (running_.load()) {
      std::shared_ptr<PluginRenderState> activeState = config.pluginRenderState != nullptr
          ? config.pluginRenderState
          : config.toneState;

      constexpr std::size_t kMaxInstrA = extracker::kMaxInstrumentSlotsForFilter;
      std::array<std::vector<double>, kMaxInstrA> instrMonoA;
      for (auto& b : instrMonoA) b.assign(config.bufferFrames, 0.0);
      std::array<double, kMaxInstrA> instrPanA;
      instrPanA.fill(0.5);
      std::array<double, kMaxInstrA> instrDepthA;
      instrDepthA.fill(0.0);
      std::size_t totalVoicesA = 0;

      const bool renderedByPluginHostA = config.pluginHost != nullptr &&
          config.pluginHost->renderPerInstrument(instrMonoA, config.sampleRate);

      if (renderedByPluginHostA) {
        // Per-instrument depth from the shared render state.
        std::lock_guard<std::mutex> lock(activeState->mutex);
        for (std::size_t fi = 0; fi < kMaxInstrA; ++fi) {
          instrDepthA[fi] = (fi < activeState->depthOffsets.size())
                              ? static_cast<double>(std::clamp(activeState->depthOffsets[fi], 0.0f, 1.0f))
                              : 0.0;
        }
      } else {
        std::lock_guard<std::mutex> lock(activeState->mutex);
        for (PluginRenderVoice& voice : activeState->voices) {
          const std::size_t fi = std::min(static_cast<std::size_t>(voice.instrument), kMaxInstrA - 1);
          if (fi < activeState->filterParams.size() && activeState->filterParams[fi].isActive()
              && activeState->filterDirty[fi]) {
            activeState->filterCoeffs[fi] = extracker::computeBiquadCoeffs(
                activeState->filterParams[fi], static_cast<double>(config.sampleRate));
            activeState->filterDirty[fi] = false;
          }
          const double pitchA = (fi < activeState->pitchSemitones.size())
                                 ? static_cast<double>(activeState->pitchSemitones[fi]) : 0.0;
          const double pitchedHzA = std::max(voice.frequencyHz, 1.0) * std::pow(2.0, pitchA / 12.0);
          for (std::uint32_t i = 0; i < config.bufferFrames; ++i) {
            voice.phase += twoPi * pitchedHzA / static_cast<double>(config.sampleRate);
            if (voice.phase >= twoPi) voice.phase -= twoPi;
            if (voice.releasing) voice.level = std::max(0.0, voice.level - releaseStep);
            else                  voice.level = std::min(voice.targetLevel, voice.level + attackStep);
            double s = (voice.waveform == PluginWaveform::Square)
                        ? (std::sin(voice.phase) >= 0.0 ? 1.0 : -1.0) : std::sin(voice.phase);
            s *= voice.level;
            if (fi < activeState->filterParams.size() && activeState->filterParams[fi].isActive())
              s = voice.filterState.process(s, activeState->filterCoeffs[fi]);
            instrMonoA[fi][i] += s;
          }
          instrPanA[fi]   = std::clamp(voice.pan, 0.0, 1.0);
          instrDepthA[fi] = (fi < activeState->depthOffsets.size())
                              ? static_cast<double>(std::clamp(activeState->depthOffsets[fi], 0.0f, 1.0f))
                              : 0.0;
          ++totalVoicesA;
        }
        activeState->voices.erase(
            std::remove_if(activeState->voices.begin(), activeState->voices.end(),
                [](const PluginRenderVoice& v){ return v.releasing && v.level <= 0.0; }),
            activeState->voices.end());
        for (std::size_t fi = 0; fi < kMaxInstrA; ++fi) {
          if (activeState->effectParams[fi].isActive())
            applyInstrumentEffects(instrMonoA[fi], activeState->effectParams[fi],
                                    activeState->effectState[fi], static_cast<double>(config.sampleRate));
        }
      }

      const auto clampToI16 = [amplitude](double value) {
        return static_cast<std::int16_t>(std::clamp(value * static_cast<double>(amplitude), -32767.0, 32767.0));
      };
      const double normA = totalVoicesA > 0 ? 1.0 / static_cast<double>(totalVoicesA) : 1.0;

      std::vector<double> reverbBusA;
      ReverbParams reverbParamsA;
      bool reverbActiveA = false;
      if (config.reverbState) {
        std::lock_guard<std::mutex> rlock(config.reverbState->mutex);
        reverbParamsA = config.reverbState->params;
        if (reverbParamsA.isActive() && (totalVoicesA > 0 || renderedByPluginHostA)) {
          reverbActiveA = true;
          reverbBusA.assign(config.bufferFrames, 0.0);
          for (std::size_t fi = 0; fi < kMaxInstrA; ++fi) {
            const float send = config.reverbState->sends[fi];
            if (send > 0.0f) {
              for (std::uint32_t i = 0; i < config.bufferFrames; ++i)
                reverbBusA[i] += instrMonoA[fi][i] * static_cast<double>(send);
            }
          }
          for (auto& s : reverbBusA) s *= normA;
        }
      }
      std::vector<double> revLA, revRA;
      if (reverbActiveA) {
        std::lock_guard<std::mutex> rlock(config.reverbState->mutex);
        config.reverbState->proc.process(reverbBusA, revLA, revRA, reverbParamsA,
                                          static_cast<double>(config.sampleRate));
      }

      constexpr double kRearGainA = 0.7071;
      const double volA = config.globalVolume
                            ? static_cast<double>(std::clamp(config.globalVolume->load(), 0.0f, 2.0f))
                            : 1.0;
      for (std::uint32_t i = 0; i < config.bufferFrames; ++i) {
        double fl = 0.0, fr = 0.0, rl = 0.0, rr = 0.0;
        for (std::size_t fi = 0; fi < kMaxInstrA; ++fi) {
          const double s     = instrMonoA[fi][i];
          const double panL  = std::sqrt(1.0 - instrPanA[fi]);
          const double panR  = std::sqrt(instrPanA[fi]);
          const double front = std::sqrt(1.0 - instrDepthA[fi]);
          const double rear  = std::sqrt(instrDepthA[fi]);
          fl += s * panL * front;
          fr += s * panR * front;
          rl += s * panL * rear;
          rr += s * panR * rear;
        }
        double mixedLeft  = (fl + rl * kRearGainA) * normA;
        double mixedRight = (fr + rr * kRearGainA) * normA;
        if (reverbActiveA) {
          mixedLeft  += revLA[i];
          mixedRight += revRA[i];
        }
        mixedLeft  *= volA;
        mixedRight *= volA;
        buffer[i * 2]     = clampToI16(mixedLeft);
        buffer[i * 2 + 1] = clampToI16(mixedRight);
        captureL[i] = static_cast<float>(std::clamp(mixedLeft,  -1.0, 1.0));
        captureR[i] = static_cast<float>(std::clamp(mixedRight, -1.0, 1.0));
      }

      if (config.captureState) {
        std::lock_guard<std::mutex> lock(config.captureState->mutex);
        if (config.captureState->callback) {
          config.captureState->callback(captureL.data(), captureR.data(), config.bufferFrames);
        }
      }

      snd_pcm_sframes_t frames = snd_pcm_writei(pcm_, buffer.data(), config.bufferFrames);
      if (frames < 0) {
        snd_pcm_prepare(pcm_);
      }
    }
  }

  snd_pcm_t* pcm_ = nullptr;
  std::atomic<bool> running_{false};
  std::thread renderThread_;
};
#endif

// ---------------------------------------------------------------------------
// Shared stereo render helper used by JACK and PipeWire backends
// ---------------------------------------------------------------------------

struct EnvelopeSteps {
  double attackStep;
  double releaseStep;
};

static EnvelopeSteps makeEnvelopeSteps(std::uint32_t sampleRate) {
  return {
    1.0 / std::max<double>(sampleRate * 0.005, 1.0),
    1.0 / std::max<double>(sampleRate * 0.060, 1.0)
  };
}

// Renders numFrames of stereo audio into leftOut/rightOut (float, normalized -1..1).
// Render one buffer into 4 discrete channels: FL, FR, RL, RR.
// Callers that only have stereo hardware fold down afterwards.
static void renderFrames4ch(const AudioConfig& config,
                             const EnvelopeSteps& env,
                             float* flOut, float* frOut,
                             float* rlOut, float* rrOut,
                             std::uint32_t numFrames) {
  static const double twoPi = 6.28318530717958647692;

  std::shared_ptr<PluginRenderState> activeState = config.pluginRenderState != nullptr
      ? config.pluginRenderState
      : config.toneState;

  constexpr std::size_t kMaxInstr = extracker::kMaxInstrumentSlotsForFilter;
  std::array<std::vector<double>, kMaxInstr> instrMono;
  for (auto& b : instrMono) b.assign(numFrames, 0.0);
  std::array<double, kMaxInstr> instrPan;
  instrPan.fill(0.5);
  std::array<double, kMaxInstr> instrDepth;
  instrDepth.fill(0.0);

  std::size_t totalVoices = 0;

  // Plugin host path: renders per-instrument so depth/pan can be applied correctly.
  const bool renderedByPluginHost = config.pluginHost != nullptr &&
      config.pluginHost->renderPerInstrument(instrMono, config.sampleRate);

  if (renderedByPluginHost) {
    // Per-instrument depth from the shared render state (written by setInstrumentDepth).
    // Pan stays at 0.5 (center) — plugin instruments have no per-voice pan in this path.
    std::lock_guard<std::mutex> lock(activeState->mutex);
    for (std::size_t fi = 0; fi < kMaxInstr; ++fi) {
      instrDepth[fi] = (fi < activeState->depthOffsets.size())
                         ? static_cast<double>(std::clamp(activeState->depthOffsets[fi], 0.0f, 1.0f))
                         : 0.0;
    }
  } else {
    // Built-in synthesis path: accumulate per-instrument mono, apply block effects, pan to 4ch.
    std::lock_guard<std::mutex> lock(activeState->mutex);

    for (PluginRenderVoice& voice : activeState->voices) {
      const std::size_t fi = std::min(static_cast<std::size_t>(voice.instrument), kMaxInstr - 1);
      if (fi < activeState->filterParams.size() && activeState->filterParams[fi].isActive()
          && activeState->filterDirty[fi]) {
        activeState->filterCoeffs[fi] = extracker::computeBiquadCoeffs(
            activeState->filterParams[fi], static_cast<double>(config.sampleRate));
        activeState->filterDirty[fi] = false;
      }
      const double pitch = (fi < activeState->pitchSemitones.size())
                            ? static_cast<double>(activeState->pitchSemitones[fi]) : 0.0;
      const double pitchedHz = std::max(voice.frequencyHz, 1.0) * std::pow(2.0, pitch / 12.0);
      for (std::uint32_t i = 0; i < numFrames; ++i) {
        voice.phase += twoPi * pitchedHz / static_cast<double>(config.sampleRate);
        if (voice.phase >= twoPi) voice.phase -= twoPi;
        if (voice.releasing) voice.level = std::max(0.0, voice.level - env.releaseStep);
        else                  voice.level = std::min(voice.targetLevel, voice.level + env.attackStep);
        double s = (voice.waveform == PluginWaveform::Square)
                    ? (std::sin(voice.phase) >= 0.0 ? 1.0 : -1.0) : std::sin(voice.phase);
        s *= voice.level;
        if (fi < activeState->filterParams.size() && activeState->filterParams[fi].isActive())
          s = voice.filterState.process(s, activeState->filterCoeffs[fi]);
        instrMono[fi][i] += s;
      }
      instrPan[fi]   = std::clamp(voice.pan,   0.0, 1.0);
      instrDepth[fi] = (fi < activeState->depthOffsets.size())
                         ? static_cast<double>(std::clamp(activeState->depthOffsets[fi], 0.0f, 1.0f))
                         : 0.0;
      ++totalVoices;
    }

    auto& voices = activeState->voices;
    voices.erase(std::remove_if(voices.begin(), voices.end(),
        [](const PluginRenderVoice& v){ return v.releasing && v.level <= 0.0; }),
        voices.end());

    for (std::size_t fi = 0; fi < kMaxInstr; ++fi) {
      if (activeState->effectParams[fi].isActive())
        applyInstrumentEffects(instrMono[fi], activeState->effectParams[fi],
                                activeState->effectState[fi], static_cast<double>(config.sampleRate));
    }
  }

  // Built-in path: normalize by total voice count so simultaneous tones don't clip.
  // Plugin path: each instrMono[fi] is already per-voice normalized by BuiltinSamplePlugin;
  // use a soft-saturation scale (tanh) chosen so a single centered instrument at unity
  // maps to unity output: scale = atanh(sqrt(0.5)) / sqrt(0.5) ≈ 1.2466.
  // This is transparent at normal levels and compresses gracefully under heavy load
  // without the dynamic pumping that a per-buffer active-count normalization would cause.
  static constexpr double kPluginSatScale = 1.2466;
  const double norm = (totalVoices > 0) ? 1.0 / static_cast<double>(totalVoices) : 1.0;

  // Reverb send bus — accumulated before stereo mix, normalized with dry
  std::vector<double> reverbBus;
  ReverbParams reverbParams;
  bool reverbActive = false;
  if (config.reverbState) {
    std::lock_guard<std::mutex> rlock(config.reverbState->mutex);
    reverbParams = config.reverbState->params;
    if (reverbParams.isActive() && (totalVoices > 0 || renderedByPluginHost)) {
      reverbActive = true;
      reverbBus.assign(numFrames, 0.0);
      for (std::size_t fi = 0; fi < kMaxInstr; ++fi) {
        const float send = config.reverbState->sends[fi];
        if (send > 0.0f) {
          for (std::uint32_t i = 0; i < numFrames; ++i)
            reverbBus[i] += instrMono[fi][i] * static_cast<double>(send);
        }
      }
      for (auto& s : reverbBus) s *= norm;
    }
  }

  for (std::uint32_t i = 0; i < numFrames; ++i) {
    double fl = 0.0, fr = 0.0, rl = 0.0, rr = 0.0;
    for (std::size_t fi = 0; fi < kMaxInstr; ++fi) {
      const double s     = instrMono[fi][i];
      const double panL  = std::sqrt(1.0 - instrPan[fi]);
      const double panR  = std::sqrt(instrPan[fi]);
      const double front = std::sqrt(1.0 - instrDepth[fi]);
      const double rear  = std::sqrt(instrDepth[fi]);
      fl += s * panL * front;
      fr += s * panR * front;
      rl += s * panL * rear;
      rr += s * panR * rear;
    }
    if (renderedByPluginHost) {
      flOut[i] = static_cast<float>(std::tanh(fl * kPluginSatScale));
      frOut[i] = static_cast<float>(std::tanh(fr * kPluginSatScale));
      rlOut[i] = static_cast<float>(std::tanh(rl * kPluginSatScale));
      rrOut[i] = static_cast<float>(std::tanh(rr * kPluginSatScale));
    } else {
      flOut[i] = static_cast<float>(std::clamp(fl * norm, -1.0, 1.0));
      frOut[i] = static_cast<float>(std::clamp(fr * norm, -1.0, 1.0));
      rlOut[i] = static_cast<float>(std::clamp(rl * norm, -1.0, 1.0));
      rrOut[i] = static_cast<float>(std::clamp(rr * norm, -1.0, 1.0));
    }
  }

  if (reverbActive) {
    std::vector<double> revL, revR;
    {
      std::lock_guard<std::mutex> rlock(config.reverbState->mutex);
      config.reverbState->proc.process(reverbBus, revL, revR, reverbParams,
                                        static_cast<double>(config.sampleRate));
    }
    for (std::uint32_t i = 0; i < numFrames; ++i) {
      flOut[i] = static_cast<float>(std::clamp(static_cast<double>(flOut[i]) + revL[i], -1.0, 1.0));
      frOut[i] = static_cast<float>(std::clamp(static_cast<double>(frOut[i]) + revR[i], -1.0, 1.0));
    }
  }

  // Global volume — applied after all processing so capture reflects what's heard.
  if (config.globalVolume) {
    const float vol = std::clamp(config.globalVolume->load(), 0.0f, 2.0f);
    if (vol != 1.0f) {
      for (std::uint32_t i = 0; i < numFrames; ++i) {
        flOut[i] = std::clamp(flOut[i] * vol, -1.0f, 1.0f);
        frOut[i] = std::clamp(frOut[i] * vol, -1.0f, 1.0f);
        rlOut[i] = std::clamp(rlOut[i] * vol, -1.0f, 1.0f);
        rrOut[i] = std::clamp(rrOut[i] * vol, -1.0f, 1.0f);
      }
    }
  }

  if (config.captureState) {
    std::lock_guard<std::mutex> lock(config.captureState->mutex);
    if (config.captureState->callback) {
      config.captureState->callback(flOut, frOut, numFrames);
    }
  }
}

// Stereo folddown wrapper — for backends that output only 2 channels.
// Rear channels are mixed into front at -3 dB to maintain loudness.
static void renderFramesToStereo(const AudioConfig& config,
                                  const EnvelopeSteps& env,
                                  float* leftOut, float* rightOut,
                                  std::uint32_t numFrames) {
  std::vector<float> rl(numFrames, 0.0f), rr(numFrames, 0.0f);
  renderFrames4ch(config, env, leftOut, rightOut, rl.data(), rr.data(), numFrames);
  constexpr float kRearGain = 0.7071f;  // -3 dB
  for (std::uint32_t i = 0; i < numFrames; ++i) {
    leftOut[i]  = std::clamp(leftOut[i]  + rl[i] * kRearGain, -1.0f, 1.0f);
    rightOut[i] = std::clamp(rightOut[i] + rr[i] * kRearGain, -1.0f, 1.0f);
  }
}

// ---------------------------------------------------------------------------
// JACK backend
// ---------------------------------------------------------------------------

#ifdef EXTRACKER_HAVE_JACK
class JackBackend final : public IAudioBackend {
public:
  std::string name() const override {
    return "JACK";
  }

  bool start(const AudioConfig& config) override {
    if (running_.load()) {
      return true;
    }
    jack_status_t status;
    client_ = jack_client_open("exTracker", JackNullOption, &status);
    if (client_ == nullptr) {
      return false;
    }
    const std::uint32_t jackRate = static_cast<std::uint32_t>(jack_get_sample_rate(client_));
    config_ = config;
    config_.sampleRate = jackRate;
    env_ = makeEnvelopeSteps(jackRate);

    jack_set_process_callback(client_, processCallback, this);

    portLeft_     = jack_port_register(client_, "out_L",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    portRight_    = jack_port_register(client_, "out_R",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    portRearLeft_ = jack_port_register(client_, "out_RL", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    portRearRight_= jack_port_register(client_, "out_RR", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (portLeft_ == nullptr || portRight_ == nullptr ||
        portRearLeft_ == nullptr || portRearRight_ == nullptr) {
      jack_client_close(client_);
      client_ = nullptr;
      return false;
    }
    if (jack_activate(client_) != 0) {
      jack_client_close(client_);
      client_ = nullptr;
      return false;
    }
    running_.store(true);
    return true;
  }

  void stop() override {
    if (!running_.load()) {
      return;
    }
    running_.store(false);
    if (client_ != nullptr) {
      jack_deactivate(client_);
      jack_client_close(client_);
      client_        = nullptr;
      portLeft_      = nullptr;
      portRight_     = nullptr;
      portRearLeft_  = nullptr;
      portRearRight_ = nullptr;
    }
  }

  bool isRunning() const override {
    return running_.load();
  }

  ~JackBackend() override {
    stop();
  }

private:
  static int processCallback(jack_nframes_t nframes, void* arg) {
    auto* self  = static_cast<JackBackend*>(arg);
    auto* flBuf = static_cast<float*>(jack_port_get_buffer(self->portLeft_,      nframes));
    auto* frBuf = static_cast<float*>(jack_port_get_buffer(self->portRight_,     nframes));
    auto* rlBuf = static_cast<float*>(jack_port_get_buffer(self->portRearLeft_,  nframes));
    auto* rrBuf = static_cast<float*>(jack_port_get_buffer(self->portRearRight_, nframes));
    renderFrames4ch(self->config_, self->env_, flBuf, frBuf, rlBuf, rrBuf,
                    static_cast<std::uint32_t>(nframes));
    return 0;
  }

  jack_client_t* client_        = nullptr;
  jack_port_t*   portLeft_      = nullptr;
  jack_port_t*   portRight_     = nullptr;
  jack_port_t*   portRearLeft_  = nullptr;
  jack_port_t*   portRearRight_ = nullptr;
  std::atomic<bool> running_{false};
  AudioConfig config_{};
  EnvelopeSteps env_{};
};
#endif  // EXTRACKER_HAVE_JACK

// ---------------------------------------------------------------------------
// PipeWire backend
// ---------------------------------------------------------------------------

#ifdef EXTRACKER_HAVE_PIPEWIRE
class PipeWireBackend final : public IAudioBackend {
public:
  PipeWireBackend() {
    streamEvents_ = {};
    streamEvents_.version    = PW_VERSION_STREAM_EVENTS;
    streamEvents_.process    = onProcess;
    streamEvents_.param_changed = onParamChanged;
  }

  std::string name() const override {
    return "PipeWire";
  }

  bool start(const AudioConfig& config) override {
    if (running_.load()) {
      return true;
    }
    config_ = config;
    env_ = makeEnvelopeSteps(config.sampleRate);

    pw_init(nullptr, nullptr);

    loop_ = pw_main_loop_new(nullptr);
    if (loop_ == nullptr) {
      return false;
    }

    std::uint8_t fmtBuffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(fmtBuffer, sizeof(fmtBuffer));
    spa_audio_info_raw audioInfo = {};
    audioInfo.format      = SPA_AUDIO_FORMAT_F32;
    audioInfo.rate        = config.sampleRate;
    audioInfo.channels    = 4;
    audioInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
    audioInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
    audioInfo.position[2] = SPA_AUDIO_CHANNEL_RL;
    audioInfo.position[3] = SPA_AUDIO_CHANNEL_RR;
    numOutputChannels_.store(4);
    const spa_pod* params[1] = {
      spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &audioInfo)
    };

    stream_ = pw_stream_new_simple(
        pw_main_loop_get_loop(loop_),
        "exTracker",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE,     "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE,     "Music",
            nullptr),
        &streamEvents_,
        this);

    if (stream_ == nullptr) {
      pw_main_loop_destroy(loop_);
      loop_ = nullptr;
      return false;
    }

    if (pw_stream_connect(stream_,
                          PW_DIRECTION_OUTPUT,
                          PW_ID_ANY,
                          static_cast<pw_stream_flags>(
                              PW_STREAM_FLAG_AUTOCONNECT |
                              PW_STREAM_FLAG_MAP_BUFFERS |
                              PW_STREAM_FLAG_RT_PROCESS),
                          params, 1) < 0) {
      pw_stream_destroy(stream_);
      stream_ = nullptr;
      pw_main_loop_destroy(loop_);
      loop_ = nullptr;
      return false;
    }

    running_.store(true);
    loopFinished_.store(false);
    loopThread_ = std::thread([this]() {
      pw_main_loop_run(loop_);
      loopFinished_.store(true);
    });
    return true;
  }

  void stop() override {
    if (!running_.load()) {
      return;
    }
    running_.store(false);
    // Defeat the quit-before-run race: if pw_main_loop_quit() lands before the
    // loop thread has entered pw_main_loop_run(), the quit is lost and the loop
    // spins forever. Re-issue quit until the thread actually exits.
    if (loop_ != nullptr) {
      while (!loopFinished_.load()) {
        pw_main_loop_quit(loop_);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    if (loopThread_.joinable()) {
      loopThread_.join();
    }
    if (stream_ != nullptr) {
      pw_stream_destroy(stream_);
      stream_ = nullptr;
    }
    if (loop_ != nullptr) {
      pw_main_loop_destroy(loop_);
      loop_ = nullptr;
    }
  }

  bool isRunning() const override {
    return running_.load();
  }

  ~PipeWireBackend() override {
    stop();
  }

private:
  static void onProcess(void* userData) {
    auto* self = static_cast<PipeWireBackend*>(userData);
    pw_buffer* buf = pw_stream_dequeue_buffer(self->stream_);
    if (buf == nullptr) {
      return;
    }
    struct spa_data& data = buf->buffer->datas[0];
    auto* dst = static_cast<float*>(data.data);
    if (dst == nullptr) {
      pw_stream_queue_buffer(self->stream_, buf);
      return;
    }
    const std::uint32_t nch = self->numOutputChannels_.load();
    const std::uint32_t maxFrames = data.maxsize / (sizeof(float) * nch);
    const std::uint32_t numFrames = (buf->requested > 0)
        ? std::min<std::uint32_t>(maxFrames, static_cast<std::uint32_t>(buf->requested))
        : maxFrames;

    if (nch == 4) {
      std::vector<float> fl(numFrames), fr(numFrames), rl(numFrames), rr(numFrames);
      renderFrames4ch(self->config_, self->env_, fl.data(), fr.data(), rl.data(), rr.data(), numFrames);
      for (std::uint32_t i = 0; i < numFrames; ++i) {
        dst[i * 4 + 0] = fl[i];
        dst[i * 4 + 1] = fr[i];
        dst[i * 4 + 2] = rl[i];
        dst[i * 4 + 3] = rr[i];
      }
    } else {
      std::vector<float> leftBuf(numFrames, 0.0f);
      std::vector<float> rightBuf(numFrames, 0.0f);
      renderFramesToStereo(self->config_, self->env_, leftBuf.data(), rightBuf.data(), numFrames);
      for (std::uint32_t i = 0; i < numFrames; ++i) {
        dst[i * 2]     = leftBuf[i];
        dst[i * 2 + 1] = rightBuf[i];
      }
    }
    data.chunk->offset = 0;
    data.chunk->stride = static_cast<std::int32_t>(sizeof(float) * nch);
    data.chunk->size   = numFrames * sizeof(float) * nch;

    pw_stream_queue_buffer(self->stream_, buf);
  }

  static void onParamChanged(void* userData, std::uint32_t id, const struct spa_pod* param) {
    if (id != SPA_PARAM_Format || param == nullptr) return;
    auto* self = static_cast<PipeWireBackend*>(userData);
    spa_audio_info_raw info = {};
    if (spa_format_audio_raw_parse(param, &info) >= 0 && info.channels > 0) {
      self->numOutputChannels_.store(info.channels);
    }
  }

  pw_main_loop*         loop_   = nullptr;
  pw_stream*            stream_ = nullptr;
  pw_stream_events      streamEvents_{};
  std::thread           loopThread_;
  std::atomic<bool>     running_{false};
  std::atomic<bool>     loopFinished_{false};
  std::atomic<uint32_t> numOutputChannels_{4};
  AudioConfig           config_{};
  EnvelopeSteps         env_{};
};
#endif  // EXTRACKER_HAVE_PIPEWIRE

// ---------------------------------------------------------------------------
// WASAPI backend (Windows)
// ---------------------------------------------------------------------------

#ifdef _WIN32
static bool wasapiCheckFloatFormat(const WAVEFORMATEX* fmt) {
  if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
  if (fmt->wFormatTag != WAVE_FORMAT_EXTENSIBLE) return false;
  const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {00000003-0000-0010-8000-00AA00389B71}
  static const GUID kFloat = {0x00000003, 0x0000, 0x0010,
      {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
  return memcmp(&ext->SubFormat, &kFloat, sizeof(GUID)) == 0;
}

class WasapiBackend final : public IAudioBackend {
public:
  std::string name() const override { return "WASAPI"; }

  bool start(const AudioConfig& config) override {
    if (running_.load()) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
    comInitialized_ = (hr == S_OK);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator_));
    if (FAILED(hr)) return releaseAll(false);

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr)) return releaseAll(false);

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&audioClient_));
    if (FAILED(hr)) return releaseAll(false);

    WAVEFORMATEX* mixFmt = nullptr;
    hr = audioClient_->GetMixFormat(&mixFmt);
    if (FAILED(hr)) return releaseAll(false);

    if (!wasapiCheckFloatFormat(mixFmt)) {
      CoTaskMemFree(mixFmt);
      return releaseAll(false);
    }
    sampleRate_ = mixFmt->nSamplesPerSec;
    channels_ = mixFmt->nChannels;

    hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  1000000 /* 100ms */, 0, mixFmt, nullptr);
    CoTaskMemFree(mixFmt);
    if (FAILED(hr)) return releaseAll(false);

    readyEvent_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (readyEvent_ == nullptr) return releaseAll(false);

    hr = audioClient_->SetEventHandle(readyEvent_);
    if (FAILED(hr)) return releaseAll(false);

    hr = audioClient_->GetService(__uuidof(IAudioRenderClient),
                                  reinterpret_cast<void**>(&renderClient_));
    if (FAILED(hr)) return releaseAll(false);

    UINT32 bufSz = 0;
    hr = audioClient_->GetBufferSize(&bufSz);
    if (FAILED(hr)) return releaseAll(false);
    bufferFrames_ = bufSz;

    config_ = config;
    config_.sampleRate = sampleRate_;
    config_.bufferFrames = bufferFrames_;
    env_ = makeEnvelopeSteps(sampleRate_);

    hr = audioClient_->Start();
    if (FAILED(hr)) return releaseAll(false);

    running_.store(true);
    renderThread_ = std::thread([this]() { renderLoop(); });
    return true;
  }

  void stop() override {
    if (!running_.load()) return;
    running_.store(false);
    if (readyEvent_ != nullptr) SetEvent(readyEvent_);
    if (renderThread_.joinable()) renderThread_.join();
    if (audioClient_ != nullptr) audioClient_->Stop();
    releaseAll(false);
  }

  bool isRunning() const override { return running_.load(); }

  ~WasapiBackend() override { stop(); }

private:
  bool releaseAll(bool retval) {
    if (renderClient_) { renderClient_->Release(); renderClient_ = nullptr; }
    if (audioClient_) { audioClient_->Release(); audioClient_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    if (enumerator_) { enumerator_->Release(); enumerator_ = nullptr; }
    if (readyEvent_) { CloseHandle(readyEvent_); readyEvent_ = nullptr; }
    if (comInitialized_) { CoUninitialize(); comInitialized_ = false; }
    return retval;
  }

  void renderLoop() {
    std::vector<float> leftBuf, rightBuf;
    while (running_.load()) {
      DWORD waitResult = WaitForSingleObject(readyEvent_, 200);
      if (!running_.load()) break;
      if (waitResult != WAIT_OBJECT_0) continue;

      UINT32 padding = 0;
      if (FAILED(audioClient_->GetCurrentPadding(&padding))) break;
      const UINT32 avail = bufferFrames_ - padding;
      if (avail == 0) continue;

      BYTE* data = nullptr;
      if (FAILED(renderClient_->GetBuffer(avail, &data))) break;

      leftBuf.resize(avail);
      rightBuf.resize(avail);
      renderFramesToStereo(config_, env_, leftBuf.data(), rightBuf.data(), avail);

      auto* dst = reinterpret_cast<float*>(data);
      for (UINT32 i = 0; i < avail; ++i) {
        dst[i * channels_] = leftBuf[i];
        if (channels_ > 1) dst[i * channels_ + 1] = rightBuf[i];
        for (UINT32 ch = 2; ch < channels_; ++ch)
          dst[i * channels_ + ch] = 0.0f;
      }

      renderClient_->ReleaseBuffer(avail, 0);
    }
  }

  IMMDeviceEnumerator* enumerator_ = nullptr;
  IMMDevice*           device_       = nullptr;
  IAudioClient*        audioClient_  = nullptr;
  IAudioRenderClient*  renderClient_ = nullptr;
  HANDLE               readyEvent_   = nullptr;
  bool                 comInitialized_ = false;

  UINT32 sampleRate_    = 48000;
  UINT32 channels_      = 2;
  UINT32 bufferFrames_  = 0;

  std::atomic<bool> running_{false};
  std::thread       renderThread_;
  AudioConfig       config_{};
  EnvelopeSteps     env_{};
};
#endif  // _WIN32

}  // namespace

struct AudioEngine::Impl {
  BackendKind preferredBackend = BackendKind::Auto;
  AudioConfig config{};
  std::unique_ptr<IAudioBackend> backend;
};

AudioEngine::AudioEngine() : impl_(new Impl()) {
  impl_->config.toneState     = std::make_shared<PluginRenderState>();
  impl_->config.captureState  = std::make_shared<CaptureState>();
  impl_->config.reverbState   = std::make_shared<ReverbState>();
  impl_->config.globalVolume  = std::make_shared<std::atomic<float>>(1.0f);
  impl_->backend = std::make_unique<NullBackend>();
}

AudioEngine::~AudioEngine() {
  delete impl_;
}

void AudioEngine::setCaptureCallback(CaptureCallback cb) {
  if (impl_->config.captureState) {
    std::lock_guard<std::mutex> lock(impl_->config.captureState->mutex);
    impl_->config.captureState->callback = std::move(cb);
  }
}

void AudioEngine::clearCaptureCallback() {
  if (impl_->config.captureState) {
    std::lock_guard<std::mutex> lock(impl_->config.captureState->mutex);
    impl_->config.captureState->callback = nullptr;
  }
}

std::uint32_t AudioEngine::currentSampleRate() const {
  return impl_->config.sampleRate;
}

void AudioEngine::setBackendPreference(BackendKind backend) {
  if (isRunning()) {
    return;
  }
  impl_->preferredBackend = backend;
}

void AudioEngine::setSampleRate(std::uint32_t sampleRate) {
  if (sampleRate > 0) {
    impl_->config.sampleRate = sampleRate;
  }
}

void AudioEngine::setBufferFrames(std::uint32_t bufferFrames) {
  if (bufferFrames > 0) {
    impl_->config.bufferFrames = bufferFrames;
  }
}

void AudioEngine::setPluginHost(PluginHost* pluginHost) {
  impl_->config.pluginHost = pluginHost;
}

void AudioEngine::noteOn(int midiNote, double frequencyHz, double velocity, bool retrigger, std::uint8_t instrument, double pan) {
  if (frequencyHz <= 0.0) {
    return;
  }

  double clampedVelocity = std::clamp(velocity, 0.0, 1.0);
  if (clampedVelocity <= 0.0) {
    return;
  }

  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  for (auto& voice : impl_->config.toneState->voices) {
    if (voice.midiNote == midiNote && voice.instrument == instrument) {
      voice.frequencyHz = frequencyHz;
      voice.pan = std::clamp(pan, 0.0, 1.0);
      voice.targetLevel = clampedVelocity;
      voice.releasing = false;
      if (retrigger) {
        voice.phase = 0.0;
        voice.level = 0.0;
      }
      return;
    }
  }

  PluginRenderVoice voice;
  voice.midiNote = midiNote;
  voice.instrument = instrument;
  voice.frequencyHz = frequencyHz;
  voice.pan = std::clamp(pan, 0.0, 1.0);
  voice.phase = 0.0;
  voice.level = 0.0;
  voice.targetLevel = clampedVelocity;
  voice.releasing = false;
  voice.waveform = PluginWaveform::Sine;
  impl_->config.toneState->voices.push_back(voice);
}

void AudioEngine::noteOff(int midiNote, std::uint8_t instrument) {
  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  bool running = isRunning();
  for (auto& voice : impl_->config.toneState->voices) {
    if (voice.midiNote == midiNote && voice.instrument == instrument) {
      voice.releasing = running;
      if (!running) {
        voice.level = 0.0;
      }
    }
  }

  if (!running) {
    auto& voices = impl_->config.toneState->voices;
    voices.erase(
        std::remove_if(
            voices.begin(),
            voices.end(),
            [](const PluginRenderVoice& voice) {
              return voice.level <= 0.0;
            }),
        voices.end());
  }
}

void AudioEngine::allNotesOff() {
  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  bool running = isRunning();
  for (auto& voice : impl_->config.toneState->voices) {
    voice.releasing = running;
    if (!running) {
      voice.level = 0.0;
    }
  }

  if (!running) {
    impl_->config.toneState->voices.clear();
  }
}

void AudioEngine::setInstrumentFilter(std::uint8_t instrument, extracker::BiquadType type,
                                      float cutoffNorm, float resonanceNorm) {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return;
  auto& ts = *impl_->config.toneState;
  std::lock_guard<std::mutex> lock(ts.mutex);
  ts.filterParams[instrument] = {type, cutoffNorm, resonanceNorm};
  ts.filterDirty[instrument]  = true;
  for (auto& v : ts.voices)
    if (v.instrument == instrument) v.filterState.reset();
}

void AudioEngine::clearInstrumentFilter(std::uint8_t instrument) {
  setInstrumentFilter(instrument, extracker::BiquadType::Off, 1.0f, 0.0f);
}

extracker::BiquadParams AudioEngine::getInstrumentFilterParams(std::uint8_t instrument) const {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return {};
  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  return impl_->config.toneState->filterParams[instrument];
}

void AudioEngine::setInstrumentEffects(std::uint8_t instrument,
                                        const extracker::InstrumentEffectParams& p) {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return;
  auto& ts = *impl_->config.toneState;
  std::lock_guard<std::mutex> lock(ts.mutex);
  ts.effectParams[instrument] = p;
  ts.effectState[instrument].reset();
}

void AudioEngine::clearInstrumentEffects(std::uint8_t instrument) {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return;
  auto& ts = *impl_->config.toneState;
  std::lock_guard<std::mutex> lock(ts.mutex);
  ts.effectParams[instrument] = extracker::InstrumentEffectParams{};
  ts.effectState[instrument].reset();
}

extracker::InstrumentEffectParams AudioEngine::getInstrumentEffectParams(std::uint8_t instrument) const {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return {};
  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  return impl_->config.toneState->effectParams[instrument];
}

void AudioEngine::setInstrumentPitch(std::uint8_t instrument, float semitones) {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return;
  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  impl_->config.toneState->pitchSemitones[instrument] = semitones;
}

float AudioEngine::getInstrumentPitch(std::uint8_t instrument) const {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return 0.0f;
  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  return impl_->config.toneState->pitchSemitones[instrument];
}

void AudioEngine::setInstrumentDepth(std::uint8_t instrument, float depth) {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return;
  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  impl_->config.toneState->depthOffsets[instrument] = std::clamp(depth, 0.0f, 1.0f);
}

float AudioEngine::getInstrumentDepth(std::uint8_t instrument) const {
  if (instrument >= extracker::kMaxInstrumentSlotsForFilter) return 0.0f;
  std::lock_guard<std::mutex> lock(impl_->config.toneState->mutex);
  return impl_->config.toneState->depthOffsets[instrument];
}

void AudioEngine::setReverbParams(const ReverbParams& p) {
  if (!impl_->config.reverbState) return;
  std::lock_guard<std::mutex> lock(impl_->config.reverbState->mutex);
  impl_->config.reverbState->params = p;
}

ReverbParams AudioEngine::getReverbParams() const {
  if (!impl_->config.reverbState) return {};
  std::lock_guard<std::mutex> lock(impl_->config.reverbState->mutex);
  return impl_->config.reverbState->params;
}

void AudioEngine::setInstrumentReverbSend(std::uint8_t instrument, float send) {
  if (instrument >= 16 || !impl_->config.reverbState) return;
  std::lock_guard<std::mutex> lock(impl_->config.reverbState->mutex);
  impl_->config.reverbState->sends[instrument] = std::clamp(send, 0.0f, 1.0f);
}

float AudioEngine::getInstrumentReverbSend(std::uint8_t instrument) const {
  if (instrument >= 16 || !impl_->config.reverbState) return 0.0f;
  std::lock_guard<std::mutex> lock(impl_->config.reverbState->mutex);
  return impl_->config.reverbState->sends[instrument];
}

void AudioEngine::clearReverb() {
  if (!impl_->config.reverbState) return;
  std::lock_guard<std::mutex> lock(impl_->config.reverbState->mutex);
  impl_->config.reverbState->params = {};
  impl_->config.reverbState->sends.fill(0.0f);
}

void AudioEngine::setGlobalVolume(float volume) {
  if (impl_->config.globalVolume)
    impl_->config.globalVolume->store(std::clamp(volume, 0.0f, 2.0f));
}

float AudioEngine::getGlobalVolume() const {
  return impl_->config.globalVolume ? impl_->config.globalVolume->load() : 1.0f;
}

void AudioEngine::setTestToneFrequencyHz(double frequencyHz) {
  if (frequencyHz > 0.0) {
    setTestToneVoicesHz({frequencyHz});
  }
}

void AudioEngine::setTestToneVoicesHz(const std::vector<double>& frequenciesHz) {
  allNotesOff();
  for (std::size_t i = 0; i < frequenciesHz.size(); ++i) {
    noteOn(static_cast<int>(i), frequenciesHz[i], 1.0, true, 0);
  }
}

double AudioEngine::testToneFrequencyHz() const {
  if (impl_->config.pluginHost != nullptr) {
    return impl_->config.pluginHost->activeRenderVoiceFrequencyHz(0);
  }

  std::shared_ptr<PluginRenderState> activeState = impl_->config.pluginRenderState != nullptr
      ? impl_->config.pluginRenderState
      : impl_->config.toneState;
  std::lock_guard<std::mutex> lock(activeState->mutex);
  for (const auto& voice : activeState->voices) {
    if (!isRunning() && voice.releasing) {
      continue;
    }
    return voice.frequencyHz;
  }
  return 0.0;
}

std::size_t AudioEngine::testToneVoiceCount() const {
  if (impl_->config.pluginHost != nullptr) {
    return impl_->config.pluginHost->activeRenderVoiceCount();
  }

  std::shared_ptr<PluginRenderState> activeState = impl_->config.pluginRenderState != nullptr
      ? impl_->config.pluginRenderState
      : impl_->config.toneState;
  std::lock_guard<std::mutex> lock(activeState->mutex);
  if (isRunning()) {
    return activeState->voices.size();
  }

  std::size_t activeCount = 0;
  for (const auto& voice : activeState->voices) {
    if (!voice.releasing) {
      activeCount += 1;
    }
  }
  return activeCount;
}

double AudioEngine::testToneVoiceHz(std::size_t voiceIndex) const {
  if (impl_->config.pluginHost != nullptr) {
    return impl_->config.pluginHost->activeRenderVoiceFrequencyHz(voiceIndex);
  }

  std::shared_ptr<PluginRenderState> activeState = impl_->config.pluginRenderState != nullptr
      ? impl_->config.pluginRenderState
      : impl_->config.toneState;
  std::lock_guard<std::mutex> lock(activeState->mutex);
  std::size_t currentIndex = 0;
  for (const auto& voice : activeState->voices) {
    if (!isRunning() && voice.releasing) {
      continue;
    }

    if (currentIndex == voiceIndex) {
      return voice.frequencyHz;
    }
    currentIndex += 1;
  }
  return 0.0;
}

double AudioEngine::testToneVoicePan(std::size_t voiceIndex) const {
  // Plugin-host rendering path currently exposes frequency/voice count only.
  if (impl_->config.pluginHost != nullptr) {
    return 0.5;
  }

  std::shared_ptr<PluginRenderState> activeState = impl_->config.pluginRenderState != nullptr
      ? impl_->config.pluginRenderState
      : impl_->config.toneState;
  std::lock_guard<std::mutex> lock(activeState->mutex);
  std::size_t currentIndex = 0;
  for (const auto& voice : activeState->voices) {
    if (!isRunning() && voice.releasing) {
      continue;
    }

    if (currentIndex == voiceIndex) {
      return voice.pan;
    }
    currentIndex += 1;
  }
  return 0.5;
}

double AudioEngine::testToneVoiceLevel(std::size_t voiceIndex) const {
  // Plugin-host rendering path currently exposes frequency/voice count only.
  if (impl_->config.pluginHost != nullptr) {
    return 0.0;
  }

  std::shared_ptr<PluginRenderState> activeState = impl_->config.pluginRenderState != nullptr
      ? impl_->config.pluginRenderState
      : impl_->config.toneState;
  std::lock_guard<std::mutex> lock(activeState->mutex);
  std::size_t currentIndex = 0;
  for (const auto& voice : activeState->voices) {
    if (!isRunning() && voice.releasing) {
      continue;
    }

    if (currentIndex == voiceIndex) {
      return std::max(voice.level, voice.targetLevel);
    }
    currentIndex += 1;
  }
  return 0.0;
}

std::string AudioEngine::status() const {
  std::string runningState = isRunning() ? "running" : "stopped";
  return "AudioEngine: " + backendName() + " backend (" + runningState + ")";
}

std::string AudioEngine::backendName() const {
  return impl_->backend->name();
}

bool AudioEngine::start() {
  if (impl_->backend->isRunning()) {
    return true;
  }

  auto useNullBackend = [this]() {
    impl_->backend = std::make_unique<NullBackend>();
    return impl_->backend->start(impl_->config);
  };

#ifdef EXTRACKER_HAVE_PIPEWIRE
  if (impl_->preferredBackend == BackendKind::PipeWire || impl_->preferredBackend == BackendKind::Auto) {
    auto pw = std::make_unique<PipeWireBackend>();
    if (pw->start(impl_->config)) {
      impl_->backend = std::move(pw);
      return true;
    }
    if (impl_->preferredBackend == BackendKind::PipeWire) {
      return false;
    }
  }
#else
  if (impl_->preferredBackend == BackendKind::PipeWire) {
    return false;
  }
#endif

#ifdef EXTRACKER_HAVE_JACK
  if (impl_->preferredBackend == BackendKind::Jack || impl_->preferredBackend == BackendKind::Auto) {
    auto jack = std::make_unique<JackBackend>();
    if (jack->start(impl_->config)) {
      impl_->backend = std::move(jack);
      return true;
    }
    if (impl_->preferredBackend == BackendKind::Jack) {
      return false;
    }
  }
#else
  if (impl_->preferredBackend == BackendKind::Jack) {
    return false;
  }
#endif

#ifdef EXTRACKER_HAVE_ALSA
  if (impl_->preferredBackend == BackendKind::Alsa || impl_->preferredBackend == BackendKind::Auto) {
    auto alsa = std::make_unique<AlsaBackend>();
    if (alsa->start(impl_->config)) {
      impl_->backend = std::move(alsa);
      return true;
    }
    if (impl_->preferredBackend == BackendKind::Alsa) {
      return false;
    }
  }
#else
  if (impl_->preferredBackend == BackendKind::Alsa) {
    return false;
  }
#endif

#ifdef _WIN32
  if (impl_->preferredBackend == BackendKind::Wasapi || impl_->preferredBackend == BackendKind::Auto) {
    auto wasapi = std::make_unique<WasapiBackend>();
    if (wasapi->start(impl_->config)) {
      impl_->backend = std::move(wasapi);
      return true;
    }
    if (impl_->preferredBackend == BackendKind::Wasapi) {
      return false;
    }
  }
#else
  if (impl_->preferredBackend == BackendKind::Wasapi) {
    return false;
  }
#endif

  return useNullBackend();
}

void AudioEngine::stop() {
  impl_->backend->stop();
}

bool AudioEngine::isRunning() const {
  return impl_->backend->isRunning();
}

}  // namespace extracker
