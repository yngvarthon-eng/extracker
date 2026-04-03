#include "extracker/plugin_host.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

struct PluginVoice {
  int midiNote = -1;
  double frequencyHz = 0.0;
  double phase = 0.0;
  double level = 0.0;
  double targetLevel = 1.0;
  bool releasing = false;
};

double midiNoteToFrequencyHz(int midiNote) {
  int clamped = std::clamp(midiNote, 0, 127);
  return 440.0 * std::pow(2.0, static_cast<double>(clamped - 69) / 12.0);
}

class BuiltinInstrumentPluginBase : public extracker::IInstrumentPlugin {
public:
  void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) override {
    double target = std::clamp(static_cast<double>(velocity) / 127.0, 0.0, 1.0) * gain_;
    if (target <= 0.0) {
      return;
    }

    for (auto& voice : voices_) {
      if (voice.midiNote == midiNote) {
        voice.frequencyHz = midiNoteToFrequencyHz(midiNote);
        voice.targetLevel = target;
        voice.releasing = false;
        if (retrigger) {
          voice.phase = 0.0;
          voice.level = 0.0;
        }
        return;
      }
    }

    PluginVoice voice;
    voice.midiNote = midiNote;
    voice.frequencyHz = midiNoteToFrequencyHz(midiNote);
    voice.targetLevel = target;
    voices_.push_back(voice);
  }

  void noteOff(int midiNote) override {
    for (auto& voice : voices_) {
      if (voice.midiNote == midiNote) {
        voice.releasing = true;
      }
    }
  }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (monoBuffer.empty() || voices_.empty() || sampleRate == 0) {
      return;
    }

    const double twoPi = 6.28318530717958647692;
    const double attackStep = 1.0 / std::max<double>(sampleRate * (attackMs_ / 1000.0), 1.0);
    const double releaseStep = 1.0 / std::max<double>(sampleRate * (releaseMs_ / 1000.0), 1.0);

    for (std::size_t frame = 0; frame < monoBuffer.size(); ++frame) {
      double mixed = 0.0;
      for (auto& voice : voices_) {
        double phaseIncrement = twoPi * std::max(voice.frequencyHz, 1.0) / static_cast<double>(sampleRate);
        voice.phase += phaseIncrement;
        if (voice.phase >= twoPi) {
          voice.phase -= twoPi;
        }

        if (voice.releasing) {
          voice.level = std::max(0.0, voice.level - releaseStep);
        } else {
          voice.level = std::min(voice.targetLevel, voice.level + attackStep);
        }

        mixed += waveformSample(voice.phase) * voice.level;
      }

      mixed /= static_cast<double>(voices_.size());
      monoBuffer[frame] += mixed;

      voices_.erase(
          std::remove_if(
              voices_.begin(),
              voices_.end(),
              [](const PluginVoice& voice) {
                return voice.releasing && voice.level <= 0.0;
              }),
          voices_.end());

      if (voices_.empty()) {
        break;
      }
    }
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "gain") {
      gain_ = std::clamp(value, 0.0, 1.0);
      return true;
    }
    if (name == "attack_ms") {
      attackMs_ = std::max(value, 1.0);
      return true;
    }
    if (name == "release_ms") {
      releaseMs_ = std::max(value, 1.0);
      return true;
    }
    return false;
  }

  double getParameter(const std::string& name) const override {
    if (name == "gain") {
      return gain_;
    }
    if (name == "attack_ms") {
      return attackMs_;
    }
    if (name == "release_ms") {
      return releaseMs_;
    }
    return 0.0;
  }

  std::size_t activeVoiceCount() const override {
    std::size_t count = 0;
    for (const auto& voice : voices_) {
      if (!voice.releasing) {
        count += 1;
      }
    }
    return count;
  }

  double activeVoiceFrequencyHz(std::size_t voiceIndex) const override {
    std::size_t currentIndex = 0;
    for (const auto& voice : voices_) {
      if (voice.releasing) {
        continue;
      }
      if (currentIndex == voiceIndex) {
        return voice.frequencyHz;
      }
      currentIndex += 1;
    }
    return 0.0;
  }

protected:
  virtual double waveformSample(double phase) const = 0;

private:
  std::vector<PluginVoice> voices_;
  double gain_ = 1.0;
  double attackMs_ = 5.0;
  double releaseMs_ = 60.0;
};

class BuiltinSinePlugin final : public BuiltinInstrumentPluginBase {
protected:
  double waveformSample(double phase) const override {
    return std::sin(phase);
  }
};

class BuiltinSquarePlugin final : public BuiltinInstrumentPluginBase {
protected:
  double waveformSample(double phase) const override {
    return std::sin(phase) >= 0.0 ? 1.0 : -1.0;
  }
};

class Lv2PlaceholderInstrumentPlugin final : public BuiltinInstrumentPluginBase {
protected:
  double waveformSample(double phase) const override {
    return std::sin(phase);
  }
};

struct NoteEvent {
  int midiNote = 0;
  std::uint8_t velocity = 0;
  bool isNoteOn = false;
};

// ── LV2 Atom / MIDI structs (mirrors official lv2/atom.h & midi.h) ──────────

using Lv2Urid = std::uint32_t;

struct Lv2Atom {
  std::uint32_t size;  // payload size, not including this header
  Lv2Urid      type;
};

struct Lv2AtomSequenceBody {
  Lv2Urid unit;  // time unit URID (0 = frames)
  Lv2Urid pad;   // unused
};

struct Lv2AtomSequence {
  Lv2Atom             atom;  // type = kUridAtomSequence, size = sizeof(body) + events
  Lv2AtomSequenceBody body;
};

// Each event in the sequence is prefixed by this header.
// The MIDI bytes follow immediately after body.
struct Lv2AtomEvent {
  std::int64_t frames;  // event timestamp in frames from block start
  Lv2Atom      body;   // body.size = 3 for 3-byte MIDI; body.type = kUridMidiEvent
};

// Well-known URIDs – we assign them statically.  A real host would call
// the LV2_URID_Map feature; plugins must accept whatever values are given.
constexpr Lv2Urid kUridAtomSequence = 1;
constexpr Lv2Urid kUridMidiEvent    = 2;
constexpr std::size_t kAtomEventSize = sizeof(Lv2AtomEvent) + 3; // 8+8+4 + 3 bytes midi

// URID map feature tables (passed to plugin on instantiate).
struct Lv2UridMapData {
  void*     handle;
  Lv2Urid (*map)(void* handle, const char* uri);
};

static Lv2Urid staticUridMap(void* /*handle*/, const char* uri) {
  if (std::string_view(uri) == "http://lv2plug.in/ns/ext/atom#Sequence") {
    return kUridAtomSequence;
  }
  if (std::string_view(uri) == "http://lv2plug.in/ns/ext/midi#MidiEvent") {
    return kUridMidiEvent;
  }
  return 0;
}

static Lv2UridMapData kStaticUridMapData{nullptr, staticUridMap};

// ────────────────────────────────────────────────────────────────────────────

struct Lv2Feature {
  const char* uri;
  const void* data;
};

using Lv2Handle = void*;

struct Lv2Descriptor {
  const char* uri;
  Lv2Handle (*instantiate)(
      const Lv2Descriptor* descriptor,
      double sampleRate,
      const char* bundlePath,
      const Lv2Feature* const* features);
  void (*connectPort)(Lv2Handle instance, std::uint32_t port, void* data);
  void (*activate)(Lv2Handle instance);
  void (*run)(Lv2Handle instance, std::uint32_t sampleCount);
  void (*deactivate)(Lv2Handle instance);
  void (*cleanup)(Lv2Handle instance);
  const void* (*extensionData)(const char* uri);
};

using Lv2DescriptorFunction = const Lv2Descriptor* (*)(std::uint32_t index);

struct Lv2DiscoveredPlugin {
  std::string uri;
  std::filesystem::path binaryPath;
  int audioInputPort = -1;
  int audioOutputPort = -1;
  std::vector<int> controlInputPorts;
  std::vector<int> controlOutputPorts;
  std::vector<int> eventInputPorts;
};

class Lv2DynamicInstrumentPlugin final : public extracker::IInstrumentPlugin {
public:
  Lv2DynamicInstrumentPlugin(const std::string& uri,
                             const std::filesystem::path& binaryPath,
                             int audioInputPort,
                             int audioOutputPort,
                             std::vector<int> controlInputPorts,
                             std::vector<int> controlOutputPorts,
                             std::vector<int> eventInputPorts = {})
      : uri_(uri),
        binaryPath_(binaryPath),
        audioInputPort_(audioInputPort),
        audioOutputPort_(audioOutputPort),
        controlInputPorts_(std::move(controlInputPorts)),
        controlOutputPorts_(std::move(controlOutputPorts)),
        eventInputPorts_(std::move(eventInputPorts)),
        fallbackSynth_(),
        moduleHandle_(nullptr),
        descriptor_(nullptr),
        instance_(nullptr),
        loaded_(false) {
      controlInputValues_.assign(controlInputPorts_.size(), 0.0f);
      controlOutputValues_.assign(controlOutputPorts_.size(), 0.0f);
      resizeAtomSequenceBuffer(4096);
      pendingNoteEvents_.clear();
    loaded_ = tryLoadDescriptor();
  }

  ~Lv2DynamicInstrumentPlugin() override {
    shutdownRuntimeInstance();
    if (moduleHandle_ != nullptr) {
      dlclose(moduleHandle_);
      moduleHandle_ = nullptr;
    }
  }

  void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) override {
    fallbackSynth_.noteOn(midiNote, velocity, retrigger);
    queueEventNote(midiNote, velocity, true);
  }

  void noteOff(int midiNote) override {
    fallbackSynth_.noteOff(midiNote);
    queueEventNote(midiNote, 0, false);
  }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    bool renderedLv2 = false;
    if (sampleRate > 0) {
      renderedLv2 = renderLv2Runtime(monoBuffer, sampleRate);
    }

    // Guarded fallback keeps existing behavior intact when runtime bridge isn't active yet.
    if (!renderedLv2) {
      fallbackSynth_.renderAdd(monoBuffer, sampleRate);
    }
  }

  bool setParameter(const std::string& name, double value) override {
    const std::string controlInputPrefix = "lv2_control_in_";
    if (name.rfind(controlInputPrefix, 0) == 0) {
      std::size_t portOrdinal = 0;
      std::istringstream parse(name.substr(controlInputPrefix.size()));
      parse >> portOrdinal;
      if (!parse || !parse.eof() || portOrdinal >= controlInputValues_.size()) {
        return false;
      }
      controlInputValues_[portOrdinal] = static_cast<float>(value);
      return true;
    }

    if (name == "lv2_loaded") {
      return false;
    }
    return fallbackSynth_.setParameter(name, value);
  }

  double getParameter(const std::string& name) const override {
    const std::string controlOutputPrefix = "lv2_control_out_";
    if (name.rfind(controlOutputPrefix, 0) == 0) {
      std::size_t portOrdinal = 0;
      std::istringstream parse(name.substr(controlOutputPrefix.size()));
      parse >> portOrdinal;
      if (!parse || !parse.eof() || portOrdinal >= controlOutputValues_.size()) {
        return 0.0;
      }
      return static_cast<double>(controlOutputValues_[portOrdinal]);
    }

    if (name == "lv2_loaded") {
      return loaded_ ? 1.0 : 0.0;
    }
    if (name == "lv2_port_map_ready") {
      return audioOutputPort_ >= 0 ? 1.0 : 0.0;
    }
    if (name == "lv2_audio_input_port") {
      return static_cast<double>(audioInputPort_);
    }
    if (name == "lv2_audio_output_port") {
      return static_cast<double>(audioOutputPort_);
    }
    if (name == "lv2_control_input_count") {
      return static_cast<double>(controlInputPorts_.size());
    }
    if (name == "lv2_control_output_count") {
      return static_cast<double>(controlOutputPorts_.size());
    }
    if (name == "lv2_event_input_count") {
      return static_cast<double>(eventInputPorts_.size());
    }
    if (name == "lv2_runtime_active") {
      return runtimeActive_ ? 1.0 : 0.0;
    }
    return fallbackSynth_.getParameter(name);
  }

  std::size_t activeVoiceCount() const override {
    return fallbackSynth_.activeVoiceCount();
  }

  double activeVoiceFrequencyHz(std::size_t voiceIndex) const override {
    return fallbackSynth_.activeVoiceFrequencyHz(voiceIndex);
  }

private:
  void shutdownRuntimeInstance() {
    if (instance_ == nullptr || descriptor_ == nullptr) {
      runtimeActive_ = false;
      return;
    }

    if (runtimeActive_ && descriptor_->deactivate != nullptr) {
      descriptor_->deactivate(instance_);
    }

    if (descriptor_->cleanup != nullptr) {
      descriptor_->cleanup(instance_);
    }

    instance_ = nullptr;
    runtimeActive_ = false;
  }

  void queueEventNote(int midiNote, std::uint8_t velocity, bool isNoteOn) {
    if (eventInputPorts_.empty()) {
      return;
    }
    NoteEvent event;
    event.midiNote = midiNote;
    event.velocity = velocity;
    event.isNoteOn = isNoteOn;
    pendingNoteEvents_.push_back(event);
  }

  bool ensureRuntimeInstance(std::uint32_t sampleRate, std::size_t frameCount) {
    if (runtimeActive_) {
      if (audioInputBuffer_.size() != frameCount) {
        audioInputBuffer_.assign(frameCount, 0.0f);
      }
      if (audioOutputBuffer_.size() != frameCount) {
        audioOutputBuffer_.assign(frameCount, 0.0f);
      }
      return true;
    }

    if (!loaded_ || descriptor_ == nullptr || audioOutputPort_ < 0 ||
        descriptor_->instantiate == nullptr || descriptor_->run == nullptr ||
        descriptor_->connectPort == nullptr) {
      return false;
    }

    Lv2Feature uridMapFeature{"http://lv2plug.in/ns/ext/urid#map", &kStaticUridMapData};
    const Lv2Feature* features[] = {&uridMapFeature, nullptr};
    instance_ = descriptor_->instantiate(descriptor_, static_cast<double>(sampleRate), nullptr, features);
    if (instance_ == nullptr) {
      return false;
    }

    audioInputBuffer_.assign(frameCount, 0.0f);
    audioOutputBuffer_.assign(frameCount, 0.0f);
    if (controlInputValues_.size() < controlInputPorts_.size()) {
      controlInputValues_.resize(controlInputPorts_.size(), 0.0f);
    }
    if (controlOutputValues_.size() < controlOutputPorts_.size()) {
      controlOutputValues_.resize(controlOutputPorts_.size(), 0.0f);
    }
    if (eventInputBuffer_.size() < 4096) {
      resizeAtomSequenceBuffer(4096);
    }

    if (audioInputPort_ >= 0) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioInputPort_), audioInputBuffer_.data());
    }
    descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioOutputPort_), audioOutputBuffer_.data());
    for (std::size_t i = 0; i < controlInputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(controlInputPorts_[i]), &controlInputValues_[i]);
    }
    for (std::size_t i = 0; i < controlOutputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(controlOutputPorts_[i]), &controlOutputValues_[i]);
    }
    for (std::size_t i = 0; i < eventInputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(eventInputPorts_[i]), eventInputBuffer_.data());
    }

    if (descriptor_->activate != nullptr) {
      descriptor_->activate(instance_);
    }

    runtimeActive_ = true;
    return true;
  }

  bool renderLv2Runtime(std::vector<double>& monoBuffer, std::uint32_t sampleRate) {
    if (monoBuffer.empty()) {
      return false;
    }

    if (!ensureRuntimeInstance(sampleRate, monoBuffer.size())) {
      return false;
    }

    std::fill(audioInputBuffer_.begin(), audioInputBuffer_.end(), 0.0f);
    std::fill(audioOutputBuffer_.begin(), audioOutputBuffer_.end(), 0.0f);

    if (audioInputPort_ >= 0) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioInputPort_), audioInputBuffer_.data());
    }
    descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioOutputPort_), audioOutputBuffer_.data());
    for (std::size_t i = 0; i < controlInputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(controlInputPorts_[i]), &controlInputValues_[i]);
    }
    for (std::size_t i = 0; i < controlOutputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(controlOutputPorts_[i]), &controlOutputValues_[i]);
    }
    for (std::size_t i = 0; i < eventInputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(eventInputPorts_[i]), eventInputBuffer_.data());
    }

    if (!eventInputPorts_.empty()) {
      populateEventBuffer();
    }

    descriptor_->run(instance_, static_cast<std::uint32_t>(monoBuffer.size()));

    bool sawNonZero = false;
    for (std::size_t i = 0; i < monoBuffer.size(); ++i) {
      double sample = static_cast<double>(audioOutputBuffer_[i]);
      if (!std::isfinite(sample)) {
        shutdownRuntimeInstance();
        return false;
      }
      if (!sawNonZero && std::abs(sample) > 1e-12) {
        sawNonZero = true;
      }
      monoBuffer[i] += sample;
    }

    return sawNonZero;
  }

  void resizeAtomSequenceBuffer(std::size_t capacityBytes) {
    eventInputBuffer_.assign(capacityBytes, 0);
    // Write an empty LV2_Atom_Sequence header so the buffer is always valid.
    if (eventInputBuffer_.size() >= sizeof(Lv2AtomSequence)) {
      auto* seq = reinterpret_cast<Lv2AtomSequence*>(eventInputBuffer_.data());
      seq->atom.type = kUridAtomSequence;
      seq->atom.size = static_cast<std::uint32_t>(sizeof(Lv2AtomSequenceBody));
      seq->body.unit = 0;  // 0 = audio frames
      seq->body.pad  = 0;
    }
  }

  void populateEventBuffer() {
    // Reset to an empty sequence.
    resizeAtomSequenceBuffer(eventInputBuffer_.size());

    auto* seq = reinterpret_cast<Lv2AtomSequence*>(eventInputBuffer_.data());
    const std::size_t headerSize = sizeof(Lv2AtomSequence);
    std::size_t writeOffset = headerSize;  // bytes written past the sequence header

    for (const auto& event : pendingNoteEvents_) {
      if (writeOffset + kAtomEventSize > eventInputBuffer_.size()) {
        break;
      }

      const std::uint8_t status = event.isNoteOn ? 0x90 : 0x80;
      const std::uint8_t note   = static_cast<std::uint8_t>(event.midiNote);
      const std::uint8_t vel    = event.isNoteOn ? event.velocity : 0;

      auto* atomEvent = reinterpret_cast<Lv2AtomEvent*>(eventInputBuffer_.data() + writeOffset);
      atomEvent->frames      = 0;
      atomEvent->body.type   = kUridMidiEvent;
      atomEvent->body.size   = 3;
      uint8_t* midiBytes = eventInputBuffer_.data() + writeOffset + sizeof(Lv2AtomEvent);
      midiBytes[0] = status;
      midiBytes[1] = note;
      midiBytes[2] = vel;

      // Pad to 8-byte alignment as per LV2 atom spec.
      const std::size_t alignedEventSize = (sizeof(Lv2AtomEvent) + 3 + 7) & ~static_cast<std::size_t>(7);
      writeOffset += alignedEventSize;

      seq->atom.size += static_cast<std::uint32_t>(alignedEventSize);
    }

    pendingNoteEvents_.clear();
  }

  bool tryLoadDescriptor() {
    if (binaryPath_.empty()) {
      return false;
    }

    moduleHandle_ = dlopen(binaryPath_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (moduleHandle_ == nullptr) {
      return false;
    }

    void* symbol = dlsym(moduleHandle_, "lv2_descriptor");
    if (symbol == nullptr) {
      dlclose(moduleHandle_);
      moduleHandle_ = nullptr;
      return false;
    }

    auto descriptorFunction = reinterpret_cast<Lv2DescriptorFunction>(symbol);
    for (std::uint32_t index = 0; index < 1024; ++index) {
      const Lv2Descriptor* descriptor = descriptorFunction(index);
      if (descriptor == nullptr) {
        break;
      }
      if (descriptor->uri != nullptr && uri_ == descriptor->uri) {
        descriptor_ = descriptor;
        return true;
      }
    }

    dlclose(moduleHandle_);
    moduleHandle_ = nullptr;
    descriptor_ = nullptr;
    return false;
  }

  std::string uri_;
  std::filesystem::path binaryPath_;
  int audioInputPort_;
  int audioOutputPort_;
  std::vector<int> controlInputPorts_;
  std::vector<int> controlOutputPorts_;
  std::vector<int> eventInputPorts_;
  Lv2PlaceholderInstrumentPlugin fallbackSynth_;
  void* moduleHandle_;
  const Lv2Descriptor* descriptor_;
  Lv2Handle instance_;
  std::vector<float> audioInputBuffer_;
  std::vector<float> audioOutputBuffer_;
  std::vector<float> controlInputValues_;
  std::vector<float> controlOutputValues_;
  std::vector<std::uint8_t> eventInputBuffer_;
  std::vector<NoteEvent> pendingNoteEvents_;
  bool loaded_;
  bool runtimeActive_ = false;
};

class Lv2ManifestAdapter final : public extracker::IExternalPluginAdapter {
public:
  std::string adapterName() const override {
    return "lv2.manifest";
  }

  std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
    std::size_t discovered = 0;
    for (const auto& plugin : discoverPlugins()) {
      const std::string pluginId = "lv2:" + plugin.uri;
      if (registeredPluginIds_.insert(pluginId).second) {
        host.registerPluginFactory(
            pluginId,
            [plugin]() {
              return std::make_unique<Lv2DynamicInstrumentPlugin>(
                  plugin.uri,
                  plugin.binaryPath,
                  plugin.audioInputPort,
                  plugin.audioOutputPort,
                  plugin.controlInputPorts,
                  plugin.controlOutputPorts,
                  plugin.eventInputPorts);
            });
        discovered += 1;
      }
    }
    return discovered;
  }

private:
  static std::vector<std::filesystem::path> candidateSearchRoots() {
    std::vector<std::filesystem::path> roots;

    if (const char* env = std::getenv("LV2_PATH")) {
      std::stringstream stream(env);
      std::string path;
      while (std::getline(stream, path, ':')) {
        if (!path.empty()) {
          roots.emplace_back(path);
        }
      }
    }

    if (const char* home = std::getenv("HOME")) {
      roots.emplace_back(std::filesystem::path(home) / ".lv2");
    }

    roots.emplace_back("/usr/lib/lv2");
    roots.emplace_back("/usr/local/lib/lv2");
    return roots;
  }

  static std::filesystem::path resolveManifestBinaryPath(
      const std::string& rawValue,
      const std::filesystem::path& bundleDirectory) {
    if (rawValue.empty()) {
      return {};
    }

    if (rawValue.rfind("file://", 0) == 0) {
      return std::filesystem::path(rawValue.substr(7));
    }

    if (rawValue.find("://") != std::string::npos) {
      return {};
    }

    return bundleDirectory / rawValue;
  }

  static std::string firstAngleToken(const std::string& line) {
    auto start = line.find('<');
    auto end = line.find('>', start == std::string::npos ? 0 : start + 1);
    if (start == std::string::npos || end == std::string::npos || end <= start + 1) {
      return "";
    }
    return line.substr(start + 1, end - start - 1);
  }

  static std::string angleTokenAfterKey(const std::string& line, const std::string& key) {
    std::size_t keyPos = line.find(key);
    if (keyPos == std::string::npos) {
      return "";
    }

    std::size_t start = line.find('<', keyPos + key.size());
    std::size_t end = line.find('>', start == std::string::npos ? 0 : start + 1);
    if (start == std::string::npos || end == std::string::npos || end <= start + 1) {
      return "";
    }
    return line.substr(start + 1, end - start - 1);
  }

  static int parsePortIndexValue(const std::string& line) {
    const std::string key = "lv2:index";
    std::size_t keyPos = line.find(key);
    if (keyPos == std::string::npos) {
      return -1;
    }

    std::string tail = line.substr(keyPos + key.size());
    std::istringstream parse(tail);
    int value = -1;
    parse >> value;
    if (!parse || value < 0) {
      return -1;
    }
    return value;
  }

  static void applyParsedPortBlock(const std::string& uri,
                                   int index,
                                   bool isInput,
                                   bool isOutput,
                                   bool isAudio,
                                   bool isControl,
                                   bool isEvent,
                                   std::vector<Lv2DiscoveredPlugin>& plugins) {
    if (uri.empty() || index < 0 || (!isAudio && !isControl && !isEvent)) {
      return;
    }

    for (auto& plugin : plugins) {
      if (plugin.uri != uri) {
        continue;
      }
      if (isAudio) {
        if (isInput && plugin.audioInputPort < 0) {
          plugin.audioInputPort = index;
        }
        if (isOutput && plugin.audioOutputPort < 0) {
          plugin.audioOutputPort = index;
        }
      }

      if (isControl) {
        if (isInput && std::find(plugin.controlInputPorts.begin(), plugin.controlInputPorts.end(), index) == plugin.controlInputPorts.end()) {
          plugin.controlInputPorts.push_back(index);
        }
        if (isOutput && std::find(plugin.controlOutputPorts.begin(), plugin.controlOutputPorts.end(), index) == plugin.controlOutputPorts.end()) {
          plugin.controlOutputPorts.push_back(index);
        }
      }

      if (isEvent && isInput && std::find(plugin.eventInputPorts.begin(), plugin.eventInputPorts.end(), index) == plugin.eventInputPorts.end()) {
        plugin.eventInputPorts.push_back(index);
      }
      break;
    }
  }

  static void parsePluginPortLayout(const std::filesystem::path& bundleDirectory,
                                    std::vector<Lv2DiscoveredPlugin>& plugins) {
    for (const auto& entry : std::filesystem::directory_iterator(bundleDirectory)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".ttl") {
        continue;
      }

      std::ifstream ttl(entry.path());
      if (!ttl) {
        continue;
      }

      std::string currentUri;
      bool inPortBlock = false;
      int portIndex = -1;
      bool isInput = false;
      bool isOutput = false;
      bool isAudio = false;
      bool isControl = false;
      bool isEvent = false;

      std::string line;
      while (std::getline(ttl, line)) {
        if (line.find("lv2:Plugin") != std::string::npos) {
          std::string uri = firstAngleToken(line);
          if (!uri.empty()) {
            currentUri = uri;
          }
        }

        const bool hasPortBlockStart =
            line.find('[') != std::string::npos &&
            (line.find("lv2:port") != std::string::npos ||
             line.find("lv2:InputPort") != std::string::npos ||
             line.find("lv2:OutputPort") != std::string::npos ||
             line.find("lv2:AudioPort") != std::string::npos ||
             line.find("lv2:ControlPort") != std::string::npos ||
             line.find("lv2:EventPort") != std::string::npos ||
             line.find("atom:AtomPort") != std::string::npos);

        const auto lineIsEventPort = [](const std::string& l) {
          return l.find("lv2:EventPort") != std::string::npos ||
                 l.find("atom:AtomPort") != std::string::npos;
        };

        if (!inPortBlock && !currentUri.empty() && hasPortBlockStart) {
          inPortBlock = true;
          portIndex = parsePortIndexValue(line);
          isInput = (line.find("lv2:InputPort") != std::string::npos);
          isOutput = (line.find("lv2:OutputPort") != std::string::npos);
          isAudio = (line.find("lv2:AudioPort") != std::string::npos);
          isControl = (line.find("lv2:ControlPort") != std::string::npos);
          isEvent = lineIsEventPort(line);
        } else if (inPortBlock) {
          int parsedIndex = parsePortIndexValue(line);
          if (parsedIndex >= 0) {
            portIndex = parsedIndex;
          }
          isInput = isInput || (line.find("lv2:InputPort") != std::string::npos);
          isOutput = isOutput || (line.find("lv2:OutputPort") != std::string::npos);
          isAudio = isAudio || (line.find("lv2:AudioPort") != std::string::npos);
          isControl = isControl || (line.find("lv2:ControlPort") != std::string::npos);
          isEvent = isEvent || lineIsEventPort(line);
        }

        if (inPortBlock && line.find(']') != std::string::npos) {
          applyParsedPortBlock(currentUri, portIndex, isInput, isOutput, isAudio, isControl, isEvent, plugins);
          inPortBlock = false;
          portIndex = -1;
          isInput = false;
          isOutput = false;
          isAudio = false;
          isControl = false;
          isEvent = false;
        }
      }
    }

    for (auto& plugin : plugins) {
      std::sort(plugin.controlInputPorts.begin(), plugin.controlInputPorts.end());
      std::sort(plugin.controlOutputPorts.begin(), plugin.controlOutputPorts.end());
      std::sort(plugin.eventInputPorts.begin(), plugin.eventInputPorts.end());
    }
  }

  static std::vector<Lv2DiscoveredPlugin> discoverPlugins() {
    std::vector<Lv2DiscoveredPlugin> plugins;
    std::unordered_set<std::string> dedup;

    for (const auto& root : candidateSearchRoots()) {
      if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        continue;
      }

      for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) {
          continue;
        }

        if (entry.path().extension() != ".lv2") {
          continue;
        }

        const auto manifestPath = entry.path() / "manifest.ttl";
        std::ifstream manifest(manifestPath);
        if (!manifest) {
          continue;
        }

        std::string currentUri;
        std::string line;
        while (std::getline(manifest, line)) {
          if (line.find("lv2:Plugin") != std::string::npos || line.find("LV2_PLUGIN") != std::string::npos) {
            const std::string uri = firstAngleToken(line);
            if (!uri.empty()) {
              currentUri = uri;
              if (dedup.insert(uri).second) {
                plugins.emplace_back();
                plugins.back().uri = uri;
              }
            }
          }

          if (currentUri.empty() || line.find("lv2:binary") == std::string::npos) {
            continue;
          }

          const std::string binaryToken = angleTokenAfterKey(line, "lv2:binary");
          auto binaryPath = resolveManifestBinaryPath(binaryToken, entry.path());
          if (binaryPath.empty()) {
            continue;
          }

          for (auto& plugin : plugins) {
            if (plugin.uri == currentUri) {
              plugin.binaryPath = binaryPath;
              break;
            }
          }
        }

        parsePluginPortLayout(entry.path(), plugins);
      }
    }

    return plugins;
  }

  std::unordered_set<std::string> registeredPluginIds_;
};

class DisabledExternalPluginAdapter final : public extracker::IExternalPluginAdapter {
public:
  std::string adapterName() const override {
    return "external.disabled";
  }

  std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
    (void)host;
    return 0;
  }
};

}  // namespace

namespace extracker {

PluginHost::PluginHost()
    : instrumentSlots_{},
      instrumentPlugins_{},
      loadedPluginIds_{},
      availablePluginIds_{},
      pluginFactories_{},
      externalAdapters_{},
      loadedPluginCount_(0),
      noteOnEventCount_(0),
      noteOffEventCount_(0) {
  registerPluginFactory("builtin.sine", []() { return std::make_unique<BuiltinSinePlugin>(); });
  registerPluginFactory("builtin.square", []() { return std::make_unique<BuiltinSquarePlugin>(); });
  registerExternalAdapter(std::make_unique<Lv2ManifestAdapter>());
  registerExternalAdapter(std::make_unique<DisabledExternalPluginAdapter>());
}

std::string PluginHost::status() const {
  std::size_t assignedSlots = 0;
  for (const std::string& slot : instrumentSlots_) {
    if (!slot.empty()) {
      assignedSlots += 1;
    }
  }

  std::ostringstream stream;
  stream << "PluginHost: " << loadedPluginCount_ << " loaded, "
         << assignedSlots << " instrument routes";
  return stream.str();
}

std::vector<std::string> PluginHost::discoverAvailablePlugins() const {
  return availablePluginIds_;
}

void PluginHost::registerExternalAdapter(std::unique_ptr<IExternalPluginAdapter> adapter) {
  if (!adapter) {
    return;
  }
  externalAdapters_.push_back(std::move(adapter));
}

std::size_t PluginHost::rescanExternalPlugins() {
  std::size_t discoveredCount = 0;
  for (auto& adapter : externalAdapters_) {
    if (!adapter) {
      continue;
    }
    discoveredCount += adapter->registerDiscoveredPlugins(*this);
  }
  return discoveredCount;
}

std::vector<std::string> PluginHost::externalAdapterNames() const {
  std::vector<std::string> names;
  names.reserve(externalAdapters_.size());
  for (const auto& adapter : externalAdapters_) {
    if (!adapter) {
      continue;
    }
    names.push_back(adapter->adapterName());
  }
  return names;
}

bool PluginHost::registerPluginFactory(const std::string& pluginId, PluginFactory factory) {
  if (pluginId.empty() || !factory) {
    return false;
  }

  auto [it, inserted] = pluginFactories_.emplace(pluginId, std::move(factory));
  if (!inserted) {
    it->second = std::move(factory);
  }

  if (std::find(availablePluginIds_.begin(), availablePluginIds_.end(), pluginId) == availablePluginIds_.end()) {
    availablePluginIds_.push_back(pluginId);
  }

  return true;
}

bool PluginHost::loadPlugin(const std::string& id) {
  if (id.empty() || !hasPluginFactory(id)) {
    return false;
  }

  if (loadedPluginIds_.insert(id).second) {
    loadedPluginCount_ += 1;
  }
  return true;
}

bool PluginHost::assignInstrument(std::uint8_t instrument, const std::string& pluginId) {
  if (!isValidInstrument(instrument) || pluginId.empty() || loadedPluginIds_.find(pluginId) == loadedPluginIds_.end()) {
    return false;
  }

  auto plugin = createPluginInstance(pluginId);
  if (!plugin) {
    return false;
  }

  instrumentSlots_[instrument] = pluginId;
  instrumentPlugins_[instrument] = std::move(plugin);
  return true;
}

bool PluginHost::hasInstrumentAssignment(std::uint8_t instrument) const {
  if (!isValidInstrument(instrument)) {
    return false;
  }
  return !instrumentSlots_[instrument].empty();
}

std::string PluginHost::pluginForInstrument(std::uint8_t instrument) const {
  if (!isValidInstrument(instrument)) {
    return "";
  }
  return instrumentSlots_[instrument];
}

bool PluginHost::isValidInstrument(std::uint8_t instrument) const {
  return instrument < kMaxInstrumentSlots;
}

bool PluginHost::triggerNoteOn(std::uint8_t instrument, int midiNote, std::uint8_t velocity, bool retrigger) {
  if (!hasInstrumentAssignment(instrument)) {
    return false;
  }

  if (!instrumentPlugins_[instrument]) {
    return false;
  }

  instrumentPlugins_[instrument]->noteOn(midiNote, velocity, retrigger);
  noteOnEventCount_ += 1;
  return true;
}

bool PluginHost::triggerNoteOff(std::uint8_t instrument, int midiNote) {
  if (!hasInstrumentAssignment(instrument)) {
    return false;
  }

  if (!instrumentPlugins_[instrument]) {
    return false;
  }

  instrumentPlugins_[instrument]->noteOff(midiNote);

  noteOffEventCount_ += 1;
  return true;
}

bool PluginHost::renderInterleaved(std::vector<double>& monoBuffer, std::uint32_t sampleRate) {
  if (monoBuffer.empty() || sampleRate == 0) {
    return false;
  }

  std::fill(monoBuffer.begin(), monoBuffer.end(), 0.0);

  bool anyRendered = false;
  for (std::size_t i = 0; i < instrumentPlugins_.size(); ++i) {
    (void)i;
    if (!instrumentPlugins_[i]) {
      continue;
    }
    instrumentPlugins_[i]->renderAdd(monoBuffer, sampleRate);
    anyRendered = anyRendered || instrumentPlugins_[i]->activeVoiceCount() > 0;
  }

  return anyRendered;
}

bool PluginHost::setInstrumentParameter(std::uint8_t instrument, const std::string& name, double value) {
  if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) {
    return false;
  }
  return instrumentPlugins_[instrument]->setParameter(name, value);
}

double PluginHost::getInstrumentParameter(std::uint8_t instrument, const std::string& name) const {
  if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) {
    return 0.0;
  }
  return instrumentPlugins_[instrument]->getParameter(name);
}

std::size_t PluginHost::noteOnEventCount() const {
  return noteOnEventCount_;
}

std::size_t PluginHost::noteOffEventCount() const {
  return noteOffEventCount_;
}

std::size_t PluginHost::activeRenderVoiceCount() const {
  std::size_t activeCount = 0;
  for (std::size_t i = 0; i < instrumentPlugins_.size(); ++i) {
    if (instrumentPlugins_[i]) {
      activeCount += instrumentPlugins_[i]->activeVoiceCount();
    }
  }
  return activeCount;
}

double PluginHost::activeRenderVoiceFrequencyHz(std::size_t voiceIndex) const {
  std::size_t currentIndex = 0;
  for (std::size_t i = 0; i < instrumentPlugins_.size(); ++i) {
    if (!instrumentPlugins_[i]) {
      continue;
    }
    std::size_t count = instrumentPlugins_[i]->activeVoiceCount();
    if (voiceIndex < currentIndex + count) {
      return instrumentPlugins_[i]->activeVoiceFrequencyHz(voiceIndex - currentIndex);
    }
    currentIndex += count;
  }
  return 0.0;
}

bool PluginHost::hasPluginFactory(const std::string& pluginId) const {
  return pluginFactories_.find(pluginId) != pluginFactories_.end();
}

std::unique_ptr<IInstrumentPlugin> PluginHost::createPluginInstance(const std::string& pluginId) const {
  auto it = pluginFactories_.find(pluginId);
  if (it == pluginFactories_.end()) {
    return nullptr;
  }
  return it->second();
}

}  // namespace extracker
