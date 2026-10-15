#pragma once
// The Vocoder's maths (builtin/devices/Vocoder.cpp), here so the engine tests and its editor can use them: where
// its bands are, the filters that make them, and the pitch tracker its Pitch Tracking carrier follows.
//
// Bands: `count` centres spaced evenly in log frequency from Low to High (one band: their geometric mean). Each
// band is as wide as the spacing between centres times BW (in octaves). Precise: every band two band-pass
// sections in a row at 0 dB, the same width and level all the way up. Retro: one section each (gentler slopes),
// narrower and louder the higher the band (as Ableton's). The carrier's bands are the modulator's moved by
// Formant (semitones), so the voice's formants come out higher or lower. Nothing goes above kTopFraction of the
// rate.
//
// The pitch tracker is YIN (de Cheveigné & Kawahara) on the input brought down to about kTrackerRate: a 25 ms
// window every 5 ms, the cumulative mean normalised difference's first dip under kYinThreshold between the
// periods of High and Low, refined by a parabola. Unvoiced frames keep the last pitch.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"

namespace sub::vocoder {

using dsp::Biquad;
using dsp::BiquadCoefficients;

inline constexpr int kMaxBands = 40;
inline constexpr int kSections = 2;
inline constexpr double kTopFraction = 0.45;
inline constexpr double kPreciseCascade = 0.6436;  // the -3 dB width of two identical sections over one's
inline constexpr double kRetroTiltDb = 9.0;        // Retro: the highest band over the lowest

// The Bands choice's counts (Ableton's: 4 to 40 in fours).
inline int bandCount(int choice) noexcept { return 4 * std::clamp(choice + 1, 1, kMaxBands / 4); }

struct Band {
    double centre = 1000.0;  // Hz (the modulator's)
    double octaves = 1.0;    // its width
    double gainDb = 0.0;     // its level (Retro tilts it)
};

struct Layout {
    int count = 0;
    bool retro = false;
    std::array<Band, kMaxBands> bands{};
};

inline Layout layout(int count, double low, double high, double bandwidth, bool retro) noexcept {
    Layout out;
    out.count = std::clamp(count, 1, kMaxBands);
    out.retro = retro;
    if (high < low) std::swap(low, high);
    low = std::max(low, 10.0);
    high = std::max(high, low);
    const double span = std::log2(high / low);
    const double spacing = out.count > 1 ? span / (out.count - 1) : std::max(span, 1.0);
    for (int k = 0; k < out.count; ++k) {
        const double at = out.count > 1 ? double(k) / (out.count - 1) : 0.5;
        Band& band = out.bands[static_cast<size_t>(k)];
        band.centre = low * std::exp2(span * at);
        band.octaves = std::clamp(spacing * bandwidth, 0.02, 4.0);
        if (retro) {
            band.octaves *= 1.4 - 0.8 * at;
            band.gainDb = kRetroTiltDb * at;
        }
    }
    return out;
}

// A band-pass section's Q for a width in octaves at `centre` Hz (the cookbook's BW form, prewarped: as wide high
// up as low down).
inline double qForOctaves(double octaves, double centre, double sampleRate) noexcept {
    const double w = dsp::kTwoPi * std::clamp(centre, 1.0, 0.49 * sampleRate) / sampleRate;
    return 1.0 / (2.0 * std::sinh(0.5 * std::log(2.0) * octaves * w / std::sin(w)));
}

// The sections of band `k`, its centre moved by `shiftSemitones` (the carrier's Formant): Precise two, Retro one
// (the second passes everything).
inline std::array<BiquadCoefficients, kSections> sections(const Layout& l, int k, double shiftSemitones,
                                                           double sampleRate) noexcept {
    const Band& band = l.bands[static_cast<size_t>(k)];
    const double centre = std::min(band.centre * std::exp2(shiftSemitones / 12.0), kTopFraction * sampleRate);
    std::array<BiquadCoefficients, kSections> s{};
    if (l.retro) {
        s[0] = BiquadCoefficients::bandpass(centre, qForOctaves(band.octaves, centre, sampleRate), sampleRate);
        s[1] = BiquadCoefficients{};
    } else {
        const double q = qForOctaves(band.octaves / kPreciseCascade, centre, sampleRate);
        s[0] = s[1] = BiquadCoefficients::bandpass(centre, q, sampleRate);
    }
    return s;
}

// A band's response at `f` Hz in dB, its level included (for the editor and the tests).
inline double bandResponseDb(const Layout& l, int k, double shiftSemitones, double f, double sampleRate) noexcept {
    const auto s = sections(l, k, shiftSemitones, sampleRate);
    return s[0].magnitudeDb(f, sampleRate) + s[1].magnitudeDb(f, sampleRate) +
           l.bands[static_cast<size_t>(k)].gainDb;
}

// --- The pitch tracker ----------------------------------------------------------------------------

inline constexpr double kTrackerRate = 12000.0;
inline constexpr double kWindowSeconds = 0.025;
inline constexpr double kHopSeconds = 0.005;
inline constexpr double kLowestPitch = 30.0;   // Hz: Low's least
inline constexpr double kHighestPitch = 2000.0;
inline constexpr double kYinThreshold = 0.2;
inline constexpr float kSilentPower = 1e-8f;    // a window quieter than this (mean square) is unvoiced

class PitchTracker {
public:
    // Not real-time: allocates.
    void prepare(double sampleRate) {
        factor_ = std::max(1, static_cast<int>(std::lround(sampleRate / kTrackerRate)));
        rate_ = sampleRate / factor_;
        window_ = std::max(16, static_cast<int>(std::lround(kWindowSeconds * rate_)));
        hop_ = std::max(1, static_cast<int>(std::lround(kHopSeconds * rate_)));
        maxTau_ = static_cast<int>(std::ceil(rate_ / kLowestPitch));
        size_ = window_ + maxTau_ + 2;
        ring_.assign(static_cast<size_t>(size_), 0.f);
        diff_.assign(static_cast<size_t>(maxTau_ + 2), 0.f);
        const auto lp = BiquadCoefficients::lowpass(std::min(0.4 * rate_, 0.45 * sampleRate), 0.7071, sampleRate);
        antiAlias_ = lp;
        reset();
    }

    void reset() noexcept {
        std::fill(ring_.begin(), ring_.end(), 0.f);
        at_ = 0;
        phase_ = 0;
        count_ = 0;
        aa_[0].reset();
        aa_[1].reset();
        pitch_ = 0.0;
        voiced_ = false;
    }

    // The range searched (Hz).
    void setRange(double low, double high) noexcept {
        if (high < low) std::swap(low, high);
        low_ = std::clamp(low, kLowestPitch, kHighestPitch);
        high_ = std::clamp(high, low_, kHighestPitch);
    }

    // Real-time: the next input sample. True when a frame was analysed with it.
    bool push(float x) noexcept {
        const float filtered = aa_[1].process(antiAlias_, aa_[0].process(antiAlias_, x));
        if (++phase_ < factor_) return false;
        phase_ = 0;
        ring_[static_cast<size_t>(at_)] = filtered;
        at_ = at_ + 1 == size_ ? 0 : at_ + 1;
        if (++count_ < hop_) return false;
        count_ = 0;
        analyse();
        return true;
    }

    // The last pitch found (Hz; 0 before any), and whether the last frame was voiced.
    double pitch() const noexcept { return pitch_; }
    bool voiced() const noexcept { return voiced_; }
    double rate() const noexcept { return rate_; }

private:
    float sample(int fromOldest) const noexcept {  // 0: the oldest kept
        int i = at_ + fromOldest;
        if (i >= size_) i -= size_;
        return ring_[static_cast<size_t>(i)];
    }

    void analyse() noexcept {
        const int tauMax = std::min(maxTau_, static_cast<int>(std::ceil(rate_ / low_)));
        const int tauMin = std::max(2, static_cast<int>(std::floor(rate_ / high_)));
        const int start = size_ - (window_ + tauMax);  // the frame's first sample, from the oldest
        double power = 0.0;
        for (int j = 0; j < window_; ++j) {
            const double v = sample(start + j);
            power += v * v;
        }
        if (power / window_ < kSilentPower) {
            voiced_ = false;
            return;
        }
        double running = 0.0;
        diff_[0] = 1.f;
        for (int tau = 1; tau <= tauMax; ++tau) {
            double d = 0.0;
            for (int j = 0; j < window_; ++j) {
                const double e = static_cast<double>(sample(start + j)) - sample(start + j + tau);
                d += e * e;
            }
            running += d;
            diff_[static_cast<size_t>(tau)] = running > 0.0 ? static_cast<float>(d * tau / running) : 1.f;
        }
        int found = -1;
        for (int tau = tauMin; tau < tauMax; ++tau) {
            if (diff_[static_cast<size_t>(tau)] < kYinThreshold) {
                while (tau + 1 < tauMax && diff_[static_cast<size_t>(tau + 1)] < diff_[static_cast<size_t>(tau)]) ++tau;
                found = tau;
                break;
            }
        }
        if (found < 0) {
            voiced_ = false;
            return;
        }
        double tau = found;
        if (found > 1 && found < tauMax) {
            const double a = diff_[static_cast<size_t>(found - 1)], b = diff_[static_cast<size_t>(found)],
                         c = diff_[static_cast<size_t>(found + 1)];
            const double denominator = a - 2.0 * b + c;
            if (denominator > 1e-12) tau += std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5);
        }
        pitch_ = rate_ / tau;
        voiced_ = true;
    }

    int factor_ = 1, window_ = 300, hop_ = 60, maxTau_ = 400, size_ = 0;
    double rate_ = kTrackerRate, low_ = 50.0, high_ = 1000.0;
    std::vector<float> ring_, diff_;
    int at_ = 0, phase_ = 0, count_ = 0;
    BiquadCoefficients antiAlias_;
    std::array<Biquad, 2> aa_;
    double pitch_ = 0.0;
    bool voiced_ = false;
};

}  // namespace sub::vocoder
