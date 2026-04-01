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

      std::shared_ptr<PluginRenderState> activeState = config.pluginRenderState != nullptr
          ? config.pluginRenderState
          : config.toneState;

      for (std::uint32_t i = 0; i < config.bufferFrames; ++i) {
        double mixed = renderedByPluginHost ? pluginBuffer[i] : 0.0;

        if (!renderedByPluginHost) {
          std::lock_guard<std::mutex> lock(activeState->mutex);
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

            mixed += sample * voice.level;
          }

          if (!activeState->voices.empty()) {
            mixed /= static_cast<double>(activeState->voices.size());
          }

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

        std::int16_t sample = static_cast<std::int16_t>(mixed * amplitude);

        buffer[i * 2] = sample;
        buffer[i * 2 + 1] = sample;
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

void AudioEngine::noteOn(int midiNote, double frequencyHz, double velocity, bool retrigger, std::uint8_t instrument) {
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
