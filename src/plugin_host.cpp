#include "extracker/plugin_host.hpp"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define EXTRACKER_DL_OPEN(path)  static_cast<void*>(LoadLibraryA(path))
#  define EXTRACKER_DL_SYM(h,sym)  static_cast<void*>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h),(sym))))
#  define EXTRACKER_DL_CLOSE(h)    FreeLibrary(static_cast<HMODULE>(h))
#else
#  include <dlfcn.h>
#  include <unistd.h>
#  include <sys/wait.h>
#  include <dirent.h>
// EXTRACKER_DL_OPEN: used inside forked scan children where only one plugin is
// ever loaded. RTLD_DEEPBIND isolates the plugin's embedded JUCE from the parent's
// JUCE symbols so global-constructor crashes are contained to the child process.
#  define EXTRACKER_DL_OPEN(path)  dlopen((path), RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND)
// EXTRACKER_DL_OPEN_LOAD: used in the parent process to instantiate a plugin for
// actual use. No RTLD_DEEPBIND: the plugin shares the host's running JUCE
// MessageManager, Desktop, etc., which makes IPlugView::createView() work.
// JUCE 7.0.x minor versions are ABI-compatible, so using host JUCE 7.0.10
// symbols from a plugin built against JUCE 7.0.8 is safe.
// RTLD_NODELETE: keep the .so mapped after dlclose() so the plugin's JUCE
// global destructors never run while our JUCE runtime is still alive —
// dlclose() of a JUCE plugin while the host JUCE is running causes
// "free(): invalid pointer" as both runtimes fight over the same singletons.
#  define EXTRACKER_DL_OPEN_LOAD(path) dlopen((path), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE)
#  define EXTRACKER_DL_SYM(h,sym)  dlsym((h),(sym))
#  define EXTRACKER_DL_CLOSE(h)    dlclose(h)
#endif

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
#include <unordered_map>
#include <mutex>
#include <string>
#include <string_view>

#ifdef EXTRACKER_HAVE_FLUIDSYNTH
#  include <fluidsynth.h>
#endif
#ifdef EXTRACKER_HAVE_SFIZZ
#  include <sfizz.h>
#endif

namespace {

#ifndef _WIN32
// Close all file descriptors > 2 except keepFd in the child after fork().
// Prevents inherited PipeWire/X11/audio fds from being touched by dlopen'd
// plugin global constructors, which would corrupt shared state in the parent.
static void closeInheritedFds(int keepFd = -1) {
  DIR* dir = opendir("/proc/self/fd");
  if (dir) {
    const int dirFd = dirfd(dir);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      char* end;
      const long fd = strtol(entry->d_name, &end, 10);
      if (*end != '\0' || fd <= 2 || fd == dirFd || fd == keepFd) continue;
      close(static_cast<int>(fd));
    }
    closedir(dir);
  } else {
    for (int fd = 3; fd < 1024; ++fd) {
      if (fd != keepFd) close(fd);
    }
  }
}
#endif

struct PluginVoice {
  int midiNote = -1;
  double frequencyHz = 0.0;
  double phase = 0.0;
  double level = 0.0;
  double targetLevel = 1.0;
  bool releasing = false;
};

struct SampleData {
  std::uint32_t sampleRate  = 44100;
  std::vector<float> mono;
  std::size_t loopStart     = 0;
  std::size_t loopEnd       = 0;  // 0 = no loop
  int         loopType      = 0;  // 0=forward, 1=ping-pong
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

bool readU16BE(std::istream& in, std::uint16_t& out) {
  unsigned char b[2]{};
  in.read(reinterpret_cast<char*>(b), 2);
  if (!in) return false;
  out = static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[0]) << 8) | b[1]);
  return true;
}

bool readU32BE(std::istream& in, std::uint32_t& out) {
  unsigned char b[4]{};
  in.read(reinterpret_cast<char*>(b), 4);
  if (!in) return false;
  out = (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
        (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
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
    } else if (id == "smpl" && chunkSize >= 36) {
      // smpl chunk: 9 fixed fields × 4 = 36 bytes, then numSampleLoops × 24 bytes
      const std::streampos smplStart = in.tellg();
      in.seekg(28, std::ios::cur);  // skip manufacturer..SMPTEOffset (7 × 4)
      std::uint32_t numLoops = 0;
      if (readU32LE(in, numLoops)) {
        in.seekg(4, std::ios::cur);  // skip samplerData
        if (numLoops > 0 && chunkSize >= 60) {  // 36 header + 24 first loop record
          std::uint32_t cueId = 0, loopType = 0, loopStart = 0, loopEnd = 0;
          if (readU32LE(in, cueId) && readU32LE(in, loopType) &&
              readU32LE(in, loopStart) && readU32LE(in, loopEnd)) {
            out.loopStart = static_cast<std::size_t>(loopStart);
            out.loopEnd   = static_cast<std::size_t>(loopEnd) + 1;  // smpl end is inclusive
            out.loopType  = (loopType == 1) ? 1 : 0;
          }
        }
      }
      // Always seek to the exact end of the smpl chunk regardless of what we read
      in.seekg(smplStart + static_cast<std::streamoff>(chunkSize));
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

  std::vector<std::string> listParameters() const override {
    return {"gain", "attack_ms", "release_ms"};
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

    if (!retrigger) return;  // don't restart a finished sample on per-tick re-issues

    Voice voice;
    voice.midiNote = midiNote;
    voice.level = vel * gain_;
    voice.pitchRatio = std::pow(2.0, static_cast<double>(midiNote - rootMidiNote_) / 12.0);
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
    voices_.clear();
  }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (monoBuffer.empty() || sampleRate == 0 || sample_.mono.empty()) {
      return;
    }

    const double baseStep = static_cast<double>(sample_.sampleRate) / static_cast<double>(sampleRate);
    const double releaseStep = 1.0 / std::max<double>(static_cast<double>(sampleRate) * 0.03, 1.0);
    const std::size_t sampleSize = sample_.mono.size();
    const std::size_t loopEndEff = (loopEnd_ != std::numeric_limits<std::size_t>::max() && loopEnd_ <= sampleSize)
        ? loopEnd_ : sampleSize;
    const std::size_t loopStartEff = (loopStart_ < loopEndEff) ? loopStart_ : 0;

    for (std::size_t frame = 0; frame < monoBuffer.size(); ++frame) {
      double mixed = 0.0;
      std::size_t activeCount = 0;

      for (auto& voice : voices_) {
        if (!voice.active) {
          continue;
        }

        // Apply loop wrapping before index computation
        if (loopMode_ != 0) {
          if (loopMode_ == 1) {  // forward loop
            if (voice.pos >= static_cast<double>(loopEndEff)) {
              voice.pos = static_cast<double>(loopStartEff);
            }
          } else if (loopMode_ == 2) {  // bidirectional
            if (voice.direction > 0.0 && voice.pos >= static_cast<double>(loopEndEff)) {
              voice.pos = static_cast<double>(loopEndEff > 0 ? loopEndEff - 1 : 0);
              voice.direction = -1.0;
            } else if (voice.direction < 0.0 && voice.pos < static_cast<double>(loopStartEff)) {
              voice.pos = static_cast<double>(loopStartEff);
              voice.direction = 1.0;
            }
          } else if (loopMode_ == 3) {  // sustain loop
            if (!voice.releasing && voice.pos >= static_cast<double>(loopEndEff)) {
              voice.pos = static_cast<double>(loopStartEff);
            }
          }
        }

        if (voice.pos < 0.0) {
          voice.pos = 0.0;
        }

        const std::size_t idx = static_cast<std::size_t>(voice.pos);
        if (idx >= sampleSize) {
          voice.active = false;
          continue;
        }

        std::size_t nextIdx = std::min(idx + 1, sampleSize - 1);
        const double frac = voice.pos - static_cast<double>(idx);
        const double sampleValue =
            static_cast<double>(sample_.mono[idx]) * (1.0 - frac) +
            static_cast<double>(sample_.mono[nextIdx]) * frac;

        if (voice.releasing) {
          voice.envelope = std::max(0.0, voice.envelope - releaseStep);
        } else {
          voice.envelope = 1.0;
        }

        mixed += sampleValue * voice.level * voice.envelope;
        voice.pos += baseStep * voice.pitchRatio * voice.direction;
        if (voice.envelope <= 0.0) {
          voice.active = false;
        }
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
      gain_ = std::clamp(value, 0.0, 2.0);
      return true;
    }
    if (name == "sample_root") {
      rootMidiNote_ = static_cast<int>(std::clamp(value, 0.0, 127.0));
      return true;
    }
    if (name == "pan") {
      pan_ = std::clamp(value, 0.0, 1.0);
      return true;
    }
    if (name == "loop_mode") {
      loopMode_ = static_cast<int>(std::clamp(value, 0.0, 3.0));
      return true;
    }
    if (name == "loop_start") {
      loopStart_ = static_cast<std::size_t>(std::max(value, 0.0));
      return true;
    }
    if (name == "loop_end") {
      if (value <= 0.0) {
        loopEnd_ = std::numeric_limits<std::size_t>::max();
      } else {
        loopEnd_ = static_cast<std::size_t>(value);
      }
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
    if (name == "pan") {
      return pan_;
    }
    if (name == "loop_mode") {
      return static_cast<double>(loopMode_);
    }
    if (name == "loop_start") {
      return static_cast<double>(loopStart_);
    }
    if (name == "loop_end") {
      return (loopEnd_ == std::numeric_limits<std::size_t>::max()) ? 0.0 : static_cast<double>(loopEnd_);
    }
    return 0.0;
  }

  std::vector<std::string> listParameters() const override {
    return {"gain", "sample_root", "pan", "loop_mode", "loop_start", "loop_end"};
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
    sourceSample_ = loaded;
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
    sourceSample_ = SampleData{};
    samplePath_.clear();
    voices_.clear();
  }

  std::string samplePath() const {
    return samplePath_;
  }

  std::size_t sampleFrameCount() const {
    return sample_.mono.size();
  }

  std::size_t sourceFrameCount() const {
    return sourceSample_.mono.size();
  }

  std::uint32_t sampleRateValue() const {
    return sample_.sampleRate;
  }

  bool trimFrames(std::size_t startFrame, std::size_t endFrameExclusive) {
    if (sourceSample_.mono.empty()) {
      return false;
    }
    if (startFrame >= endFrameExclusive || endFrameExclusive > sourceSample_.mono.size()) {
      return false;
    }

    sample_.mono = std::vector<float>(sourceSample_.mono.begin() + startFrame,
                                      sourceSample_.mono.begin() + endFrameExclusive);
    voices_.clear();
    return !sample_.mono.empty();
  }

  bool restoreSource() {
    if (sourceSample_.mono.empty()) {
      return false;
    }
    sample_ = sourceSample_;
    voices_.clear();
    return true;
  }

  bool normalizeFrames(std::size_t startFrame, std::size_t endFrameExclusive) {
    if (sample_.mono.empty() || startFrame >= endFrameExclusive || endFrameExclusive > sample_.mono.size()) {
      return false;
    }

    float maxAbs = 0.0f;
    for (std::size_t i = startFrame; i < endFrameExclusive; ++i) {
      maxAbs = std::max(maxAbs, std::abs(sample_.mono[i]));
    }
    if (maxAbs <= 0.000001f) {
      return false;
    }

    const float gain = 1.0f / maxAbs;
    for (std::size_t i = startFrame; i < endFrameExclusive; ++i) {
      sample_.mono[i] = std::clamp(sample_.mono[i] * gain, -1.0f, 1.0f);
    }
    return true;
  }

  bool fadeInFrames(std::size_t startFrame, std::size_t endFrameExclusive) {
    if (sample_.mono.empty() || startFrame >= endFrameExclusive || endFrameExclusive > sample_.mono.size()) {
      return false;
    }
    const double denom = std::max<double>(static_cast<double>(endFrameExclusive - startFrame - 1), 1.0);
    for (std::size_t i = startFrame; i < endFrameExclusive; ++i) {
      const double t = static_cast<double>(i - startFrame) / denom;
      sample_.mono[i] = static_cast<float>(sample_.mono[i] * t);
    }
    return true;
  }

  bool fadeOutFrames(std::size_t startFrame, std::size_t endFrameExclusive) {
    if (sample_.mono.empty() || startFrame >= endFrameExclusive || endFrameExclusive > sample_.mono.size()) {
      return false;
    }
    const double denom = std::max<double>(static_cast<double>(endFrameExclusive - startFrame - 1), 1.0);
    for (std::size_t i = startFrame; i < endFrameExclusive; ++i) {
      const double t = static_cast<double>(i - startFrame) / denom;
      sample_.mono[i] = static_cast<float>(sample_.mono[i] * (1.0 - t));
    }
    return true;
  }

  bool reverseFrames(std::size_t startFrame, std::size_t endFrameExclusive) {
    if (sample_.mono.empty() || startFrame >= endFrameExclusive || endFrameExclusive > sample_.mono.size()) {
      return false;
    }
    std::reverse(sample_.mono.begin() + startFrame, sample_.mono.begin() + endFrameExclusive);
    voices_.clear();
    return true;
  }

  bool resampleTo(std::uint32_t newRate) {
    if (sample_.mono.empty() || newRate < 1000 || newRate > 192000) {
      return false;
    }
    if (newRate == sample_.sampleRate) {
      return false;
    }

    const std::size_t oldLen = sample_.mono.size();
    // source-frames advanced per output-frame; >1 when downsampling.
    const double ratio = static_cast<double>(sample_.sampleRate) / static_cast<double>(newRate);
    const std::size_t newLen = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(static_cast<double>(oldLen) / ratio)));

    // Lanczos-3 windowed sinc. When downsampling (ratio > 1) the kernel is widened
    // in source space by `ratio` so it band-limits to the new Nyquist (anti-aliasing).
    constexpr int kA = 3;
    const double scale = std::max(ratio, 1.0);      // kernel stretch in source space
    const double invScale = 1.0 / scale;
    const double support = static_cast<double>(kA) * scale;

    auto lanczos = [](double x) -> double {
      if (x == 0.0) return 1.0;
      if (x <= -kA || x >= kA) return 0.0;
      const double px = M_PI * x;
      return (std::sin(px) / px) * (std::sin(px / kA) / (px / kA));
    };

    std::vector<float> out(newLen, 0.0f);
    for (std::size_t i = 0; i < newLen; ++i) {
      const double srcPos = static_cast<double>(i) * ratio;
      const long first = static_cast<long>(std::floor(srcPos - support)) + 1;
      const long last = static_cast<long>(std::floor(srcPos + support));

      double acc = 0.0;
      double wsum = 0.0;
      for (long s = first; s <= last; ++s) {
        const double w = lanczos((srcPos - static_cast<double>(s)) * invScale);
        if (w == 0.0) continue;
        const std::size_t idx = static_cast<std::size_t>(
            std::clamp<long>(s, 0, static_cast<long>(oldLen) - 1));
        acc += static_cast<double>(sample_.mono[idx]) * w;
        wsum += w;
      }
      out[i] = (wsum != 0.0) ? static_cast<float>(std::clamp(acc / wsum, -1.0, 1.0)) : 0.0f;
    }

    sample_.mono = std::move(out);
    sample_.sampleRate = newRate;
    voices_.clear();
    return true;
  }

  bool quantizeBits(int bits, std::size_t startFrame, std::size_t endFrameExclusive) {
    if (sample_.mono.empty() || bits < 1 || bits > 32) {
      return false;
    }
    if (startFrame >= endFrameExclusive || endFrameExclusive > sample_.mono.size()) {
      return false;
    }
    const double levels = std::pow(2.0, bits - 1);
    for (std::size_t i = startFrame; i < endFrameExclusive; ++i) {
      const double q = std::round(static_cast<double>(sample_.mono[i]) * levels) / levels;
      sample_.mono[i] = static_cast<float>(std::clamp(q, -1.0, 1.0));
    }
    voices_.clear();
    return true;
  }

  // Smooth the forward/sustain loop seam by blending the loop tail with the
  // audio just before loopStart (equal-power crossfade), so wrapping from
  // loopEnd back to loopStart is click-free.
  bool crossfadeLoop(std::size_t lengthFrames) {
    if (sample_.mono.empty()) {
      return false;
    }
    if (loopMode_ != 1 && loopMode_ != 3) {  // forward / sustain only
      return false;
    }
    const std::size_t sampleSize = sample_.mono.size();
    const std::size_t loopEnd = (loopEnd_ != std::numeric_limits<std::size_t>::max() && loopEnd_ <= sampleSize)
        ? loopEnd_ : sampleSize;
    const std::size_t loopStart = loopStart_;
    if (loopStart < 1 || loopEnd <= loopStart) {
      return false;
    }

    const std::size_t n = std::min({lengthFrames, loopStart, loopEnd - loopStart});
    if (n == 0) {
      return false;
    }

    const double denom = std::max<double>(static_cast<double>(n) - 1.0, 1.0);
    for (std::size_t i = 0; i < n; ++i) {
      const double t = static_cast<double>(i) / denom;
      const double gOut = std::cos(t * M_PI / 2.0);   // loop tail fades out
      const double gIn = std::sin(t * M_PI / 2.0);    // pre-loopStart fades in
      const std::size_t tailIdx = loopEnd - n + i;
      const std::size_t preIdx = loopStart - n + i;
      const double mixed = static_cast<double>(sample_.mono[tailIdx]) * gOut +
                           static_cast<double>(sample_.mono[preIdx]) * gIn;
      sample_.mono[tailIdx] = static_cast<float>(std::clamp(mixed, -1.0, 1.0));
    }
    voices_.clear();
    return true;
  }

  std::vector<float> waveformPreview(std::size_t maxPoints) const {
    if (sample_.mono.empty() || maxPoints == 0) {
      return {};
    }

    const std::size_t points = std::min<std::size_t>(maxPoints, sample_.mono.size());
    std::vector<float> preview(points, 0.0f);
    for (std::size_t i = 0; i < points; ++i) {
      const double pos = static_cast<double>(i) * static_cast<double>(sample_.mono.size() - 1) /
                         static_cast<double>(std::max<std::size_t>(points - 1, 1));
      preview[i] = sample_.mono[static_cast<std::size_t>(std::llround(pos))];
    }
    return preview;
  }

private:
  struct Voice {
    int midiNote = -1;
    double pos = 0.0;
    double level = 0.0;
    double pitchRatio = 1.0;
    bool active = true;
    bool releasing = false;
    double envelope = 1.0;
    double direction = 1.0;  // 1.0 = forward, -1.0 = reverse (bidirectional loop)
  };

  SampleData sample_;
  SampleData sourceSample_;
  std::vector<Voice> voices_;
  std::string samplePath_;
  int rootMidiNote_ = 60;
  double gain_ = 1.0;
  double pan_ = 0.5;
  int loopMode_ = 0;  // 0=none, 1=forward, 2=bidi, 3=sustain
  std::size_t loopStart_ = 0;
  std::size_t loopEnd_ = std::numeric_limits<std::size_t>::max();
};

#ifndef _WIN32  // LV2 is not supported on Windows

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
  bool isCc = false;      // if true, midiNote holds the CC number and velocity holds the value
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

// Growing URID map: a conformant urid:map must return a stable, unique,
// non-zero id for ANY uri the plugin asks about — not just the two the event
// bridge hard-codes. Returning 0 made real plugins (Calf, synthv1, ...) abort
// inside their post_instantiate() asserts. We reserve the two fixed ids used by
// populateEventBuffer and allocate fresh ids (>= 3) for everything else.
struct Lv2UridRegistry {
  std::mutex mutex;
  std::unordered_map<std::string, Lv2Urid> ids;
  Lv2Urid next = 3;  // ids 1 and 2 are reserved for the two URIs below
};

static Lv2UridRegistry& uridRegistry() {
  static Lv2UridRegistry registry;
  return registry;
}

static Lv2Urid staticUridMap(void* /*handle*/, const char* uri) {
  if (uri == nullptr) {
    return 0;
  }
  const std::string_view view(uri);
  if (view == "http://lv2plug.in/ns/ext/atom#Sequence") {
    return kUridAtomSequence;
  }
  if (view == "http://lv2plug.in/ns/ext/midi#MidiEvent") {
    return kUridMidiEvent;
  }
  auto& registry = uridRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  auto [it, inserted] = registry.ids.emplace(std::string(view), registry.next);
  if (inserted) {
    registry.next += 1;
  }
  return it->second;
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
  int audioOutputPort2 = -1;  // second audio out (right channel for stereo)
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
                             std::vector<int> eventInputPorts = {},
                             int audioOutputPort2 = -1,
                             std::vector<extracker::PluginControlPortMeta> controlInputMeta = {})
      : uri_(uri),
        binaryPath_(binaryPath),
        audioInputPort_(audioInputPort),
        audioOutputPort_(audioOutputPort),
        audioOutputPort2_(audioOutputPort2),
        controlInputPorts_(std::move(controlInputPorts)),
        controlOutputPorts_(std::move(controlOutputPorts)),
        eventInputPorts_(std::move(eventInputPorts)),
        fallbackSynth_(),
        moduleHandle_(nullptr),
        descriptor_(nullptr),
        instance_(nullptr),
        loaded_(false) {
      controlInputValues_.resize(controlInputPorts_.size());
      for (std::size_t i = 0; i < controlInputValues_.size(); ++i) {
        controlInputValues_[i] = (i < controlInputMeta.size() && controlInputMeta[i].hasDefault)
                                 ? controlInputMeta[i].defaultVal : 0.0f;
      }
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
    // Only send a MIDI note-on for genuine new attacks. retrigger=false means a
    // continuation/modulation update (the sequencer calls this every tick for slides,
    // vibrato, arpeggio, etc.). Sending 0x90 every tick would stack hundreds of
    // un-stoppable voices in polyphonic LV2 synths.
    if (retrigger) {
      {
        std::lock_guard<std::mutex> lock(eventsMutex_);
        activePitches_.insert(midiNote);
      }
      queueEventNote(midiNote, velocity, true);
    }
  }

  void noteOff(int midiNote) override {
    fallbackSynth_.noteOff(midiNote);
    {
      std::lock_guard<std::mutex> lock(eventsMutex_);
      activePitches_.erase(midiNote);
    }
    queueEventNote(midiNote, 0, false);
  }

  void allNotesOff() override {
    fallbackSynth_.allNotesOff();
    {
      std::lock_guard<std::mutex> lock(eventsMutex_);
      activePitches_.clear();
    }
    // CC 120 = All Sound Off: immediate silence regardless of release envelope.
    // CC 123 = All Notes Off: triggers release (slower). We want hard stop.
    queueCcEvent(120, 0);
    queueCcEvent(123, 0);
  }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    {
      std::lock_guard<std::mutex> lock(eventsMutex_);
      if (activePitches_.empty()) {
        return;  // No note should be sounding — skip rendering entirely.
        // Any pending note-off events remain in pendingNoteEvents_ and will be
        // flushed on the next render cycle that follows a new note-on.
      }
    }
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
    if (name == "lv2_audio_output_port2") {
      return static_cast<double>(audioOutputPort2_);
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

  std::vector<std::string> listParameters() const override {
    // Return current control input values with their names from the registry (if available).
    std::vector<std::string> result;
    result.reserve(controlInputPorts_.size() + controlOutputPorts_.size());
    for (std::size_t i = 0; i < controlInputPorts_.size(); ++i) {
      result.push_back("lv2_control_in_" + std::to_string(i)
                       + " [port " + std::to_string(controlInputPorts_[i]) + "]"
                       + " = " + std::to_string(controlInputValues_[i]));
    }
    for (std::size_t i = 0; i < controlOutputPorts_.size(); ++i) {
      result.push_back("lv2_control_out_" + std::to_string(i)
                       + " [port " + std::to_string(controlOutputPorts_[i]) + "]"
                       + " = " + std::to_string(controlOutputValues_[i]) + " (output)");
    }
    return result;
  }

  bool savePreset(const std::string& path) const override {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write("LV2PRE", 6);
    const uint32_t count = static_cast<uint32_t>(controlInputValues_.size());
    f.write(reinterpret_cast<const char*>(&count), 4);
    f.write(reinterpret_cast<const char*>(controlInputValues_.data()),
            static_cast<std::streamsize>(count * sizeof(float)));
    return f.good();
  }

  bool loadPreset(const std::string& path) override {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[6]{};
    f.read(magic, 6);
    if (std::memcmp(magic, "LV2PRE", 6) != 0) return false;
    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), 4);
    if (count != static_cast<uint32_t>(controlInputValues_.size())) return false;
    f.read(reinterpret_cast<char*>(controlInputValues_.data()),
           static_cast<std::streamsize>(count * sizeof(float)));
    return f.good();
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
    std::lock_guard<std::mutex> lock(eventsMutex_);
    pendingNoteEvents_.push_back(event);
  }

  void queueCcEvent(int ccNumber, std::uint8_t value) {
    if (eventInputPorts_.empty()) {
      return;
    }
    NoteEvent event;
    event.midiNote = ccNumber;
    event.velocity = value;
    event.isCc = true;
    std::lock_guard<std::mutex> lock(eventsMutex_);
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
      if (audioOutputPort2_ >= 0 && audioOutputBufferR_.size() != frameCount) {
        audioOutputBufferR_.assign(frameCount, 0.0f);
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
    if (audioOutputPort2_ >= 0) audioOutputBufferR_.assign(frameCount, 0.0f);
    if (controlInputValues_.size() < controlInputPorts_.size()) {
      controlInputValues_.resize(controlInputPorts_.size(), 0.0f);
    }
    if (controlOutputValues_.size() < controlOutputPorts_.size()) {
      controlOutputValues_.resize(controlOutputPorts_.size(), 0.0f);
    }
    if (eventInputBuffer_.size() < 4096) {
      resizeAtomSequenceBuffer(4096);
    }

    // LV2 spec: every port must be connected before activate() or run().
    // Our TTL parser may have missed some ports (e.g. the Calf Organ has
    // 129 ports; sparse parsers leave gaps).  Pre-connect every port up to
    // the highest known index to a dummy buffer so the plugin never sees a
    // null pointer.  Known ports are overridden with real buffers below.
    {
      int maxPort = std::max({audioInputPort_, audioOutputPort_, audioOutputPort2_, -1});
      for (int p : controlInputPorts_)  maxPort = std::max(maxPort, p);
      for (int p : controlOutputPorts_) maxPort = std::max(maxPort, p);
      for (int p : eventInputPorts_)    maxPort = std::max(maxPort, p);

      if (maxPort >= 0) {
        static std::vector<float> sDummy;
        const std::size_t needed = std::max(frameCount, std::size_t{4096});
        if (sDummy.size() < needed) sDummy.assign(needed, 0.0f);
        for (int i = 0; i <= maxPort; ++i) {
          descriptor_->connectPort(instance_, static_cast<std::uint32_t>(i), sDummy.data());
        }
      }
    }

    if (audioInputPort_ >= 0) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioInputPort_), audioInputBuffer_.data());
    }
    descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioOutputPort_), audioOutputBuffer_.data());
    if (audioOutputPort2_ >= 0) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioOutputPort2_), audioOutputBufferR_.data());
    }
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
    if (audioOutputPort2_ >= 0 && audioOutputBufferR_.size() == monoBuffer.size()) {
      std::fill(audioOutputBufferR_.begin(), audioOutputBufferR_.end(), 0.0f);
    }

    if (audioInputPort_ >= 0) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioInputPort_), audioInputBuffer_.data());
    }
    descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioOutputPort_), audioOutputBuffer_.data());
    if (audioOutputPort2_ >= 0 && audioOutputBufferR_.size() == monoBuffer.size()) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioOutputPort2_), audioOutputBufferR_.data());
    }
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

    const bool hasStereo = audioOutputPort2_ >= 0 && audioOutputBufferR_.size() == monoBuffer.size();
    const double scale   = hasStereo ? 0.5 : 1.0;
    for (std::size_t i = 0; i < monoBuffer.size(); ++i) {
      double sample = static_cast<double>(audioOutputBuffer_[i]);
      if (hasStereo) sample = (sample + static_cast<double>(audioOutputBufferR_[i])) * scale;
      if (!std::isfinite(sample)) {
        shutdownRuntimeInstance();
        return false;
      }
      monoBuffer[i] += sample;
    }

    // Return true whenever the instance is running — zero output is valid
    // (e.g. an effect plugin given no audio input, or a synth between notes).
    return true;
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

    // Swap pending events into a local vector under the mutex so the sequencer
    // thread can safely queue new events concurrently with this audio-thread call.
    std::vector<NoteEvent> events;
    {
      std::lock_guard<std::mutex> lock(eventsMutex_);
      events.swap(pendingNoteEvents_);
    }

    auto* seq = reinterpret_cast<Lv2AtomSequence*>(eventInputBuffer_.data());
    const std::size_t headerSize = sizeof(Lv2AtomSequence);
    std::size_t writeOffset = headerSize;  // bytes written past the sequence header

    for (const auto& event : events) {
      // Each MIDI event encodes as (status, note, velocity). Note-off events are
      // sent as both 0x80 (explicit note-off) and 0x90+vel=0 (running-status form
      // that some LV2 plugins expect) to maximise compatibility.
      const bool isNoteOff = !event.isCc && !event.isNoteOn;
      const int copies = isNoteOff ? 2 : 1;

      for (int copy = 0; copy < copies; ++copy) {
        if (writeOffset + kAtomEventSize > eventInputBuffer_.size()) {
          break;
        }

        std::uint8_t status;
        if (event.isCc) {
          status = 0xB0;
        } else if (event.isNoteOn) {
          status = 0x90;
        } else {
          // copy 0 → 0x80 (standard note-off), copy 1 → 0x90+vel=0 (alt form)
          status = (copy == 0) ? 0x80 : 0x90;
        }
        const std::uint8_t note = static_cast<std::uint8_t>(event.midiNote);
        const std::uint8_t vel  = event.isCc ? event.velocity
                                 : (event.isNoteOn ? event.velocity : 0);

        auto* atomEvent = reinterpret_cast<Lv2AtomEvent*>(eventInputBuffer_.data() + writeOffset);
        atomEvent->frames    = 0;
        atomEvent->body.type = kUridMidiEvent;
        atomEvent->body.size = 3;
        uint8_t* midiBytes = eventInputBuffer_.data() + writeOffset + sizeof(Lv2AtomEvent);
        midiBytes[0] = status;
        midiBytes[1] = note;
        midiBytes[2] = vel;

        const std::size_t alignedEventSize = (sizeof(Lv2AtomEvent) + 3 + 7) & ~static_cast<std::size_t>(7);
        writeOffset += alignedEventSize;
        seq->atom.size += static_cast<std::uint32_t>(alignedEventSize);
      }
    }
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
  int audioOutputPort2_ = -1;  // right channel for stereo plugins
  std::vector<int> controlInputPorts_;
  std::vector<int> controlOutputPorts_;
  std::vector<int> eventInputPorts_;
  Lv2PlaceholderInstrumentPlugin fallbackSynth_;
  void* moduleHandle_;
  const Lv2Descriptor* descriptor_;
  Lv2Handle instance_;
  std::vector<float> audioInputBuffer_;
  std::vector<float> audioOutputBuffer_;
  std::vector<float> audioOutputBufferR_;  // right channel
  std::vector<float> controlInputValues_;
  std::vector<float> controlOutputValues_;
  std::vector<std::uint8_t> eventInputBuffer_;
  std::vector<NoteEvent> pendingNoteEvents_;
  std::unordered_set<int> activePitches_;  // MIDI notes currently supposed to sound
  std::mutex eventsMutex_;
  bool loaded_;
  bool runtimeActive_ = false;
};

// ---------------------------------------------------------------------------
// XPM (Akai MPC Keygroup) multi-sample instrument
// ---------------------------------------------------------------------------

static std::string xpmTagValue(const std::string& line, const std::string& tag) {
  const std::string open  = "<"  + tag + ">";
  const std::string close = "</" + tag + ">";
  const auto s = line.find(open);
  if (s == std::string::npos) return {};
  const auto e = line.find(close, s + open.size());
  if (e == std::string::npos) return {};
  return line.substr(s + open.size(), e - s - open.size());
}

class XpmKeygroupPlugin final : public extracker::IInstrumentPlugin {
public:
  struct Layer {
    bool active   = false;
    bool keyTrack = true;
    int  rootNote = 60;
    int  velStart = 0;
    int  velEnd   = 127;
    std::size_t sliceStart = 0;
    std::size_t sliceEnd   = 0;
    SampleData sample;
  };

  struct Zone {
    int   lowNote     = 0;
    int   highNote    = 127;
    int   rootNote    = -1;  // derived from first valid layer; -1 = unset
    float volume      = 1.0f;
    float decaySec    = 1.0f;
    float releaseSec  = 0.5f;
    std::vector<Layer> layers;
  };

  bool loadFromFile(const std::string& xpmPath) {
    std::ifstream f(xpmPath);
    if (!f) return false;

    // directory of the XPM for resolving WAV paths
    const std::string dir = [&] {
      const auto sep = xpmPath.rfind('/');
      return (sep == std::string::npos) ? std::string(".") : xpmPath.substr(0, sep);
    }();

    programName_ = xpmPath;
    if (const auto sep = programName_.rfind('/'); sep != std::string::npos)
      programName_ = programName_.substr(sep + 1);

    zones_.clear();

    Zone* curZone  = nullptr;
    Layer* curLayer = nullptr;
    bool inInstrument = false;
    bool inLayer      = false;

    std::string line;
    while (std::getline(f, line)) {
      // -- zone open/close
      if (line.find("<Instrument ") != std::string::npos && line.find("number=") != std::string::npos) {
        zones_.emplace_back();
        curZone  = &zones_.back();
        curLayer = nullptr;
        inInstrument = true;
        inLayer = false;
        continue;
      }
      if (line.find("</Instrument>") != std::string::npos) {
        inInstrument = false;
        inLayer = false;
        curZone  = nullptr;
        curLayer = nullptr;
        continue;
      }
      // -- layer open/close
      if (inInstrument && line.find("<Layer ") != std::string::npos && line.find("number=") != std::string::npos) {
        if (curZone) {
          curZone->layers.emplace_back();
          curLayer = &curZone->layers.back();
        }
        inLayer = true;
        continue;
      }
      if (inInstrument && line.find("</Layer>") != std::string::npos) {
        inLayer = false;
        curLayer = nullptr;
        continue;
      }

      if (!curZone) continue;

      // -- zone-level fields
      if (!inLayer) {
        if (const auto v = xpmTagValue(line, "LowNote");  !v.empty()) curZone->lowNote  = std::stoi(v);
        if (const auto v = xpmTagValue(line, "HighNote"); !v.empty()) curZone->highNote = std::stoi(v);
        if (const auto v = xpmTagValue(line, "Volume");   !v.empty()) curZone->volume   = std::stof(v);
        if (const auto v = xpmTagValue(line, "VolumeDecay");   !v.empty()) curZone->decaySec   = std::stof(v);
        if (const auto v = xpmTagValue(line, "VolumeRelease"); !v.empty()) curZone->releaseSec = std::stof(v);
        continue;
      }

      // -- layer-level fields
      if (!curLayer) continue;
      if (const auto v = xpmTagValue(line, "Active");     !v.empty()) curLayer->active   = (v == "True");
      if (const auto v = xpmTagValue(line, "KeyTrack");   !v.empty()) curLayer->keyTrack = (v != "False");
      if (const auto v = xpmTagValue(line, "RootNote");   !v.empty()) {
        curLayer->rootNote = std::stoi(v);
        if (curZone && curZone->rootNote < 0 && curLayer->rootNote > 0)
          curZone->rootNote = curLayer->rootNote;
      }
      if (const auto v = xpmTagValue(line, "VelStart");   !v.empty()) curLayer->velStart = std::stoi(v);
      if (const auto v = xpmTagValue(line, "VelEnd");     !v.empty()) curLayer->velEnd   = std::stoi(v);
      if (const auto v = xpmTagValue(line, "SliceStart"); !v.empty()) curLayer->sliceStart = static_cast<std::size_t>(std::stoull(v));
      if (const auto v = xpmTagValue(line, "SliceEnd");   !v.empty()) curLayer->sliceEnd   = static_cast<std::size_t>(std::stoull(v));

      if (const auto sampleName = xpmTagValue(line, "SampleName"); !sampleName.empty()) {
        // Try .WAV then .wav
        for (const char* ext : {".WAV", ".wav"}) {
          const std::string wavPath = dir + "/" + sampleName + ext;
          SampleData sd;
          if (loadWavFile(wavPath, sd)) {
            // Apply slice if set
            if (curLayer->sliceEnd > curLayer->sliceStart && curLayer->sliceEnd <= sd.mono.size()) {
              sd.mono = std::vector<float>(sd.mono.begin() + curLayer->sliceStart,
                                          sd.mono.begin() + curLayer->sliceEnd);
            }
            curLayer->sample = std::move(sd);
            break;
          }
        }
      }
    }

    return !zones_.empty();
  }

  void noteOn(int midiNote, std::uint8_t velocity, bool /*retrigger*/) override {
    const Zone* zone = findZone(midiNote);
    if (!zone) return;
    const Layer* layer = findLayer(*zone, velocity);
    if (!layer || layer->sample.mono.empty() || layer->sample.sampleRate == 0) return;

    const double vel = std::clamp(static_cast<double>(velocity) / 127.0, 0.0, 1.0);
    // Always pitch-track: use layer root when set, fall back to zone root, then no shift.
    // KeyTrack=False in F9 XPM means "fixed timbre per zone", not "fixed pitch".
    const int pivotNote = (layer->rootNote > 0) ? layer->rootNote
                        : (zone->rootNote > 0)  ? zone->rootNote
                        : midiNote;
    const double pitchRatio = std::pow(2.0, static_cast<double>(midiNote - pivotNote) / 12.0);

    // Retrigger existing voice for same note
    for (auto& v : voices_) {
      if (v.midiNote == midiNote && v.active) {
        v.pos = 0.0;
        v.level = vel * static_cast<double>(zone->volume);
        v.pitchRatio = pitchRatio;
        v.releasing = false;
        v.envelope = 1.0;
        v.layer = layer;
        v.decaySec = zone->decaySec;
        v.releaseSec = zone->releaseSec;
        return;
      }
    }

    Voice voice;
    voice.midiNote   = midiNote;
    voice.level      = vel * static_cast<double>(zone->volume);
    voice.pitchRatio = pitchRatio;
    voice.layer      = layer;
    voice.decaySec   = zone->decaySec;
    voice.releaseSec = zone->releaseSec;
    voices_.push_back(voice);
  }

  void noteOff(int midiNote) override {
    for (auto& v : voices_)
      if (v.midiNote == midiNote && v.active && !v.releasing)
        v.releasing = true;
  }

  void allNotesOff() override { voices_.clear(); }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (monoBuffer.empty() || sampleRate == 0) return;

    for (std::size_t frame = 0; frame < monoBuffer.size(); ++frame) {
      double mixed = 0.0;
      std::size_t active = 0;

      for (auto& v : voices_) {
        if (!v.active || !v.layer) continue;
        const auto& mono = v.layer->sample.mono;
        if (mono.empty()) continue;

        const SampleData& sd = v.layer->sample;
        const bool hasLoop = !v.releasing && sd.loopEnd > sd.loopStart &&
                             sd.loopEnd <= mono.size();
        const std::size_t loopS = sd.loopStart;
        const std::size_t loopE = sd.loopEnd;

        const double baseStep = static_cast<double>(sd.sampleRate) / static_cast<double>(sampleRate);

        // Sustain loop: wrap position while key is held
        if (hasLoop && v.pos >= static_cast<double>(loopE)) {
          v.pos = static_cast<double>(loopS) +
                  std::fmod(v.pos - static_cast<double>(loopS),
                            static_cast<double>(loopE - loopS));
        }

        const std::size_t idx = static_cast<std::size_t>(v.pos);
        if (idx >= mono.size()) { v.active = false; continue; }

        const std::size_t nxt = std::min(idx + 1, mono.size() - 1);
        const double frac = v.pos - static_cast<double>(idx);
        const double s = static_cast<double>(mono[idx]) * (1.0 - frac)
                       + static_cast<double>(mono[nxt]) * frac;

        const double releaseStep = sampleRate > 0 ? 1.0 / std::max(static_cast<double>(sampleRate) * static_cast<double>(v.releaseSec), 1.0) : 0.0;

        if (v.releasing) {
          v.envelope = std::max(0.0, v.envelope - releaseStep);
        } else if (!hasLoop) {
          // Natural decay for non-looping samples
          const double decayStep = sampleRate > 0 ? 1.0 / std::max(static_cast<double>(sampleRate) * static_cast<double>(v.decaySec), 1.0) : 0.0;
          v.envelope = std::max(0.0, v.envelope - decayStep);
        }
        // Looping samples: hold envelope at current level while key is held

        mixed += s * v.level * v.envelope;
        v.pos += baseStep * v.pitchRatio;

        if (v.envelope <= 0.0) { v.active = false; continue; }
        active += 1;
      }

      if (active > 0) monoBuffer[frame] += mixed / static_cast<double>(active);

      voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
          [](const Voice& v) { return !v.active; }), voices_.end());
    }
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "gain") { gain_ = std::clamp(value, 0.0, 4.0); return true; }
    return false;
  }
  double getParameter(const std::string& name) const override {
    if (name == "gain") return gain_;
    return 0.0;
  }
  std::size_t activeVoiceCount() const override {
    return std::count_if(voices_.begin(), voices_.end(), [](const Voice& v){ return v.active && !v.releasing; });
  }
  double activeVoiceFrequencyHz(std::size_t /*i*/) const override { return 0.0; }

  bool exportSamples(const std::string& dir) const override {
    bool ok = false;
    std::size_t idx = 0;
    for (const auto& zone : zones_) {
      for (const auto& layer : zone.layers) {
        if (layer.sample.mono.empty()) continue;
        const std::string path = dir + "/zone" + std::to_string(idx++) + ".wav";
        ok |= saveWavFile(path, layer.sample);
      }
    }
    return ok;
  }

private:
  struct Voice {
    int    midiNote   = 0;
    double pos        = 0.0;
    double pitchRatio = 1.0;
    double level      = 1.0;
    double envelope   = 1.0;
    float  decaySec   = 1.0f;
    float  releaseSec = 0.5f;
    bool   releasing  = false;
    bool   active     = true;
    const Layer* layer = nullptr;
  };

  const Zone* findZone(int midiNote) const {
    for (const auto& z : zones_)
      if (midiNote >= z.lowNote && midiNote <= z.highNote)
        return &z;
    return nullptr;
  }

  const Layer* findLayer(const Zone& zone, std::uint8_t velocity) const {
    for (const auto& l : zone.layers)
      if (l.active && !l.sample.mono.empty()
          && velocity >= static_cast<std::uint8_t>(l.velStart)
          && velocity <= static_cast<std::uint8_t>(l.velEnd))
        return &l;
    return nullptr;
  }

  std::vector<Zone>  zones_;
  std::vector<Voice> voices_;
  std::string        programName_;
  double             gain_ = 1.0;
};

// ── SfzPlugin ────────────────────────────────────────────────────────────────
// Minimal built-in SFZ loader — no external library needed.
// Handles the <group>/<region> subset used by DSK-style SFZ packs:
//   sample, lokey/hikey, pitch_keycenter, lovel/hivel, pan, loop_mode.
// Backslash paths (Windows convention) are normalized to forward slashes.

class SfzPlugin final : public extracker::IInstrumentPlugin {
  // A single key/velocity zone mapped to one WAV sample.
  struct Region {
    int       loKey       = 0;
    int       hiKey       = 127;
    int       loVel       = 0;
    int       hiVel       = 127;
    int       rootNote    = 60;
    float     pan         = 0.5f;  // 0=left 0.5=center 1=right
    bool      loop        = false;
    SampleData sample;
  };

  struct Voice {
    int    midiNote  = -1;
    double pos       = 0.0;
    double pitchRatio = 1.0;
    double level     = 1.0;
    bool   releasing = false;
    bool   active    = true;
    double envelope  = 1.0;
    double relSec    = 0.3;
    const Region* region = nullptr;
  };

  std::vector<Region> regions_;
  std::vector<Voice>  voices_;
  double              gain_ = 1.0;

  static std::string normPath(const std::string& s) {
    std::string r = s;
    for (auto& c : r) if (c == '\\') c = '/';
    return r;
  }

  static std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
  }

  static bool parseOpcode(const std::string& token,
                           std::string& key, std::string& val) {
    const auto eq = token.find('=');
    if (eq == std::string::npos) return false;
    key = trim(token.substr(0, eq));
    val = trim(token.substr(eq + 1));
    return !key.empty() && !val.empty();
  }

  const Region* findRegion(int midiNote, int velocity) const {
    for (const auto& r : regions_) {
      if (midiNote >= r.loKey && midiNote <= r.hiKey &&
          velocity >= r.loVel && velocity <= r.hiVel &&
          !r.sample.mono.empty())
        return &r;
    }
    return nullptr;
  }

public:
  bool loadFromFile(const std::string& sfzPath) {
    std::ifstream f(sfzPath);
    if (!f) return false;

    const std::string dir = [&] {
      const auto sep = sfzPath.rfind('/');
      return (sep == std::string::npos) ? std::string(".") : sfzPath.substr(0, sep);
    }();

    regions_.clear();

    // Group-level inherited defaults (reset per <group>)
    int grpLoKey = 0, grpHiKey = 127, grpLoVel = 0, grpHiVel = 127;
    int grpRoot = 60;
    float grpPan = 0.5f;
    bool grpLoop = false;

    // Pending region accumulators
    bool inRegion = false;
    std::string regSamplePath;
    int regLoKey = 0, regHiKey = 127, regLoVel = 0, regHiVel = 127;
    int regRoot = 60;
    float regPan = 0.5f;
    bool regLoop = false;

    auto flushRegion = [&]() {
      if (!inRegion || regSamplePath.empty()) return;
      Region r;
      r.loKey = regLoKey; r.hiKey = regHiKey;
      r.loVel = regLoVel; r.hiVel = regHiVel;
      r.rootNote = regRoot;
      r.pan = regPan;
      r.loop = regLoop;
      const std::string fullPath = dir + "/" + normPath(regSamplePath);
      if (loadWavFile(fullPath, r.sample))
        regions_.push_back(std::move(r));
    };

    auto resetRegion = [&]() {
      regSamplePath.clear();
      regLoKey = grpLoKey; regHiKey = grpHiKey;
      regLoVel = grpLoVel; regHiVel = grpHiVel;
      regRoot = grpRoot;
      regPan = grpPan;
      regLoop = grpLoop;
    };

    resetRegion();

    std::string line;
    while (std::getline(f, line)) {
      // Strip comments (// or ;)
      for (const char* pfx : {"//", ";"}) {
        const auto cp = line.find(pfx);
        if (cp != std::string::npos) line = line.substr(0, cp);
      }

      // Find section headers anywhere in the line
      std::string tok = line;
      while (!tok.empty()) {
        const auto open = tok.find('<');
        if (open == std::string::npos) break;
        const auto close = tok.find('>', open);
        if (close == std::string::npos) break;
        const std::string tag = tok.substr(open + 1, close - open - 1);
        tok = tok.substr(close + 1);

        if (tag == "group") {
          flushRegion();
          inRegion = false;
          // Reset group defaults
          grpLoKey = 0; grpHiKey = 127; grpLoVel = 0; grpHiVel = 127;
          grpRoot = 60; grpPan = 0.5f; grpLoop = false;
          resetRegion();
        } else if (tag == "region") {
          flushRegion();
          inRegion = true;
          resetRegion();
        }
      }

      // Parse opcodes (key=value pairs, possibly multiple per line)
      std::istringstream ss(line);
      std::string token;
      while (ss >> token) {
        // Detect start of a new section header already handled above; skip
        if (token.find('<') != std::string::npos) continue;
        // sample= may have spaces if the path contains spaces — collect remainder
        if (token.rfind("sample=", 0) == 0) {
          std::string val = token.substr(7);
          // If the value contains backslash or looks like a relative path
          // it may continue; but typically no spaces inside — just use as-is.
          // Collect rest of line if needed (sample paths can have spaces)
          std::string rest;
          if (std::getline(ss, rest)) {
            // Strip trailing opcodes: stop at the first token that looks like opcode
            // Heuristic: split on whitespace, keep segments until we find `key=`
            std::istringstream rs(rest);
            std::string seg;
            while (rs >> seg) {
              if (seg.find('=') != std::string::npos) {
                // push back to re-parse as opcode
                // re-insert into our outer stream by prepending — not possible;
                // re-parse this segment directly:
                std::string k2, v2;
                if (parseOpcode(seg, k2, v2)) {
                  auto applyOpcode = [&](const std::string& k, const std::string& v) {
                    const bool hasR = inRegion;
                    if      (k == "lokey")         { int n = std::stoi(v); if (hasR) regLoKey = n; else grpLoKey = n; }
                    else if (k == "hikey")         { int n = std::stoi(v); if (hasR) regHiKey = n; else grpHiKey = n; }
                    else if (k == "pitch_keycenter"){ int n = std::stoi(v); if (hasR) regRoot = n; else grpRoot = n; }
                    else if (k == "lovel")         { int n = std::stoi(v); if (hasR) regLoVel = n; else grpLoVel = n; }
                    else if (k == "hivel")         { int n = std::stoi(v); if (hasR) regHiVel = n; else grpHiVel = n; }
                    else if (k == "pan") {
                      float p = std::stof(v);  // -100..+100 → 0..1
                      p = (p + 100.0f) / 200.0f;
                      if (hasR) regPan = p; else grpPan = p;
                    }
                    else if (k == "loop_mode") {
                      bool lp = (v == "loop_continuous" || v == "loop_sustain");
                      if (hasR) regLoop = lp; else grpLoop = lp;
                    }
                  };
                  applyOpcode(k2, v2);
                }
              } else {
                val += ' ';
                val += seg;
              }
            }
          }
          val = trim(val);
          if (inRegion && !val.empty()) regSamplePath = val;
        } else {
          std::string k, v;
          if (parseOpcode(token, k, v)) {
            try {
              const bool hasR = inRegion;
              if      (k == "lokey")          { int n = std::stoi(v); if (hasR) regLoKey = n; else grpLoKey = n; }
              else if (k == "hikey")          { int n = std::stoi(v); if (hasR) regHiKey = n; else grpHiKey = n; }
              else if (k == "pitch_keycenter"){ int n = std::stoi(v); if (hasR) regRoot = n; else grpRoot = n; }
              else if (k == "lovel")          { int n = std::stoi(v); if (hasR) regLoVel = n; else grpLoVel = n; }
              else if (k == "hivel")          { int n = std::stoi(v); if (hasR) regHiVel = n; else grpHiVel = n; }
              else if (k == "pan") {
                float p = std::stof(v);
                p = (p + 100.0f) / 200.0f;
                if (hasR) regPan = p; else grpPan = p;
              }
              else if (k == "loop_mode") {
                bool lp = (v == "loop_continuous" || v == "loop_sustain");
                if (hasR) regLoop = lp; else grpLoop = lp;
              }
            } catch (...) {}
          }
        }
      }
    }
    flushRegion();

    return !regions_.empty();
  }

  void noteOn(int midiNote, std::uint8_t velocity, bool /*retrigger*/) override {
    const Region* region = findRegion(midiNote, static_cast<int>(velocity));
    if (!region) return;
    const double vel = std::clamp(static_cast<double>(velocity) / 127.0, 0.0, 1.0);
    const double pr  = std::pow(2.0, static_cast<double>(midiNote - region->rootNote) / 12.0);

    for (auto& v : voices_) {
      if (v.midiNote == midiNote && v.active) {
        v.pos = 0.0; v.level = vel * gain_; v.pitchRatio = pr;
        v.releasing = false; v.envelope = 1.0; v.region = region;
        return;
      }
    }
    Voice v;
    v.midiNote = midiNote; v.level = vel * gain_;
    v.pitchRatio = pr; v.region = region;
    voices_.push_back(v);
  }

  void noteOff(int midiNote) override {
    for (auto& v : voices_)
      if (v.midiNote == midiNote && v.active && !v.releasing)
        v.releasing = true;
  }

  void allNotesOff() override { voices_.clear(); }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (monoBuffer.empty() || sampleRate == 0 || voices_.empty()) return;

    const double relStep = 1.0 / std::max<double>(sampleRate * 0.3, 1.0);

    for (std::size_t frame = 0; frame < monoBuffer.size(); ++frame) {
      double mixed = 0.0;
      for (auto& v : voices_) {
        if (!v.active || !v.region) continue;
        const auto& mono = v.region->sample.mono;
        if (mono.empty()) continue;

        const double baseStep = static_cast<double>(v.region->sample.sampleRate)
                              / static_cast<double>(sampleRate);
        const double step = baseStep * v.pitchRatio;

        const std::size_t i0 = static_cast<std::size_t>(v.pos);
        const std::size_t i1 = i0 + 1;
        const double frac = v.pos - static_cast<double>(i0);

        double s = 0.0;
        if (i0 < mono.size()) {
          const float s0 = mono[i0];
          const float s1 = i1 < mono.size() ? mono[i1] : 0.0f;
          s = static_cast<double>(s0) + frac * (static_cast<double>(s1) - static_cast<double>(s0));
        }

        mixed += s * v.level * v.envelope;

        if (v.releasing) {
          v.envelope -= relStep;
          if (v.envelope <= 0.0) { v.active = false; v.envelope = 0.0; }
        }

        v.pos += step;

        // Loop handling
        if (v.region->loop) {
          const auto& sd = v.region->sample;
          const std::size_t loopEnd = sd.loopEnd > 0 ? sd.loopEnd : sd.mono.size();
          const std::size_t loopStart = sd.loopStart;
          if (v.pos >= static_cast<double>(loopEnd))
            v.pos -= static_cast<double>(loopEnd - loopStart);
        } else if (v.pos >= static_cast<double>(mono.size())) {
          v.active = false;
        }
      }
      monoBuffer[frame] += mixed;
    }

    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v){ return !v.active; }),
                  voices_.end());
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "gain") { gain_ = std::clamp(value, 0.0, 4.0); return true; }
    return false;
  }
  double getParameter(const std::string& name) const override {
    if (name == "gain")    return gain_;
    if (name == "regions") return static_cast<double>(regions_.size());
    return 0.0;
  }
  std::vector<std::string> listParameters() const override { return {"gain", "regions"}; }
  std::size_t activeVoiceCount() const override {
    return std::count_if(voices_.begin(), voices_.end(), [](const Voice& v){ return v.active; });
  }
  double activeVoiceFrequencyHz(std::size_t /*idx*/) const override { return 0.0; }
};

// ── SfzScanAdapter (built-in, no sfizz) ────────────────────────────────────
class BuiltinSfzScanAdapter final : public extracker::IExternalPluginAdapter {
  std::unordered_set<std::string> registered_;
public:
  std::string adapterName() const override { return "sfz-builtin"; }

  std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
    std::vector<std::string> searchPaths;

    const char* sfzPath = std::getenv("SFZ_PATH");
    if (sfzPath) {
      std::istringstream ss(sfzPath);
      std::string seg;
      while (std::getline(ss, seg, ':'))
        if (!seg.empty()) searchPaths.push_back(seg);
    } else {
      searchPaths = {
          "/usr/share/sounds",
          "/usr/share/sfizz",
          "/usr/local/share/sounds",
      };
      const char* home = std::getenv("HOME");
      if (home) {
        const std::string h(home);
        searchPaths.push_back(h + "/.local/share/sounds");
        searchPaths.push_back(h + "/.local/share/sfizz");
        searchPaths.push_back(h + "/Musikk/musicworks/instruments");
        searchPaths.push_back(h + "/Musikk/instruments");
        searchPaths.push_back(h + "/Music/musicworks/instruments");
        searchPaths.push_back(h + "/Music/instruments");
      }
    }

    std::size_t count = 0;
    for (const auto& dir : searchPaths) {
      std::error_code ec;
      if (!std::filesystem::is_directory(dir, ec)) continue;
      for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        const auto ext = entry.path().extension().string();
        std::string extLow = ext;
        for (auto& c : extLow)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extLow != ".sfz") continue;

        const std::string pathStr  = entry.path().string();
        const std::string pluginId = "sfz:" + pathStr;
        if (registered_.insert(pluginId).second) {
          host.registerPluginFactory(pluginId, [pathStr]() {
            auto p = std::make_unique<SfzPlugin>();
            if (!p->loadFromFile(pathStr)) return std::unique_ptr<extracker::IInstrumentPlugin>{};
            return std::unique_ptr<extracker::IInstrumentPlugin>(std::move(p));
          });
          ++count;
        }
      }
    }
    return count;
  }
};

// ── SingleSampleInstrumentBase ────────────────────────────────────────────────
// Shared voice rendering for S3I and IFF 8SVX (both are single-sample formats).

class SingleSampleInstrumentBase : public extracker::IInstrumentPlugin {
public:
  void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) override {
    if (sample_.mono.empty() || sample_.sampleRate == 0) return;
    const double vel = std::clamp(static_cast<double>(velocity) / 127.0, 0.0, 1.0);
    const double pr = std::pow(2.0, static_cast<double>(midiNote - rootMidiNote_) / 12.0);
    for (auto& v : voices_) {
      if (v.midiNote == midiNote) {
        v.level = vel * gain_;
        v.pitchRatio = pr;
        v.releasing = false;
        v.envelope = 1.0;
        if (retrigger) v.pos = 0.0;
        return;
      }
    }
    SSVoice voice;
    voice.midiNote = midiNote;
    voice.level = vel * gain_;
    voice.pitchRatio = pr;
    voices_.push_back(voice);
  }

  void noteOff(int midiNote) override {
    for (auto& v : voices_)
      if (v.midiNote == midiNote) v.releasing = true;
  }

  void allNotesOff() override { voices_.clear(); }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (monoBuffer.empty() || sampleRate == 0 || sample_.mono.empty()) return;
    const double baseStep = static_cast<double>(sample_.sampleRate) / static_cast<double>(sampleRate);
    const double relStep = 1.0 / std::max<double>(static_cast<double>(sampleRate) * 0.05, 1.0);
    const std::size_t sz = sample_.mono.size();

    for (std::size_t frame = 0; frame < monoBuffer.size(); ++frame) {
      double mixed = 0.0;
      std::size_t active = 0;
      for (auto& v : voices_) {
        if (!v.active) continue;
        const std::size_t idx = static_cast<std::size_t>(v.pos);
        if (idx >= sz) { v.active = false; continue; }
        const std::size_t nxt = std::min(idx + 1, sz - 1);
        const double frac = v.pos - static_cast<double>(idx);
        const double s = static_cast<double>(sample_.mono[idx]) * (1.0 - frac)
                       + static_cast<double>(sample_.mono[nxt]) * frac;
        if (v.releasing) {
          v.envelope = std::max(0.0, v.envelope - relStep);
          if (v.envelope <= 0.0) { v.active = false; continue; }
        }
        mixed += s * v.level * v.envelope;
        v.pos += baseStep * v.pitchRatio;
        active++;
      }
      if (active > 0) monoBuffer[frame] += mixed / static_cast<double>(active);
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
        [](const SSVoice& v) { return !v.active; }), voices_.end());
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "gain") { gain_ = std::clamp(value, 0.0, 4.0); return true; }
    return false;
  }
  double getParameter(const std::string& name) const override {
    if (name == "gain") return gain_;
    return 0.0;
  }
  std::size_t activeVoiceCount() const override {
    return std::count_if(voices_.begin(), voices_.end(),
        [](const SSVoice& v) { return v.active && !v.releasing; });
  }
  double activeVoiceFrequencyHz(std::size_t) const override { return 0.0; }

  bool exportSamples(const std::string& dir) const override {
    if (sample_.mono.empty()) return false;
    const std::string filename = name_.empty() ? "sample" : name_;
    return saveWavFile(dir + "/" + filename + ".wav", sample_);
  }

protected:
  struct SSVoice {
    int    midiNote   = 0;
    double pos        = 0.0;
    double pitchRatio = 1.0;
    double level      = 1.0;
    double envelope   = 1.0;
    bool   releasing  = false;
    bool   active     = true;
  };
  SampleData         sample_;
  std::vector<SSVoice> voices_;
  int    rootMidiNote_ = 60;
  double gain_         = 1.0;
  std::string name_;
};

// ── Opl2Plugin — OPL2 FM synthesis for S3I type=2 (Adlib melody) ─────────────
// Implements a two-operator FM voice using the OPL2 register layout stored in
// S3I headers D00-D11 (offsets 0x10-0x1B).  Up to 9 simultaneous voices.

class Opl2Plugin final : public extracker::IInstrumentPlugin {
  struct OpParams {
    std::uint8_t mult = 1;  // 0=0.5, 1-15=integer
    std::uint8_t tl   = 0;  // total level 0-63 (0=max, 63=silent)
    std::uint8_t ar   = 15;
    std::uint8_t dr   = 0;
    std::uint8_t sl   = 0;  // sustain level 0-15 (0=full, 15=silent)
    std::uint8_t rr   = 5;
    std::uint8_t wf   = 0;  // waveform 0-3
    bool         egt  = true;  // sustained envelope
  };

  struct FmOp {
    double phase = 0.0;
    double env   = 0.0;
    int    stage = 0;  // 0=idle 1=attack 2=decay 3=sustain 4=release

    void keyOn()  { phase = 0.0; env = 0.0; stage = 1; }
    void keyOff() { if (stage > 0 && stage < 4) stage = 4; }
    bool isActive() const { return stage != 0; }

    static double sustainTarget(const OpParams& p) {
      if (p.sl == 0) return 1.0;
      return std::pow(10.0, -static_cast<double>(p.sl) * 3.0 / 20.0);
    }

    static double envStep(std::uint8_t rate, double sr) {
      if (rate == 0) return 0.0;
      if (rate >= 15) return 1.0;
      return std::pow(2.0, static_cast<double>(rate) - 15.0) * 1000.0 / sr;
    }

    static double waveVal(std::uint8_t wf, double theta) {
      const double s = std::sin(theta);
      switch (wf & 3) {
        case 1: return s >= 0.0 ? s : 0.0;
        case 2: return std::abs(s);
        case 3: {
          const double t = std::fmod(theta / (2.0 * M_PI), 1.0);
          return (t >= 0.0 && t < 0.5) ? std::abs(s) : 0.0;
        }
        default: return s;
      }
    }

    // Returns operator output; phaseAdd is the modulation offset in radians.
    double tick(const OpParams& p, double freqHz, double sr, double phaseAdd = 0.0) {
      const double sTgt = sustainTarget(p);
      const double step = envStep(p.ar, sr);
      switch (stage) {
        case 1:
          env = std::min(1.0, env + step * 4.0);
          if (p.ar >= 15 || env >= 1.0) { env = 1.0; stage = 2; }
          break;
        case 2:
          env = std::max(sTgt, env - envStep(p.dr, sr));
          if (env <= sTgt) { env = sTgt; stage = p.egt ? 3 : 4; }
          break;
        case 3: break;
        case 4:
          env = std::max(0.0, env - envStep(p.rr, sr));
          if (env <= 0.0) { env = 0.0; stage = 0; }
          break;
        default: break;
      }
      if (stage == 0) return 0.0;
      const double mf = p.mult == 0 ? 0.5 : static_cast<double>(p.mult);
      phase += mf * freqHz / sr;
      const double gain = std::pow(10.0, -static_cast<double>(p.tl) * 0.75 / 20.0);
      return waveVal(p.wf & 3, phase * 2.0 * M_PI + phaseAdd) * env * gain;
    }
  };

  struct FmVoice {
    int     midiNote  = -1;
    double  freqHz    = 0.0;
    double  velScale  = 1.0;
    bool    active    = false;
    FmOp    mod, car;

    bool isActive() const { return active && (mod.isActive() || car.isActive()); }

    double tick(const OpParams& mp, const OpParams& cp, double sr) {
      const double modOut = mod.tick(mp, freqHz, sr);
      const double carOut = car.tick(cp, freqHz, sr, modOut * 2.0 * M_PI);
      if (!isActive()) active = false;
      return carOut * velScale;
    }
  };

  std::array<FmVoice, 9> voices_{};
  std::size_t nextVoice_ = 0;
  OpParams modP_, carP_;
  double   masterVol_ = 1.0;
  std::string name_;

public:
  bool loadFromS3i(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    std::uint8_t type = 0;
    f.read(reinterpret_cast<char*>(&type), 1);
    if (type != 2) return false;

    // DOS filename field 0x01-0x0C as fallback name
    char dosName[13]{};
    f.seekg(0x01);
    f.read(dosName, 12);
    dosName[12] = '\0';

    // D00-D11 at 0x10-0x1B
    f.seekg(0x10);
    std::uint8_t d[12]{};
    f.read(reinterpret_cast<char*>(d), 12);
    if (!f) return false;

    std::uint8_t volume = 64;
    f.seekg(0x1C);
    f.read(reinterpret_cast<char*>(&volume), 1);

    // Instrument name at 0x30
    char nameRaw[29]{};
    f.seekg(0x30);
    f.read(nameRaw, 28);
    name_ = std::string(nameRaw, ::strnlen(nameRaw, 28));
    if (name_.empty()) name_ = std::string(dosName, ::strnlen(dosName, 12));

    auto parseOp = [](const std::uint8_t* d, int mi, int ti, int adi, int sri, int wi) {
      OpParams p;
      p.mult = d[mi] & 0x0F;
      p.egt  = (d[mi] >> 5) & 1;
      p.tl   = d[ti] & 0x3F;
      p.ar   = (d[adi] >> 4) & 0x0F;
      p.dr   = d[adi] & 0x0F;
      p.sl   = (d[sri] >> 4) & 0x0F;
      p.rr   = d[sri] & 0x0F;
      p.wf   = d[wi] & 0x07;
      return p;
    };

    modP_ = parseOp(d, 0, 2, 4, 6, 8);
    carP_ = parseOp(d, 1, 3, 5, 7, 9);
    masterVol_ = std::clamp(static_cast<double>(volume) / 64.0, 0.0, 1.0);
    return true;
  }

  void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) override {
    const double freqHz = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
    // Retrigger existing voice for this note
    for (auto& v : voices_) {
      if (v.midiNote == midiNote && v.active) {
        if (retrigger) { v.mod.keyOn(); v.car.keyOn(); }
        v.velScale = std::clamp(static_cast<double>(velocity) / 127.0, 0.0, 1.0) * masterVol_;
        v.freqHz = freqHz;
        return;
      }
    }
    // Prefer a truly idle slot, fall back to round-robin steal
    FmVoice* target = nullptr;
    for (auto& slot : voices_) {
      if (!slot.isActive()) { target = &slot; break; }
    }
    if (!target) { target = &voices_[nextVoice_ % voices_.size()]; }
    nextVoice_++;
    target->midiNote = midiNote;
    target->freqHz   = freqHz;
    target->velScale = std::clamp(static_cast<double>(velocity) / 127.0, 0.0, 1.0) * masterVol_;
    target->active   = true;
    target->mod.keyOn();
    target->car.keyOn();
  }

  void noteOff(int midiNote) override {
    for (auto& v : voices_) {
      if (v.midiNote == midiNote && v.active && v.mod.stage != 4) {
        v.mod.keyOff();
        v.car.keyOff();
      }
    }
  }

  void allNotesOff() override {
    for (auto& v : voices_) { v.mod.keyOff(); v.car.keyOff(); }
  }

  void renderAdd(std::vector<double>& buf, std::uint32_t sr) override {
    if (buf.empty() || sr == 0) return;
    const double dsr = static_cast<double>(sr);
    for (std::size_t i = 0; i < buf.size(); ++i) {
      double mixed = 0.0;
      std::size_t active = 0;
      for (auto& v : voices_) {
        if (!v.isActive()) { v.active = false; continue; }
        mixed += v.tick(modP_, carP_, dsr);
        ++active;
      }
      if (active > 0) buf[i] += mixed / static_cast<double>(active);
    }
  }

  bool   setParameter(const std::string&, double) override { return false; }
  double getParameter(const std::string&) const override   { return 0.0; }

  std::size_t activeVoiceCount() const override {
    return static_cast<std::size_t>(
        std::count_if(voices_.begin(), voices_.end(),
            [](const FmVoice& v) { return v.isActive() && v.car.stage != 4; }));
  }
  double activeVoiceFrequencyHz(std::size_t i) const override {
    std::size_t idx = 0;
    for (const auto& v : voices_) {
      if (v.isActive()) { if (idx++ == i) return v.freqHz; }
    }
    return 0.0;
  }
};

// ── S3iPlugin — Scream Tracker 3 / Impulse Tracker instrument ────────────────
// 80-byte header (same layout as in an S3M module) + raw PCM at offset 0x50.
// Flags byte 0x1F: bit0=loop, bit1=stereo, bit2=16-bit.  C5 speed = sample Hz at C5.

class S3iPlugin final : public SingleSampleInstrumentBase {
public:
  bool loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    std::uint8_t type = 0;
    f.read(reinterpret_cast<char*>(&type), 1);
    if (type != 1) return false;  // not a PCM sample

    f.seekg(0x10);
    std::uint32_t sampleLength = 0, loopBeginIgnored = 0, loopEndIgnored = 0;
    if (!readU32LE(f, sampleLength) ||
        !readU32LE(f, loopBeginIgnored) ||
        !readU32LE(f, loopEndIgnored)) return false;

    f.seekg(0x1C);
    std::uint8_t volume = 64, packing = 0, flags = 0;
    f.read(reinterpret_cast<char*>(&volume), 1);
    f.seekg(1, std::ios::cur);  // skip dsk byte
    f.read(reinterpret_cast<char*>(&packing), 1);
    f.read(reinterpret_cast<char*>(&flags), 1);

    std::uint32_t c5speed = 8363;
    if (!readU32LE(f, c5speed) || c5speed == 0) c5speed = 8363;

    char nameRaw[28]{};
    f.seekg(0x30);
    f.read(nameRaw, 28);
    name_ = std::string(nameRaw, ::strnlen(nameRaw, 28));

    f.seekg(0x50);
    if (sampleLength == 0 || packing != 0 || !f) return false;

    const bool is16bit  = (flags & 4u) != 0;
    const bool isStereo = (flags & 2u) != 0;
    const int  channels = isStereo ? 2 : 1;

    sample_.sampleRate = c5speed;
    sample_.mono.resize(sampleLength);
    rootMidiNote_ = 60;
    gain_ = std::clamp(static_cast<double>(volume) / 64.0, 0.0, 1.0);

    for (std::uint32_t i = 0; i < sampleLength; ++i) {
      double mixed = 0.0;
      for (int ch = 0; ch < channels; ++ch) {
        if (is16bit) {
          std::uint16_t raw = 0;
          if (!readU16LE(f, raw)) { sample_.mono.clear(); return false; }
          mixed += static_cast<double>(static_cast<std::int16_t>(raw)) / 32768.0;
        } else {
          std::uint8_t s = 0;
          f.read(reinterpret_cast<char*>(&s), 1);
          if (!f) { sample_.mono.clear(); return false; }
          mixed += (static_cast<double>(s) - 128.0) / 128.0;
        }
      }
      sample_.mono[i] = static_cast<float>(std::clamp(mixed / channels, -1.0, 1.0));
    }
    return !sample_.mono.empty();
  }
};

// ── IffSvxPlugin — Amiga IFF 8SVX ────────────────────────────────────────────
// Big-endian chunk format.  VHDR gives sample rate and loop info; BODY is
// signed 8-bit PCM (or Fibonacci-delta encoded when compression == 1).

class IffSvxPlugin final : public SingleSampleInstrumentBase {
public:
  bool loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char formId[4]{}, formType[4]{};
    std::uint32_t formSize = 0;
    f.read(formId, 4);
    if (!readU32BE(f, formSize)) return false;
    f.read(formType, 4);
    if (!f || std::string_view(formId, 4) != "FORM" || std::string_view(formType, 4) != "8SVX")
      return false;

    std::uint32_t oneShotSamples = 0, repeatSamples = 0;
    std::uint16_t sampleRate = 0;
    std::uint8_t  numOctaves = 1, compression = 0;
    std::uint32_t volumeFixed = 0x10000;
    bool hasVhdr = false;
    std::vector<std::uint8_t> bodyData;

    while (f) {
      char id[4]{};
      std::uint32_t csz = 0;
      f.read(id, 4);
      if (!readU32BE(f, csz) || !f) break;
      const std::string chunkId(id, 4);

      if (chunkId == "VHDR" && csz >= 20) {
        std::uint32_t samplesPerCycle = 0;
        if (!readU32BE(f, oneShotSamples) || !readU32BE(f, repeatSamples) ||
            !readU32BE(f, samplesPerCycle) || !readU16BE(f, sampleRate)) break;
        f.read(reinterpret_cast<char*>(&numOctaves), 1);
        f.read(reinterpret_cast<char*>(&compression), 1);
        if (!readU32BE(f, volumeFixed)) break;
        hasVhdr = true;
        if (csz > 20) f.seekg(static_cast<std::streamoff>(csz - 20), std::ios::cur);
      } else if (chunkId == "BODY") {
        bodyData.resize(csz);
        f.read(reinterpret_cast<char*>(bodyData.data()), csz);
        if ((csz & 1u) != 0u) f.seekg(1, std::ios::cur);
      } else {
        f.seekg(static_cast<std::streamoff>(csz + (csz & 1u)), std::ios::cur);
      }
    }

    if (!hasVhdr || bodyData.empty() || sampleRate == 0) return false;

    sample_.sampleRate = sampleRate;
    rootMidiNote_ = 60;
    gain_ = std::clamp(static_cast<double>(volumeFixed) / 65536.0, 0.0, 1.0);

    const std::uint32_t oct0Samples = oneShotSamples + repeatSamples;

    if (compression == 0) {
      // Raw signed 8-bit.  Skip higher octaves (stored first, each half the previous size).
      std::size_t skip = 0;
      for (int o = numOctaves - 1; o > 0; --o)
        skip += (oct0Samples > 0u) ? static_cast<std::size_t>(oct0Samples >> o) : 0;
      if (skip >= bodyData.size()) skip = 0;
      const std::size_t n = (oct0Samples > 0 && skip + oct0Samples <= bodyData.size())
                                ? oct0Samples : bodyData.size() - skip;
      sample_.mono.resize(n);
      for (std::size_t i = 0; i < n; ++i)
        sample_.mono[i] = static_cast<float>(static_cast<std::int8_t>(bodyData[skip + i])) / 128.0f;
    } else if (compression == 1) {
      // Fibonacci delta — 4-bit nibbles, high nibble first.
      static constexpr std::int8_t kFib[16] = {-34,-21,-13,-8,-5,-3,-2,-1,0,1,2,3,5,8,13,21};
      const std::size_t maxOut = (oct0Samples > 0) ? static_cast<std::size_t>(oct0Samples) : bodyData.size() * 2;
      sample_.mono.reserve(maxOut);
      std::int8_t prev = 0;
      for (std::uint8_t byte : bodyData) {
        if (sample_.mono.size() >= maxOut) break;
        prev = static_cast<std::int8_t>(std::clamp<int>(prev + kFib[(byte >> 4) & 0x0F], -128, 127));
        sample_.mono.push_back(static_cast<float>(prev) / 128.0f);
        if (sample_.mono.size() < maxOut) {
          prev = static_cast<std::int8_t>(std::clamp<int>(prev + kFib[byte & 0x0F], -128, 127));
          sample_.mono.push_back(static_cast<float>(prev) / 128.0f);
        }
      }
    } else {
      return false;
    }

    name_ = path;
    if (const auto s = name_.rfind('/'); s != std::string::npos) name_ = name_.substr(s + 1);
    if (name_.size() > 4) name_ = name_.substr(0, name_.size() - 4);
    return !sample_.mono.empty();
  }
};

// ── XiPlugin — FastTracker 2 Extended Instrument ─────────────────────────────
// Multi-sample with a 96-note mapping table.  Samples are delta-encoded.
// Pitch: baseStep = 1.0 (samples assumed at output rate); tuning via relNote+finetune.

class XiPlugin final : public extracker::IInstrumentPlugin {
public:
  struct XiSample {
    SampleData  data;
    std::int8_t relNote  = 0;
    std::int8_t finetune = 0;
    std::string name;
  };

  bool loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[21]{};
    f.read(magic, 21);
    if (!f || std::string_view(magic, 21) != "Extended Instrument: ") return false;

    // Jump past name(22) + 0x1A(1) + tracker(20) + version(2) = 45 bytes → offset 66
    f.seekg(66);

    // Read note-to-sample map (96 bytes)
    std::uint8_t noteMap[96]{};
    f.read(reinterpret_cast<char*>(noteMap), 96);
    if (!f) return false;

    // Skip the two envelopes (48+48=96 bytes), 14 single-byte fields, fadeout+reserved (4 bytes)
    // → total 66 + 96 + 96 + 14 + 4 = 276 → numSamples sits at file offset 276
    f.seekg(276);

    std::uint16_t numSamples = 0;
    if (!readU16LE(f, numSamples) || numSamples == 0) return false;

    // Sample headers (40 bytes each starting at offset 278)
    struct SHdr {
      std::uint32_t length, loopStart, loopLength;
      std::uint8_t  volume;
      std::int8_t   finetune;
      std::uint8_t  type, panning;
      std::int8_t   relNote;
      std::uint8_t  reserved;
      char          name[22];
    };
    std::vector<SHdr> hdrs(numSamples);
    for (auto& h : hdrs) {
      if (!readU32LE(f, h.length) || !readU32LE(f, h.loopStart) || !readU32LE(f, h.loopLength))
        return false;
      f.read(reinterpret_cast<char*>(&h.volume), 1);
      f.read(reinterpret_cast<char*>(&h.finetune), 1);
      f.read(reinterpret_cast<char*>(&h.type), 1);
      f.read(reinterpret_cast<char*>(&h.panning), 1);
      f.read(reinterpret_cast<char*>(&h.relNote), 1);
      f.read(reinterpret_cast<char*>(&h.reserved), 1);
      f.read(h.name, 22);
      if (!f) return false;
    }

    // Sample data (concatenated, delta-encoded)
    samples_.resize(numSamples);
    for (std::uint16_t i = 0; i < numSamples; ++i) {
      auto& h  = hdrs[i];
      auto& xs = samples_[i];
      xs.relNote  = h.relNote;
      xs.finetune = h.finetune;
      xs.name     = std::string(h.name, ::strnlen(h.name, 22));
      xs.data.sampleRate = 0;  // no rate stored in XI; treated as "output rate"
      if (h.length == 0) continue;

      const bool is16bit = (h.type & 0x10u) != 0;
      if (is16bit) {
        const std::uint32_t frames = h.length / 2;
        xs.data.mono.resize(frames);
        std::int16_t prev = 0;
        for (std::uint32_t j = 0; j < frames; ++j) {
          std::uint16_t raw = 0;
          if (!readU16LE(f, raw)) { xs.data.mono.clear(); break; }
          prev += static_cast<std::int16_t>(raw);
          xs.data.mono[j] = static_cast<float>(prev) / 32768.0f;
        }
      } else {
        xs.data.mono.resize(h.length);
        std::int8_t prev = 0;
        for (std::uint32_t j = 0; j < h.length; ++j) {
          std::uint8_t raw = 0;
          f.read(reinterpret_cast<char*>(&raw), 1);
          if (!f) { xs.data.mono.clear(); break; }
          prev += static_cast<std::int8_t>(raw);
          xs.data.mono[j] = static_cast<float>(prev) / 128.0f;
        }
      }
    }

    for (int n = 0; n < 96; ++n)  noteMap_[n] = noteMap[n];
    for (int n = 96; n < 128; ++n) noteMap_[n] = noteMap[95];
    return !samples_.empty();
  }

  void noteOn(int midiNote, std::uint8_t velocity, bool retrigger) override {
    if (midiNote < 0 || midiNote > 127) return;
    const std::uint8_t si = noteMap_[midiNote];
    if (si >= samples_.size() || samples_[si].data.mono.empty()) return;
    const auto& xs = samples_[si];
    const double vel = std::clamp(static_cast<double>(velocity) / 127.0, 0.0, 1.0);
    // Pitch relative to C5=60: relNote shifts in semitones, finetune in 1/128 semitones
    const double pr = std::pow(2.0, (static_cast<double>(midiNote - 60)
                                     + static_cast<double>(xs.relNote)
                                     + static_cast<double>(xs.finetune) / 128.0) / 12.0);
    for (auto& v : voices_) {
      if (v.midiNote == midiNote) {
        v.sampleIdx = si; v.pitchRatio = pr; v.level = vel * gain_;
        v.releasing = false; v.envelope = 1.0;
        if (retrigger) v.pos = 0.0;
        return;
      }
    }
    XiVoice voice;
    voice.midiNote = midiNote; voice.sampleIdx = si;
    voice.pitchRatio = pr; voice.level = vel * gain_;
    voices_.push_back(voice);
  }

  void noteOff(int midiNote) override {
    for (auto& v : voices_)
      if (v.midiNote == midiNote) v.releasing = true;
  }

  void allNotesOff() override { voices_.clear(); }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (monoBuffer.empty() || sampleRate == 0 || samples_.empty()) return;
    const double relStep = 1.0 / std::max<double>(static_cast<double>(sampleRate) * 0.05, 1.0);

    for (std::size_t frame = 0; frame < monoBuffer.size(); ++frame) {
      double mixed = 0.0;
      std::size_t active = 0;
      for (auto& v : voices_) {
        if (!v.active || v.sampleIdx >= samples_.size()) { v.active = false; continue; }
        const auto& mono = samples_[v.sampleIdx].data.mono;
        if (mono.empty()) { v.active = false; continue; }
        const std::size_t idx = static_cast<std::size_t>(v.pos);
        if (idx >= mono.size()) { v.active = false; continue; }
        const std::size_t nxt = std::min(idx + 1, mono.size() - 1);
        const double frac = v.pos - static_cast<double>(idx);
        const double s = static_cast<double>(mono[idx]) * (1.0 - frac)
                       + static_cast<double>(mono[nxt]) * frac;
        if (v.releasing) {
          v.envelope = std::max(0.0, v.envelope - relStep);
          if (v.envelope <= 0.0) { v.active = false; continue; }
        }
        mixed += s * v.level * v.envelope;
        v.pos += v.pitchRatio;  // baseStep = 1.0 for XI (no stored sample rate)
        active++;
      }
      if (active > 0) monoBuffer[frame] += mixed / static_cast<double>(active);
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
        [](const XiVoice& v) { return !v.active; }), voices_.end());
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "gain") { gain_ = std::clamp(value, 0.0, 4.0); return true; }
    return false;
  }
  double getParameter(const std::string& name) const override {
    if (name == "gain") return gain_;
    return 0.0;
  }
  std::size_t activeVoiceCount() const override {
    return std::count_if(voices_.begin(), voices_.end(),
        [](const XiVoice& v) { return v.active && !v.releasing; });
  }
  double activeVoiceFrequencyHz(std::size_t) const override { return 0.0; }

  bool exportSamples(const std::string& dir) const override {
    bool ok = false;
    for (std::size_t i = 0; i < samples_.size(); ++i) {
      if (samples_[i].data.mono.empty()) continue;
      std::string n = samples_[i].name.empty()
                    ? ("sample" + std::to_string(i)) : samples_[i].name;
      for (char& c : n) if (c == '/' || c == '\\' || c == ':' || c == '\0') c = '_';
      SampleData sd = samples_[i].data;
      if (sd.sampleRate == 0) sd.sampleRate = 44100;
      ok |= saveWavFile(dir + "/" + n + ".wav", sd);
    }
    return ok;
  }

private:
  struct XiVoice {
    int          midiNote   = 0;
    std::uint8_t sampleIdx  = 0;
    double       pos        = 0.0;
    double       pitchRatio = 1.0;
    double       level      = 1.0;
    double       envelope   = 1.0;
    bool         releasing  = false;
    bool         active     = true;
  };
  std::vector<XiSample>  samples_;
  std::vector<XiVoice>   voices_;
  std::uint8_t           noteMap_[128]{};
  double                 gain_ = 1.0;
};

class BuiltinGainEffect final : public extracker::IEffectPlugin {
public:
  void process(std::vector<double>& monoBuffer, std::uint32_t /*sampleRate*/) override {
    for (auto& s : monoBuffer) s *= gain_;
  }
  bool setParameter(const std::string& name, double value) override {
    if (name == "gain" || name == "volume") {
      gain_ = std::clamp(value, 0.0, 4.0);
      return true;
    }
    return false;
  }
  double getParameter(const std::string& name) const override {
    if (name == "gain" || name == "volume") return gain_;
    return 0.0;
  }
  std::string name() const override { return "Gain"; }
private:
  double gain_ = 1.0;
};

class Lv2DynamicEffectPlugin final : public extracker::IEffectPlugin {
public:
  Lv2DynamicEffectPlugin(const std::string& uri,
                         const std::filesystem::path& binaryPath,
                         int audioInputPort,
                         int audioOutputPort,
                         std::vector<int> controlInputPorts,
                         std::vector<int> controlOutputPorts,
                         std::vector<extracker::PluginControlPortMeta> controlInputMeta = {})
      : uri_(uri),
        binaryPath_(binaryPath),
        audioInputPort_(audioInputPort),
        audioOutputPort_(audioOutputPort),
        controlInputPorts_(std::move(controlInputPorts)),
        controlOutputPorts_(std::move(controlOutputPorts)),
        moduleHandle_(nullptr),
        descriptor_(nullptr),
        instance_(nullptr),
        loaded_(false),
        runtimeActive_(false) {
    controlInputValues_.resize(controlInputPorts_.size());
    for (std::size_t i = 0; i < controlInputValues_.size(); ++i) {
      controlInputValues_[i] = (i < controlInputMeta.size() && controlInputMeta[i].hasDefault)
                               ? controlInputMeta[i].defaultVal : 0.0f;
    }
    controlOutputValues_.assign(controlOutputPorts_.size(), 0.0f);
    loaded_ = tryLoadDescriptor();
  }

  ~Lv2DynamicEffectPlugin() override {
    shutdownRuntimeInstance();
    if (moduleHandle_ != nullptr) {
      dlclose(moduleHandle_);
      moduleHandle_ = nullptr;
    }
  }

  void process(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (monoBuffer.empty() || sampleRate == 0) {
      return;
    }
    if (!ensureRuntimeInstance(sampleRate, monoBuffer.size())) {
      return;
    }
    for (std::size_t i = 0; i < monoBuffer.size(); ++i) {
      audioInputBuffer_[i] = static_cast<float>(monoBuffer[i]);
    }
    std::fill(audioOutputBuffer_.begin(), audioOutputBuffer_.end(), 0.0f);

    descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioInputPort_), audioInputBuffer_.data());
    descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioOutputPort_), audioOutputBuffer_.data());
    for (std::size_t i = 0; i < controlInputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(controlInputPorts_[i]), &controlInputValues_[i]);
    }
    for (std::size_t i = 0; i < controlOutputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(controlOutputPorts_[i]), &controlOutputValues_[i]);
    }

    descriptor_->run(instance_, static_cast<std::uint32_t>(monoBuffer.size()));

    for (std::size_t i = 0; i < monoBuffer.size(); ++i) {
      const double sample = static_cast<double>(audioOutputBuffer_[i]);
      monoBuffer[i] = std::isfinite(sample) ? sample : 0.0;
    }
  }

  bool setParameter(const std::string& name, double value) override {
    const std::string prefix = "lv2_control_in_";
    if (name.rfind(prefix, 0) == 0) {
      std::size_t ordinal = 0;
      std::istringstream parse(name.substr(prefix.size()));
      parse >> ordinal;
      if (!parse || !parse.eof() || ordinal >= controlInputValues_.size()) {
        return false;
      }
      controlInputValues_[ordinal] = static_cast<float>(value);
      return true;
    }
    return false;
  }

  double getParameter(const std::string& name) const override {
    const std::string inPrefix = "lv2_control_in_";
    if (name.rfind(inPrefix, 0) == 0) {
      std::size_t ordinal = 0;
      std::istringstream parse(name.substr(inPrefix.size()));
      parse >> ordinal;
      if (!parse || !parse.eof() || ordinal >= controlInputValues_.size()) {
        return 0.0;
      }
      return static_cast<double>(controlInputValues_[ordinal]);
    }
    const std::string outPrefix = "lv2_control_out_";
    if (name.rfind(outPrefix, 0) == 0) {
      std::size_t ordinal = 0;
      std::istringstream parse(name.substr(outPrefix.size()));
      parse >> ordinal;
      if (!parse || !parse.eof() || ordinal >= controlOutputValues_.size()) {
        return 0.0;
      }
      return static_cast<double>(controlOutputValues_[ordinal]);
    }
    if (name == "lv2_loaded") { return loaded_ ? 1.0 : 0.0; }
    if (name == "lv2_runtime_active") { return runtimeActive_ ? 1.0 : 0.0; }
    return 0.0;
  }

  std::string name() const override {
    return "lv2:" + uri_;
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

  bool ensureRuntimeInstance(std::uint32_t sampleRate, std::size_t frameCount) {
    if (runtimeActive_) {
      audioInputBuffer_.resize(frameCount, 0.0f);
      audioOutputBuffer_.resize(frameCount, 0.0f);
      return true;
    }
    if (!loaded_ || descriptor_ == nullptr || audioInputPort_ < 0 || audioOutputPort_ < 0 ||
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
    {
      int maxPort = std::max({audioInputPort_, audioOutputPort_, -1});
      for (int p : controlInputPorts_)  maxPort = std::max(maxPort, p);
      for (int p : controlOutputPorts_) maxPort = std::max(maxPort, p);
      if (maxPort >= 0) {
        static std::vector<float> sDummy;
        const std::size_t needed = std::max(frameCount, std::size_t{4096});
        if (sDummy.size() < needed) sDummy.assign(needed, 0.0f);
        for (int i = 0; i <= maxPort; ++i) {
          descriptor_->connectPort(instance_, static_cast<std::uint32_t>(i), sDummy.data());
        }
      }
    }
    descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioInputPort_), audioInputBuffer_.data());
    descriptor_->connectPort(instance_, static_cast<std::uint32_t>(audioOutputPort_), audioOutputBuffer_.data());
    for (std::size_t i = 0; i < controlInputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(controlInputPorts_[i]), &controlInputValues_[i]);
    }
    for (std::size_t i = 0; i < controlOutputPorts_.size(); ++i) {
      descriptor_->connectPort(instance_, static_cast<std::uint32_t>(controlOutputPorts_[i]), &controlOutputValues_[i]);
    }
    if (descriptor_->activate != nullptr) {
      descriptor_->activate(instance_);
    }
    runtimeActive_ = true;
    return true;
  }

  bool tryLoadDescriptor() {
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
      if (descriptor == nullptr) { break; }
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
  void* moduleHandle_;
  const Lv2Descriptor* descriptor_;
  Lv2Handle instance_;
  std::vector<float> audioInputBuffer_;
  std::vector<float> audioOutputBuffer_;
  std::vector<float> controlInputValues_;
  std::vector<float> controlOutputValues_;
  bool loaded_;
  bool runtimeActive_;
};

class Lv2ManifestAdapter final : public extracker::IExternalPluginAdapter {
public:
  std::string adapterName() const override {
    return "lv2.manifest";
  }

  // Verify a plugin's .so can be dlopen'd without crashing.  Runs the probe in
  // a forked child so a SIGSEGV in Calf or similar plugins cannot kill the
  // parent.  Returns false and prints a warning if the child is signaled.
  static bool probeLv2Plugin(const std::filesystem::path& binaryPath) {
#ifdef _WIN32
    return true;
#else
    const pid_t pid = fork();
    if (pid == 0) {
      setpgid(0, 0);  // isolate child process group so kill(0,sig) from plugin can't reach parent
      closeInheritedFds();
      void* handle = dlopen(binaryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (handle) {
        void* sym = dlsym(handle, "lv2_descriptor");
        if (sym) {
          auto fn = reinterpret_cast<Lv2DescriptorFunction>(sym);
          fn(0);
        }
      }
      _exit(0);
    }
    if (pid < 0) return true;

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    if (WIFSIGNALED(wstatus)) {
      fprintf(stderr, "[lv2] plugin crashed during scan, skipping: %s\n",
              binaryPath.filename().c_str());
      fflush(stderr);
      return false;
    }
    return true;
#endif
  }

  std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
    std::size_t discovered = 0;
    for (const auto& plugin : discoverPlugins()) {
      if (!plugin.binaryPath.empty() && !probeLv2Plugin(plugin.binaryPath)) {
        continue;
      }
      // Pre-load the binary in the calling process right now.  When this
      // function is called before audio starts (the GUI does this in
      // ExTrackerApp::initialise before audio.start()), all transitive deps
      // (libfluidsynth → libpipewire, libjack, …) initialise their globals
      // before any audio context exists, preventing conflicts when the user
      // later assigns the plugin.  RTLD_NODELETE keeps the library resident
      // so the subsequent tryLoadDescriptor dlopen is a no-op ref-count bump.
#ifndef _WIN32
      if (!plugin.binaryPath.empty()) {
        dlopen(plugin.binaryPath.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
      }
#endif
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
                  plugin.eventInputPorts,
                  plugin.audioOutputPort2,
                  plugin.controlInputMeta);
            });
        // LV2 manifests don't carry a plugin-level display name where we
        // scan (that lives in the plugin's own turtle file via
        // rdfs:seeAlso, which isn't parsed here) — fall back to the last
        // path segment of the URI, e.g. ".../plugins/Reverb" -> "Reverb".
        {
          std::string uriTail = plugin.uri;
          if (const auto slash = uriTail.find_last_of('/'); slash != std::string::npos) {
            uriTail = uriTail.substr(slash + 1);
          }
          if (const auto hash = uriTail.find_last_of('#'); hash != std::string::npos) {
            uriTail = uriTail.substr(hash + 1);
          }
          if (!uriTail.empty()) {
            host.registerPluginDisplayName(pluginId, uriTail);
          }
        }
        extracker::PluginPortInfo portInfo;
        portInfo.audioIn        = plugin.audioInputPort;
        portInfo.audioOut       = plugin.audioOutputPort;
        portInfo.audioOut2      = plugin.audioOutputPort2;
        portInfo.controlInCount = static_cast<int>(plugin.controlInputPorts.size());
        portInfo.controlOutCount= static_cast<int>(plugin.controlOutputPorts.size());
        portInfo.eventInCount   = static_cast<int>(plugin.eventInputPorts.size());
        portInfo.controlInMeta  = plugin.controlInputMeta;
        portInfo.controlOutMeta = plugin.controlOutputMeta;
        host.registerPluginPortInfo(pluginId, portInfo);
        if (plugin.audioInputPort >= 0 && plugin.audioOutputPort >= 0) {
          host.registerEffectFactory(
              pluginId,
              [plugin]() {
                return std::make_unique<Lv2DynamicEffectPlugin>(
                    plugin.uri,
                    plugin.binaryPath,
                    plugin.audioInputPort,
                    plugin.audioOutputPort,
                    plugin.controlInputPorts,
                    plugin.controlOutputPorts,
                    plugin.controlInputMeta);
              });
        }
        discovered += 1;
      }
    }
    return discovered;
  }

private:
  static std::vector<std::filesystem::path> candidateSearchRoots() {
    std::vector<std::filesystem::path> roots;

    if (const char* env = std::getenv("LV2_PATH")) {
      // LV2_PATH is exclusive (overrides system dirs) when set
      std::stringstream stream(env);
      std::string path;
      while (std::getline(stream, path, ':')) {
        if (!path.empty()) {
          roots.emplace_back(path);
        }
      }
      return roots;
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
        if (isOutput) {
          if (plugin.audioOutputPort < 0) {
            plugin.audioOutputPort = index;
          } else if (plugin.audioOutputPort2 < 0 && index != plugin.audioOutputPort) {
            plugin.audioOutputPort2 = index;
          }
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
          // lv2:name is the preferred display name; rdfs:label is a fallback.
          // Guard both with "only if empty" so inline scale-point rdfs:label
          // values can't overwrite a port name already parsed from lv2:name.
          if (portLabel.empty()) {
            if (const std::string value = parseQuotedStringAfterKey(line, "lv2:name"); !value.empty()) {
              portLabel = value;
            }
          }
          if (portLabel.empty()) {
            if (const std::string value = parseQuotedStringAfterKey(line, "rdfs:label"); !value.empty()) {
              portLabel = value;
            }
          }
          if (auto v = parseFloatAfterKey(line, "lv2:minimum")) { portMinVal = v; }
          if (auto v = parseFloatAfterKey(line, "lv2:maximum")) { portMaxVal = v; }
          if (auto v = parseFloatAfterKey(line, "lv2:default")) { portDefaultVal = v; }

        }

        // Close the current port block when this line contains ].
        // Runs after both the if(!inPortBlock) and else-if(inPortBlock) branches,
        // so single-line [ ... ] ports flush correctly (the if branch sets inPortBlock=true,
        // then this check immediately flushes it on the same line).
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
          // Handle Turtle "] , [": next port block starts on same line after the ]
          const std::size_t closeBracket = line.find(']');
          const std::size_t nextOpen = line.find('[', closeBracket + 1);
          if (nextOpen != std::string::npos && !currentUri.empty()) {
            inPortBlock = true;
            const std::string remainder = line.substr(nextOpen + 1);
            portIndex = parsePortIndexValue(remainder);
            isInput  = remainder.find("lv2:InputPort")  != std::string::npos;
            isOutput = remainder.find("lv2:OutputPort") != std::string::npos;
            isAudio  = remainder.find("lv2:AudioPort")  != std::string::npos;
            isControl = remainder.find("lv2:ControlPort") != std::string::npos;
            isEvent = lineIsEventPort(remainder);
            portSymbol = parseQuotedStringAfterKey(remainder, "lv2:symbol");
            portLabel  = parseQuotedStringAfterKey(remainder, "rdfs:label");
            portMinVal     = parseFloatAfterKey(remainder, "lv2:minimum");
            portMaxVal     = parseFloatAfterKey(remainder, "lv2:maximum");
            portDefaultVal = parseFloatAfterKey(remainder, "lv2:default");
          }
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

#endif  // !_WIN32 (end of LV2 block)

// ── VST3 COM stubs ───────────────────────────────────────────────────────────
// Self-contained ABI-compatible COM stubs for Steinberg VST3 on Linux/x86_64.
// No external SDK headers: vtable layout is determined by C++ declaration order
// (Itanium ABI). NO virtual destructors in the abstract interfaces — the SDK
// uses COM reference counting, not C++ RAII deletion through base pointers.
// IIDs are stable Steinberg GUIDs, stored as 4 big-endian uint32 values.
// ─────────────────────────────────────────────────────────────────────────────

using Vst3TResult = int32_t;
using Vst3TBool   = int32_t;
using Vst3SpeakerArrangement = uint64_t;
using Vst3ParamID    = uint32_t;
using Vst3ParamValue = double;

static constexpr Vst3TResult kVst3Ok          = 0;
static constexpr Vst3TResult kVst3ResultFalse = 1;
static constexpr Vst3TResult kVst3NoInterface = static_cast<Vst3TResult>(0x80004002);
static constexpr Vst3SpeakerArrangement kVst3Stereo = 0x3;
static constexpr Vst3SpeakerArrangement kVst3Mono   = 0x1;

static const uint8_t kVst3IIDIPluginFactory[16]  = {
    0x7A,0x4D,0x81,0x1C, 0x52,0x11,0x4A,0x1F, 0xAE,0xD9,0xD2,0xEE, 0x0B,0x61,0x5A,0xA3};
static const uint8_t kVst3IIDIComponent[16]      = {
    0xE8,0x31,0xFF,0x31, 0xF2,0xD5,0x43,0x01, 0x92,0x8E,0xBB,0xEE, 0x25,0x69,0x78,0x02};
static const uint8_t kVst3IIDIAudioProcessor[16] = {
    0x42,0x04,0x3F,0x99, 0xB7,0xDA,0x45,0x3C, 0xA5,0x69,0xE7,0x9D, 0x9A,0xAE,0xC3,0x3D};
static const uint8_t kVst3IIDIEventList[16]      = {
    0x3A,0x2C,0x42,0x14, 0x34,0x63,0x49,0xFE, 0xB2,0xC4,0xF3,0x97, 0xB9,0x69,0x5A,0x44};
static const uint8_t kVst3IIDIEditController[16] = {
    0xDC,0xD7,0xBB,0xE3, 0x77,0x42,0x44,0x8D, 0xA8,0x74,0xAA,0xCC, 0x97,0x9C,0x75,0x9E};
static const uint8_t kVst3IIDIPlugView[16]       = {
    0x5B,0xC3,0x25,0x07, 0xD0,0x60,0x49,0xEA, 0xA6,0x15,0x1B,0x52, 0x2B,0x75,0x5B,0x29};

// Data structures — natural alignment only (no pragmas).
struct Vst3PFactoryInfo { char vendor[64]; char url[256]; char email[128]; int32_t flags; };
struct Vst3PClassInfo   { uint8_t classId[16]; int32_t cardinality; char category[32]; char name[64]; };

struct Vst3BusInfo {
    int32_t  mediaType;
    int32_t  direction;
    int32_t  channelCount;
    int16_t  name[128];
    int32_t  busType;
    uint32_t flags;
};

struct Vst3RoutingInfo { int32_t mediaType; int32_t busIndex; int32_t channel; };
struct Vst3ViewRect   { int32_t left, top, right, bottom; };

struct Vst3AudioBusBuffers {
    int32_t  numChannels;
    uint64_t silenceFlags;   // 4-byte natural-alignment pad before this on x86_64
    float**  channelBuffers32;
};

struct Vst3ProcessSetup {
    int32_t processMode;
    int32_t symbolicSampleSize;
    int32_t maxSamplesPerBlock;
    double  sampleRate;       // 4-byte natural-alignment pad before this on x86_64
};

struct Vst3NoteOnEvent  { int16_t channel, pitch; float tuning, velocity; int32_t length, noteId; };
struct Vst3NoteOffEvent { int16_t channel, pitch; float velocity; int32_t noteId; float tuning; };

static constexpr uint16_t kVst3NoteOnEvent  = 0;
static constexpr uint16_t kVst3NoteOffEvent = 1;

struct Vst3ProcessContext {
    uint32_t state;
    double   sampleRate;
    int64_t  projectTimeSamples;
    int64_t  systemTime;
    double   continousTimeSamples;
    double   projectTimeMusic;
    double   barPositionMusic;
    double   cycleStartMusic;
    double   cycleEndMusic;
    double   tempo;
    int32_t  timeSigNumerator;
    int32_t  timeSigDenominator;
    int32_t  chord;
    int32_t  smpteOffsetSubframes;
    struct   { int32_t framesPerSecond; uint32_t flags; } frameRate;
    int32_t  samplesToNextClock;
};
static constexpr uint32_t kVst3CtxPlaying      = 1u << 1;
static constexpr uint32_t kVst3CtxTempoValid   = 1u << 10;
static constexpr uint32_t kVst3CtxTimeSigValid = 1u << 11;
static constexpr uint32_t kVst3CtxContTimeValid = 1u << 17;

struct Vst3Event {
    int32_t  busIndex;
    int32_t  sampleOffset;
    double   ppqPosition;
    uint16_t flags;
    uint16_t type;
    union { Vst3NoteOnEvent noteOn; Vst3NoteOffEvent noteOff; };
};

// Abstract COM interfaces — declaration order == vtable slot order.
class Vst3FUnknown {
public:
    virtual Vst3TResult queryInterface(const char* iid, void** obj) = 0;
    virtual uint32_t    addRef()  = 0;
    virtual uint32_t    release() = 0;
};

class Vst3IBStream;
class Vst3IParamValueQueue;
struct Vst3ProcessData;

class Vst3IPluginBase : public Vst3FUnknown {  // slots 3–4
public:
    virtual Vst3TResult initialize(Vst3FUnknown* context) = 0;
    virtual Vst3TResult terminate() = 0;
};

class Vst3IBStream : public Vst3FUnknown { // slots 3–6
public:
    virtual Vst3TResult read (void* buffer, int32_t numBytes, int32_t* numBytesRead)    = 0;
    virtual Vst3TResult write(void* buffer, int32_t numBytes, int32_t* numBytesWritten) = 0;
    virtual Vst3TResult seek (int64_t pos, int32_t mode, int64_t* result)               = 0;
    virtual Vst3TResult tell (int64_t* pos)                                             = 0;
};

class Vst3IComponent : public Vst3IPluginBase { // slots 5–13
public:
    virtual Vst3TResult getControllerClassId(char* classId) = 0;
    virtual Vst3TResult setIoMode(int32_t mode) = 0;
    virtual int32_t     getBusCount(int32_t type, int32_t dir) = 0;
    virtual Vst3TResult getBusInfo(int32_t type, int32_t dir, int32_t index, Vst3BusInfo& bus) = 0;
    virtual Vst3TResult getRoutingInfo(Vst3RoutingInfo& inInfo, Vst3RoutingInfo& outInfo) = 0;
    virtual Vst3TResult activateBus(int32_t type, int32_t dir, int32_t index, Vst3TBool state) = 0;
    virtual Vst3TResult setActive(Vst3TBool state) = 0;
    virtual Vst3TResult setState(Vst3IBStream* state) = 0;
    virtual Vst3TResult getState(Vst3IBStream* state) = 0;
};

class Vst3IAudioProcessor : public Vst3FUnknown { // slots 3–10
public:
    virtual Vst3TResult setBusArrangements(Vst3SpeakerArrangement* ins, int32_t numIns,
                                           Vst3SpeakerArrangement* outs, int32_t numOuts) = 0;
    virtual Vst3TResult getBusArrangement(int32_t dir, int32_t index, Vst3SpeakerArrangement& arr) = 0;
    virtual Vst3TResult canProcessSampleSize(int32_t symbolicSampleSize) = 0;
    virtual uint32_t    getLatencySamples() = 0;
    virtual Vst3TResult setupProcessing(Vst3ProcessSetup& setup) = 0;
    virtual Vst3TResult setProcessing(Vst3TBool state) = 0;
    virtual Vst3TResult process(Vst3ProcessData& data) = 0;
    virtual uint32_t    getTailSamples() = 0;
};

class Vst3IEventList : public Vst3FUnknown { // slots 3–5
public:
    virtual int32_t     getEventCount() = 0;
    virtual Vst3TResult getEvent(int32_t index, Vst3Event& e) = 0;
    virtual Vst3TResult addEvent(Vst3Event& e) = 0;
};

class Vst3IParamValueQueue : public Vst3FUnknown { // slots 3–6
public:
    virtual Vst3ParamID getParameterId() = 0;
    virtual int32_t     getPointCount() = 0;
    virtual Vst3TResult getPoint(int32_t index, int32_t& sampleOffset, Vst3ParamValue& value) = 0;
    virtual Vst3TResult addPoint(int32_t sampleOffset, Vst3ParamValue value, int32_t& index) = 0;
};

class Vst3IParameterChanges : public Vst3FUnknown { // slots 3–5
public:
    virtual int32_t               getParameterCount() = 0;
    virtual Vst3IParamValueQueue* getParameterData(int32_t index) = 0;
    virtual Vst3IParamValueQueue* addParameterData(const Vst3ParamID& id, int32_t& index) = 0;
};

class Vst3IPluginFactory : public Vst3FUnknown { // slots 3–6
public:
    virtual Vst3TResult getFactoryInfo(Vst3PFactoryInfo* info) = 0;
    virtual int32_t     countClasses() = 0;
    virtual Vst3TResult getClassInfo(int32_t index, Vst3PClassInfo* info) = 0;
    virtual Vst3TResult createInstance(const char* cid, const char* iid, void** obj) = 0;
};

// ProcessData — natural alignment on x86_64 adds 4 bytes of pad after numOutputs.
struct Vst3ProcessData {
    int32_t                processMode;
    int32_t                symbolicSampleSize;
    int32_t                numSamples;
    int32_t                numInputs;
    int32_t                numOutputs;
    Vst3AudioBusBuffers*   inputs;               // offset 24 (natural alignment)
    Vst3AudioBusBuffers*   outputs;
    Vst3IParameterChanges* inputParameterChanges;
    Vst3IParameterChanges* outputParameterChanges;
    Vst3IEventList*        inputEvents;
    Vst3IEventList*        outputEvents;
    void*                  processContext;        // optional, nullptr allowed
};

// ── Host-side stub implementations ──────────────────────────────────────────

class Vst3HostContext final : public Vst3FUnknown {
public:
    Vst3TResult queryInterface(const char*, void** obj) override {
        if (obj) *obj = nullptr;
        return kVst3NoInterface;
    }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }
};

// Stub IComponentHandler — plugins call these when parameters change in their UI.
class Vst3ComponentHandlerStub : public Vst3FUnknown {
public:
    Vst3TResult queryInterface(const char*, void** obj) override { if(obj)*obj=nullptr; return kVst3NoInterface; }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }
    virtual Vst3TResult beginEdit(Vst3ParamID)              { return kVst3Ok; }
    virtual Vst3TResult performEdit(Vst3ParamID, double)    { return kVst3Ok; }
    virtual Vst3TResult endEdit(Vst3ParamID)                { return kVst3Ok; }
    virtual Vst3TResult restartComponent(int32_t)           { return kVst3Ok; }
};

class Vst3NullParamQueue final : public Vst3IParamValueQueue {
public:
    Vst3TResult queryInterface(const char*, void** obj) override { if(obj)*obj=nullptr; return kVst3NoInterface; }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }
    Vst3ParamID getParameterId() override { return 0; }
    int32_t     getPointCount()  override { return 0; }
    Vst3TResult getPoint(int32_t, int32_t&, Vst3ParamValue&) override { return kVst3ResultFalse; }
    Vst3TResult addPoint(int32_t, Vst3ParamValue, int32_t& idx) override { idx = 0; return kVst3Ok; }
};

class Vst3EmptyParamChanges final : public Vst3IParameterChanges {
public:
    Vst3TResult queryInterface(const char*, void** obj) override { if(obj)*obj=nullptr; return kVst3NoInterface; }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }
    int32_t               getParameterCount() override { return 0; }
    Vst3IParamValueQueue* getParameterData(int32_t) override { return nullptr; }
    Vst3IParamValueQueue* addParameterData(const Vst3ParamID&, int32_t& idx) override {
        idx = 0; return &nullQueue_;
    }
private:
    Vst3NullParamQueue nullQueue_;
};

class Vst3SinkEventList final : public Vst3IEventList {
public:
    Vst3TResult queryInterface(const char*, void** obj) override { if(obj)*obj=nullptr; return kVst3NoInterface; }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }
    int32_t     getEventCount() override { return 0; }
    Vst3TResult getEvent(int32_t, Vst3Event&) override { return kVst3ResultFalse; }
    Vst3TResult addEvent(Vst3Event&) override { return kVst3Ok; }
};

// Memory-backed IBStream — host provides it to plugin for setState/getState.
class Vst3MemoryStream final : public Vst3IBStream {
public:
    Vst3TResult queryInterface(const char*, void** obj) override { if (obj) *obj = nullptr; return kVst3NoInterface; }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }

    Vst3TResult read(void* buffer, int32_t numBytes, int32_t* numBytesRead) override {
        if (!buffer || numBytes < 0) return kVst3ResultFalse;
        const int64_t available = static_cast<int64_t>(data_.size()) - cursor_;
        const int32_t actual    = static_cast<int32_t>(std::min<int64_t>(numBytes, available));
        if (actual > 0) std::memcpy(buffer, data_.data() + cursor_, static_cast<std::size_t>(actual));
        cursor_ += actual;
        if (numBytesRead) *numBytesRead = actual;
        return (actual == numBytes) ? kVst3Ok : kVst3ResultFalse;
    }

    Vst3TResult write(void* buffer, int32_t numBytes, int32_t* numBytesWritten) override {
        if (!buffer || numBytes < 0) return kVst3ResultFalse;
        const std::size_t needed = static_cast<std::size_t>(cursor_ + numBytes);
        if (data_.size() < needed) data_.resize(needed);
        std::memcpy(data_.data() + cursor_, buffer, static_cast<std::size_t>(numBytes));
        cursor_ += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kVst3Ok;
    }

    Vst3TResult seek(int64_t pos, int32_t mode, int64_t* result) override {
        int64_t newCursor = cursor_;
        if      (mode == 0) newCursor = pos;                                        // kIBSeekSet
        else if (mode == 1) newCursor = cursor_ + pos;                              // kIBSeekCur
        else if (mode == 2) newCursor = static_cast<int64_t>(data_.size()) + pos;   // kIBSeekEnd
        else return kVst3ResultFalse;
        cursor_ = std::max<int64_t>(0, newCursor);
        if (result) *result = cursor_;
        return kVst3Ok;
    }

    Vst3TResult tell(int64_t* pos) override {
        if (pos) *pos = cursor_;
        return kVst3Ok;
    }

    void rewind() { cursor_ = 0; }
    const std::vector<uint8_t>& bytes() const { return data_; }
    void setBytes(std::vector<uint8_t> d) { data_ = std::move(d); cursor_ = 0; }

private:
    std::vector<uint8_t> data_;
    int64_t              cursor_ = 0;
};

class Vst3InputEventList final : public Vst3IEventList {
public:
    Vst3TResult queryInterface(const char* iid, void** obj) override {
        if (std::memcmp(iid, kVst3IIDIEventList, 16) == 0) { *obj = this; return kVst3Ok; }
        if (obj) *obj = nullptr;
        return kVst3NoInterface;
    }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }
    int32_t getEventCount() override { return static_cast<int32_t>(events_.size()); }
    Vst3TResult getEvent(int32_t index, Vst3Event& e) override {
        if (index < 0 || static_cast<std::size_t>(index) >= events_.size()) return kVst3ResultFalse;
        e = events_[static_cast<std::size_t>(index)]; return kVst3Ok;
    }
    Vst3TResult addEvent(Vst3Event&) override { return kVst3ResultFalse; }
    void clear() { events_.clear(); }
    void pushNoteOn(int midiNote, uint8_t velocity, int32_t sampleOffset = 0) {
        Vst3Event e{};
        e.busIndex = 0; e.sampleOffset = sampleOffset; e.ppqPosition = 0.0; e.flags = 1;
        e.type = kVst3NoteOnEvent;
        e.noteOn = {0, static_cast<int16_t>(midiNote), 0.0f,
                    static_cast<float>(velocity) / 127.0f, 0, midiNote};
        events_.push_back(e);
    }
    void pushNoteOff(int midiNote, int32_t sampleOffset = 0) {
        Vst3Event e{};
        e.busIndex = 0; e.sampleOffset = sampleOffset; e.ppqPosition = 0.0; e.flags = 1;
        e.type = kVst3NoteOffEvent;
        e.noteOff = {0, static_cast<int16_t>(midiNote), 1.0f, midiNote, 0.0f};
        events_.push_back(e);
    }
private:
    std::vector<Vst3Event> events_;
};

// ── Shared utilities ─────────────────────────────────────────────────────────

static std::filesystem::path vst3ResolveSoPath(const std::filesystem::path& bundle) {
    if (std::filesystem::is_directory(bundle)) {
#ifdef _WIN32
        const std::vector<std::string> archdirs = {"x86_64-win", "x86-win"};
        const std::string libext = ".dll";
#else
        const std::vector<std::string> archdirs = {"x86_64-linux", "i686-linux", "aarch64-linux"};
        const std::string libext = ".so";
#endif
        for (const auto& arch : archdirs) {
            const auto contentsDir = bundle / "Contents" / arch;
            if (!std::filesystem::exists(contentsDir)) continue;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(contentsDir, ec)) {
                if (!ec && entry.path().extension() == libext) return entry.path();
            }
        }
    }
#ifdef _WIN32
    if (bundle.extension() == ".vst3" && std::filesystem::is_regular_file(bundle)) return bundle;
#else
    if (bundle.extension() == ".vst3" && std::filesystem::is_regular_file(bundle)) return bundle;
#endif
    return {};
}

static std::string vst3ClassIdToHex(const uint8_t id[16]) {
    static const char kH[] = "0123456789ABCDEF";
    std::string s(32, '\0');
    for (int i = 0; i < 16; ++i) {
        s[i*2]   = kH[(id[i] >> 4) & 0xF];
        s[i*2+1] = kH[id[i] & 0xF];
    }
    return s;
}

static bool vst3HexToClassId(const std::string& hex, uint8_t out[16]) {
    if (hex.size() != 32) return false;
    for (int i = 0; i < 16; ++i) {
        const char b[3] = {hex[i*2], hex[i*2+1], '\0'};
        char* end = nullptr;
        const long v = std::strtol(b, &end, 16);
        if (!end || *end != '\0' || v < 0 || v > 255) return false;
        out[i] = static_cast<uint8_t>(v);
    }
    return true;
}

// ── Vtable dispatch helpers ───────────────────────────────────────────────────
// GCC may devirtualize COM virtual calls to __cxa_pure_virtual when no concrete
// implementation exists in this TU (all plugin types live in dlopen'd .so files).
// Explicit vtable slot extraction bypasses this optimization.

static inline void** vst3VT(void* obj) { return *reinterpret_cast<void***>(obj); }

// FUnknown (slots 0-2)
static Vst3TResult vst3QI(void* o, const char* iid, void** out) {
    return reinterpret_cast<Vst3TResult(*)(void*,const char*,void**)>(vst3VT(o)[0])(o,iid,out);
}
static uint32_t vst3Release(void* o) {
    return reinterpret_cast<uint32_t(*)(void*)>(vst3VT(o)[2])(o);
}
// IPluginFactory (FUnknown 0-2, then 3-6)
static int32_t vst3FCountClasses(void* f) {
    return reinterpret_cast<int32_t(*)(void*)>(vst3VT(f)[4])(f);
}
static Vst3TResult vst3FGetClassInfo(void* f, int32_t i, Vst3PClassInfo* p) {
    return reinterpret_cast<Vst3TResult(*)(void*,int32_t,Vst3PClassInfo*)>(vst3VT(f)[5])(f,i,p);
}
static Vst3TResult vst3FCreateInstance(void* f, const char* cid, const char* iid, void** obj) {
    return reinterpret_cast<Vst3TResult(*)(void*,const char*,const char*,void**)>(vst3VT(f)[6])(f,cid,iid,obj);
}
// IComponent (FUnknown 0-2, IPluginBase 3-4, IComponent 5-13)
static Vst3TResult vst3CInitialize(void* c, Vst3FUnknown* ctx) {
    return reinterpret_cast<Vst3TResult(*)(void*,Vst3FUnknown*)>(vst3VT(c)[3])(c,ctx);
}
static Vst3TResult vst3CTerminate(void* c) {
    return reinterpret_cast<Vst3TResult(*)(void*)>(vst3VT(c)[4])(c);
}
static Vst3TResult vst3CActivateBus(void* c, int32_t t, int32_t d, int32_t i, Vst3TBool s) {
    return reinterpret_cast<Vst3TResult(*)(void*,int32_t,int32_t,int32_t,Vst3TBool)>(vst3VT(c)[10])(c,t,d,i,s);
}
static Vst3TResult vst3CSetActive(void* c, Vst3TBool s) {
    return reinterpret_cast<Vst3TResult(*)(void*,Vst3TBool)>(vst3VT(c)[11])(c,s);
}
// IAudioProcessor (FUnknown 0-2, IAudioProcessor 3-10)
static Vst3TResult vst3PSetBusArrangements(void* p, Vst3SpeakerArrangement* ins, int32_t ni,
                                            Vst3SpeakerArrangement* outs, int32_t no) {
    return reinterpret_cast<Vst3TResult(*)(void*,Vst3SpeakerArrangement*,int32_t,
                                           Vst3SpeakerArrangement*,int32_t)>(vst3VT(p)[3])(p,ins,ni,outs,no);
}
static Vst3TResult vst3PGetBusArrangement(void* p, int32_t d, int32_t i, Vst3SpeakerArrangement* arr) {
    return reinterpret_cast<Vst3TResult(*)(void*,int32_t,int32_t,Vst3SpeakerArrangement*)>(vst3VT(p)[4])(p,d,i,arr);
}
static Vst3TResult vst3PSetupProcessing(void* p, Vst3ProcessSetup* s) {
    return reinterpret_cast<Vst3TResult(*)(void*,Vst3ProcessSetup*)>(vst3VT(p)[7])(p,s);
}
static Vst3TResult vst3PSetProcessing(void* p, Vst3TBool s) {
    return reinterpret_cast<Vst3TResult(*)(void*,Vst3TBool)>(vst3VT(p)[8])(p,s);
}
static Vst3TResult vst3PProcess(void* p, Vst3ProcessData* d) {
    return reinterpret_cast<Vst3TResult(*)(void*,Vst3ProcessData*)>(vst3VT(p)[9])(p,d);
}
// IComponent setState (slot 12), getState (slot 13)
static Vst3TResult vst3CGetState(void* c, void* stream) {
    return reinterpret_cast<Vst3TResult(*)(void*,void*)>(vst3VT(c)[13])(c,stream);
}
static Vst3TResult vst3CSetState(void* c, void* stream) {
    return reinterpret_cast<Vst3TResult(*)(void*,void*)>(vst3VT(c)[12])(c,stream);
}
// IComponent slot 5: getControllerClassId
static Vst3TResult vst3CGetControllerClassId(void* c, char* cid) {
    return reinterpret_cast<Vst3TResult(*)(void*,char*)>(vst3VT(c)[5])(c,cid);
}
// IEditController (FUnknown 0-2, IPluginBase 3-4, IEditController 5-17)
// slot 16: setComponentHandler, slot 17: createView
static Vst3TResult vst3ECSetComponentHandler(void* ec, void* handler) {
    return reinterpret_cast<Vst3TResult(*)(void*,void*)>(vst3VT(ec)[16])(ec,handler);
}
static void* vst3ECCreateView(void* ec, const char* name) {
    return reinterpret_cast<void*(*)(void*,const char*)>(vst3VT(ec)[17])(ec,name);
}
// IPlugView (FUnknown 0-2, IPlugView 3-14)
static Vst3TResult vst3PVIsPlatformTypeSupported(void* pv, const char* type) {
    return reinterpret_cast<Vst3TResult(*)(void*,const char*)>(vst3VT(pv)[3])(pv,type);
}
static Vst3TResult vst3PVAttached(void* pv, void* parent, const char* type) {
    return reinterpret_cast<Vst3TResult(*)(void*,void*,const char*)>(vst3VT(pv)[4])(pv,parent,type);
}
static Vst3TResult vst3PVRemoved(void* pv) {
    return reinterpret_cast<Vst3TResult(*)(void*)>(vst3VT(pv)[5])(pv);
}
static Vst3TResult vst3PVGetSize(void* pv, Vst3ViewRect* rect) {
    return reinterpret_cast<Vst3TResult(*)(void*,Vst3ViewRect*)>(vst3VT(pv)[9])(pv,rect);
}

// ── Vst3InstrumentPlugin ─────────────────────────────────────────────────────

class Vst3InstrumentPlugin final : public extracker::IInstrumentPlugin {
public:
    Vst3InstrumentPlugin(const std::string& classIdHex, const std::filesystem::path& bundlePath)
        : classIdHex_(classIdHex), bundlePath_(bundlePath) {
        loaded_ = vst3HexToClassId(classIdHex_, binaryClassId_) && tryLoad();
    }

    ~Vst3InstrumentPlugin() override {
        shutdown();
        if (moduleHandle_) { EXTRACKER_DL_CLOSE(moduleHandle_); moduleHandle_ = nullptr; }
    }

    void noteOn(int midiNote, std::uint8_t velocity, bool /*retrigger*/) override {
        if (midiNote < 0 || midiNote > 127) return;
        pendingEvents_.pushNoteOn(midiNote, velocity);
        activeNotes_[midiNote] = true;
    }

    void noteOff(int midiNote) override {
        if (midiNote < 0 || midiNote > 127) return;
        pendingEvents_.pushNoteOff(midiNote);
        activeNotes_[midiNote] = false;
    }

    void allNotesOff() override {
        for (int n = 0; n < 128; ++n) {
            if (activeNotes_[n]) { pendingEvents_.pushNoteOff(n); activeNotes_[n] = false; }
        }
    }

    void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
        if (monoBuffer.empty() || sampleRate == 0 || !loaded_) return;
        if (!ensureRuntime(sampleRate, monoBuffer.size())) return;
        renderVst3(monoBuffer);
    }

    void setTransportContext(const extracker::PluginTransportContext& ctx, int64_t projSamples) override {
        transportCtx_         = ctx;
        projectTimeSamples_   = projSamples;
    }

    bool setParameter(const std::string& name, double value) override {
        const std::string prefix = "vst3_param_";
        if (name.rfind(prefix, 0) == 0) {
            Vst3ParamID id = 0;
            std::istringstream parse(name.substr(prefix.size()));
            parse >> id;
            if (parse && parse.eof()) { params_[id] = std::clamp(value, 0.0, 1.0); return true; }
        }
        return false;
    }

    double getParameter(const std::string& name) const override {
        const std::string prefix = "vst3_param_";
        if (name.rfind(prefix, 0) == 0) {
            Vst3ParamID id = 0;
            std::istringstream parse(name.substr(prefix.size()));
            parse >> id;
            if (parse && parse.eof()) {
                const auto it = params_.find(id);
                return (it != params_.end()) ? it->second : 0.0;
            }
        }
        if (name == "vst3_loaded") return loaded_ ? 1.0 : 0.0;
        if (name == "vst3_active") return runtimeActive_ ? 1.0 : 0.0;
        return 0.0;
    }

    std::vector<std::string> listParameters() const override {
        std::vector<std::string> result;
        for (const auto& kv : params_)
            result.push_back("vst3_param_" + std::to_string(kv.first)
                             + " = " + std::to_string(kv.second));
        return result;
    }

    bool savePreset(const std::string& path) const override {
        if (!component_) return false;
        Vst3MemoryStream stream;
        if (vst3CGetState(component_, &stream) != kVst3Ok) return false;
        const auto& bytes = stream.bytes();
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        f.write("VST3PRE", 7);
        const uint32_t size = static_cast<uint32_t>(bytes.size());
        f.write(reinterpret_cast<const char*>(&size), 4);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return f.good();
    }

    bool loadPreset(const std::string& path) override {
        if (!component_) return false;
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        char magic[7]{};
        f.read(magic, 7);
        if (std::memcmp(magic, "VST3PRE", 7) != 0) return false;
        uint32_t size = 0;
        f.read(reinterpret_cast<char*>(&size), 4);
        std::vector<uint8_t> bytes(size);
        f.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!f) return false;
        Vst3MemoryStream stream;
        stream.setBytes(std::move(bytes));
        return vst3CSetState(component_, &stream) == kVst3Ok;
    }


    void* openEditor() override {
        if (plugView_) return plugView_;
        fprintf(stderr, "[vst3 editor] factory=%p component=%p\n", factory_, component_); fflush(stderr);
        if (!factory_) return nullptr;
        // Create component without audio setup if not already done
        if (!component_) {
            void* obj = nullptr;
            if (vst3FCreateInstance(factory_, reinterpret_cast<const char*>(binaryClassId_),
                                    reinterpret_cast<const char*>(kVst3IIDIComponent),
                                    &obj) != kVst3Ok || !obj) {
                fprintf(stderr, "[vst3 editor] createInstance failed\n"); fflush(stderr);
                return nullptr;
            }
            component_ = obj;
            if (vst3CInitialize(component_, &hostCtx_) != kVst3Ok) {
                fprintf(stderr, "[vst3 editor] initialize failed\n"); fflush(stderr);
                vst3Release(component_); component_ = nullptr; return nullptr;
            }
        }
        // Try component itself as IEditController (single-object plugins)
        void* ctrlObj = nullptr;
        const Vst3TResult qiRes = vst3QI(component_,
            reinterpret_cast<const char*>(kVst3IIDIEditController), &ctrlObj);
        fprintf(stderr, "[vst3 editor] QI(IEditController)=%d ctrl=%p\n", qiRes, ctrlObj); fflush(stderr);
        if (qiRes == kVst3Ok && ctrlObj) {
            editController_ = ctrlObj;
            controllerOwned_ = false;
        } else {
            // Separate controller: get class ID from component and create from factory
            uint8_t cid[16]{};
            const Vst3TResult cidRes = vst3CGetControllerClassId(component_, reinterpret_cast<char*>(cid));
            fprintf(stderr, "[vst3 editor] getControllerClassId=%d cid=%02x%02x%02x%02x...\n",
                    cidRes, cid[0],cid[1],cid[2],cid[3]); fflush(stderr);
            if (cidRes != kVst3Ok) return nullptr;
            if (std::all_of(std::begin(cid), std::end(cid), [](uint8_t b){ return b == 0; })) {
                fprintf(stderr, "[vst3 editor] controller class ID is all zeros\n"); fflush(stderr);
                return nullptr;
            }
            const Vst3TResult ciRes = vst3FCreateInstance(factory_, reinterpret_cast<const char*>(cid),
                                    reinterpret_cast<const char*>(kVst3IIDIEditController), &ctrlObj);
            fprintf(stderr, "[vst3 editor] createController=%d ctrl=%p\n", ciRes, ctrlObj); fflush(stderr);
            if (ciRes != kVst3Ok || !ctrlObj) return nullptr;
            editController_ = ctrlObj;
            controllerOwned_ = true;
            vst3CInitialize(editController_, &hostCtx_);
        }
        vst3ECSetComponentHandler(editController_, &compHandler_);
        plugView_ = vst3ECCreateView(editController_, "editor");
        fprintf(stderr, "[vst3 editor] createView=%p\n", plugView_); fflush(stderr);
        if (!plugView_) {
            if (controllerOwned_) { vst3CTerminate(editController_); vst3Release(editController_); }
            editController_ = nullptr;
            return nullptr;
        }
        return plugView_;
    }

    void closeEditor() override {
        if (plugView_) {
            vst3PVRemoved(plugView_);
            vst3Release(plugView_);
            plugView_ = nullptr;
        }
        if (editController_ && controllerOwned_) {
            vst3CTerminate(editController_);
            vst3Release(editController_);
        }
        editController_ = nullptr;
    }

    bool attachEditor(void* handle, const char* type) override {
        if (!plugView_) return false;
        if (vst3PVIsPlatformTypeSupported(plugView_, type) != kVst3Ok) return false;
        return vst3PVAttached(plugView_, handle, type) == kVst3Ok;
    }

    bool getEditorPreferredSize(int& w, int& h) override {
        if (!plugView_) return false;
        Vst3ViewRect rect{};
        if (vst3PVGetSize(plugView_, &rect) != kVst3Ok) return false;
        w = rect.right - rect.left;
        h = rect.bottom - rect.top;
        return w > 0 && h > 0;
    }

    std::size_t activeVoiceCount() const override {
        std::size_t count = 0;
        for (bool b : activeNotes_) count += b ? 1u : 0u;
        return count;
    }

    double activeVoiceFrequencyHz(std::size_t voiceIndex) const override {
        std::size_t idx = 0;
        for (int n = 0; n < 128; ++n) {
            if (activeNotes_[n]) {
                if (idx == voiceIndex) return midiNoteToFrequencyHz(n);
                ++idx;
            }
        }
        return 0.0;
    }

private:
    bool tryLoad() {
        const auto soPath = vst3ResolveSoPath(bundlePath_);
        if (soPath.empty() || !std::filesystem::exists(soPath)) return false;
        moduleHandle_ = EXTRACKER_DL_OPEN_LOAD(soPath.c_str());
        if (!moduleHandle_) {
            fprintf(stderr, "[vst3] tryLoad dlopen failed: %s\n", dlerror()); fflush(stderr);
            return false;
        }
        void* sym = EXTRACKER_DL_SYM(moduleHandle_, "GetPluginFactory");
        if (!sym) { EXTRACKER_DL_CLOSE(moduleHandle_); moduleHandle_ = nullptr; return false; }
        factory_ = reinterpret_cast<void*(*)()>(sym)();
        if (!factory_) { EXTRACKER_DL_CLOSE(moduleHandle_); moduleHandle_ = nullptr; return false; }
        const int32_t n = vst3FCountClasses(factory_);
        for (int32_t i = 0; i < n; ++i) {
            Vst3PClassInfo info{};
            if (vst3FGetClassInfo(factory_, i, &info) == kVst3Ok &&
                std::memcmp(info.classId, binaryClassId_, 16) == 0) return true;
        }
        EXTRACKER_DL_CLOSE(moduleHandle_); moduleHandle_ = nullptr; factory_ = nullptr;
        return false;
    }

    bool ensureRuntime(std::uint32_t sampleRate, std::size_t blockSize) {
        if (runtimeActive_) {
            if (leftBuf_.size() != blockSize) {
                leftBuf_.assign(blockSize, 0.0f);
                rightBuf_.assign(blockSize, 0.0f);
            }
            return true;
        }
        if (!factory_) return false;

        if (!component_) {
            void* obj = nullptr;
            if (vst3FCreateInstance(factory_, reinterpret_cast<const char*>(binaryClassId_),
                                    reinterpret_cast<const char*>(kVst3IIDIComponent),
                                    &obj) != kVst3Ok || !obj) { return false; }
            component_ = obj;
            if (vst3CInitialize(component_, &hostCtx_) != kVst3Ok) {
                vst3Release(component_); component_ = nullptr; return false;
            }
        }

        void* procObj = nullptr;
        if (vst3QI(component_, reinterpret_cast<const char*>(kVst3IIDIAudioProcessor),
                   &procObj) != kVst3Ok || !procObj) {
            vst3CTerminate(component_); vst3Release(component_); component_ = nullptr; return false;
        }
        processor_ = procObj;

        Vst3SpeakerArrangement arr = kVst3Stereo;
        if (vst3PSetBusArrangements(processor_, nullptr, 0, &arr, 1) != kVst3Ok) {
            arr = kVst3Mono;
            vst3PSetBusArrangements(processor_, nullptr, 0, &arr, 1);
        }
        Vst3SpeakerArrangement actual = arr;
        vst3PGetBusArrangement(processor_, 1 /*output*/, 0, &actual);
        numChannels_ = (actual == kVst3Stereo) ? 2 : 1;

        vst3CActivateBus(component_, 0 /*kAudio*/, 1 /*kOutput*/, 0, 1);
        vst3CActivateBus(component_, 1 /*kEvent*/, 0 /*kInput*/,  0, 1);

        Vst3ProcessSetup setup{};
        setup.processMode        = 0;
        setup.symbolicSampleSize = 0;
        setup.maxSamplesPerBlock = static_cast<int32_t>(blockSize);
        setup.sampleRate         = static_cast<double>(sampleRate);
        vst3PSetupProcessing(processor_, &setup);
        sampleRate_ = sampleRate;

        vst3CSetActive(component_, 1);
        vst3PSetProcessing(processor_, 1);

        leftBuf_.assign(blockSize, 0.0f);
        rightBuf_.assign(blockSize, 0.0f);
        channelPtrs_[0] = leftBuf_.data();
        channelPtrs_[1] = rightBuf_.data();
        runtimeActive_ = true;
        return true;
    }

    void renderVst3(std::vector<double>& monoBuffer) {
        const std::size_t n = monoBuffer.size();
        if (leftBuf_.size() != n) {
            leftBuf_.assign(n, 0.0f); rightBuf_.assign(n, 0.0f);
        }
        std::fill(leftBuf_.begin(), leftBuf_.end(), 0.0f);
        std::fill(rightBuf_.begin(), rightBuf_.end(), 0.0f);
        channelPtrs_[0] = leftBuf_.data();
        channelPtrs_[1] = rightBuf_.data();

        Vst3AudioBusBuffers outBus{};
        outBus.numChannels      = numChannels_;
        outBus.silenceFlags     = 0;
        outBus.channelBuffers32 = channelPtrs_;

        Vst3ProcessContext processCtx{};
        processCtx.sampleRate = static_cast<double>(sampleRate_ > 0 ? sampleRate_ : 44100);
        processCtx.state      = kVst3CtxTempoValid | kVst3CtxTimeSigValid | kVst3CtxContTimeValid;
        if (transportCtx_.isPlaying) processCtx.state |= kVst3CtxPlaying;
        processCtx.tempo             = transportCtx_.tempoBpm;
        processCtx.timeSigNumerator  = transportCtx_.timeSigNumerator;
        processCtx.timeSigDenominator = transportCtx_.timeSigDenominator;
        processCtx.projectTimeSamples = projectTimeSamples_;
        processCtx.continousTimeSamples = static_cast<double>(projectTimeSamples_);
        if (processCtx.sampleRate > 0.0) {
            const double posSeconds = static_cast<double>(projectTimeSamples_) / processCtx.sampleRate;
            const double posBeats   = posSeconds * (transportCtx_.tempoBpm / 60.0);
            processCtx.projectTimeMusic  = posBeats;
            const double beatsPerBar     = static_cast<double>(transportCtx_.timeSigNumerator);
            processCtx.barPositionMusic  = std::floor(posBeats / beatsPerBar) * beatsPerBar;
        }

        Vst3EmptyParamChanges noParams;
        Vst3SinkEventList     noOutputEvents;
        Vst3ProcessData data{};
        data.processMode            = 0;
        data.symbolicSampleSize     = 0;
        data.numSamples             = static_cast<int32_t>(n);
        data.numInputs              = 0;
        data.numOutputs             = 1;
        data.inputs                 = nullptr;
        data.outputs                = &outBus;
        data.inputParameterChanges  = &noParams;
        data.outputParameterChanges = nullptr;
        data.inputEvents            = &pendingEvents_;
        data.outputEvents           = &noOutputEvents;
        data.processContext         = &processCtx;

        vst3PProcess(processor_, &data);
        pendingEvents_.clear();

        const double scale = (numChannels_ == 2) ? 0.5 : 1.0;
        for (std::size_t i = 0; i < n; ++i) {
            double s = static_cast<double>(leftBuf_[i]);
            if (numChannels_ == 2) s = (s + static_cast<double>(rightBuf_[i])) * scale;
            if (!std::isfinite(s)) { shutdown(); return; }
            monoBuffer[i] += s;
        }
    }

    void shutdown() {
        closeEditor();
        if (processor_) {
            vst3PSetProcessing(processor_, 0);
            vst3Release(processor_);
            processor_ = nullptr;
        }
        if (component_) {
            vst3CSetActive(component_, 0);
            vst3CTerminate(component_);
            vst3Release(component_);
            component_ = nullptr;
        }
        if (factory_) {
            vst3Release(factory_);
            factory_ = nullptr;
        }
        runtimeActive_ = false;
    }

    std::string           classIdHex_;
    std::filesystem::path bundlePath_;
    uint8_t               binaryClassId_[16]{};
    void*                 moduleHandle_    = nullptr;
    void*                 factory_         = nullptr;
    void*                 component_       = nullptr;
    void*                 processor_       = nullptr;
    void*                 editController_  = nullptr;
    void*                 plugView_        = nullptr;
    bool                  controllerOwned_ = false;
    // Must outlive component_/editController_: plugins store these pointers from initialize().
    Vst3HostContext              hostCtx_;
    Vst3ComponentHandlerStub     compHandler_;
    Vst3InputEventList    pendingEvents_;
    std::vector<float>    leftBuf_;
    std::vector<float>    rightBuf_;
    float*                channelPtrs_[2]{nullptr, nullptr};
    std::unordered_map<Vst3ParamID, double> params_;
    std::array<bool, 128>  activeNotes_{};
    extracker::PluginTransportContext transportCtx_;
    int64_t                projectTimeSamples_ = 0;
    uint32_t               sampleRate_    = 0;
    int32_t                numChannels_   = 2;
    bool                   loaded_        = false;
    bool                   runtimeActive_ = false;
};

// ── Vst3DiscoveryAdapter ─────────────────────────────────────────────────────

class Vst3DiscoveryAdapter final : public extracker::IExternalPluginAdapter {
public:
    std::string adapterName() const override { return "vst3"; }

    std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
        std::size_t discovered = 0;
        for (const auto& bundle : discoverBundles()) {
            for (const auto& ci : scanBundle(bundle)) {
                const std::string pluginId = "vst3." + ci.classIdHex;
                if (registeredIds_.insert(pluginId).second) {
                    const std::string hexCopy  = ci.classIdHex;
                    const auto        pathCopy = bundle;
                    host.registerPluginFactory(pluginId, [hexCopy, pathCopy]() {
                        return std::make_unique<Vst3InstrumentPlugin>(hexCopy, pathCopy);
                    });
                    if (!ci.name.empty()) {
                        host.registerPluginDisplayName(pluginId, ci.name);
                    }
                    discovered += 1;
                }
            }
        }
        return discovered;
    }

private:
    struct ClassEntry { std::string classIdHex; std::string name; };

    static std::vector<std::filesystem::path> searchRoots() {
        std::vector<std::filesystem::path> roots;
        if (const char* env = std::getenv("VST3_PATH")) {
            // If VST3_PATH is set it overrides all defaults (same as LV2_PATH convention)
            std::istringstream ss(env);
            std::string part;
#ifdef _WIN32
            const char sep = ';';
#else
            const char sep = ':';
#endif
            while (std::getline(ss, part, sep)) {
                if (!part.empty()) roots.emplace_back(part);
            }
            return roots;
        }
#ifdef _WIN32
        if (const char* pf = std::getenv("COMMONPROGRAMFILES")) {
            roots.emplace_back(std::filesystem::path(pf) / "VST3");
        }
        if (const char* la = std::getenv("LOCALAPPDATA")) {
            roots.emplace_back(std::filesystem::path(la) / "Programs" / "Common" / "VST3");
        }
#else
        if (const char* home = std::getenv("HOME")) {
            roots.emplace_back(std::filesystem::path(home) / ".vst3");
        }
        roots.emplace_back("/usr/lib/vst3");
        roots.emplace_back("/usr/local/lib/vst3");
#endif
        return roots;
    }

    static std::vector<std::filesystem::path> discoverBundles() {
        std::vector<std::filesystem::path> bundles;
        for (const auto& root : searchRoots()) {
            if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) continue;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
                if (!ec && entry.path().extension() == ".vst3") bundles.push_back(entry.path());
            }
        }
        return bundles;
    }

    // Each bundle is scanned in a forked child so its embedded JUCE (typically a
    // different version than the host) cannot conflict with the host or with other
    // plugins' JUCE instances. RTLD_DEEPBIND isolates symbols within one process
    // but cannot prevent two simultaneously-live JUCE runtimes from fighting over
    // shared X11/signal-handler state. The child writes hex class IDs to a pipe and
    // exits with _exit() to skip C++ global destructors entirely.
    static std::vector<ClassEntry> scanBundle(const std::filesystem::path& bundle) {
        std::vector<ClassEntry> results;
        const auto soPath = vst3ResolveSoPath(bundle);
        if (soPath.empty() || !std::filesystem::exists(soPath)) return results;

#ifndef _WIN32
        int pipefd[2];
        if (pipe(pipefd) != 0) return results;

        const pid_t pid = fork();
        if (pid == 0) {
            // Child: load the plugin and stream class IDs to the pipe, then exit.
            setpgid(0, 0);  // isolate child so kill(0,sig) from plugin can't reach parent
            close(pipefd[0]);
            closeInheritedFds(pipefd[1]);
            void* handle = EXTRACKER_DL_OPEN(soPath.c_str());
            if (handle) {
                void* sym = EXTRACKER_DL_SYM(handle, "GetPluginFactory");
                if (sym) {
                    void* factory = reinterpret_cast<void*(*)()>(sym)();
                    if (factory) {
                        const int32_t n = vst3FCountClasses(factory);
                        for (int32_t i = 0; i < n; ++i) {
                            Vst3PClassInfo info{};
                            if (vst3FGetClassInfo(factory, i, &info) == kVst3Ok &&
                                std::string_view(info.category) == "Audio Module Class") {
                                std::string hex = vst3ClassIdToHex(info.classId);
                                std::string name(info.name, strnlen(info.name, sizeof(info.name)));
                                for (char& c : name) {
                                    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
                                }
                                std::string line = hex + '\t' + name + '\n';
                                write(pipefd[1], line.c_str(), static_cast<ssize_t>(line.size()));
                            }
                        }
                    }
                }
            }
            close(pipefd[1]);
            _exit(0);
        }

        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return results;
        }

        // Parent: read hex IDs from pipe.
        close(pipefd[1]);
        std::string accumulated;
        char buf[512];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
            accumulated.append(buf, static_cast<std::size_t>(n));
        }
        close(pipefd[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) {
            fprintf(stderr, "[vst3] plugin crashed during scan, skipping: %s\n",
                    bundle.filename().c_str());
            return results;
        }

        std::size_t pos = 0;
        while (pos < accumulated.size()) {
            const auto nl = accumulated.find('\n', pos);
            const auto end = (nl == std::string::npos) ? accumulated.size() : nl;
            if (end > pos) {
                const std::string line = accumulated.substr(pos, end - pos);
                const auto tab = line.find('\t');
                if (tab == std::string::npos) {
                    results.push_back({line, std::string()});
                } else {
                    results.push_back({line.substr(0, tab), line.substr(tab + 1)});
                }
            }
            pos = (nl == std::string::npos) ? end : nl + 1;
        }
#else
        // Windows: no fork, fall back to in-process scan
        void* handle = EXTRACKER_DL_OPEN(soPath.c_str());
        if (!handle) return results;
        void* sym = EXTRACKER_DL_SYM(handle, "GetPluginFactory");
        if (!sym) { EXTRACKER_DL_CLOSE(handle); return results; }
        void* factory = reinterpret_cast<void*(*)()>(sym)();
        if (!factory) { EXTRACKER_DL_CLOSE(handle); return results; }
        const int32_t count = vst3FCountClasses(factory);
        for (int32_t i = 0; i < count; ++i) {
            Vst3PClassInfo info{};
            if (vst3FGetClassInfo(factory, i, &info) == kVst3Ok &&
                std::string_view(info.category) == "Audio Module Class") {
                std::string name(info.name, strnlen(info.name, sizeof(info.name)));
                results.push_back({vst3ClassIdToHex(info.classId), name});
            }
        }
        EXTRACKER_DL_CLOSE(handle);
#endif
        return results;
    }

    std::unordered_set<std::string> registeredIds_;
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

#ifdef EXTRACKER_HAVE_FLUIDSYNTH

class SF2Plugin final : public extracker::IInstrumentPlugin {
  std::string        path_;
  int                bank_    = 0;
  int                preset_  = 0;
  double             gain_    = 1.0;
  // Initialized lazily on first use so we get the correct sample rate.
  fluid_settings_t*  settings_ = nullptr;
  fluid_synth_t*     synth_    = nullptr;
  int                sfontId_  = -1;
  std::uint32_t      initRate_ = 0;
  std::vector<float> renderBuf_;

  void applyBankPreset() {
    if (bank_ == 128)
      fluid_synth_set_channel_type(synth_, 0, CHANNEL_TYPE_DRUM);
    else
      fluid_synth_set_channel_type(synth_, 0, CHANNEL_TYPE_MELODIC);
    fluid_synth_bank_select(synth_, 0, bank_);
    fluid_synth_program_change(synth_, 0, preset_);
  }

  bool ensureInit(std::uint32_t sampleRate) {
    if (synth_) return sfontId_ >= 0;
    const std::uint32_t rate = sampleRate > 0 ? sampleRate : 44100u;
    settings_ = new_fluid_settings();
    if (!settings_) return false;
    fluid_settings_setnum(settings_, "synth.sample-rate", static_cast<double>(rate));
    fluid_settings_setint(settings_, "synth.polyphony", 64);
    fluid_settings_setint(settings_, "synth.reverb.active", 0);
    fluid_settings_setint(settings_, "synth.chorus.active", 0);
    fluid_settings_setstr(settings_, "audio.driver", "none");
    synth_ = new_fluid_synth(settings_);
    if (!synth_) return false;
    initRate_ = rate;
    sfontId_  = fluid_synth_sfload(synth_, path_.c_str(), 0);
    if (sfontId_ >= 0) {
      // If the default bank=0/preset=0 doesn't exist in this SF2, auto-pick the
      // first available preset (common for drum kits stored at bank=128).
      if (bank_ == 0 && preset_ == 0) {
        fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(synth_, sfontId_);
        if (sfont && !fluid_sfont_get_preset(sfont, 0, 0)) {
          fluid_sfont_iteration_start(sfont);
          fluid_preset_t* first = fluid_sfont_iteration_next(sfont);
          if (first) {
            bank_   = fluid_preset_get_banknum(first);
            preset_ = fluid_preset_get_num(first);
          }
        }
      }
      applyBankPreset();
    }
    return sfontId_ >= 0;
  }

public:
  SF2Plugin(std::string path, int bank, int preset)
      : path_(std::move(path)), bank_(bank), preset_(preset) {}

  ~SF2Plugin() override {
    if (synth_)    delete_fluid_synth(synth_);
    if (settings_) delete_fluid_settings(settings_);
  }

  SF2Plugin(const SF2Plugin&)            = delete;
  SF2Plugin& operator=(const SF2Plugin&) = delete;

  void noteOn(int midiNote, std::uint8_t velocity, bool /*retrigger*/) override {
    if (ensureInit(initRate_ > 0 ? initRate_ : 44100u) && sfontId_ >= 0)
      fluid_synth_noteon(synth_, 0, midiNote, static_cast<int>(velocity));
  }

  void noteOff(int midiNote) override {
    if (synth_ && sfontId_ >= 0)
      fluid_synth_noteoff(synth_, 0, midiNote);
  }

  void allNotesOff() override {
    if (synth_) fluid_synth_all_notes_off(synth_, 0);
  }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (!ensureInit(sampleRate) || monoBuffer.empty()) return;

    const int nframes = static_cast<int>(monoBuffer.size());
    renderBuf_.assign(static_cast<std::size_t>(nframes * 2), 0.0f);
    // Interleaved stereo: L at indices 0,2,4,…; R at 1,3,5,…
    fluid_synth_write_float(synth_, nframes,
        renderBuf_.data(), 0, 2,
        renderBuf_.data(), 1, 2);

    const double scale = gain_ * 0.5;
    for (int i = 0; i < nframes; ++i) {
      const float l = renderBuf_[static_cast<std::size_t>(i * 2)];
      const float r = renderBuf_[static_cast<std::size_t>(i * 2 + 1)];
      monoBuffer[static_cast<std::size_t>(i)] += scale * (l + r);
    }
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "bank") {
      bank_ = std::clamp(static_cast<int>(value), 0, 16383);
      if (synth_) applyBankPreset();
      return true;
    }
    if (name == "preset") {
      preset_ = std::clamp(static_cast<int>(value), 0, 127);
      if (synth_) applyBankPreset();
      return true;
    }
    if (name == "gain") {
      gain_ = std::clamp(value, 0.0, 4.0);
      return true;
    }
    return false;
  }

  double getParameter(const std::string& name) const override {
    if (name == "bank")   return static_cast<double>(bank_);
    if (name == "preset") return static_cast<double>(preset_);
    if (name == "gain")   return gain_;
    if (name == "loaded") return sfontId_ >= 0 ? 1.0 : 0.0;
    return 0.0;
  }

  std::vector<std::string> listParameters() const override {
    return {"bank", "preset", "gain", "loaded"};
  }

  std::size_t activeVoiceCount() const override {
    if (!synth_) return 0;
    return static_cast<std::size_t>(fluid_synth_get_active_voice_count(synth_));
  }

  double activeVoiceFrequencyHz(std::size_t /*idx*/) const override {
    return 0.0;
  }
};

// Plays one locked drum key chromatically across the keyboard via pitch bend.
// Plugin ID format: sf2:/path/to/file.sf2:melodic:NN  (NN = MIDI key to lock)
class SF2MelodicPlugin final : public extracker::IInstrumentPlugin {
  std::string        path_;
  int                lockKey_;
  double             gain_      = 1.0;
  fluid_settings_t*  settings_  = nullptr;
  fluid_synth_t*     synth_     = nullptr;
  int                sfontId_   = -1;
  std::uint32_t      initRate_  = 0;
  std::vector<float> renderBuf_;

  // Pitch bend range in semitones (±64 covers the full MIDI keyboard from any lock key).
  static constexpr int kBendRange = 64;

  void applyPitchBendRange() {
    fluid_synth_cc(synth_, 0, 101, 0);          // RPN MSB
    fluid_synth_cc(synth_, 0, 100, 0);          // RPN LSB (selects pitch-bend range)
    fluid_synth_cc(synth_, 0,   6, kBendRange); // data entry: semitones
    fluid_synth_cc(synth_, 0,  38, 0);          // data entry: cents
  }

  bool ensureInit(std::uint32_t sampleRate) {
    if (synth_) return sfontId_ >= 0;
    const std::uint32_t rate = sampleRate > 0 ? sampleRate : 44100u;
    settings_ = new_fluid_settings();
    if (!settings_) return false;
    fluid_settings_setnum(settings_, "synth.sample-rate", static_cast<double>(rate));
    fluid_settings_setint(settings_, "synth.polyphony", 32);
    fluid_settings_setint(settings_, "synth.reverb.active", 0);
    fluid_settings_setint(settings_, "synth.chorus.active", 0);
    fluid_settings_setstr(settings_, "audio.driver", "none");
    synth_ = new_fluid_synth(settings_);
    if (!synth_) return false;
    initRate_ = rate;
    sfontId_  = fluid_synth_sfload(synth_, path_.c_str(), 0);
    if (sfontId_ >= 0) {
      // Prefer bank=128/preset=0 (GM drums). Fall back to first available preset.
      int bank = 128;
      int preset = 0;
      fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(synth_, sfontId_);
      if (sfont && !fluid_sfont_get_preset(sfont, 128, 0)) {
        fluid_sfont_iteration_start(sfont);
        fluid_preset_t* first = fluid_sfont_iteration_next(sfont);
        if (first) {
          bank   = fluid_preset_get_banknum(first);
          preset = fluid_preset_get_num(first);
        }
      }
      if (bank == 128)
        fluid_synth_set_channel_type(synth_, 0, CHANNEL_TYPE_DRUM);
      else
        fluid_synth_set_channel_type(synth_, 0, CHANNEL_TYPE_MELODIC);
      fluid_synth_bank_select(synth_, 0, bank);
      fluid_synth_program_change(synth_, 0, preset);
      applyPitchBendRange();
    }
    return sfontId_ >= 0;
  }

public:
  SF2MelodicPlugin(std::string path, int lockKey)
      : path_(std::move(path)), lockKey_(std::clamp(lockKey, 0, 127)) {}

  ~SF2MelodicPlugin() override {
    if (synth_)    delete_fluid_synth(synth_);
    if (settings_) delete_fluid_settings(settings_);
  }

  SF2MelodicPlugin(const SF2MelodicPlugin&)            = delete;
  SF2MelodicPlugin& operator=(const SF2MelodicPlugin&) = delete;

  void noteOn(int midiNote, std::uint8_t velocity, bool /*retrigger*/) override {
    if (!ensureInit(initRate_ > 0 ? initRate_ : 44100u) || sfontId_ < 0) return;
    const int offset    = std::clamp(midiNote - lockKey_, -kBendRange, kBendRange);
    const int bendValue = std::clamp(8192 + offset * (8192 / kBendRange), 0, 16383);
    fluid_synth_all_notes_off(synth_, 0);
    fluid_synth_pitch_bend(synth_, 0, bendValue);
    fluid_synth_noteon(synth_, 0, lockKey_, static_cast<int>(velocity));
  }

  void noteOff(int /*midiNote*/) override {
    if (synth_ && sfontId_ >= 0)
      fluid_synth_noteoff(synth_, 0, lockKey_);
  }

  void allNotesOff() override {
    if (synth_) fluid_synth_all_notes_off(synth_, 0);
  }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (!ensureInit(sampleRate) || monoBuffer.empty()) return;

    const int nframes = static_cast<int>(monoBuffer.size());
    renderBuf_.assign(static_cast<std::size_t>(nframes * 2), 0.0f);
    fluid_synth_write_float(synth_, nframes,
        renderBuf_.data(), 0, 2,
        renderBuf_.data(), 1, 2);

    const double scale = gain_ * 0.5;
    for (int i = 0; i < nframes; ++i) {
      const float l = renderBuf_[static_cast<std::size_t>(i * 2)];
      const float r = renderBuf_[static_cast<std::size_t>(i * 2 + 1)];
      monoBuffer[static_cast<std::size_t>(i)] += scale * (l + r);
    }
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "lockkey") { lockKey_ = std::clamp(static_cast<int>(value), 0, 127); return true; }
    if (name == "gain")    { gain_ = std::clamp(value, 0.0, 4.0); return true; }
    return false;
  }

  double getParameter(const std::string& name) const override {
    if (name == "lockkey") return static_cast<double>(lockKey_);
    if (name == "gain")    return gain_;
    if (name == "loaded")  return sfontId_ >= 0 ? 1.0 : 0.0;
    return 0.0;
  }

  std::vector<std::string> listParameters() const override {
    return {"lockkey", "gain", "loaded"};
  }

  std::size_t activeVoiceCount() const override {
    if (!synth_) return 0;
    return static_cast<std::size_t>(fluid_synth_get_active_voice_count(synth_));
  }

  double activeVoiceFrequencyHz(std::size_t /*idx*/) const override { return 0.0; }
};

class SF2ScanAdapter final : public extracker::IExternalPluginAdapter {
  std::unordered_set<std::string> registered_;
public:
  std::string adapterName() const override { return "sf2"; }

  std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
    std::vector<std::string> searchPaths;

    const char* sf2Path = std::getenv("SF2_PATH");
    if (sf2Path) {
      // When SF2_PATH is set it is used exclusively (overrides system dirs)
      std::istringstream ss(sf2Path);
      std::string seg;
      while (std::getline(ss, seg, ':'))
        if (!seg.empty()) searchPaths.push_back(seg);
    } else {
      searchPaths = {
          "/usr/share/sounds/sf2",
          "/usr/share/soundfonts",
          "/usr/local/share/sounds/sf2",
          "/usr/local/share/soundfonts",
      };
      const char* home = std::getenv("HOME");
      if (home) {
        const std::string h(home);
        searchPaths.push_back(h + "/.local/share/sounds/sf2");
        searchPaths.push_back(h + "/.local/share/soundfonts");
        // Common user instrument library locations
        searchPaths.push_back(h + "/Musikk/musicworks/instruments");
        searchPaths.push_back(h + "/Musikk/instruments");
        searchPaths.push_back(h + "/Music/musicworks/instruments");
        searchPaths.push_back(h + "/Music/instruments");
        searchPaths.push_back(h + "/Nedlastinger");
        searchPaths.push_back(h + "/Downloads");
        searchPaths.push_back(h + "/instruments");
        searchPaths.push_back(h + "/soundfonts");
        searchPaths.push_back(h + "/sf2");
      }
    }

    std::size_t count = 0;
    for (const auto& dir : searchPaths) {
      std::error_code ec;
      if (!std::filesystem::is_directory(dir, ec)) continue;
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        const auto ext = entry.path().extension().string();
        std::string extLow = ext;
        for (auto& c : extLow)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extLow != ".sf2") continue;

        const std::string pathStr  = entry.path().string();
        const std::string pluginId = "sf2:" + pathStr;
        if (registered_.insert(pluginId).second) {
          host.registerPluginFactory(pluginId, [pathStr]() {
            return std::make_unique<SF2Plugin>(pathStr, 0, 0);
          });
          ++count;
        }
      }
    }
    return count;
  }
};

#endif  // EXTRACKER_HAVE_FLUIDSYNTH

#ifdef EXTRACKER_HAVE_SFIZZ
#  include <sfizz.h>

class SFZPlugin final : public extracker::IInstrumentPlugin {
  std::string    path_;
  double         gain_     = 1.0;
  sfizz_synth_t* synth_    = nullptr;
  std::uint32_t  initRate_ = 0;
  // sfizz renders to separate float* channels; we keep these pre-allocated.
  std::vector<float> bufL_;
  std::vector<float> bufR_;

  bool ensureInit(std::uint32_t sampleRate) {
    if (synth_) return true;
    const std::uint32_t rate = sampleRate > 0 ? sampleRate : 44100u;
    synth_ = sfizz_create_synth();
    if (!synth_) return false;
    sfizz_set_sample_rate(synth_, static_cast<float>(rate));
    sfizz_set_samples_per_block(synth_, 1024);
    initRate_ = rate;
    if (!sfizz_load_file(synth_, path_.c_str())) {
      sfizz_free(synth_);
      synth_ = nullptr;
      return false;
    }
    return true;
  }

public:
  explicit SFZPlugin(std::string path) : path_(std::move(path)) {}

  ~SFZPlugin() override {
    if (synth_) sfizz_free(synth_);
  }

  SFZPlugin(const SFZPlugin&)            = delete;
  SFZPlugin& operator=(const SFZPlugin&) = delete;

  void noteOn(int midiNote, std::uint8_t velocity, bool /*retrigger*/) override {
    if (ensureInit(initRate_ > 0 ? initRate_ : 44100u))
      sfizz_send_note_on(synth_, 0, midiNote, static_cast<int>(velocity));
  }

  void noteOff(int midiNote) override {
    if (synth_)
      sfizz_send_note_off(synth_, 0, midiNote, 0);
  }

  void allNotesOff() override {
    if (synth_)
      sfizz_send_hd_note_off(synth_, 0, -1, 0.0f);
  }

  void renderAdd(std::vector<double>& monoBuffer, std::uint32_t sampleRate) override {
    if (!ensureInit(sampleRate) || monoBuffer.empty()) return;

    const int nframes = static_cast<int>(monoBuffer.size());
    bufL_.assign(static_cast<std::size_t>(nframes), 0.0f);
    bufR_.assign(static_cast<std::size_t>(nframes), 0.0f);

    float* channels[2] = { bufL_.data(), bufR_.data() };
    sfizz_render_block(synth_, channels, 2, nframes);

    const double scale = gain_ * 0.5;
    for (int i = 0; i < nframes; ++i) {
      monoBuffer[static_cast<std::size_t>(i)] +=
          scale * (bufL_[static_cast<std::size_t>(i)] +
                   bufR_[static_cast<std::size_t>(i)]);
    }
  }

  bool setParameter(const std::string& name, double value) override {
    if (name == "gain") {
      gain_ = std::clamp(value, 0.0, 4.0);
      return true;
    }
    if (name == "volume" && synth_) {
      sfizz_send_hdcc(synth_, 0, 7, static_cast<float>(std::clamp(value, 0.0, 1.0)));
      return true;
    }
    return false;
  }

  double getParameter(const std::string& name) const override {
    if (name == "gain")   return gain_;
    if (name == "loaded") return synth_ ? 1.0 : 0.0;
    return 0.0;
  }

  std::vector<std::string> listParameters() const override {
    return {"gain", "volume", "loaded"};
  }

  std::size_t activeVoiceCount() const override {
    if (!synth_) return 0;
    return static_cast<std::size_t>(sfizz_get_num_active_voices(synth_));
  }

  double activeVoiceFrequencyHz(std::size_t /*idx*/) const override {
    return 0.0;
  }
};

class SFZScanAdapter final : public extracker::IExternalPluginAdapter {
  std::unordered_set<std::string> registered_;
public:
  std::string adapterName() const override { return "sfz"; }

  std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
    std::vector<std::string> searchPaths;

    const char* sfzPath = std::getenv("SFZ_PATH");
    if (sfzPath) {
      // When SFZ_PATH is set it is used exclusively (overrides system dirs)
      std::istringstream ss(sfzPath);
      std::string seg;
      while (std::getline(ss, seg, ':'))
        if (!seg.empty()) searchPaths.push_back(seg);
    } else {
      searchPaths = {
          "/usr/share/sounds",
          "/usr/share/sfizz",
          "/usr/local/share/sounds",
      };
      const char* home = std::getenv("HOME");
      if (home) {
        const std::string h(home);
        searchPaths.push_back(h + "/.local/share/sounds");
        searchPaths.push_back(h + "/.local/share/sfizz");
        searchPaths.push_back(h + "/Musikk/musicworks/instruments");
        searchPaths.push_back(h + "/Musikk/instruments");
        searchPaths.push_back(h + "/Music/musicworks/instruments");
        searchPaths.push_back(h + "/Music/instruments");
      }
    }

    std::size_t count = 0;
    for (const auto& dir : searchPaths) {
      std::error_code ec;
      if (!std::filesystem::is_directory(dir, ec)) continue;
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        const auto ext = entry.path().extension().string();
        std::string extLow = ext;
        for (auto& c : extLow)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extLow != ".sfz") continue;

        const std::string pathStr  = entry.path().string();
        const std::string pluginId = "sfz:" + pathStr;
        if (registered_.insert(pluginId).second) {
          host.registerPluginFactory(pluginId, [pathStr]() {
            return std::make_unique<SFZPlugin>(pathStr);
          });
          ++count;
        }
      }
    }
    return count;
  }
};

#endif  // EXTRACKER_HAVE_SFIZZ

class S3IScanAdapter final : public extracker::IExternalPluginAdapter {
  std::unordered_set<std::string> registered_;
public:
  std::string adapterName() const override { return "s3i"; }

  std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
    std::vector<std::string> searchPaths;

    const char* s3iEnv = std::getenv("S3I_PATH");
    if (s3iEnv) {
      std::istringstream ss(s3iEnv);
      std::string seg;
      while (std::getline(ss, seg, ':'))
        if (!seg.empty()) searchPaths.push_back(seg);
    } else {
      const char* home = std::getenv("HOME");
      if (home) {
        const std::string h(home);
        searchPaths.push_back(h + "/Musikk/musicworks/instruments");
        searchPaths.push_back(h + "/Musikk/instruments");
        searchPaths.push_back(h + "/Music/musicworks/instruments");
        searchPaths.push_back(h + "/Music/instruments");
        searchPaths.push_back(h + "/instruments");
        searchPaths.push_back(h + "/.local/share/instruments");
      }
    }

    std::size_t count = 0;
    for (const auto& dir : searchPaths) {
      std::error_code ec;
      if (!std::filesystem::is_directory(dir, ec)) continue;
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        const auto ext = entry.path().extension().string();
        std::string extLow = ext;
        for (auto& c : extLow)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extLow != ".s3i") continue;

        const std::string pathStr = entry.path().string();

        // Peek type byte: 2=OPL2 melody, 1=PCM; skip unsupported rhythm types 3-7
        std::uint8_t s3iType = 0;
        if (std::ifstream f(pathStr, std::ios::binary); f)
          f.read(reinterpret_cast<char*>(&s3iType), 1);
        if (s3iType >= 3 && s3iType <= 7) continue;

        const std::string pluginId = "s3i:" + pathStr;
        if (registered_.insert(pluginId).second) {
          host.registerPluginFactory(pluginId, [pathStr]() -> std::unique_ptr<extracker::IInstrumentPlugin> {
            std::uint8_t type = 0;
            if (std::ifstream f(pathStr, std::ios::binary); f)
              f.read(reinterpret_cast<char*>(&type), 1);
            if (type == 2) {
              auto opl = std::make_unique<Opl2Plugin>();
              if (!opl->loadFromS3i(pathStr)) return nullptr;
              return opl;
            } else {
              auto pcm = std::make_unique<S3iPlugin>();
              if (!pcm->loadFromFile(pathStr)) return nullptr;
              return pcm;
            }
          });
          ++count;
        }
      }
    }
    return count;
  }
};

// Discovers .xpm (Akai-style multi-zone keygroup) programs. Registered
// under the bare file path (not a synthetic "xpm:" prefix) so a
// discovered id is exactly what `loadInstrumentAuto`'s extension dispatch
// already loads directly via loadXpmInstrument -- no separate resolution
// path to keep in sync, and `plugin assign <instr> <path>` works whether
// or not the path was ever scanned.
class XpmScanAdapter final : public extracker::IExternalPluginAdapter {
  std::unordered_set<std::string> registered_;
public:
  std::string adapterName() const override { return "xpm"; }

  std::size_t registerDiscoveredPlugins(extracker::PluginHost& host) override {
    std::vector<std::string> searchPaths;

    const char* xpmEnv = std::getenv("XPM_PATH");
    if (xpmEnv) {
      std::istringstream ss(xpmEnv);
      std::string seg;
      while (std::getline(ss, seg, ':'))
        if (!seg.empty()) searchPaths.push_back(seg);
    } else {
      const char* home = std::getenv("HOME");
      if (home) {
        const std::string h(home);
        searchPaths.push_back(h + "/Musikk/musicworks/instruments");
        searchPaths.push_back(h + "/Musikk/instruments");
        searchPaths.push_back(h + "/Music/musicworks/instruments");
        searchPaths.push_back(h + "/Music/instruments");
        searchPaths.push_back(h + "/instruments");
        searchPaths.push_back(h + "/.local/share/instruments");
      }
    }

    std::size_t count = 0;
    for (const auto& dir : searchPaths) {
      std::error_code ec;
      if (!std::filesystem::is_directory(dir, ec)) continue;
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        const auto ext = entry.path().extension().string();
        std::string extLow = ext;
        for (auto& c : extLow)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extLow != ".xpm") continue;

        const std::string pathStr = entry.path().string();
        if (registered_.insert(pathStr).second) {
          host.registerPluginFactory(pathStr, [pathStr]() -> std::unique_ptr<extracker::IInstrumentPlugin> {
            auto plugin = std::make_unique<XpmKeygroupPlugin>();
            if (!plugin->loadFromFile(pathStr)) return nullptr;
            return plugin;
          });
          ++count;
        }
      }
    }
    return count;
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
      effectFactories_{},
      effectSlots_{},
      effectPlugins_{},
      externalAdapters_{},
      loadedPluginCount_(0),
      noteOnEventCount_(0),
      noteOffEventCount_(0) {
  registerPluginFactory("builtin.sine", []() { return std::make_unique<BuiltinSinePlugin>(); });
  registerPluginFactory("builtin.square", []() { return std::make_unique<BuiltinSquarePlugin>(); });
  registerPluginFactory("builtin.sample", []() { return std::make_unique<BuiltinSamplePlugin>(); });
  registerEffectFactory("builtin.gain", []() { return std::make_unique<BuiltinGainEffect>(); });
#ifndef _WIN32
  registerExternalAdapter(std::make_unique<Lv2ManifestAdapter>());
#endif
  registerExternalAdapter(std::make_unique<Vst3DiscoveryAdapter>());
#ifdef EXTRACKER_HAVE_FLUIDSYNTH
  registerExternalAdapter(std::make_unique<SF2ScanAdapter>());
#endif
#ifdef EXTRACKER_HAVE_SFIZZ
  registerExternalAdapter(std::make_unique<SFZScanAdapter>());
#else
  registerExternalAdapter(std::make_unique<BuiltinSfzScanAdapter>());
#endif
  registerExternalAdapter(std::make_unique<S3IScanAdapter>());
  registerExternalAdapter(std::make_unique<XpmScanAdapter>());
  registerExternalAdapter(std::make_unique<DisabledExternalPluginAdapter>());
  instrumentSampleSlots_.fill(-1);
}

void PluginHost::clearInstrumentSlots() {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  for (auto& p : instrumentPlugins_) p.reset();
  instrumentSlots_.fill({});
}

void PluginHost::unloadAll() {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  for (auto& p : instrumentPlugins_) p.reset();
  for (auto& p : sampleSlotPlugins_) p.reset();
  for (auto& p : effectPlugins_) p.reset();
  instrumentSlots_.fill({});
  effectSlots_.fill({});
  loadedPluginIds_.clear();
  loadedPluginCount_ = 0;
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

std::size_t PluginHost::rescanLv2Only() {
  std::size_t discoveredCount = 0;
  for (auto& adapter : externalAdapters_) {
    if (!adapter || adapter->adapterName() != "lv2.manifest") {
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

void PluginHost::registerPluginDisplayName(const std::string& pluginId, const std::string& displayName) {
  if (pluginId.empty() || displayName.empty()) {
    return;
  }
  std::lock_guard<std::timed_mutex> lock(mutex_);
  pluginDisplayNames_[pluginId] = displayName;
}

std::string PluginHost::pluginDisplayName(const std::string& pluginId) const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (auto it = pluginDisplayNames_.find(pluginId); it != pluginDisplayNames_.end()) {
    return it->second;
  }
  return pluginId;
}

bool PluginHost::registerPluginFactory(const std::string& pluginId, PluginFactory factory) {
  if (pluginId.empty() || !factory) {
    return false;
  }

  std::lock_guard<std::timed_mutex> lock(mutex_);

  // Use insert_or_assign to avoid the double-move pitfall: emplace() may move
  // from factory before discovering the key exists, leaving it empty for the
  // subsequent reassignment.
  pluginFactories_.insert_or_assign(pluginId, std::move(factory));

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

  if (!isValidInstrument(instrument) || pluginId.empty()) {
    return false;
  }

  // Auto-load builtins on first assignment so callers don't need a separate loadPlugin call.
  if (loadedPluginIds_.find(pluginId) == loadedPluginIds_.end()) {
    if (pluginFactories_.find(pluginId) == pluginFactories_.end()) {
      return false;
    }
    loadedPluginIds_.insert(pluginId);
    loadedPluginCount_ += 1;
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
  std::lock_guard<std::timed_mutex> lock(mutex_);
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
    auto* sp = asSamplePlugin(instrumentPlugins_[instrument].get());
    if (sp && sp->sampleFrameCount() == 0) {
      const int mapped = instrumentSampleSlots_[instrument];
      const std::uint16_t slot = (mapped >= 0) ? static_cast<std::uint16_t>(mapped)
                                               : static_cast<std::uint16_t>(instrument);
      if (isValidSampleSlot(slot) && !sampleSlotPaths_[slot].empty() && sampleSlotPlugins_[slot]) {
        sampleSlotPlugins_[slot]->noteOn(midiNote, velocity, retrigger);
        noteOnEventCount_ += 1;
        return true;
      }
    }
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
    auto* sp = asSamplePlugin(instrumentPlugins_[instrument].get());
    if (sp && sp->sampleFrameCount() == 0) {
      const int mapped = instrumentSampleSlots_[instrument];
      const std::uint16_t slot = (mapped >= 0) ? static_cast<std::uint16_t>(mapped)
                                               : static_cast<std::uint16_t>(instrument);
      if (isValidSampleSlot(slot) && !sampleSlotPaths_[slot].empty() && sampleSlotPlugins_[slot]) {
        sampleSlotPlugins_[slot]->noteOff(midiNote);
        noteOffEventCount_ += 1;
        return true;
      }
    }
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
  // Apply per-instrument pitch offset (rounded to nearest semitone for MIDI)
  if (instrument < kMaxInstrumentSlotsForFilter) {
    const int offset = static_cast<int>(std::round(pitchOffsets_[instrument]));
    if (offset != 0)
      midiNote = std::clamp(midiNote + offset, 0, 127);
  }

  // If a pattern step explicitly carries a sample slot, that sample has priority over instrument plugins.
  if (sampleSlot != 0xFFFF &&
      isValidSampleSlot(sampleSlot) &&
      !sampleSlotPaths_[sampleSlot].empty() &&
      sampleSlotPlugins_[sampleSlot]) {
    sampleSlotPlugins_[sampleSlot]->noteOn(midiNote, velocity, retrigger);
    noteOnEventCount_ += 1;
    return true;
  }

  // Legacy compatibility: allow instrument column values beyond instrument slots
  // to address sample slots when no explicit sample field is present.
  if (!isValidInstrument(instrument)) {
    const std::uint16_t legacySampleSlot = static_cast<std::uint16_t>(instrument);
    if (isValidSampleSlot(legacySampleSlot) &&
        !sampleSlotPaths_[legacySampleSlot].empty() &&
        sampleSlotPlugins_[legacySampleSlot]) {
      sampleSlotPlugins_[legacySampleSlot]->noteOn(midiNote, velocity, retrigger);
      noteOnEventCount_ += 1;
      return true;
    }
    return false;
  }

  if (!instrumentSlots_[instrument].empty() && instrumentPlugins_[instrument]) {
    // If this is a builtin.sample with no data (e.g. loaded from a legacy song without
    // INSTR_SAMPLE_SLOT), fall back to the sample slot — prefer the mapped slot, otherwise
    // treat the instrument index as the sample slot index.
    auto* sp = asSamplePlugin(instrumentPlugins_[instrument].get());
    if (sp && sp->sampleFrameCount() == 0) {
      const int mapped = instrumentSampleSlots_[instrument];
      const std::uint16_t slot = (mapped >= 0) ? static_cast<std::uint16_t>(mapped)
                                               : static_cast<std::uint16_t>(instrument);
      if (isValidSampleSlot(slot) && !sampleSlotPaths_[slot].empty() && sampleSlotPlugins_[slot]) {
        sampleSlotPlugins_[slot]->noteOn(midiNote, velocity, retrigger);
        noteOnEventCount_ += 1;
        return true;
      }
    }
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
    const std::uint16_t legacySampleSlot = static_cast<std::uint16_t>(instrument);
    if (isValidSampleSlot(legacySampleSlot) &&
        !sampleSlotPaths_[legacySampleSlot].empty() &&
        sampleSlotPlugins_[legacySampleSlot]) {
      sampleSlotPlugins_[legacySampleSlot]->noteOff(midiNote);
      noteOffEventCount_ += 1;
      return true;
    }
    return false;
  }

  if (!instrumentSlots_[instrument].empty() && instrumentPlugins_[instrument]) {
    auto* sp = asSamplePlugin(instrumentPlugins_[instrument].get());
    if (sp && sp->sampleFrameCount() == 0) {
      const int mapped = instrumentSampleSlots_[instrument];
      const std::uint16_t slot = (mapped >= 0) ? static_cast<std::uint16_t>(mapped)
                                               : static_cast<std::uint16_t>(instrument);
      if (isValidSampleSlot(slot) && !sampleSlotPaths_[slot].empty() && sampleSlotPlugins_[slot]) {
        sampleSlotPlugins_[slot]->noteOff(midiNote);
        noteOffEventCount_ += 1;
        return true;
      }
    }
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
  std::lock_guard<std::timed_mutex> lock(mutex_);

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

void PluginHost::setTransportContext(const PluginTransportContext& ctx) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return;
  if (!transportCtx_.isPlaying && ctx.isPlaying) projectTimeSamples_ = 0;
  transportCtx_ = ctx;
}

bool PluginHost::renderInterleaved(std::vector<double>& monoBuffer, std::uint32_t sampleRate) {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (monoBuffer.empty() || sampleRate == 0) {
    return false;
  }

  std::fill(monoBuffer.begin(), monoBuffer.end(), 0.0);

  bool anyRendered = false;
  for (std::size_t i = 0; i < instrumentPlugins_.size(); ++i) {
    if (!instrumentPlugins_[i]) {
      continue;
    }
    instrumentPlugins_[i]->setTransportContext(transportCtx_, projectTimeSamples_);
    const bool hasFilter  = i < instrumentFilters_.size() && instrumentFilters_[i].isActive();
    const bool hasEffects = i < instrumentEffects_.size() && instrumentEffects_[i].params.isActive();
    if (hasFilter || hasEffects) {
      std::vector<double> instrBuf(monoBuffer.size(), 0.0);
      instrumentPlugins_[i]->renderAdd(instrBuf, sampleRate);
      if (hasFilter)  instrumentFilters_[i].apply(instrBuf, static_cast<double>(sampleRate));
      if (hasEffects) applyInstrumentEffects(instrBuf, instrumentEffects_[i].params,
                                              instrumentEffects_[i].state,
                                              static_cast<double>(sampleRate));
      for (std::size_t j = 0; j < monoBuffer.size(); ++j)
        monoBuffer[j] += instrBuf[j];
    } else {
      instrumentPlugins_[i]->renderAdd(monoBuffer, sampleRate);
    }
    anyRendered = anyRendered || instrumentPlugins_[i]->activeVoiceCount() > 0;
  }
  for (std::size_t i = 0; i < sampleSlotPlugins_.size(); ++i) {
    if (!sampleSlotPlugins_[i] || sampleSlotPaths_[i].empty()) {
      continue;
    }
    sampleSlotPlugins_[i]->renderAdd(monoBuffer, sampleRate);
    anyRendered = anyRendered || sampleSlotPlugins_[i]->activeVoiceCount() > 0;
  }
  if (transportCtx_.isPlaying) {
    projectTimeSamples_ += static_cast<int64_t>(monoBuffer.size());
  }

  return anyRendered;
}

bool PluginHost::renderPerInstrument(
    std::array<std::vector<double>, kMaxInstrumentSlots>& instrBuffers,
    std::uint32_t sampleRate) {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (sampleRate == 0) return false;
  const std::size_t n = instrBuffers[0].size();
  if (n == 0) return false;

  for (auto& b : instrBuffers) { b.assign(n, 0.0); }

  bool anyRendered = false;

  for (std::size_t i = 0; i < instrumentPlugins_.size(); ++i) {
    if (!instrumentPlugins_[i]) continue;
    instrumentPlugins_[i]->setTransportContext(transportCtx_, projectTimeSamples_);
    instrumentPlugins_[i]->renderAdd(instrBuffers[i], sampleRate);
    const bool hasFilter  = i < instrumentFilters_.size() && instrumentFilters_[i].isActive();
    const bool hasEffects = i < instrumentEffects_.size() && instrumentEffects_[i].params.isActive();
    if (hasFilter)
      instrumentFilters_[i].apply(instrBuffers[i], static_cast<double>(sampleRate));
    if (hasEffects)
      applyInstrumentEffects(instrBuffers[i], instrumentEffects_[i].params,
                             instrumentEffects_[i].state, static_cast<double>(sampleRate));
    anyRendered = anyRendered || instrumentPlugins_[i]->activeVoiceCount() > 0;
  }

  // Route each sample-slot plugin to the instrument it's assigned to.
  for (std::size_t slot = 0; slot < sampleSlotPlugins_.size(); ++slot) {
    if (!sampleSlotPlugins_[slot] || sampleSlotPaths_[slot].empty()) continue;
    int assignedInstr = -1;
    for (std::size_t instr = 0; instr < instrumentSampleSlots_.size(); ++instr) {
      if (instrumentSampleSlots_[instr] == static_cast<int>(slot)) {
        assignedInstr = static_cast<int>(instr);
        break;
      }
    }
    auto& dest = (assignedInstr >= 0 && assignedInstr < static_cast<int>(instrBuffers.size()))
                   ? instrBuffers[static_cast<std::size_t>(assignedInstr)]
                   : instrBuffers[0];
    sampleSlotPlugins_[slot]->renderAdd(dest, sampleRate);
    anyRendered = anyRendered || sampleSlotPlugins_[slot]->activeVoiceCount() > 0;
  }

  if (transportCtx_.isPlaying) {
    projectTimeSamples_ += static_cast<int64_t>(n);
  }

  return anyRendered;
}

void PluginHost::setInstrumentFilter(std::uint8_t instrument, BiquadType type, float cutoffNorm, float resonanceNorm) {
  if (instrument >= kMaxInstrumentSlots) return;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  instrumentFilters_[instrument].setParams({type, cutoffNorm, resonanceNorm});
  instrumentFilters_[instrument].reset();
}

void PluginHost::clearInstrumentFilter(std::uint8_t instrument) {
  if (instrument >= kMaxInstrumentSlots) return;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  instrumentFilters_[instrument].setParams({BiquadType::Off, 1.0f, 0.0f});
  instrumentFilters_[instrument].reset();
}

BiquadParams PluginHost::getInstrumentFilterParams(std::uint8_t instrument) const {
  if (instrument >= kMaxInstrumentSlots) return {};
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return instrumentFilters_[instrument].params;
}

void PluginHost::setInstrumentEffects(std::uint8_t instrument, const InstrumentEffectParams& p) {
  if (instrument >= kMaxInstrumentSlots) return;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  instrumentEffects_[instrument].params = p;
  instrumentEffects_[instrument].state.reset();
}

void PluginHost::clearInstrumentEffects(std::uint8_t instrument) {
  if (instrument >= kMaxInstrumentSlots) return;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  instrumentEffects_[instrument].params = InstrumentEffectParams{};
  instrumentEffects_[instrument].state.reset();
}

InstrumentEffectParams PluginHost::getInstrumentEffectParams(std::uint8_t instrument) const {
  if (instrument >= kMaxInstrumentSlots) return {};
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return instrumentEffects_[instrument].params;
}

void PluginHost::setInstrumentPitch(std::uint8_t instrument, float semitones) {
  if (instrument >= kMaxInstrumentSlotsForFilter) return;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  pitchOffsets_[instrument] = semitones;
}

float PluginHost::getInstrumentPitch(std::uint8_t instrument) const {
  if (instrument >= kMaxInstrumentSlotsForFilter) return 0.0f;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return pitchOffsets_[instrument];
}

void PluginHost::setInstrumentReverbSend(std::uint8_t instrument, float send) {
  if (instrument >= kMaxInstrumentSlotsForFilter) return;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  reverbSends_[instrument] = std::clamp(send, 0.0f, 1.0f);
}

float PluginHost::getInstrumentReverbSend(std::uint8_t instrument) const {
  if (instrument >= kMaxInstrumentSlotsForFilter) return 0.0f;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return reverbSends_[instrument];
}

void PluginHost::setInstrumentDepth(std::uint8_t instrument, float depth) {
  if (instrument >= kMaxInstrumentSlotsForFilter) return;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  depthOffsets_[instrument] = std::clamp(depth, 0.0f, 1.0f);
}

float PluginHost::getInstrumentDepth(std::uint8_t instrument) const {
  if (instrument >= kMaxInstrumentSlotsForFilter) return 0.0f;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return depthOffsets_[instrument];
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

std::vector<std::string> PluginHost::listInstrumentParameters(std::uint8_t instrument) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidInstrument(instrument) || !instrumentPlugins_[instrument]) {
    return {};
  }
  return instrumentPlugins_[instrument]->listParameters();
}

bool PluginHost::saveInstrumentPreset(std::uint8_t instrument, const std::string& path) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidInstrument(instrument) || !instrumentPlugins_[instrument]) {
    return false;
  }
  return instrumentPlugins_[instrument]->savePreset(path);
}

bool PluginHost::loadInstrumentPreset(std::uint8_t instrument, const std::string& path) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidInstrument(instrument) || !instrumentPlugins_[instrument]) {
    return false;
  }
  return instrumentPlugins_[instrument]->loadPreset(path);
}

bool PluginHost::openPluginEditor(std::uint8_t instrument) {
  IInstrumentPlugin* plugin = nullptr;
  std::string slotId;
  {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(200))) {
      fprintf(stderr, "[editor] lock timeout for instrument %d\n", instrument); fflush(stderr);
      return false;
    }
    if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) {
      fprintf(stderr, "[editor] no plugin at instrument %d\n", instrument); fflush(stderr);
      return false;
    }
    plugin = instrumentPlugins_[instrument].get();
    slotId = instrumentSlots_[instrument];
  }
  fprintf(stderr, "[editor] calling openEditor on instrument %d pluginId='%s' plugin=%p\n",
          instrument, slotId.c_str(), static_cast<void*>(plugin)); fflush(stderr);
  // Call openEditor without the mutex held — it may block while creating the plugin component.
  return plugin->openEditor() != nullptr;
}

void PluginHost::closePluginEditor(std::uint8_t instrument) {
  IInstrumentPlugin* plugin = nullptr;
  {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(200))) return;
    if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) return;
    plugin = instrumentPlugins_[instrument].get();
  }
  plugin->closeEditor();
}

bool PluginHost::attachPluginEditorToWindow(std::uint8_t instrument, void* nativeWindowHandle, const char* platformType) {
  IInstrumentPlugin* plugin = nullptr;
  {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(200))) return false;
    if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) return false;
    plugin = instrumentPlugins_[instrument].get();
  }
  return plugin->attachEditor(nativeWindowHandle, platformType);
}

bool PluginHost::getPluginEditorPreferredSize(std::uint8_t instrument, int& widthOut, int& heightOut) {
  IInstrumentPlugin* plugin = nullptr;
  {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(200))) return false;
    if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) return false;
    plugin = instrumentPlugins_[instrument].get();
  }
  return plugin->getEditorPreferredSize(widthOut, heightOut);
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

std::size_t PluginHost::sampleFrameCountForSlot(std::uint16_t sampleSlot) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return 0;
  }

  const auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return 0;
  }
  return samplePlugin->sampleFrameCount();
}

std::uint32_t PluginHost::sampleRateForSlot(std::uint16_t sampleSlot) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return 0;
  }

  const auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return 0;
  }
  return samplePlugin->sampleRateValue();
}

std::size_t PluginHost::sampleSourceFrameCountForSlot(std::uint16_t sampleSlot) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return 0;
  }

  const auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return 0;
  }
  return samplePlugin->sourceFrameCount();
}

bool PluginHost::trimSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->trimFrames(startFrame, endFrameExclusive);
}

bool PluginHost::restoreSampleSlotSource(std::uint16_t sampleSlot) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->restoreSource();
}

bool PluginHost::normalizeSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->normalizeFrames(startFrame, endFrameExclusive);
}

bool PluginHost::fadeInSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->fadeInFrames(startFrame, endFrameExclusive);
}

bool PluginHost::fadeOutSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->fadeOutFrames(startFrame, endFrameExclusive);
}

bool PluginHost::reverseSampleSlot(std::uint16_t sampleSlot, std::size_t startFrame, std::size_t endFrameExclusive) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->reverseFrames(startFrame, endFrameExclusive);
}

bool PluginHost::resampleSampleSlot(std::uint16_t sampleSlot, std::uint32_t newRate) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->resampleTo(newRate);
}

bool PluginHost::bitDepthSampleSlot(std::uint16_t sampleSlot, int bits, std::size_t startFrame, std::size_t endFrameExclusive) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->quantizeBits(bits, startFrame, endFrameExclusive);
}

bool PluginHost::crossfadeLoopSampleSlot(std::uint16_t sampleSlot, std::size_t lengthFrames) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }

  auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return false;
  }
  return samplePlugin->crossfadeLoop(lengthFrames);
}

std::vector<float> PluginHost::sampleWaveformForSlot(std::uint16_t sampleSlot, std::size_t maxPoints) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return {};
  }

  const auto* samplePlugin = asSamplePlugin(sampleSlotPlugins_[sampleSlot].get());
  if (!samplePlugin) {
    return {};
  }
  return samplePlugin->waveformPreview(maxPoints);
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

bool PluginHost::loadXpmInstrument(const std::string& xpmPath, std::uint8_t instrument) {
  auto plugin = std::make_unique<XpmKeygroupPlugin>();
  if (!plugin->loadFromFile(xpmPath)) {
    return false;
  }
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidInstrument(instrument)) {
    return false;
  }
  instrumentSlots_[instrument] = xpmPath;
  instrumentPlugins_[instrument] = std::move(plugin);
  instrumentSampleSlots_[instrument] = -1;
  if (loadedPluginIds_.insert(xpmPath).second) {
    loadedPluginCount_ += 1;
  }
  return true;
}

bool PluginHost::loadSfzInstrument(const std::string& sfzPath, std::uint8_t instrument) {
  auto plugin = std::make_unique<SfzPlugin>();
  if (!plugin->loadFromFile(sfzPath)) return false;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidInstrument(instrument)) return false;
  instrumentSlots_[instrument] = sfzPath;
  instrumentPlugins_[instrument] = std::move(plugin);
  instrumentSampleSlots_[instrument] = -1;
  if (loadedPluginIds_.insert(sfzPath).second) loadedPluginCount_ += 1;
  return true;
}

bool PluginHost::loadSf2Instrument(const std::string& sf2Path, std::uint8_t instrument) {
#ifdef EXTRACKER_HAVE_FLUIDSYNTH
  if (!std::filesystem::exists(sf2Path)) return false;
  // Register on demand (rather than requiring the file to have been found by
  // a directory `plugin scan`) so an arbitrary/bundled .sf2 path always
  // loads, the same way loadSfzInstrument/loadXpmInstrument already do for
  // their formats.
  const std::string pluginId = "sf2:" + sf2Path;
  registerPluginFactory(pluginId, [sf2Path]() {
    return std::make_unique<SF2Plugin>(sf2Path, 0, 0);
  });
  return assignInstrument(instrument, pluginId);
#else
  (void)sf2Path;
  (void)instrument;
  return false;
#endif
}

bool PluginHost::loadS3iInstrument(const std::string& path, std::uint8_t instrument) {
  // Peek type byte: 1=PCM, 2=OPL2 melody
  std::uint8_t s3iType = 0;
  if (std::ifstream peek(path, std::ios::binary); peek)
    peek.read(reinterpret_cast<char*>(&s3iType), 1);

  std::unique_ptr<IInstrumentPlugin> plugin;
  if (s3iType == 2) {
    auto opl = std::make_unique<Opl2Plugin>();
    if (!opl->loadFromS3i(path)) return false;
    plugin = std::move(opl);
  } else {
    auto pcm = std::make_unique<S3iPlugin>();
    if (!pcm->loadFromFile(path)) return false;
    plugin = std::move(pcm);
  }

  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidInstrument(instrument)) return false;
  instrumentSlots_[instrument] = path;
  instrumentPlugins_[instrument] = std::move(plugin);
  instrumentSampleSlots_[instrument] = -1;
  if (loadedPluginIds_.insert(path).second) loadedPluginCount_ += 1;
  return true;
}

bool PluginHost::loadIffSvxInstrument(const std::string& path, std::uint8_t instrument) {
  auto plugin = std::make_unique<IffSvxPlugin>();
  if (!plugin->loadFromFile(path)) return false;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidInstrument(instrument)) return false;
  instrumentSlots_[instrument] = path;
  instrumentPlugins_[instrument] = std::move(plugin);
  instrumentSampleSlots_[instrument] = -1;
  if (loadedPluginIds_.insert(path).second) loadedPluginCount_ += 1;
  return true;
}

bool PluginHost::loadXiInstrument(const std::string& path, std::uint8_t instrument) {
  auto plugin = std::make_unique<XiPlugin>();
  if (!plugin->loadFromFile(path)) return false;
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidInstrument(instrument)) return false;
  instrumentSlots_[instrument] = path;
  instrumentPlugins_[instrument] = std::move(plugin);
  instrumentSampleSlots_[instrument] = -1;
  if (loadedPluginIds_.insert(path).second) loadedPluginCount_ += 1;
  return true;
}

bool PluginHost::loadInstrumentAuto(const std::string& pathOrId, std::uint8_t instrument) {
  if (pathOrId.empty()) return false;

#ifdef EXTRACKER_HAVE_FLUIDSYNTH
  // sf2:/path/to/file.sf2:melodic:NN — pitch-shifted single-drum-key chromatic mode.
  // Use rfind(":melodic:") so paths with colons still work as long as the suffix is last.
  if (pathOrId.size() > 4 && pathOrId.compare(0, 4, "sf2:") == 0) {
    const auto melodicPos = pathOrId.rfind(":melodic:");
    if (melodicPos != std::string::npos) {
      const std::string sf2Path = pathOrId.substr(4, melodicPos - 4);
      int lockKey = 36;
      try { lockKey = std::stoi(pathOrId.substr(melodicPos + 9)); } catch (...) {}
      const int safeKey  = std::clamp(lockKey, 0, 127);
      const std::string pluginId = "sf2:" + sf2Path + ":melodic:" + std::to_string(safeKey);
      registerPluginFactory(pluginId, [sf2Path, safeKey]() {
        return std::make_unique<SF2MelodicPlugin>(sf2Path, safeKey);
      });
      return assignInstrument(instrument, pluginId);
    }
    // Plain sf2:<path> — register on demand rather than requiring a prior
    // `plugin scan` to have found this exact path.
    return loadSf2Instrument(pathOrId.substr(4), instrument);
  }
#endif

  // Determine extension (lowercase) to decide how to load
  std::string ext;
  const auto dot = pathOrId.rfind('.');
  if (dot != std::string::npos) {
    ext = pathOrId.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  if (ext == ".xpm") return loadXpmInstrument(pathOrId, instrument);
  if (ext == ".sfz") return loadSfzInstrument(pathOrId, instrument);
  if (ext == ".sf2") return loadSf2Instrument(pathOrId, instrument);
  if (ext == ".s3i") return loadS3iInstrument(pathOrId, instrument);
  if (ext == ".xi")  return loadXiInstrument(pathOrId, instrument);
  if (ext == ".iff" || ext == ".8svx") return loadIffSvxInstrument(pathOrId, instrument);

  // Builtin or registered plugin ID — use the standard path
  if (!loadPlugin(pathOrId)) return false;
  return assignInstrument(instrument, pathOrId);
}

std::vector<int> PluginHost::enumSF2DrumKeys(const std::string& sf2Path) const {
#ifdef EXTRACKER_HAVE_FLUIDSYNTH
  fluid_settings_t* settings = new_fluid_settings();
  if (!settings) return {};
  fluid_settings_setnum(settings, "synth.sample-rate", 44100.0);
  fluid_settings_setint(settings, "synth.polyphony", 16);
  fluid_settings_setint(settings, "synth.reverb.active", 0);
  fluid_settings_setint(settings, "synth.chorus.active", 0);
  fluid_settings_setstr(settings, "audio.driver", "none");
  fluid_synth_t* synth = new_fluid_synth(settings);
  if (!synth) { delete_fluid_settings(settings); return {}; }

  const int sfontId = fluid_synth_sfload(synth, sf2Path.c_str(), 0);
  if (sfontId < 0) {
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
    return {};
  }

  // Auto-select the drum preset: prefer bank=128/preset=0 (GM drums).
  // If that doesn't exist in this SF2, fall back to the first available preset.
  int probeBank = 128;
  int probePreset = 0;
  {
    fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(synth, sfontId);
    if (sfont && !fluid_sfont_get_preset(sfont, 128, 0)) {
      fluid_sfont_iteration_start(sfont);
      fluid_preset_t* first = fluid_sfont_iteration_next(sfont);
      if (first) {
        probeBank   = fluid_preset_get_banknum(first);
        probePreset = fluid_preset_get_num(first);
      }
    }
  }
  if (probeBank == 128)
    fluid_synth_set_channel_type(synth, 0, CHANNEL_TYPE_DRUM);
  else
    fluid_synth_set_channel_type(synth, 0, CHANNEL_TYPE_MELODIC);
  fluid_synth_bank_select(synth, 0, probeBank);
  fluid_synth_program_change(synth, 0, probePreset);

  // Probe each key: fire noteOn, render a tiny buffer, check for non-zero output.
  static constexpr int kProbeFrames = 128;
  std::vector<float> buf(static_cast<std::size_t>(kProbeFrames * 2), 0.0f);
  std::vector<int> result;

  for (int key = 0; key < 128; ++key) {
    std::fill(buf.begin(), buf.end(), 0.0f);
    fluid_synth_noteon(synth, 0, key, 100);
    fluid_synth_write_float(synth, kProbeFrames, buf.data(), 0, 2, buf.data(), 1, 2);
    fluid_synth_noteoff(synth, 0, key);
    fluid_synth_all_sounds_off(synth, 0);

    for (const float s : buf) {
      if (s != 0.0f) { result.push_back(key); break; }
    }
  }

  delete_fluid_synth(synth);
  delete_fluid_settings(settings);
  return result;
#else
  (void)sf2Path;
  return {};
#endif
}

bool PluginHost::exportInstrumentSamples(std::uint8_t instrument, const std::string& outputDir) {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidInstrument(instrument) || !instrumentPlugins_[instrument]) return false;
  return instrumentPlugins_[instrument]->exportSamples(outputDir);
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

std::unique_ptr<IEffectPlugin> PluginHost::createEffectInstance(const std::string& pluginId) const {
  auto it = effectFactories_.find(pluginId);
  if (it == effectFactories_.end()) {
    return nullptr;
  }
  return it->second();
}

bool PluginHost::isValidEffectSlot(std::uint8_t slot) const {
  return slot < kMaxEffectSlots;
}

bool PluginHost::registerEffectFactory(const std::string& pluginId, EffectFactory factory) {
  if (pluginId.empty() || !factory) {
    return false;
  }
  std::lock_guard<std::timed_mutex> lock(mutex_);
  effectFactories_[pluginId] = std::move(factory);
  return true;
}

bool PluginHost::assignEffect(std::uint8_t slot, const std::string& pluginId) {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidEffectSlot(slot) || pluginId.empty()) {
    return false;
  }
  if (effectFactories_.find(pluginId) == effectFactories_.end()) {
    return false;
  }
  auto effect = createEffectInstance(pluginId);
  if (!effect) {
    return false;
  }
  effectSlots_[slot] = pluginId;
  effectPlugins_[slot] = std::move(effect);
  return true;
}

bool PluginHost::removeEffect(std::uint8_t slot) {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidEffectSlot(slot)) {
    return false;
  }
  effectSlots_[slot].clear();
  effectPlugins_[slot].reset();
  return true;
}

bool PluginHost::hasEffectAssignment(std::uint8_t slot) const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  if (!isValidEffectSlot(slot)) {
    return false;
  }
  return !effectSlots_[slot].empty();
}

std::string PluginHost::pluginForEffect(std::uint8_t slot) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidEffectSlot(slot)) {
    return "";
  }
  return effectSlots_[slot];
}

bool PluginHost::setEffectParameter(std::uint8_t slot, const std::string& name, double value) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidEffectSlot(slot) || !effectPlugins_[slot]) {
    return false;
  }
  return effectPlugins_[slot]->setParameter(name, value);
}

double PluginHost::getEffectParameter(std::uint8_t slot, const std::string& name) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock() || !isValidEffectSlot(slot) || !effectPlugins_[slot]) {
    return 0.0;
  }
  return effectPlugins_[slot]->getParameter(name);
}

void PluginHost::renderEffectChain(std::vector<double>& monoBuffer, std::uint32_t sampleRate) {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  for (std::size_t i = 0; i < kMaxEffectSlots; ++i) {
    if (effectPlugins_[i]) {
      effectPlugins_[i]->process(monoBuffer, sampleRate);
    }
  }
}

bool PluginHost::setSampleSlotParameter(std::uint16_t sampleSlot, const std::string& name, double value) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return false;
  }
  if (!sampleSlotPlugins_[sampleSlot]) {
    return false;
  }
  return sampleSlotPlugins_[sampleSlot]->setParameter(name, value);
}

double PluginHost::getSampleSlotParameter(std::uint16_t sampleSlot, const std::string& name) const {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)) || !isValidSampleSlot(sampleSlot)) {
    return 0.0;
  }
  if (!sampleSlotPlugins_[sampleSlot]) {
    return 0.0;
  }
  return sampleSlotPlugins_[sampleSlot]->getParameter(name);
}

}  // namespace extracker
