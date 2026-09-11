#pragma once

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

namespace extracker {

// ── Distortion ───────────────────────────────────────────────────────────────

enum class DistortionType : std::uint8_t { Off=0, Soft=1, Hard=2, Fuzz=3 };

struct DistortionParams {
    DistortionType type  = DistortionType::Off;
    float          drive = 0.5f;   // 0..1  → internal gain 1..40x
    float          mix   = 1.0f;   // 0..1 dry/wet
    bool isActive() const { return type != DistortionType::Off; }
};

inline void applyDistortion(std::vector<double>& buf, const DistortionParams& p) {
    if (!p.isActive()) return;
    const double gain = 1.0 + static_cast<double>(p.drive) * 39.0;
    const double wet  = static_cast<double>(p.mix);
    const double dry  = 1.0 - wet;
    for (auto& s : buf) {
        double w;
        switch (p.type) {
            case DistortionType::Hard:
                w = std::clamp(s * gain, -1.0, 1.0);
                break;
            case DistortionType::Fuzz:
                w = s >= 0.0 ? std::min(s * gain, 1.0) : std::max(s * gain * 0.7, -0.7);
                break;
            case DistortionType::Soft:
            default: {
                const double tg = std::tanh(gain);
                w = tg > 0.0 ? std::tanh(s * gain) / tg : 0.0;
                break;
            }
        }
        s = dry * s + wet * w;
    }
}

// ── Delay ────────────────────────────────────────────────────────────────────

struct DelayParams {
    float timeMs   = 375.0f;  // 1..2000 ms
    float feedback = 0.4f;    // 0..0.98
    float wet      = 0.0f;    // 0..1  (0 = off)
    bool isActive() const { return wet > 0.001f; }
};

struct DelayState {
    std::vector<double> buffer;
    std::size_t         writePos = 0;
    double              lastSr   = 0.0;

    void reset() { buffer.clear(); writePos = 0; lastSr = 0.0; }
};

inline void applyDelay(std::vector<double>& buf, const DelayParams& p,
                        DelayState& st, double sr) {
    if (!p.isActive()) return;
    if (st.lastSr != sr || st.buffer.empty()) {
        const std::size_t cap = static_cast<std::size_t>(sr * 2.05);
        st.buffer.assign(cap, 0.0);
        st.writePos = 0;
        st.lastSr   = sr;
    }
    const std::size_t bufSz = st.buffer.size();
    const std::size_t delaySamples = static_cast<std::size_t>(
        std::clamp(static_cast<double>(p.timeMs) / 1000.0 * sr, 1.0,
                   static_cast<double>(bufSz - 1)));
    const double fb  = static_cast<double>(p.feedback);
    const double wet = static_cast<double>(p.wet);
    const double dry = 1.0 - wet;
    for (auto& s : buf) {
        const std::size_t rp   = (st.writePos + bufSz - delaySamples) % bufSz;
        const double      echo = st.buffer[rp];
        st.buffer[st.writePos] = s + echo * fb;
        st.writePos = (st.writePos + 1) % bufSz;
        s = dry * s + wet * echo;
    }
}

// ── Chorus ───────────────────────────────────────────────────────────────────

struct ChorusParams {
    float rate  = 0.5f;   // Hz  0.05..5.0
    float depth = 0.5f;   // 0..1  → 0..20 ms modulation
    float wet   = 0.0f;   // 0..1  (0 = off)
    bool isActive() const { return wet > 0.001f; }
};

struct ChorusState {
    std::vector<double> buffer;
    std::size_t         writePos = 0;
    double              lfoPhase = 0.0;
    double              lastSr   = 0.0;

    void reset() { buffer.clear(); writePos = 0; lfoPhase = 0.0; lastSr = 0.0; }
};

inline void applyChorus(std::vector<double>& buf, const ChorusParams& p,
                         ChorusState& st, double sr) {
    if (!p.isActive()) return;
    if (st.lastSr != sr || st.buffer.empty()) {
        const std::size_t cap = std::max(static_cast<std::size_t>(sr * 0.035), std::size_t{4});
        st.buffer.assign(cap, 0.0);
        st.writePos = 0;
        st.lfoPhase = 0.0;
        st.lastSr   = sr;
    }
    const std::size_t bufSz     = st.buffer.size();
    const double      phaseInc  = 2.0 * M_PI * static_cast<double>(p.rate) / sr;
    const double      maxDepth  = static_cast<double>(p.depth) * 0.020 * sr; // samples
    const double      wet       = static_cast<double>(p.wet);
    const double      dry       = 1.0 - wet;
    for (auto& s : buf) {
        const double lfo = std::sin(st.lfoPhase);
        st.lfoPhase += phaseInc;
        if (st.lfoPhase >= 2.0 * M_PI) st.lfoPhase -= 2.0 * M_PI;

        const double delay = (1.0 + lfo) * 0.5 * maxDepth + 1.0;
        const auto   d0    = static_cast<std::size_t>(delay);
        const double frac  = delay - static_cast<double>(d0);
        const std::size_t r0 = (st.writePos + bufSz - std::min(d0,     bufSz - 1)) % bufSz;
        const std::size_t r1 = (st.writePos + bufSz - std::min(d0 + 1, bufSz - 1)) % bufSz;
        const double chorus = st.buffer[r0] * (1.0 - frac) + st.buffer[r1] * frac;

        st.buffer[st.writePos] = s;
        st.writePos = (st.writePos + 1) % bufSz;
        s = dry * s + wet * chorus;
    }
}

// ── Combined per-instrument effect params & state ────────────────────────────

struct InstrumentEffectParams {
    DistortionParams distortion;
    DelayParams      delay;
    ChorusParams     chorus;
    bool isActive() const {
        return distortion.isActive() || delay.isActive() || chorus.isActive();
    }
};

struct InstrumentEffectState {
    DelayState  delay;
    ChorusState chorus;
    void reset() { delay.reset(); chorus.reset(); }
};

inline void applyInstrumentEffects(std::vector<double>& buf,
                                    const InstrumentEffectParams& p,
                                    InstrumentEffectState& st,
                                    double sr) {
    if (p.distortion.isActive()) applyDistortion(buf, p.distortion);
    if (p.delay.isActive())      applyDelay(buf,      p.delay,   st.delay,  sr);
    if (p.chorus.isActive())     applyChorus(buf,     p.chorus,  st.chorus, sr);
}

}  // namespace extracker
