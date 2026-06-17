#include "extracker/audio_engine.hpp"

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

namespace extracker {

namespace {

struct AudioConfig {
  std::uint32_t sampleRate = 48000;
  std::uint32_t bufferFrames = 256;

  PluginHost* pluginHost = nullptr;
  std::shared_ptr<PluginRenderState> toneState;
  std::shared_ptr<PluginRenderState> pluginRenderState;
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
    const std::int32_t amplitude = 5000;
    const double twoPi = 6.28318530717958647692;
    const double attackStep = 1.0 / std::max<double>(config.sampleRate * 0.005, 1.0);
    const double releaseStep = 1.0 / std::max<double>(config.sampleRate * 0.060, 1.0);

    while (running_.load()) {
      std::vector<double> pluginBuffer(config.bufferFrames, 0.0);
      bool renderedByPluginHost = config.pluginHost != nullptr &&
                                  config.pluginHost->renderInterleaved(pluginBuffer, config.sampleRate);
      if (renderedByPluginHost && config.pluginHost != nullptr) {
        config.pluginHost->renderEffectChain(pluginBuffer, config.sampleRate);
      }

      std::shared_ptr<PluginRenderState> activeState = config.pluginRenderState != nullptr
          ? config.pluginRenderState
          : config.toneState;

      for (std::uint32_t i = 0; i < config.bufferFrames; ++i) {
        double mixed = renderedByPluginHost ? pluginBuffer[i] : 0.0;
        double mixedLeft = mixed;
        double mixedRight = mixed;

        if (!renderedByPluginHost) {
          std::lock_guard<std::mutex> lock(activeState->mutex);
          double monoMixed = 0.0;
          double leftMixed = 0.0;
          double rightMixed = 0.0;
          for (PluginRenderVoice& voice : activeState->voices) {
            double frequencyHz = std::max(voice.frequencyHz, 1.0);
            double phaseIncrement = twoPi * frequencyHz / static_cast<double>(config.sampleRate);
            voice.phase += phaseIncrement;
            if (voice.phase >= twoPi) {
              voice.phase -= twoPi;
            }

            if (voice.releasing) {
              voice.level = std::max(0.0, voice.level - releaseStep);
            } else {
              voice.level = std::min(voice.targetLevel, voice.level + attackStep);
            }

            double sample = 0.0;
            switch (voice.waveform) {
              case PluginWaveform::Square:
                sample = std::sin(voice.phase) >= 0.0 ? 1.0 : -1.0;
                break;
              case PluginWaveform::Sine:
              default:
                sample = std::sin(voice.phase);
                break;
            }

            const double voiceSample = sample * voice.level;
            monoMixed += voiceSample;

            const double pan = std::clamp(voice.pan, 0.0, 1.0);
            const double leftGain = std::sqrt(1.0 - pan);
            const double rightGain = std::sqrt(pan);
            leftMixed += voiceSample * leftGain;
            rightMixed += voiceSample * rightGain;
          }

          if (!activeState->voices.empty()) {
            const double voiceCount = static_cast<double>(activeState->voices.size());
            monoMixed /= voiceCount;
            leftMixed /= voiceCount;
            rightMixed /= voiceCount;
          }

          mixed += monoMixed;
          mixedLeft += leftMixed;
          mixedRight += rightMixed;

          auto& voices = activeState->voices;
          voices.erase(
              std::remove_if(
                  voices.begin(),
                  voices.end(),
                  [](const PluginRenderVoice& voice) {
                    return voice.releasing && voice.level <= 0.0;
                  }),
              voices.end());
        }

        const auto clampToI16 = [amplitude](double value) {
          return static_cast<std::int16_t>(std::clamp(value * static_cast<double>(amplitude), -32767.0, 32767.0));
        };
        buffer[i * 2] = clampToI16(mixedLeft);
        buffer[i * 2 + 1] = clampToI16(mixedRight);
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
static void renderFramesToStereo(const AudioConfig& config,
                                  const EnvelopeSteps& env,
                                  float* leftOut, float* rightOut,
                                  std::uint32_t numFrames) {
  static const double twoPi = 6.28318530717958647692;

  std::vector<double> pluginBuffer(numFrames, 0.0);
  bool renderedByPluginHost = config.pluginHost != nullptr &&
                              config.pluginHost->renderInterleaved(pluginBuffer, config.sampleRate);
  if (renderedByPluginHost && config.pluginHost != nullptr) {
    config.pluginHost->renderEffectChain(pluginBuffer, config.sampleRate);
  }

  std::shared_ptr<PluginRenderState> activeState = config.pluginRenderState != nullptr
      ? config.pluginRenderState
      : config.toneState;

  for (std::uint32_t i = 0; i < numFrames; ++i) {
    double mixed = renderedByPluginHost ? pluginBuffer[i] : 0.0;
    double mixedLeft = mixed;
    double mixedRight = mixed;

    if (!renderedByPluginHost) {
      std::lock_guard<std::mutex> lock(activeState->mutex);
      double monoMixed = 0.0;
      double leftMixed = 0.0;
      double rightMixed = 0.0;
      for (PluginRenderVoice& voice : activeState->voices) {
        double frequencyHz = std::max(voice.frequencyHz, 1.0);
        double phaseIncrement = twoPi * frequencyHz / static_cast<double>(config.sampleRate);
        voice.phase += phaseIncrement;
        if (voice.phase >= twoPi) {
          voice.phase -= twoPi;
        }
        if (voice.releasing) {
          voice.level = std::max(0.0, voice.level - env.releaseStep);
        } else {
          voice.level = std::min(voice.targetLevel, voice.level + env.attackStep);
        }
        double sample = 0.0;
        switch (voice.waveform) {
          case PluginWaveform::Square:
            sample = std::sin(voice.phase) >= 0.0 ? 1.0 : -1.0;
            break;
          case PluginWaveform::Sine:
          default:
            sample = std::sin(voice.phase);
            break;
        }
        const double voiceSample = sample * voice.level;
        monoMixed += voiceSample;
        const double pan = std::clamp(voice.pan, 0.0, 1.0);
        const double leftGain = std::sqrt(1.0 - pan);
        const double rightGain = std::sqrt(pan);
        leftMixed += voiceSample * leftGain;
        rightMixed += voiceSample * rightGain;
      }
      if (!activeState->voices.empty()) {
        const double voiceCount = static_cast<double>(activeState->voices.size());
        monoMixed /= voiceCount;
        leftMixed /= voiceCount;
        rightMixed /= voiceCount;
      }
      (void)monoMixed;
      mixedLeft += leftMixed;
      mixedRight += rightMixed;
      auto& voices = activeState->voices;
      voices.erase(
          std::remove_if(
              voices.begin(),
              voices.end(),
              [](const PluginRenderVoice& voice) {
                return voice.releasing && voice.level <= 0.0;
              }),
          voices.end());
    }
    leftOut[i]  = static_cast<float>(std::clamp(mixedLeft,  -1.0, 1.0));
    rightOut[i] = static_cast<float>(std::clamp(mixedRight, -1.0, 1.0));
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

    portLeft_  = jack_port_register(client_, "out_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    portRight_ = jack_port_register(client_, "out_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (portLeft_ == nullptr || portRight_ == nullptr) {
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
      client_ = nullptr;
      portLeft_ = nullptr;
      portRight_ = nullptr;
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
    auto* self = static_cast<JackBackend*>(arg);
    auto* leftBuf  = static_cast<float*>(jack_port_get_buffer(self->portLeft_,  nframes));
    auto* rightBuf = static_cast<float*>(jack_port_get_buffer(self->portRight_, nframes));
    renderFramesToStereo(self->config_, self->env_, leftBuf, rightBuf,
                         static_cast<std::uint32_t>(nframes));
    return 0;
  }

  jack_client_t* client_   = nullptr;
  jack_port_t*   portLeft_ = nullptr;
  jack_port_t*   portRight_= nullptr;
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
    streamEvents_.version = PW_VERSION_STREAM_EVENTS;
    streamEvents_.process = onProcess;
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
    audioInfo.format   = SPA_AUDIO_FORMAT_F32;
    audioInfo.rate     = config.sampleRate;
    audioInfo.channels = 2;
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
    const std::uint32_t maxFrames = data.maxsize / (sizeof(float) * 2);
    const std::uint32_t numFrames = (buf->requested > 0)
        ? std::min<std::uint32_t>(maxFrames, static_cast<std::uint32_t>(buf->requested))
        : maxFrames;

    std::vector<float> leftBuf(numFrames, 0.0f);
    std::vector<float> rightBuf(numFrames, 0.0f);
    renderFramesToStereo(self->config_, self->env_, leftBuf.data(), rightBuf.data(), numFrames);

    for (std::uint32_t i = 0; i < numFrames; ++i) {
      dst[i * 2]     = leftBuf[i];
      dst[i * 2 + 1] = rightBuf[i];
    }
    data.chunk->offset = 0;
    data.chunk->stride = static_cast<std::int32_t>(sizeof(float) * 2);
    data.chunk->size   = numFrames * sizeof(float) * 2;

    pw_stream_queue_buffer(self->stream_, buf);
  }

  pw_main_loop*      loop_   = nullptr;
  pw_stream*         stream_ = nullptr;
  pw_stream_events   streamEvents_{};
  std::thread        loopThread_;
  std::atomic<bool>  running_{false};
  std::atomic<bool>  loopFinished_{false};
  AudioConfig        config_{};
  EnvelopeSteps      env_{};
};
#endif  // EXTRACKER_HAVE_PIPEWIRE

}  // namespace

struct AudioEngine::Impl {
  BackendKind preferredBackend = BackendKind::Auto;
  AudioConfig config{};
  std::unique_ptr<IAudioBackend> backend;
};

AudioEngine::AudioEngine() : impl_(new Impl()) {
  impl_->config.toneState = std::make_shared<PluginRenderState>();
  impl_->backend = std::make_unique<NullBackend>();
}

AudioEngine::~AudioEngine() {
  delete impl_;
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

  return useNullBackend();
}

void AudioEngine::stop() {
  impl_->backend->stop();
}

bool AudioEngine::isRunning() const {
  return impl_->backend->isRunning();
}

}  // namespace extracker
