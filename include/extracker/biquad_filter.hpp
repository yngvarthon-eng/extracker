#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace extracker {

enum class BiquadType : std::uint8_t {
    Off      = 0,
    LowPass  = 1,
    HighPass = 2,
    BandPass = 3,
    Notch    = 4,
};

struct BiquadParams {
    BiquadType type         = BiquadType::Off;
    float      cutoffNorm   = 1.0f;   // 0..1 → 20 Hz..20 kHz (exponential)
    float      resonanceNorm = 0.0f;  // 0..1 → Q 0.5..20

    bool isActive() const noexcept { return type != BiquadType::Off; }
};

struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

inline BiquadCoeffs computeBiquadCoeffs(const BiquadParams& p, double sampleRate) noexcept {
    if (!p.isActive() || sampleRate <= 0.0) return {};

    const double fc = 20.0 * std::pow(1000.0, static_cast<double>(p.cutoffNorm));
    const double clampedFc = std::max(20.0, std::min(fc, sampleRate * 0.499));
    const double Q  = 0.5 + 19.5 * static_cast<double>(p.resonanceNorm);

    const double omega = 2.0 * M_PI * clampedFc / sampleRate;
    const double sinO  = std::sin(omega);
    const double cosO  = std::cos(omega);
    const double alpha = sinO / (2.0 * Q);

    BiquadCoeffs c;
    switch (p.type) {
        case BiquadType::LowPass:
            c.b0 = (1.0 - cosO) * 0.5;
            c.b1 =  1.0 - cosO;
            c.b2 = (1.0 - cosO) * 0.5;
            break;
        case BiquadType::HighPass:
            c.b0 =  (1.0 + cosO) * 0.5;
            c.b1 = -(1.0 + cosO);
            c.b2 =  (1.0 + cosO) * 0.5;
            break;
        case BiquadType::BandPass:
            c.b0 =  sinO * 0.5;
            c.b1 =  0.0;
            c.b2 = -sinO * 0.5;
            break;
        case BiquadType::Notch:
            c.b0 =  1.0;
            c.b1 = -2.0 * cosO;
            c.b2 =  1.0;
            break;
        default:
            return {};
    }

    const double a0    = 1.0 + alpha;
    const double inv   = 1.0 / a0;
    c.b0 *= inv; c.b1 *= inv; c.b2 *= inv;
    c.a1 = (-2.0 * cosO) * inv;
    c.a2 = (1.0 - alpha)  * inv;
    return c;
}

struct BiquadState {
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    double process(double in, const BiquadCoeffs& c) noexcept {
        const double out = c.b0 * in + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return out;
    }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
};

// Full filter: params + cached coefficients + state — one per instrument in PluginHost
class BiquadFilter {
public:
    BiquadParams params;

    void setParams(const BiquadParams& p) noexcept { params = p; dirty_ = true; }
    bool isActive() const noexcept { return params.isActive(); }
    void reset()    noexcept { state_.reset(); }

    void apply(std::vector<double>& buf, double sampleRate) noexcept {
        if (!params.isActive() || buf.empty()) return;
        ensureCoeffs(sampleRate);
        for (auto& s : buf) s = state_.process(s, coeffs_);
    }

    const BiquadCoeffs& coeffs(double sampleRate) noexcept {
        ensureCoeffs(sampleRate);
        return coeffs_;
    }

private:
    BiquadCoeffs coeffs_;
    BiquadState  state_;
    bool         dirty_    = true;
    double       cachedSr_ = 0.0;

    void ensureCoeffs(double sr) noexcept {
        if (dirty_ || sr != cachedSr_) {
            coeffs_   = computeBiquadCoeffs(params, sr);
            cachedSr_ = sr;
            dirty_    = false;
        }
    }
};

}  // namespace extracker
