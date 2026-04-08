#include "extracker/plugin_host.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <optional>
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

struct SampleData {
  std::uint32_t sampleRate = 44100;
  std::vector<float> mono;
};

bool readU16LE(std::istream& in, std::uint16_t& out) {
  unsigned char b[2]{};
  in.read(reinterpret_cast<char*>(b), 2);
  if (!in) {
    return false;
  }
  out = static_cast<std::uint16_t>(b[0] | (static_cast<std::uint16_t>(b[1]) << 8));
  return true;
}

bool readU32LE(std::istream& in, std::uint32_t& out) {
  unsigned char b[4]{};
  in.read(reinterpret_cast<char*>(b), 4);
  if (!in) {
    return false;
  }
  out = static_cast<std::uint32_t>(b[0]) |
        (static_cast<std::uint32_t>(b[1]) << 8) |
        (static_cast<std::uint32_t>(b[2]) << 16) |
        (static_cast<std::uint32_t>(b[3]) << 24);
  return true;
}

void writeU16LE(std::ostream& out, std::uint16_t value) {
  char b[2] = {
      static_cast<char>(value & 0xFF),
      static_cast<char>((value >> 8) & 0xFF)};
  out.write(b, 2);
}

void writeU32LE(std::ostream& out, std::uint32_t value) {
  char b[4] = {
      static_cast<char>(value & 0xFF),
      static_cast<char>((value >> 8) & 0xFF),
      static_cast<char>((value >> 16) & 0xFF),
      static_cast<char>((value >> 24) & 0xFF)};
  out.write(b, 4);
}

float decodeSample(std::istream& in, std::uint16_t formatTag, std::uint16_t bitsPerSample) {
  if (formatTag == 1) {
    if (bitsPerSample == 8) {
      unsigned char v = 0;
      in.read(reinterpret_cast<char*>(&v), 1);
      if (!in) {
        return 0.0f;
      }
      return (static_cast<float>(v) - 128.0f) / 128.0f;
    }
    if (bitsPerSample == 16) {
      std::uint16_t raw = 0;
      if (!readU16LE(in, raw)) {
        return 0.0f;
      }
      std::int16_t s = static_cast<std::int16_t>(raw);
      return static_cast<float>(s) / 32768.0f;
    }
    if (bitsPerSample == 24) {
      unsigned char b[3]{};
      in.read(reinterpret_cast<char*>(b), 3);
      if (!in) {
        return 0.0f;
      }
      std::int32_t v = static_cast<std::int32_t>(b[0]) |
                       (static_cast<std::int32_t>(b[1]) << 8) |
                       (static_cast<std::int32_t>(b[2]) << 16);
      if ((v & 0x00800000) != 0) {
        v |= ~0x00FFFFFF;
      }
      return static_cast<float>(v) / 8388608.0f;
    }
    if (bitsPerSample == 32) {
      std::uint32_t raw = 0;
      if (!readU32LE(in, raw)) {
        return 0.0f;
      }
      std::int32_t s = static_cast<std::int32_t>(raw);
      return static_cast<float>(s) / 2147483648.0f;
    }
  }

  if (formatTag == 3 && bitsPerSample == 32) {
    std::uint32_t raw = 0;
    if (!readU32LE(in, raw)) {
      return 0.0f;
    }
    float f = 0.0f;
    std::memcpy(&f, &raw, sizeof(float));
    return std::clamp(f, -1.0f, 1.0f);
  }

  return 0.0f;
}

bool loadWavFile(const std::string& path, SampleData& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }

  char riff[4]{};
  char wave[4]{};
  std::uint32_t riffSize = 0;
  in.read(riff, 4);
  if (!readU32LE(in, riffSize)) {
    return false;
  }
  in.read(wave, 4);
  if (!in || std::string_view(riff, 4) != "RIFF" || std::string_view(wave, 4) != "WAVE") {
    return false;
  }

  std::uint16_t formatTag = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitsPerSample = 0;
  std::streampos dataPos = 0;
  std::uint32_t dataSize = 0;

  while (in) {
    char chunkId[4]{};
    std::uint32_t chunkSize = 0;
    in.read(chunkId, 4);
    if (!in || !readU32LE(in, chunkSize)) {
      break;
    }

    const std::string id(chunkId, 4);
    if (id == "fmt ") {
      if (!readU16LE(in, formatTag) || !readU16LE(in, channels) || !readU32LE(in, sampleRate)) {
        return false;
      }
      std::uint32_t byteRate = 0;
      std::uint16_t blockAlign = 0;
      if (!readU32LE(in, byteRate) || !readU16LE(in, blockAlign) || !readU16LE(in, bitsPerSample)) {
        return false;
      }
      (void)byteRate;
      (void)blockAlign;
      if (chunkSize > 16) {
        in.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
      }
    } else if (id == "data") {
      dataPos = in.tellg();
      dataSize = chunkSize;
      in.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
    } else {
      in.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
    }

    if ((chunkSize & 1u) != 0u) {
      in.seekg(1, std::ios::cur);
    }
  }

  if (dataPos <= 0 || dataSize == 0 || channels == 0 || sampleRate == 0) {
    return false;
  }

  const std::uint32_t bytesPerSample = static_cast<std::uint32_t>(bitsPerSample / 8);
  if (bytesPerSample == 0) {
    return false;
  }
  const std::uint32_t frameSize = bytesPerSample * static_cast<std::uint32_t>(channels);
  if (frameSize == 0) {
    return false;
  }
  const std::size_t frames = static_cast<std::size_t>(dataSize / frameSize);
  if (frames == 0) {
    return false;
  }

  in.clear();
  in.seekg(dataPos);
  if (!in) {
    return false;
  }

  out.sampleRate = sampleRate;
  out.mono.assign(frames, 0.0f);
  for (std::size_t i = 0; i < frames; ++i) {
    double mixed = 0.0;
    for (std::uint16_t ch = 0; ch < channels; ++ch) {
      mixed += static_cast<double>(decodeSample(in, formatTag, bitsPerSample));
      if (!in) {
        return false;
      }
    }
    out.mono[i] = static_cast<float>(std::clamp(mixed / static_cast<double>(channels), -1.0, 1.0));
  }

  return true;
}

bool saveWavFile(const std::string& path, const SampleData& sample) {
  if (sample.mono.empty() || sample.sampleRate == 0) {
    return false;
  }

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }

  const std::uint16_t channels = 1;
  const std::uint16_t bitsPerSample = 16;
  const std::uint32_t blockAlign = static_cast<std::uint32_t>(channels * (bitsPerSample / 8));
  const std::uint32_t byteRate = sample.sampleRate * blockAlign;
  const std::uint32_t dataSize = static_cast<std::uint32_t>(sample.mono.size() * blockAlign);
  const std::uint32_t riffSize = 4 + (8 + 16) + (8 + dataSize);

  out.write("RIFF", 4);
  writeU32LE(out, riffSize);
  out.write("WAVE", 4);

  out.write("fmt ", 4);
  writeU32LE(out, 16);
  writeU16LE(out, 1);
  writeU16LE(out, channels);
  writeU32LE(out, sample.sampleRate);
  writeU32LE(out, byteRate);
  writeU16LE(out, static_cast<std::uint16_t>(blockAlign));
  writeU16LE(out, bitsPerSample);

  out.write("data", 4);
  writeU32LE(out, dataSize);
  for (float value : sample.mono) {
    float clamped = std::clamp(value, -1.0f, 1.0f);
    std::int16_t pcm = static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
    writeU16LE(out, static_cast<std::uint16_t>(pcm));
  }

  return static_cast<bool>(out);
}

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

    void allNotesOff() override {
      for (auto& voice : voices_) {
        voice.releasing = true;
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

class BuiltinSamplePlugin final : public extracker::IInstrumentPlugin {
public:
  void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) override {
    if (sample_.mono.empty() || sample_.sampleRate == 0) {
      return;
    }

    const double vel = std::clamp(static_cast<double>(velocity) / 127.0, 0.0, 1.0);
    for (auto& voice : voices_) {
      if (voice.midiNote == midiNote) {
        voice.active = true;
        voice.pos = retrigger ? 0.0 : voice.pos;
        voice.level = vel * gain_;
        voice.pitchRatio = std::pow(2.0, static_cast<double>(midiNote - rootMidiNote_) / 12.0);
        return;
      }
    }

    Voice voice;
    voice.midiNote = midiNote;
    voice.level = vel * gain_;
    voice.pitchRatio = std::pow(2.0, static_cast<double>(midiNote - rootMidiNote_) / 12.0);
    voices_.push_back(voice);
  }

  void noteOff(int midiNote) override {
    for (auto& voice : voices_) {
      if (voice.midiNote == midiNote) {
        voice.active = false;
      }
    }
  }

  void allNotesOff() override {
    voices_.clear();
  }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (monoBuffer.empty() || sampleRate == 0 || sample_.mono.empty()) {
      return;
    }

    const double baseStep = static_cast<double>(sample_.sampleRate) / static_cast<double>(sampleRate);
    for (std::size_t frame = 0; frame < monoBuffer.size(); ++frame) {
      double mixed = 0.0;
      std::size_t activeCount = 0;

      for (auto& voice : voices_) {
        if (!voice.active) {
          continue;
        }

        std::size_t idx = static_cast<std::size_t>(voice.pos);
        if (idx >= sample_.mono.size()) {
          voice.active = false;
          continue;
        }

        std::size_t nextIdx = std::min(idx + 1, sample_.mono.size() - 1);
        const double frac = voice.pos - static_cast<double>(idx);
        const double sampleValue =
            static_cast<double>(sample_.mono[idx]) * (1.0 - frac) +
            static_cast<double>(sample_.mono[nextIdx]) * frac;

        mixed += sampleValue * voice.level;
        voice.pos += baseStep * voice.pitchRatio;
        activeCount += 1;
      }

      if (activeCount > 0) {
        monoBuffer[frame] += mixed / static_cast<double>(activeCount);
      }
    }

    voices_.erase(
        std::remove_if(
            voices_.begin(),
            voices_.end(),
            [](const Voice& voice) {
              return !voice.active;
            }),
        voices_.end());
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "gain") {
      gain_ = std::clamp(value, 0.0, 1.0);
      return true;
    }
    if (name == "sample_root") {
      rootMidiNote_ = static_cast<int>(std::clamp(value, 0.0, 127.0));
      return true;
    }
    return false;
  }

  double getParameter(const std::string& name) const override {
    if (name == "gain") {
      return gain_;
    }
    if (name == "sample_root") {
      return static_cast<double>(rootMidiNote_);
    }
    return 0.0;
  }

  std::size_t activeVoiceCount() const override {
    return voices_.size();
  }

  double activeVoiceFrequencyHz(std::size_t voiceIndex) const override {
    if (voiceIndex >= voices_.size()) {
      return 0.0;
    }
    return midiNoteToFrequencyHz(voices_[voiceIndex].midiNote);
  }

  bool loadSample(const std::string& wavPath) {
    SampleData loaded;
    if (!loadWavFile(wavPath, loaded)) {
      return false;
    }
    sample_ = std::move(loaded);
    samplePath_ = wavPath;
    voices_.clear();
    return true;
  }

  bool saveSample(const std::string& wavPath) const {
    return saveWavFile(wavPath, sample_);
  }

  void clearSample() {
    sample_ = SampleData{};
    samplePath_.clear();
    voices_.clear();
  }

  std::string samplePath() const {
    return samplePath_;
  }

private:
  struct Voice {
    int midiNote = -1;
    double pos = 0.0;
    double level = 0.0;
    double pitchRatio = 1.0;
    bool active = true;
  };

  SampleData sample_;
  std::vector<Voice> voices_;
  std::string samplePath_;
  int rootMidiNote_ = 60;
  double gain_ = 1.0;
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
  std::vector<extracker::PluginControlPortMeta> controlInputMeta;
  std::vector<extracker::PluginControlPortMeta> controlOutputMeta;
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

    void allNotesOff() override {
      fallbackSynth_.allNotesOff();
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
    const std::string controlInputPrefix = "lv2_control_in_";
    if (name.rfind(controlInputPrefix, 0) == 0) {
      std::size_t portOrdinal = 0;
      std::istringstream parse(name.substr(controlInputPrefix.size()));
      parse >> portOrdinal;
      if (!parse || !parse.eof() || portOrdinal >= controlInputValues_.size()) {
        return 0.0;
      }
      return static_cast<double>(controlInputValues_[portOrdinal]);
    }

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
        extracker::PluginPortInfo portInfo;
        portInfo.audioIn        = plugin.audioInputPort;
        portInfo.audioOut       = plugin.audioOutputPort;
        portInfo.controlInCount = static_cast<int>(plugin.controlInputPorts.size());
        portInfo.controlOutCount= static_cast<int>(plugin.controlOutputPorts.size());
        portInfo.eventInCount   = static_cast<int>(plugin.eventInputPorts.size());
        portInfo.controlInMeta  = plugin.controlInputMeta;
        portInfo.controlOutMeta = plugin.controlOutputMeta;
        host.registerPluginPortInfo(pluginId, portInfo);
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

  static std::optional<float> parseFloatAfterKey(const std::string& line, const std::string& key) {
    const std::size_t pos = line.find(key);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    const std::string tail = line.substr(pos + key.size());
    std::istringstream parse(tail);
    float val = 0.0f;
    parse >> val;
    if (!parse) {
      return std::nullopt;
    }
    return val;
  }

  static std::string parseQuotedStringAfterKey(const std::string& line, const std::string& key) {
    const std::size_t pos = line.find(key);
    if (pos == std::string::npos) {
      return "";
    }
    const std::size_t start = line.find('"', pos + key.size());
    const std::size_t end = line.find('"', start == std::string::npos ? 0 : start + 1);
    if (start == std::string::npos || end == std::string::npos || end <= start + 1) {
      return "";
    }
    return line.substr(start + 1, end - start - 1);
  }

  static void applyParsedPortBlock(const std::string& uri,
                                   int index,
                                   bool isInput,
                                   bool isOutput,
                                   bool isAudio,
                                   bool isControl,
                                   bool isEvent,
                                   const std::string& portSymbol,
                                   const std::string& portLabel,
                                   std::optional<float> portMinVal,
                                   std::optional<float> portMaxVal,
                                   std::optional<float> portDefaultVal,
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
          extracker::PluginControlPortMeta meta;
          meta.index = index;
          meta.symbol = portSymbol;
          meta.label = portLabel;
          if (portMinVal) { meta.minVal = *portMinVal; meta.hasMin = true; }
          if (portMaxVal) { meta.maxVal = *portMaxVal; meta.hasMax = true; }
          if (portDefaultVal) { meta.defaultVal = *portDefaultVal; meta.hasDefault = true; }
          plugin.controlInputMeta.push_back(meta);
        }
        if (isOutput && std::find(plugin.controlOutputPorts.begin(), plugin.controlOutputPorts.end(), index) == plugin.controlOutputPorts.end()) {
          plugin.controlOutputPorts.push_back(index);
          extracker::PluginControlPortMeta meta;
          meta.index = index;
          meta.symbol = portSymbol;
          meta.label = portLabel;
          if (portMinVal) { meta.minVal = *portMinVal; meta.hasMin = true; }
          if (portMaxVal) { meta.maxVal = *portMaxVal; meta.hasMax = true; }
          if (portDefaultVal) { meta.defaultVal = *portDefaultVal; meta.hasDefault = true; }
          plugin.controlOutputMeta.push_back(meta);
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
      std::string portSymbol;
      std::string portLabel;
      std::optional<float> portMinVal;
      std::optional<float> portMaxVal;
      std::optional<float> portDefaultVal;

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
          portSymbol = parseQuotedStringAfterKey(line, "lv2:symbol");
          portLabel = parseQuotedStringAfterKey(line, "rdfs:label");
          portMinVal = parseFloatAfterKey(line, "lv2:minimum");
          portMaxVal = parseFloatAfterKey(line, "lv2:maximum");
          portDefaultVal = parseFloatAfterKey(line, "lv2:default");
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
          if (const std::string value = parseQuotedStringAfterKey(line, "lv2:symbol"); !value.empty()) {
            portSymbol = value;
          }
          if (const std::string value = parseQuotedStringAfterKey(line, "rdfs:label"); !value.empty()) {
            portLabel = value;
          }
          if (auto v = parseFloatAfterKey(line, "lv2:minimum")) { portMinVal = v; }
          if (auto v = parseFloatAfterKey(line, "lv2:maximum")) { portMaxVal = v; }
          if (auto v = parseFloatAfterKey(line, "lv2:default")) { portDefaultVal = v; }
        }

        if (inPortBlock && line.find(']') != std::string::npos) {
          applyParsedPortBlock(currentUri, portIndex, isInput, isOutput, isAudio, isControl, isEvent, portSymbol, portLabel, portMinVal, portMaxVal, portDefaultVal, plugins);
          inPortBlock = false;
          portIndex = -1;
          isInput = false;
          isOutput = false;
          isAudio = false;
          isControl = false;
          isEvent = false;
          portSymbol.clear();
          portLabel.clear();
          portMinVal = std::nullopt;
          portMaxVal = std::nullopt;
          portDefaultVal = std::nullopt;
        }
      }
    }

    for (auto& plugin : plugins) {
      std::sort(plugin.controlInputPorts.begin(), plugin.controlInputPorts.end());
      std::sort(plugin.controlOutputPorts.begin(), plugin.controlOutputPorts.end());
      std::sort(plugin.eventInputPorts.begin(), plugin.eventInputPorts.end());
      std::sort(plugin.controlInputMeta.begin(), plugin.controlInputMeta.end(),
                [](const extracker::PluginControlPortMeta& a, const extracker::PluginControlPortMeta& b) {
                  return a.index < b.index;
                });
      std::sort(plugin.controlOutputMeta.begin(), plugin.controlOutputMeta.end(),
                [](const extracker::PluginControlPortMeta& a, const extracker::PluginControlPortMeta& b) {
                  return a.index < b.index;
                });
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

namespace {

BuiltinSamplePlugin* asSamplePlugin(IInstrumentPlugin* plugin) {
  return dynamic_cast<BuiltinSamplePlugin*>(plugin);
}

}  // namespace

PluginHost::PluginHost()
    : instrumentSlots_{},
      instrumentPlugins_{},
  instrumentSampleSlots_{},
  sampleSlotPlugins_{},
  sampleSlotPaths_{},
  sampleSlotNames_{},
      loadedPluginIds_{},
      availablePluginIds_{},
      pluginPortInfoMap_{},
      pluginFactories_{},
      externalAdapters_{},
      loadedPluginCount_(0),
      noteOnEventCount_(0),
      noteOffEventCount_(0) {
  registerPluginFactory("builtin.sine", []() { return std::make_unique<BuiltinSinePlugin>(); });
  registerPluginFactory("builtin.square", []() { return std::make_unique<BuiltinSquarePlugin>(); });
  registerPluginFactory("builtin.sample", []() { return std::make_unique<BuiltinSamplePlugin>(); });
  registerExternalAdapter(std::make_unique<Lv2ManifestAdapter>());
  registerExternalAdapter(std::make_unique<DisabledExternalPluginAdapter>());
  instrumentSampleSlots_.fill(-1);
}

std::string PluginHost::status() const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
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
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return {};
  }
  return availablePluginIds_;
}

void PluginHost::registerExternalAdapter(std::unique_ptr<IExternalPluginAdapter> adapter) {
  if (!adapter) {
    return;
  }
  std::lock_guard<std::timed_mutex> lock(mutex_);
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
  std::lock_guard<std::timed_mutex> lock(mutex_);
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

void PluginHost::registerPluginPortInfo(const std::string& pluginId, PluginPortInfo info) {
  if (!pluginId.empty()) {
    std::lock_guard<std::timed_mutex> lock(mutex_);
    pluginPortInfoMap_[pluginId] = info;
  }
}

bool PluginHost::getPluginPortInfo(const std::string& pluginId, PluginPortInfo& out) const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  auto it = pluginPortInfoMap_.find(pluginId);
  if (it == pluginPortInfoMap_.end()) {
    return false;
  }
  out = it->second;
  return true;
}

bool PluginHost::registerPluginFactory(const std::string& pluginId, PluginFactory factory) {
  if (pluginId.empty() || !factory) {
    return false;
  }

  std::lock_guard<std::timed_mutex> lock(mutex_);

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
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (id.empty() || pluginFactories_.find(id) == pluginFactories_.end()) {
    return false;
  }

  if (loadedPluginIds_.insert(id).second) {
    loadedPluginCount_ += 1;
  }
  return true;
}

bool PluginHost::assignInstrument(std::uint8_t instrument, const std::string& pluginId) {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (!isValidInstrument(instrument) || pluginId.empty() || loadedPluginIds_.find(pluginId) == loadedPluginIds_.end()) {
    return false;
  }

  auto plugin = createPluginInstance(pluginId);
  if (!plugin) {
    return false;
  }

  instrumentSlots_[instrument] = pluginId;
  instrumentPlugins_[instrument] = std::move(plugin);
  if (pluginId != "builtin.sample") {
    instrumentSampleSlots_[instrument] = -1;
  }
  return true;
}

bool PluginHost::hasInstrumentAssignment(std::uint8_t instrument) const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidInstrument(instrument)) {
    return false;
  }
  return !instrumentSlots_[instrument].empty() ||
         (static_cast<std::size_t>(instrument) < sampleSlotPaths_.size() &&
          !sampleSlotPaths_[instrument].empty());
}

std::string PluginHost::pluginForInstrument(std::uint8_t instrument) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return "";
  }
  if (!isValidInstrument(instrument)) {
    return "";
  }
  if (instrumentSlots_[instrument].empty() &&
      static_cast<std::size_t>(instrument) < sampleSlotPaths_.size() &&
      !sampleSlotPaths_[instrument].empty()) {
    return "builtin.sample";
  }
  return instrumentSlots_[instrument];
}

bool PluginHost::isValidInstrument(std::uint8_t instrument) const {
  return instrument < kMaxInstrumentSlots;
}

bool PluginHost::isValidSampleSlot(std::uint16_t sampleSlot) const {
  return sampleSlot < kMaxSampleSlots;
}

bool PluginHost::triggerNoteOn(std::uint8_t instrument, int midiNote, std::uint8_t velocity, bool retrigger) {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (!isValidInstrument(instrument)) {
    return false;
  }

  if (!instrumentSlots_[instrument].empty() && instrumentPlugins_[instrument]) {
    instrumentPlugins_[instrument]->noteOn(midiNote, velocity, retrigger);
    noteOnEventCount_ += 1;
    return true;
  }

  if (static_cast<std::size_t>(instrument) >= sampleSlotPlugins_.size() ||
      sampleSlotPaths_[instrument].empty() ||
      !sampleSlotPlugins_[instrument]) {
    return false;
  }

  sampleSlotPlugins_[instrument]->noteOn(midiNote, velocity, retrigger);
  noteOnEventCount_ += 1;
  return true;
}

bool PluginHost::triggerNoteOff(std::uint8_t instrument, int midiNote) {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (!isValidInstrument(instrument)) {
    return false;
  }

  if (!instrumentSlots_[instrument].empty() && instrumentPlugins_[instrument]) {
    instrumentPlugins_[instrument]->noteOff(midiNote);
    noteOffEventCount_ += 1;
    return true;
  }

  if (static_cast<std::size_t>(instrument) >= sampleSlotPlugins_.size() ||
      sampleSlotPaths_[instrument].empty() ||
      !sampleSlotPlugins_[instrument]) {
    return false;
  }

  sampleSlotPlugins_[instrument]->noteOff(midiNote);

  noteOffEventCount_ += 1;
  return true;
}

bool PluginHost::triggerNoteOnResolved(std::uint8_t instrument,
                                       std::uint16_t sampleSlot,
                                       int midiNote,
                                       std::uint8_t velocity,
                                       bool retrigger) {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  // If a pattern step explicitly carries a sample slot, that sample has priority over instrument plugins.
  if (sampleSlot != 0xFFFF &&
      isValidSampleSlot(sampleSlot) &&
      !sampleSlotPaths_[sampleSlot].empty() &&
      sampleSlotPlugins_[sampleSlot]) {
    sampleSlotPlugins_[sampleSlot]->noteOn(midiNote, velocity, retrigger);
    noteOnEventCount_ += 1;
    return true;
  }

  if (!isValidInstrument(instrument)) {
    return false;
  }

  if (!instrumentSlots_[instrument].empty() && instrumentPlugins_[instrument]) {
    instrumentPlugins_[instrument]->noteOn(midiNote, velocity, retrigger);
    noteOnEventCount_ += 1;
    return true;
  }

  if (static_cast<std::size_t>(instrument) >= sampleSlotPlugins_.size() ||
      sampleSlotPaths_[instrument].empty() ||
      !sampleSlotPlugins_[instrument]) {
    return false;
  }

  sampleSlotPlugins_[instrument]->noteOn(midiNote, velocity, retrigger);
  noteOnEventCount_ += 1;
  return true;
}

bool PluginHost::triggerNoteOffResolved(std::uint8_t instrument, std::uint16_t sampleSlot, int midiNote) {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (sampleSlot != 0xFFFF &&
      isValidSampleSlot(sampleSlot) &&
      !sampleSlotPaths_[sampleSlot].empty() &&
      sampleSlotPlugins_[sampleSlot]) {
    sampleSlotPlugins_[sampleSlot]->noteOff(midiNote);
    noteOffEventCount_ += 1;
    return true;
  }

  if (!isValidInstrument(instrument)) {
    return false;
  }

  if (!instrumentSlots_[instrument].empty() && instrumentPlugins_[instrument]) {
    instrumentPlugins_[instrument]->noteOff(midiNote);
    noteOffEventCount_ += 1;
    return true;
  }

  if (static_cast<std::size_t>(instrument) >= sampleSlotPlugins_.size() ||
      sampleSlotPaths_[instrument].empty() ||
      !sampleSlotPlugins_[instrument]) {
    return false;
  }

  sampleSlotPlugins_[instrument]->noteOff(midiNote);
  noteOffEventCount_ += 1;
  return true;
}

void PluginHost::allNotesOff() {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return;
  }

  for (auto& plugin : instrumentPlugins_) {
    if (!plugin) {
      continue;
    }
    plugin->allNotesOff();
  }
  for (auto& plugin : sampleSlotPlugins_) {
    if (!plugin) {
      continue;
    }
    plugin->allNotesOff();
  }
}

bool PluginHost::renderInterleaved(std::vector<double>& monoBuffer, std::uint32_t sampleRate) {
  std::lock_guard<std::timed_mutex> lock(mutex_);

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
  for (std::size_t i = 0; i < sampleSlotPlugins_.size(); ++i) {
    if (!sampleSlotPlugins_[i] || sampleSlotPaths_[i].empty()) {
      continue;
    }
    sampleSlotPlugins_[i]->renderAdd(monoBuffer, sampleRate);
    anyRendered = anyRendered || sampleSlotPlugins_[i]->activeVoiceCount() > 0;
  }

  return anyRendered;
}

bool PluginHost::setInstrumentParameter(std::uint8_t instrument, const std::string& name, double value) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }
  if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) {
    return false;
  }
  return instrumentPlugins_[instrument]->setParameter(name, value);
}

double PluginHost::getInstrumentParameter(std::uint8_t instrument, const std::string& name) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return 0.0;
  }
  if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) {
    return 0.0;
  }
  return instrumentPlugins_[instrument]->getParameter(name);
}

bool PluginHost::loadSampleToSlot(std::uint16_t sampleSlot, const std::string& wavPath) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot) || wavPath.empty()) {
    return false;
  }

  SampleData validated;
  if (!loadWavFile(wavPath, validated)) {
    return false;
  }

  if (loadedPluginIds_.find("builtin.sample") == loadedPluginIds_.end()) {
    loadedPluginIds_.insert("builtin.sample");
    loadedPluginCount_ += 1;
  }

  if (!sampleSlotPlugins_[sampleSlot]) {
    auto plugin = createPluginInstance("builtin.sample");
    if (!plugin) {
      return false;
    }
    sampleSlotPlugins_[sampleSlot] = std::move(plugin);
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin || !samplePlugin->loadSample(wavPath)) {
    return false;
  }

  sampleSlotPaths_[sampleSlot] = wavPath;
  return true;
}

bool PluginHost::saveSampleFromSlot(std::uint16_t sampleSlot, const std::string& wavPath) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot) || wavPath.empty()) {
    return false;
  }

  if (const auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get())) {
    return samplePlugin->saveSample(wavPath);
  }

  const std::string& sourcePath = sampleSlotPaths_[sampleSlot];
  if (sourcePath.empty()) {
    return false;
  }

  SampleData source;
  if (!loadWavFile(sourcePath, source)) {
    return false;
  }
  return saveWavFile(wavPath, source);
}

bool PluginHost::clearSampleSlot(std::uint16_t sampleSlot) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  sampleSlotPaths_[sampleSlot].clear();
  sampleSlotNames_[sampleSlot].clear();
  sampleSlotPlugins_[sampleSlot].reset();
  for (std::size_t instrument = 0; instrument < instrumentSampleSlots_.size(); ++instrument) {
    if (instrumentSampleSlots_[instrument] == static_cast<int>(sampleSlot)) {
      instrumentSampleSlots_[instrument] = -1;
    }
  }
  return true;
}

std::string PluginHost::samplePathForSlot(std::uint16_t sampleSlot) const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidSampleSlot(sampleSlot)) {
    return "";
  }
  return sampleSlotPaths_[sampleSlot];
}

bool PluginHost::setSampleNameForSlot(std::uint16_t sampleSlot, const std::string& name) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }
  sampleSlotNames_[sampleSlot] = name;
  return true;
}

std::string PluginHost::sampleNameForSlot(std::uint16_t sampleSlot) const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidSampleSlot(sampleSlot)) {
    return "";
  }
  return sampleSlotNames_[sampleSlot];
}

bool PluginHost::assignSampleSlotToInstrument(std::uint16_t sampleSlot, std::uint8_t instrument) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot) || !isValidInstrument(instrument)) {
    return false;
  }

  const std::string& sourcePath = sampleSlotPaths_[sampleSlot];
  if (sourcePath.empty()) {
    return false;
  }

  if (loadedPluginIds_.find("builtin.sample") == loadedPluginIds_.end()) {
    loadedPluginIds_.insert("builtin.sample");
    loadedPluginCount_ += 1;
  }

  if (!instrumentPlugins_[instrument] || instrumentSlots_[instrument] != "builtin.sample") {
    auto plugin = createPluginInstance("builtin.sample");
    if (!plugin) {
      return false;
    }
    instrumentSlots_[instrument] = "builtin.sample";
    instrumentPlugins_[instrument] = std::move(plugin);
  }

  auto* samplePlugin = asSamplePlugin(instrumentPlugins_[instrument].get());
  if (!samplePlugin || !samplePlugin->loadSample(sourcePath)) {
    return false;
  }

  instrumentSampleSlots_[instrument] = static_cast<int>(sampleSlot);
  return true;
}

int PluginHost::sampleSlotForInstrument(std::uint8_t instrument) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidInstrument(instrument)) {
    return -1;
  }
  return instrumentSampleSlots_[instrument];
}

bool PluginHost::loadSampleToInstrument(std::uint8_t instrument, const std::string& wavPath) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidInstrument(instrument) || wavPath.empty()) {
    return false;
  }

  if (loadedPluginIds_.find("builtin.sample") == loadedPluginIds_.end()) {
    loadedPluginIds_.insert("builtin.sample");
    loadedPluginCount_ += 1;
  }

  if (!instrumentPlugins_[instrument] || instrumentSlots_[instrument] != "builtin.sample") {
    auto plugin = createPluginInstance("builtin.sample");
    if (!plugin) {
      return false;
    }
    instrumentSlots_[instrument] = "builtin.sample";
    instrumentPlugins_[instrument] = std::move(plugin);
  }

  auto* samplePlugin = asSamplePlugin(instrumentPlugins_[instrument].get());
  if (!samplePlugin) {
    return false;
  }
  const bool loaded = samplePlugin->loadSample(wavPath);
  if (loaded) {
    instrumentSampleSlots_[instrument] = -1;
  }
  return loaded;
}

bool PluginHost::saveSampleFromInstrument(std::uint8_t instrument, const std::string& wavPath) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidInstrument(instrument) || wavPath.empty()) {
    return false;
  }

  const auto* samplePlugin = asSamplePlugin(instrumentPlugins_[instrument].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->saveSample(wavPath);
}

bool PluginHost::clearSampleFromInstrument(std::uint8_t instrument) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidInstrument(instrument)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(instrumentPlugins_[instrument].get());
  if (!samplePlugin) {
    return false;
  }
  samplePlugin->clearSample();
  instrumentSampleSlots_[instrument] = -1;
  return true;
}

std::string PluginHost::samplePathForInstrument(std::uint8_t instrument) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidInstrument(instrument)) {
    return "";
  }

  const int mappedSlot = instrumentSampleSlots_[instrument];
  if (mappedSlot >= 0 && mappedSlot < static_cast<int>(sampleSlotPaths_.size())) {
    const std::string& mappedPath = sampleSlotPaths_[static_cast<std::size_t>(mappedSlot)];
    if (!mappedPath.empty()) {
      return mappedPath;
    }
  }

  const auto* samplePlugin = asSamplePlugin(instrumentPlugins_[instrument].get());
  if (!samplePlugin) {
    return "";
  }
  return samplePlugin->samplePath();
}

std::size_t PluginHost::activeVoiceCountForInstrument(std::uint8_t instrument) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return 0;
  }
  if (!isValidInstrument(instrument)) {
    return 0;
  }
  if (instrumentPlugins_[instrument]) {
    return instrumentPlugins_[instrument]->activeVoiceCount();
  }
  if (static_cast<std::size_t>(instrument) < sampleSlotPlugins_.size() && sampleSlotPlugins_[instrument]) {
    return sampleSlotPlugins_[instrument]->activeVoiceCount();
  }
  return 0;
}

double PluginHost::activeVoiceFrequencyHzForInstrument(std::uint8_t instrument, std::size_t voiceIndex) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return 0.0;
  }
  if (!isValidInstrument(instrument)) {
    return 0.0;
  }
  if (instrumentPlugins_[instrument]) {
    return instrumentPlugins_[instrument]->activeVoiceFrequencyHz(voiceIndex);
  }
  if (static_cast<std::size_t>(instrument) < sampleSlotPlugins_.size() && sampleSlotPlugins_[instrument]) {
    return sampleSlotPlugins_[instrument]->activeVoiceFrequencyHz(voiceIndex);
  }
  return 0.0;
}

std::size_t PluginHost::noteOnEventCount() const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return noteOnEventCount_;
}

std::size_t PluginHost::noteOffEventCount() const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return noteOffEventCount_;
}

std::size_t PluginHost::activeRenderVoiceCount() const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  std::size_t activeCount = 0;
  for (std::size_t i = 0; i < instrumentPlugins_.size(); ++i) {
    if (instrumentPlugins_[i]) {
      activeCount += instrumentPlugins_[i]->activeVoiceCount();
    }
  }
  for (std::size_t i = 0; i < sampleSlotPlugins_.size(); ++i) {
    if (sampleSlotPlugins_[i] && !sampleSlotPaths_[i].empty()) {
      activeCount += sampleSlotPlugins_[i]->activeVoiceCount();
    }
  }
  return activeCount;
}

double PluginHost::activeRenderVoiceFrequencyHz(std::size_t voiceIndex) const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
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
  for (std::size_t i = 0; i < sampleSlotPlugins_.size(); ++i) {
    if (!sampleSlotPlugins_[i] || sampleSlotPaths_[i].empty()) {
      continue;
    }
    std::size_t count = sampleSlotPlugins_[i]->activeVoiceCount();
    if (voiceIndex < currentIndex + count) {
      return sampleSlotPlugins_[i]->activeVoiceFrequencyHz(voiceIndex - currentIndex);
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
