#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace {

struct Lv2Feature {
  const char* uri;
  const void* data;
};

using Lv2Handle = void*;

// Mirror of LV2 atom structs (must match host definition)
struct Lv2Atom {
  std::uint32_t size;
  std::uint32_t type;
};

struct Lv2AtomSequenceBody {
  std::uint32_t unit;
  std::uint32_t pad;
};

struct Lv2AtomSequence {
  Lv2Atom atom;
  Lv2AtomSequenceBody body;
};

struct Lv2AtomEvent {
  std::int64_t  frames;
  Lv2Atom       body;
};

static constexpr std::uint32_t kUridMidiEvent = 2;  // matches host assignment

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

struct PluginState {
  float*   in = nullptr;
  float*   out = nullptr;
  float*   gain = nullptr;
  float*   meter = nullptr;
  void*    atomSeq = nullptr;  // LV2_Atom_Sequence*
  float    phase = 0.0f;
  float    frequency = 0.0f;
  bool     noteActive = false;
  float    eventBoost = 0.0f;
};

Lv2Handle instantiate(const Lv2Descriptor*, double, const char*, const Lv2Feature* const*) {
  return new PluginState();
}

void connectPort(Lv2Handle instance, std::uint32_t port, void* data) {
  auto* state = static_cast<PluginState*>(instance);
  if (state == nullptr) {
    return;
  }

  if (port == 0) {
    state->in = static_cast<float*>(data);
  } else if (port == 1) {
    state->out = static_cast<float*>(data);
  } else if (port == 2) {
    state->gain = static_cast<float*>(data);
  } else if (port == 3) {
    state->meter = static_cast<float*>(data);
  } else if (port == 4) {
    state->atomSeq = data;  // LV2_Atom_Sequence*
  }
}

void activate(Lv2Handle) {}

void run(Lv2Handle instance, std::uint32_t sampleCount) {
  auto* state = static_cast<PluginState*>(instance);
  if (state == nullptr || state->out == nullptr) {
    return;
  }

  if (state->atomSeq != nullptr) {
    const auto* seq = static_cast<const Lv2AtomSequence*>(state->atomSeq);
    const char* iter = reinterpret_cast<const char*>(seq) + sizeof(Lv2AtomSequence);
    const char* end  = reinterpret_cast<const char*>(seq) + sizeof(Lv2Atom) + seq->atom.size;
    while (iter < end) {
      const auto* ev = reinterpret_cast<const Lv2AtomEvent*>(iter);
      if (ev->body.type == kUridMidiEvent && ev->body.size >= 3) {
        const auto* midi = reinterpret_cast<const std::uint8_t*>(ev) + sizeof(Lv2AtomEvent);
        const std::uint8_t status   = midi[0];
        const std::uint8_t note     = midi[1];
        const std::uint8_t velocity = midi[2];
        if ((status & 0xF0) == 0x90 && velocity > 0) {
          state->noteActive = true;
          state->frequency  = 440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
          state->phase      = 0.0f;
          state->eventBoost = 0.3f;
        } else if ((status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && velocity == 0)) {
          state->noteActive = false;
          state->eventBoost = 0.0f;
        }
      }
      // Advance by sizeof(Lv2AtomEvent) + body.size, padded to 8-byte boundary.
      const std::size_t eventBytes = sizeof(Lv2AtomEvent) + ev->body.size;
      const std::size_t aligned    = (eventBytes + 7) & ~static_cast<std::size_t>(7);
      iter += aligned;
    }
  }

  const float gain = (state->gain != nullptr) ? *state->gain : 1.0f;
  float sumAbs = 0.0f;
  constexpr float twoPi = 6.28318530717958f;

  for (std::uint32_t i = 0; i < sampleCount; ++i) {
    float outSample = 0.0f;

    if (state->noteActive && state->frequency > 0.0f) {
      outSample = std::sin(state->phase);
      state->phase += twoPi * state->frequency / 44100.0f;
      if (state->phase >= twoPi) {
        state->phase -= twoPi;
      }
    }

    const float inSample = state->in != nullptr ? state->in[i] : 0.0f;
    state->out[i] = (inSample + 0.25f + state->eventBoost + outSample) * gain;
    sumAbs += std::abs(state->out[i]);
  }

  if (state->meter != nullptr && sampleCount > 0) {
    *state->meter = sumAbs / static_cast<float>(sampleCount);
  }
}

void deactivate(Lv2Handle) {}

void cleanup(Lv2Handle instance) {
  delete static_cast<PluginState*>(instance);
}

const void* extensionData(const char*) {
  return nullptr;
}

const Lv2Descriptor kDescriptor = {
    "urn:extracker:test:runtime",
    instantiate,
    connectPort,
    activate,
    run,
    deactivate,
    cleanup,
    extensionData};

}  // namespace

extern "C" const Lv2Descriptor* lv2_descriptor(std::uint32_t index) {
  if (index == 0) {
    return &kDescriptor;
  }
  return nullptr;
}
