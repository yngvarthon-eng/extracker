#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <vector>

namespace extracker {

struct ReverbParams {
    float roomSize = 0.5f;  // 0..1
    float damping  = 0.5f;  // 0..1
    float wet      = 0.0f;  // 0..1 — 0 means bypassed
    float width    = 1.0f;  // 0..1 stereo width

    bool isActive() const { return wet > 0.0f; }
};

// ---------------------------------------------------------------------------
// Freeverb (Jezar at Dreampoint) — feedback comb + allpass network
// ---------------------------------------------------------------------------
namespace freeverb_detail {

class CombFilter {
public:
    void resize(std::size_t n) {
        buf_.assign(n, 0.0f);
        pos_   = 0;
        filt_  = 0.0f;
    }
    void setFeedback(float v) { feedback_ = v; }
    void setDamp(float v)     { d1_ = v; d2_ = 1.0f - v; }
    float process(float in) {
        const float out = buf_[pos_];
        filt_ = out * d2_ + filt_ * d1_;
        buf_[pos_] = in + filt_ * feedback_;
        if (++pos_ >= buf_.size()) pos_ = 0;
        return out;
    }
private:
    std::vector<float> buf_;
    std::size_t pos_ = 0;
    float filt_ = 0.0f, feedback_ = 0.5f, d1_ = 0.5f, d2_ = 0.5f;
};

class AllpassFilter {
public:
    void resize(std::size_t n) {
        buf_.assign(n, 0.0f);
        pos_ = 0;
    }
    float process(float in) {
        const float out = buf_[pos_];
        buf_[pos_] = in + out * 0.5f;
        if (++pos_ >= buf_.size()) pos_ = 0;
        return out - in;
    }
private:
    std::vector<float> buf_;
    std::size_t pos_ = 0;
};

} // namespace freeverb_detail

class FreeverbProcessor {
public:
    // Delay lengths in samples at 44100 Hz
    static constexpr int kCombL[8]    = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int kCombR[8]    = {1139, 1211, 1300, 1379, 1445, 1514, 1580, 1640};
    static constexpr int kAllpassL[4] = {556, 441, 341, 225};
    static constexpr int kAllpassR[4] = {579, 464, 364, 248};

    void reset() { lastSr_ = 0.0; }

    // in    — mono reverb send bus (already normalized)
    // outL/R — wet stereo output (caller adds to dry)
    void process(const std::vector<double>& in,
                 std::vector<double>& outL,
                 std::vector<double>& outR,
                 const ReverbParams& p,
                 double sr) {
        const std::size_t n = in.size();
        if (std::abs(sr - lastSr_) > 1.0) initBuffers(sr);
        updateParams(p);
        outL.resize(n);
        outR.resize(n);

        // Stereo wet mix coefficients
        const float w1 = p.wet * (p.width * 0.5f + 0.5f);
        const float w2 = p.wet * (0.5f   - p.width * 0.5f);

        constexpr float kGain = 0.015f;
        for (std::size_t i = 0; i < n; ++i) {
            const float s = static_cast<float>(in[i]) * kGain;
            float l = 0.0f, r = 0.0f;
            for (int c = 0; c < 8; ++c) {
                l += combL_[c].process(s);
                r += combR_[c].process(s);
            }
            for (int a = 0; a < 4; ++a) {
                l = allpassL_[a].process(l);
                r = allpassR_[a].process(r);
            }
            outL[i] = static_cast<double>(l * w1 + r * w2);
            outR[i] = static_cast<double>(r * w1 + l * w2);
        }
    }

private:
    freeverb_detail::CombFilter    combL_[8];
    freeverb_detail::CombFilter    combR_[8];
    freeverb_detail::AllpassFilter allpassL_[4];
    freeverb_detail::AllpassFilter allpassR_[4];
    ReverbParams lastParams_{-1.0f, -1.0f, -1.0f, -1.0f};
    double lastSr_ = 0.0;

    void initBuffers(double sr) {
        const double scale = sr / 44100.0;
        for (int i = 0; i < 8; ++i) {
            combL_[i].resize(static_cast<std::size_t>(kCombL[i] * scale + 0.5));
            combR_[i].resize(static_cast<std::size_t>(kCombR[i] * scale + 0.5));
        }
        for (int i = 0; i < 4; ++i) {
            allpassL_[i].resize(static_cast<std::size_t>(kAllpassL[i] * scale + 0.5));
            allpassR_[i].resize(static_cast<std::size_t>(kAllpassR[i] * scale + 0.5));
        }
        lastSr_  = sr;
        lastParams_ = {-1.0f, -1.0f, -1.0f, -1.0f};  // force updateParams
    }

    void updateParams(const ReverbParams& p) {
        if (p.roomSize == lastParams_.roomSize && p.damping == lastParams_.damping) return;
        const float feedback = p.roomSize * 0.28f + 0.7f;
        const float damp     = p.damping  * 0.4f;
        for (int i = 0; i < 8; ++i) {
            combL_[i].setFeedback(feedback); combL_[i].setDamp(damp);
            combR_[i].setFeedback(feedback); combR_[i].setDamp(damp);
        }
        lastParams_ = p;
    }
};

struct ReverbState {
    std::mutex mutex;
    ReverbParams params;
    FreeverbProcessor proc;
    std::array<float, 16> sends{};  // per-instrument send level 0..1
};

} // namespace extracker
