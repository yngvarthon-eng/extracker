// Minimal VST3 instrument plugin for integration tests.
// All COM ABI stubs are defined inline — no external SDK dependency.
// This .so is loaded by the Vst3DiscoveryAdapter via RTLD_NOW | RTLD_LOCAL.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace {

// ── COM types (must match host stub vtable order exactly) ───────────────────

using TResult = int32_t;
using TBool   = int32_t;
using SpeakerArrangement = uint64_t;
using ParamID = uint32_t;
using ParamValue = double;

static constexpr TResult kOk          = 0;
static constexpr TResult kResultFalse = 1;
static constexpr TResult kNoInterface = static_cast<TResult>(0x80004002);
static constexpr SpeakerArrangement kStereo = 0x3;
static constexpr SpeakerArrangement kMono   = 0x1;

static const uint8_t kIIDIPluginFactory[16]  = {
    0x7A,0x4D,0x81,0x1C, 0x52,0x11,0x4A,0x1F, 0xAE,0xD9,0xD2,0xEE, 0x0B,0x61,0x5A,0xA3};
static const uint8_t kIIDIComponent[16]      = {
    0xE8,0x31,0xFF,0x31, 0xF2,0xD5,0x43,0x01, 0x92,0x8E,0xBB,0xEE, 0x25,0x69,0x78,0x02};
static const uint8_t kIIDIAudioProcessor[16] = {
    0x42,0x04,0x3F,0x99, 0xB7,0xDA,0x45,0x3C, 0xA5,0x69,0xE7,0x9D, 0x9A,0xAE,0xC3,0x3D};
static const uint8_t kIIDIEventList[16]      = {
    0x3A,0x2C,0x42,0x14, 0x34,0x63,0x49,0xFE, 0xB2,0xC4,0xF3,0x97, 0xB9,0x69,0x5A,0x44};

// Test plugin classId — host will call it "vst3.0102030405060708090A0B0C0D0E0F10"
static const uint8_t kTestClassId[16] = {
    0x01,0x02,0x03,0x04, 0x05,0x06,0x07,0x08, 0x09,0x0A,0x0B,0x0C, 0x0D,0x0E,0x0F,0x10};

// Data structures — natural alignment, matching host layout on x86_64.
struct PFactoryInfo { char vendor[64]; char url[256]; char email[128]; int32_t flags; };
struct PClassInfo   { uint8_t classId[16]; int32_t cardinality; char category[32]; char name[64]; };

struct BusInfo {
    int32_t  mediaType;
    int32_t  direction;
    int32_t  channelCount;
    int16_t  name[128];
    int32_t  busType;
    uint32_t flags;
};

struct RoutingInfo { int32_t mediaType; int32_t busIndex; int32_t channel; };

struct AudioBusBuffers {
    int32_t  numChannels;
    uint64_t silenceFlags;
    float**  channelBuffers32;
};

struct ProcessSetup {
    int32_t processMode;
    int32_t symbolicSampleSize;
    int32_t maxSamplesPerBlock;
    double  sampleRate;
};

struct NoteOnEvent  { int16_t channel, pitch; float tuning, velocity; int32_t length, noteId; };
struct NoteOffEvent { int16_t channel, pitch; float velocity; int32_t noteId; float tuning; };

static constexpr uint16_t kNoteOnEvent  = 0;
static constexpr uint16_t kNoteOffEvent = 1;

struct Event {
    int32_t  busIndex;
    int32_t  sampleOffset;
    double   ppqPosition;
    uint16_t flags;
    uint16_t type;
    union { NoteOnEvent noteOn; NoteOffEvent noteOff; };
};

struct ProcessData {
    int32_t         processMode;
    int32_t         symbolicSampleSize;
    int32_t         numSamples;
    int32_t         numInputs;
    int32_t         numOutputs;
    AudioBusBuffers* inputs;
    AudioBusBuffers* outputs;
    void*           inputParameterChanges;
    void*           outputParameterChanges;
    void*           inputEvents;    // IEventList*
    void*           outputEvents;
    void*           processContext;
};

// Abstract COM interfaces (same vtable slot order as host stubs).
class FUnknown {
public:
    virtual TResult queryInterface(const char* iid, void** obj) = 0;
    virtual uint32_t addRef()  = 0;
    virtual uint32_t release() = 0;
};

class IPluginBase : public FUnknown {
public:
    virtual TResult initialize(FUnknown* context) = 0;
    virtual TResult terminate() = 0;
};

class IBStream;

class IComponent : public IPluginBase {
public:
    virtual TResult getControllerClassId(char*) = 0;
    virtual TResult setIoMode(int32_t) = 0;
    virtual int32_t getBusCount(int32_t type, int32_t dir) = 0;
    virtual TResult getBusInfo(int32_t type, int32_t dir, int32_t index, BusInfo& bus) = 0;
    virtual TResult getRoutingInfo(RoutingInfo& inInfo, RoutingInfo& outInfo) = 0;
    virtual TResult activateBus(int32_t type, int32_t dir, int32_t index, TBool state) = 0;
    virtual TResult setActive(TBool state) = 0;
    virtual TResult setState(IBStream* state) = 0;
    virtual TResult getState(IBStream* state) = 0;
};

class IAudioProcessor : public FUnknown {
public:
    virtual TResult setBusArrangements(SpeakerArrangement* ins, int32_t numIns,
                                       SpeakerArrangement* outs, int32_t numOuts) = 0;
    virtual TResult getBusArrangement(int32_t dir, int32_t index, SpeakerArrangement& arr) = 0;
    virtual TResult canProcessSampleSize(int32_t) = 0;
    virtual uint32_t getLatencySamples() = 0;
    virtual TResult setupProcessing(ProcessSetup& setup) = 0;
    virtual TResult setProcessing(TBool state) = 0;
    virtual TResult process(ProcessData& data) = 0;
    virtual uint32_t getTailSamples() = 0;
};

// IEventList interface for reading host events during process().
class IEventList : public FUnknown {
public:
    virtual int32_t getEventCount() = 0;
    virtual TResult getEvent(int32_t index, Event& e) = 0;
    virtual TResult addEvent(Event& e) = 0;
};

class IPluginFactory : public FUnknown {
public:
    virtual TResult getFactoryInfo(PFactoryInfo* info) = 0;
    virtual int32_t countClasses() = 0;
    virtual TResult getClassInfo(int32_t index, PClassInfo* info) = 0;
    virtual TResult createInstance(const char* cid, const char* iid, void** obj) = 0;
};

// ── TestPlugin — implements IComponent + IAudioProcessor ────────────────────
// Dual inheritance: createInstance queries for IComponent or IAudioProcessor
// by returning the same object via queryInterface.

class TestPlugin : public IComponent, public IAudioProcessor {
public:
    // FUnknown (shared by IComponent slot 0-2 and IAudioProcessor slot 0-2)
    TResult queryInterface(const char* iid, void** obj) override {
        if (std::memcmp(iid, kIIDIComponent, 16) == 0)      { *obj = static_cast<IComponent*>(this);      return kOk; }
        if (std::memcmp(iid, kIIDIAudioProcessor, 16) == 0) { *obj = static_cast<IAudioProcessor*>(this); return kOk; }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }

    // IPluginBase
    TResult initialize(FUnknown*) override { return kOk; }
    TResult terminate() override           { return kOk; }

    // IComponent
    TResult getControllerClassId(char*) override   { return kResultFalse; }
    TResult setIoMode(int32_t) override            { return kOk; }
    int32_t getBusCount(int32_t type, int32_t dir) override {
        // One audio output bus, one event input bus.
        if (type == 0 /*kAudio*/ && dir == 1 /*kOutput*/) return 1;
        if (type == 1 /*kEvent*/ && dir == 0 /*kInput*/)  return 1;
        return 0;
    }
    TResult getBusInfo(int32_t type, int32_t dir, int32_t index, BusInfo& bus) override {
        if (index != 0) return kResultFalse;
        std::memset(&bus, 0, sizeof(bus));
        if (type == 0 /*kAudio*/ && dir == 1) { bus.mediaType = 0; bus.direction = 1; bus.channelCount = 1; bus.busType = 0; bus.flags = 1; return kOk; }
        if (type == 1 /*kEvent*/ && dir == 0) { bus.mediaType = 1; bus.direction = 0; bus.channelCount = 1; bus.busType = 0; bus.flags = 1; return kOk; }
        return kResultFalse;
    }
    TResult getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kResultFalse; }
    TResult activateBus(int32_t, int32_t, int32_t, TBool) override { return kOk; }
    TResult setActive(TBool) override  { return kOk; }
    TResult setState(IBStream*) override { return kOk; }
    TResult getState(IBStream*) override { return kOk; }

    // IAudioProcessor
    TResult setBusArrangements(SpeakerArrangement*, int32_t, SpeakerArrangement* outs, int32_t numOuts) override {
        if (numOuts == 1) { outputArrangement_ = outs[0]; return kOk; }
        return kResultFalse;
    }
    TResult getBusArrangement(int32_t dir, int32_t index, SpeakerArrangement& arr) override {
        if (dir == 1 && index == 0) { arr = outputArrangement_; return kOk; }
        return kResultFalse;
    }
    TResult canProcessSampleSize(int32_t size) override { return (size == 0) ? kOk : kResultFalse; }
    uint32_t getLatencySamples() override { return 0; }
    TResult setupProcessing(ProcessSetup& setup) override {
        sampleRate_ = static_cast<float>(setup.sampleRate);
        return kOk;
    }
    TResult setProcessing(TBool) override { return kOk; }
    uint32_t getTailSamples() override { return 0; }

    TResult process(ProcessData& data) override {
        if (data.inputEvents) {
            auto* evList = static_cast<IEventList*>(data.inputEvents);
            const int32_t n = evList->getEventCount();
            for (int32_t i = 0; i < n; ++i) {
                Event e{};
                if (evList->getEvent(i, e) == kOk) {
                    if (e.type == kNoteOnEvent)  { activeNote_ = e.noteOn.pitch;  velocity_ = e.noteOn.velocity; }
                    if (e.type == kNoteOffEvent) { if (activeNote_ == e.noteOff.pitch) activeNote_ = -1; }
                }
            }
        }

        if (!data.outputs || data.numOutputs < 1) return kOk;
        AudioBusBuffers& out = data.outputs[0];
        const int32_t frames = data.numSamples;
        const int32_t nch    = out.numChannels;
        if (nch < 1 || !out.channelBuffers32) return kOk;

        if (activeNote_ >= 0) {
            const float freq = 440.0f * std::pow(2.0f, (static_cast<float>(activeNote_) - 69.0f) / 12.0f);
            const float step = freq / (sampleRate_ > 0.0f ? sampleRate_ : 44100.0f);
            for (int32_t s = 0; s < frames; ++s) {
                const float sample = velocity_ * std::sin(phase_ * 6.2831853f);
                for (int32_t ch = 0; ch < nch; ++ch) {
                    if (out.channelBuffers32[ch]) out.channelBuffers32[ch][s] = sample;
                }
                phase_ += step;
                if (phase_ >= 1.0f) phase_ -= 1.0f;
            }
        } else {
            for (int32_t ch = 0; ch < nch; ++ch) {
                if (out.channelBuffers32[ch])
                    std::memset(out.channelBuffers32[ch], 0, static_cast<std::size_t>(frames) * sizeof(float));
            }
        }
        return kOk;
    }

private:
    SpeakerArrangement outputArrangement_ = kMono;
    float sampleRate_ = 44100.0f;
    float phase_      = 0.0f;
    float velocity_   = 0.0f;
    int32_t activeNote_ = -1;
};

// ── Factory ─────────────────────────────────────────────────────────────────

class TestPluginFactory : public IPluginFactory {
public:
    TResult queryInterface(const char* iid, void** obj) override {
        if (std::memcmp(iid, kIIDIPluginFactory, 16) == 0) { *obj = this; return kOk; }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }
    uint32_t addRef()  override { return 1; }
    uint32_t release() override { return 1; }

    TResult getFactoryInfo(PFactoryInfo* info) override {
        if (!info) return kResultFalse;
        std::memset(info, 0, sizeof(*info));
        std::strncpy(info->vendor, "ExTracker Tests", sizeof(info->vendor) - 1);
        return kOk;
    }

    int32_t countClasses() override { return 1; }

    TResult getClassInfo(int32_t index, PClassInfo* info) override {
        if (index != 0 || !info) return kResultFalse;
        std::memset(info, 0, sizeof(*info));
        std::memcpy(info->classId, kTestClassId, 16);
        info->cardinality = 0x7FFFFFFF;
        std::strncpy(info->category, "Audio Module Class", sizeof(info->category) - 1);
        std::strncpy(info->name,     "ExTrackerTestSynth", sizeof(info->name) - 1);
        return kOk;
    }

    TResult createInstance(const char* cid, const char* iid, void** obj) override {
        if (!cid || !iid || !obj) return kResultFalse;
        if (std::memcmp(cid, kTestClassId, 16) != 0) return kResultFalse;
        auto* p = new TestPlugin();
        if (std::memcmp(iid, kIIDIComponent, 16) == 0) {
            *obj = static_cast<IComponent*>(p); return kOk;
        }
        if (std::memcmp(iid, kIIDIAudioProcessor, 16) == 0) {
            *obj = static_cast<IAudioProcessor*>(p); return kOk;
        }
        delete p;
        return kNoInterface;
    }
};

static TestPluginFactory gFactory;

} // anonymous namespace

extern "C" {
    IPluginFactory* GetPluginFactory() { return &gFactory; }
}
