#pragma once
// Over The Top's squash: multiband upward + downward compression, the sound of
// every drop since 2014. Three bands split by Linkwitz-Riley crossovers; each
// band is squashed hard from above and pulled up hard from below, then made up.
// The depth scales every band's gain in dB, as Live's Amount scales its OTT
// preset's: 50 % is half the dB, not half the processed signal. Shared by the
// Over The Top device and Snarl's Squash.
//
// The numbers are fitted to renders of Live's OTT preset (drums, a growl, a
// piano at Amount 25, 50 and 100 %): Live's ratios and nearly its thresholds,
// but far quicker detectors than its panel's times suggest. Its exports were
// peak-normalized, which hides the overall level, so the make-ups average the
// preset's band outputs (10.3, 5.7, 10.3 dB).
//
// At depth 0 every band's gain is 0 dB: the bands summed unprocessed (an
// allpass of the input), so in-between settings don't comb-filter against the
// crossover's phase shift.

#include <algorithm>
#include <array>
#include <cmath>

#include "builtin/Dsp.h"
#include "builtin/MultibandDesign.h"
#include "rt/RtUtils.h"

namespace sub::ott {

class Squash {
public:
    static constexpr int kBands = 3;
    static constexpr int kChannels = 2;
    static constexpr float kLowCrossover = 88.3f;  // Hz
    static constexpr float kHighCrossover = 2500.f;

    void prepare(double sampleRate) noexcept {
        const dsp::SvfCoefficients low = crossover(kLowCrossover, sampleRate);
        const dsp::SvfCoefficients high = crossover(kHighCrossover, sampleRate);
        for (auto& ch : channels_) {
            ch.lowSplit.coeffs = ch.lowLp.coeffs = ch.lowHp.coeffs = low;
            ch.highSplit.coeffs = ch.highLp.coeffs = ch.highHp.coeffs = ch.lowAllpass.coeffs = high;
        }
        for (int b = 0; b < kBands; ++b) {
            attack_[b] = onePole(kBandSettings[b].attackMs, sampleRate);
            release_[b] = onePole(kBandSettings[b].releaseMs, sampleRate);
        }
        reset();
    }

    void reset() noexcept {
        for (auto& ch : channels_) ch.clear();
        env_.fill(0.f);
        mix_.fill(1.f);
    }

    // One sample of `n` channels (1 or 2) in place, at `depth` (0..1). The
    // channels are split into bands, detected on the loudest (linked stereo).
    // Without `regain` the bands keep the gains of the last sample that had it
    // (the detectors still follow every sample): an oversampled caller works
    // them out once a base sample, what the ear can follow.
    void tick(float* x, int n, float depth, bool regain = true) noexcept {
        std::array<std::array<float, kBands>, kChannels> bands{};
        std::array<float, kBands> peak{};
        for (int c = 0; c < n; ++c) {
            Channel& s = channels_[c];
            float lp, hp;
            s.lowSplit.split(x[c], lp, hp);
            const float low = s.lowAllpass.allpass(s.lowLp.lowpass(lp));
            const float rest = s.lowHp.highpass(hp);
            s.highSplit.split(rest, lp, hp);
            bands[c] = {low, s.highLp.lowpass(lp), s.highHp.highpass(hp)};
            for (int b = 0; b < kBands; ++b) peak[b] = std::max(peak[b], std::abs(bands[c][b]));
        }
        for (int b = 0; b < kBands; ++b) {
            const float coeff = peak[b] > env_[b] ? attack_[b] : release_[b];
            env_[b] = peak[b] + coeff * (env_[b] - peak[b]);
            if (regain) mix_[b] = expDbToGain(depth * bandGainDb(b, env_[b]));
        }
        for (int c = 0; c < n; ++c) {
            float sum = 0.f;
            for (int b = 0; b < kBands; ++b) sum += bands[c][b] * mix_[b];
            x[c] = sum;
        }
    }

private:
    static constexpr float kDownRatio = 66.7f;  // Live's Above: effectively a limiter
    static constexpr float kUpRatio = 4.17f;    // and its Below, 1:4.17 upwards

    // Fitted to Live's renders (see the top). The attacks catch a transient
    // within a millisecond, so it isn't let through with the quiet sound's lift.
    struct BandSettings {
        float downThresholdDb, upThresholdDb, makeupDb, attackMs, releaseMs;
    };
    static constexpr std::array<BandSettings, kBands> kBandSettings = {{
        {-36.1f, -42.0f, 12.7f, 0.77f, 84.f},  // low
        {-29.1f, -41.8f, 5.4f, 0.66f, 59.f},   // mid (the renders hold little this quiet: Live's Below)
        {-33.4f, -36.1f, 8.3f, 0.55f, 25.f},   // high
    }};

    // A Butterworth (k = sqrt 2) state-variable filter section at a crossover.
    // Two in a row make a Linkwitz-Riley crossover half.
    static dsp::SvfCoefficients crossover(float cutoff, double sampleRate) {
        const float g = static_cast<float>(std::tan(3.14159265358979 * std::min<double>(cutoff, 0.49 * sampleRate) / sampleRate));
        return {g, 1.41421356f};
    }
    struct Svf {
        dsp::SvfCoefficients coeffs;
        dsp::Svf state;
        void split(float x, float& lp, float& hp) noexcept {
            const dsp::Svf::Outputs o = state.tick(coeffs, x);
            lp = o.low;
            hp = x - coeffs.k * o.band - o.low;
        }
        float lowpass(float x) noexcept { return state.tick(coeffs, x).low; }
        float highpass(float x) noexcept {
            const dsp::Svf::Outputs o = state.tick(coeffs, x);
            return x - coeffs.k * o.band - o.low;
        }
        // What an LR4 low + high pass at this frequency sum to, so the low band
        // gets the same phase shift from the upper crossover as the others.
        float allpass(float x) noexcept { return x - 2.f * coeffs.k * state.tick(coeffs, x).band; }
        void clear() noexcept { state.reset(); }
    };
    struct Channel {
        Svf lowSplit, lowLp, lowHp, highSplit, highLp, highHp, lowAllpass;
        void clear() noexcept {
            for (Svf* f : {&lowSplit, &lowLp, &lowHp, &highSplit, &highLp, &highHp, &lowAllpass}) f->clear();
        }
    };

    static float onePole(float ms, double sampleRate) {
        return static_cast<float>(onePoleCoefficient(ms * 0.001, sampleRate));
    }

    // The band's gain (dB) for its envelope: down above one threshold, up below
    // the other (Multiband Dynamics' law: at most 30 dB up, faded out towards
    // silence), then the make-up.
    static float bandGainDb(int band, float envelope) noexcept {
        const BandSettings& s = kBandSettings[band];
        const float level = gainToDb(envelope);
        return multiband::aboveGainDb(level, s.downThresholdDb, multiband::slope(kDownRatio), 0.f) +
               multiband::belowGainDb(level, s.upThresholdDb, multiband::slope(kUpRatio), 0.f) + s.makeupDb;
    }

    std::array<Channel, kChannels> channels_{};
    std::array<float, kBands> env_{}, attack_{}, release_{};
    std::array<float, kBands> mix_{1.f, 1.f, 1.f};  // each band's gain
};

}  // namespace sub::ott
