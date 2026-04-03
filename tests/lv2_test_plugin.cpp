#include <cstdint>
#include <cstdlib>

namespace {

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

struct PluginState {
  float* in = nullptr;
  float* out = nullptr;
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
  }
}

void activate(Lv2Handle) {}

void run(Lv2Handle instance, std::uint32_t sampleCount) {
  auto* state = static_cast<PluginState*>(instance);
  if (state == nullptr || state->out == nullptr) {
    return;
  }

  for (std::uint32_t i = 0; i < sampleCount; ++i) {
    const float inSample = state->in != nullptr ? state->in[i] : 0.0f;
    state->out[i] = inSample + 0.25f;
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
