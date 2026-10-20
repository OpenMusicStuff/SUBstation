#pragma once
// What each echo of the built-in delays goes through, modelled on Valhalla
// Delay's default preset (Era Past, Age 50 %, the plug-in's own modulation,
// wow and flutter), measured from renders of it: Digital (the Delay's sound)
// and Tape (the Vintage Delay's). Each echo passes once, so the repeats pile
// it up as the plug-in's do.
//
// Digital, an early digital delay:
//   - the delay time swings ±2.67 ms on a 0.5 Hz triangle, rounded by a 2 Hz
//     one-pole (starting at its middle, rising);
//   - high-passes at 5, 10 and 10 Hz (one-poles), then a band-limit to about
//     7.8 kHz with a slight droop above 3 kHz: an impulse response measured
//     per sample rate (DelayCharacterTables.h), about 10 samples late;
//   - a compressor on a side's echoes together (the plug-in's one line holds
//     every repeat at once): its level is their RMS through a high shelf (+12.5 dB above 3.75 kHz), 2 ms up and 0.94 ms down, and the
//     gain (1 + (level / 0.85)^1.97)^(-1 / 1.97): unity for quiet echoes, loud
//     ones (treble sooner) held down, each repeat again;
//   - what goes into the line is rounded to 16 bits.
// Tape, a tape echo whose speed follows the time:
//   - the tape's speed: k = time / 200 ms, kept within 1..3 (slower tape for
//     longer times, down to a third). The playback EQ is a curve scaled down
//     in frequency by k (a low shelf, a broad bump, the treble's roll-off)
//     after a fixed 10 Hz high-pass and a fixed cut-off just under 24 kHz,
//     4.86 samples late;
//   - wow and flutter: smooth random wander (cubic B-splines through random
//     values): a drift (a new value every 8.3 s, 2.6 ms RMS), wow (5 Hz,
//     0.32 ms RMS at full speed) and flutter (33 Hz, 0.064 ms); the latter two
//     deeper on slower tape;
//   - the splice: once a pass of the 10 s (× k) loop, the time bumps up by
//     1.08 ms × k and settles (0.19 s and 0.125 s exponentials: an impulse of
//     7.32 ms × k into both, their difference peaking at 0.148 of it), the echo
//     dipping a little with it;
//   - a fast compressor (its level the RMS of the echoes through a treble
//     shelf, +20 dB above 10 kHz) and a tanh well off centre: tape's soft
//     saturation, treble first, with even harmonics;
//   - the plug-in's treble also steps with the time (its tape buffer's): a
//     shelf above 9 kHz, from the times measured;
//   - the feedback path: a 100 Hz one-pole high-pass and +2 dB at 8.5 kHz, so
//     each repeat thins out at the bottom.
// The modulation is shared by both sides (the plug-in's is mono), and all of
// it starts from the same state on reset(), so renders repeat exactly.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "builtin/DelayCharacterTables.h"
#include "builtin/DspBlocks.h"
#include "rt/FastMath.h"

namespace sub::delay_character {

enum class Character { Digital, Tape };

// --- Filters from analog prototypes -----------------------------------------------------------

// A second-order section from its analog prototype (bilinear, prewarped at f0;
// f0 kept under 0.4999 of the rate). Low shelf and peak take a linear gain g
// (the shelf's at DC), with A = sqrt(g).
enum class Section { LowPass, HighPass, LowShelf, Peak };

inline dsp::BiquadCoefficients analogSection(Section type, double f0, double q, double g, double sr) noexcept {
    f0 = std::min(f0, 0.4999 * sr);
    const double k = std::tan(std::numbers::pi * f0 / sr);
    // H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0), s normalised to f0.
    double n2 = 0, n1 = 0, n0 = 1, d2 = 1, d1 = 1 / q, d0 = 1;
    switch (type) {
    case Section::LowPass: break;
    case Section::HighPass: n2 = 1, n0 = 0; break;
    case Section::LowShelf: {
        const double a = std::sqrt(g), ra = std::sqrt(a);
        n2 = a * a, n1 = a * ra / q, n0 = a, d1 = ra / q, d0 = a;
        break;
    }
    case Section::Peak: {
        const double a = std::sqrt(g);
        n2 = 1, n1 = a / q, n0 = 1, d1 = 1 / (a * q);
        break;
    }
    }
    const auto z = [k](double c2, double c1, double c0) {
        return std::array<double, 3>{c2 + c1 * k + c0 * k * k, -2 * c2 + 2 * c0 * k * k, c2 - c1 * k + c0 * k * k};
    };
    const auto b = z(n2, n1, n0), a = z(d2, d1, d0);
    return {b[0] / a[0], b[1] / a[0], b[2] / a[0], a[1] / a[0], a[2] / a[0]};
}

// A first-order high shelf, (1 + gain s/w) / (1 + s/w) (bilinear, prewarped): the
// compressors' level weighting.
struct ShelfCoefficients {
    float b0 = 1.f, b1 = 0.f, a1 = 0.f;
    ShelfCoefficients() = default;
    ShelfCoefficients(double f, double gain, double sr) noexcept {
        const double k = std::tan(std::numbers::pi * std::min(f, 0.49 * sr) / sr);
        b0 = static_cast<float>((k + gain) / (k + 1));
        b1 = static_cast<float>((k - gain) / (k + 1));
        a1 = static_cast<float>((k - 1) / (k + 1));
    }
};
struct Shelf {
    float x1 = 0.f, y1 = 0.f;
    void reset() noexcept { x1 = y1 = 0.f; }
    float process(const ShelfCoefficients& c, float x) noexcept {
        const float y = c.b0 * x + c.b1 * x1 - c.a1 * y1;
        x1 = x;
        y1 = dsp::flushTiny(y);
        return y;
    }
};

// A cubic B-spline through random values in -1..1, a new one every `1 / rate`
// seconds: smooth random wander, 0.399 RMS (kSplineRms).
class RandomSpline {
public:
    static constexpr float kSplineRms = 0.399f;

    void reset(uint32_t seed) noexcept {
        noise_.seed(seed);
        for (float& v : values_) v = noise_.next();
        phase_ = 0.0;
    }
    float next(double step) noexcept {
        const auto t = static_cast<float>(phase_);
        const float t2 = t * t, t3 = t2 * t, u = 1.f - t;
        const float out = (u * u * u * values_[0] + (3.f * t3 - 6.f * t2 + 4.f) * values_[1] +
                           (-3.f * t3 + 3.f * t2 + 3.f * t + 1.f) * values_[2] + t3 * values_[3]) /
                          6.f;
        phase_ += step;
        if (phase_ >= 1.0) {
            phase_ -= std::floor(phase_);
            values_ = {values_[1], values_[2], values_[3], noise_.next()};
        }
        return out;
    }

private:
    dsp::Noise noise_;
    std::array<float, 4> values_{};
    double phase_ = 0.0;
};

// --- Digital --------------------------------------------------------------------------------

// The modulation, one for the device: the delay's offset in samples.
class DigitalModulation {
public:
    static constexpr double kDepthMs = 2.6667, kRateHz = 0.5, kSmoothHz = 2.0, kStartPhase = 0.2496;

    void prepare(double sr) noexcept {
        depth_ = kDepthMs * 0.001 * sr;
        step_ = kRateHz / sr;
        smooth_ = std::exp(-2.0 * std::numbers::pi * kSmoothHz / sr);
        reset();
    }
    void reset() noexcept {
        phase_ = kStartPhase;
        smoothed_ = triangle(phase_);
    }
    float next() noexcept {
        smoothed_ = triangle(phase_) + smooth_ * (smoothed_ - triangle(phase_));
        phase_ += step_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        return static_cast<float>(depth_ * smoothed_);
    }

private:
    static double triangle(double p) noexcept { return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p; }

    double depth_ = 0.0, step_ = 0.0, smooth_ = 0.0, phase_ = 0.0, smoothed_ = 0.0;
};

// Shared by every echo of a device: the band-limit's taps at this rate, the filters' coefficients.
struct DigitalDesign {
    static constexpr double kHighPassHz[3] = {4.987, 10.006, 10.006};
    static constexpr double kShelfHz = 3753.0, kShelfGain = 4.229;
    static constexpr double kAttackMs = 2.018, kReleaseMs = 0.938;
    static constexpr float kThreshold = 0.8472f, kPower = 1.973f;

    std::vector<float> taps;  // the band-limit
    float highPass[3] = {};   // one-poles' coefficients
    ShelfCoefficients shelf;
    float attack = 0.f, release = 0.f;

    // Allocates.
    void prepare(double sr) {
        taps = bandLimit(sr);
        for (int i = 0; i < 3; ++i) highPass[i] = static_cast<float>(std::exp(-2.0 * std::numbers::pi * kHighPassHz[i] / sr));
        shelf = ShelfCoefficients(kShelfHz, kShelfGain, sr);
        attack = static_cast<float>(std::exp(-1.0 / (kAttackMs * 0.001 * sr)));
        release = static_cast<float>(std::exp(-1.0 / (kReleaseMs * 0.001 * sr)));
    }

    // The measured response nearest this rate; resampled (windowed sinc) when the rate is none of them.
    static std::vector<float> bandLimit(double sr) {
        struct Table {
            double rate;
            const float* taps;
            size_t size;
        };
        const Table tables[] = {
            {44100.0, tables::kDigitalBandLimit44100.data(), tables::kDigitalBandLimit44100.size()},
            {48000.0, tables::kDigitalBandLimit48000.data(), tables::kDigitalBandLimit48000.size()},
            {88200.0, tables::kDigitalBandLimit88200.data(), tables::kDigitalBandLimit88200.size()},
            {96000.0, tables::kDigitalBandLimit96000.data(), tables::kDigitalBandLimit96000.size()},
        };
        const Table* best = &tables[0];
        for (const Table& t : tables)
            if (std::abs(std::log(t.rate / sr)) < std::abs(std::log(best->rate / sr))) best = &t;
        if (std::abs(best->rate - sr) < 1.0) return {best->taps, best->taps + best->size};
        // The response is the same in time: sample it at the new rate (it holds nothing near either Nyquist).
        const double ratio = best->rate / sr;  // source samples per output sample
        const auto size = static_cast<size_t>(std::ceil(static_cast<double>(best->size) / ratio));
        std::vector<float> out(size, 0.f);
        constexpr int kHalf = 16;
        for (size_t n = 0; n < size; ++n) {
            const double at = static_cast<double>(n) * ratio;
            double sum = 0.0;
            for (auto i = static_cast<int64_t>(std::floor(at)) - kHalf; i <= static_cast<int64_t>(at) + kHalf; ++i) {
                if (i < 0 || i >= static_cast<int64_t>(best->size)) continue;
                const double x = at - static_cast<double>(i);
                const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                const double window = 0.5 + 0.5 * std::cos(std::numbers::pi * x / (kHalf + 1));
                sum += best->taps[i] * sinc * window;
            }
            out[n] = static_cast<float>(sum * ratio);  // a tap a source sample's share of the time
        }
        return out;
    }
};

// One echo's linear part (a stage of the delay's loop): the high-passes and the band-limit.
class DigitalEcho {
public:
    // Allocates.
    void prepare(const DigitalDesign& design) {
        history_.assign(2 * design.taps.size(), 0.f);
        reset();
    }
    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.f);
        pos_ = 0;
        for (auto& h : hp_) h = {};
    }

    float process(const DigitalDesign& d, float x) noexcept {
        for (int i = 0; i < 3; ++i) {  // y = a (y + x - x1)
            HighPass& h = hp_[i];
            h.y = dsp::flushTiny(d.highPass[i] * (h.y + x - h.x));
            h.x = x;
            x = h.y;
        }
        // The band-limit: the history is kept twice over, so the taps read one straight run.
        const size_t n = d.taps.size();
        history_[pos_] = history_[pos_ + n] = x;
        const float* recent = &history_[pos_ + n];
        float y = 0.f;
        for (size_t k = 0; k < n; ++k) y += d.taps[k] * recent[-static_cast<ptrdiff_t>(k)];
        pos_ = pos_ + 1 == n ? 0 : pos_ + 1;
        return y;
    }

    // What goes into the line: 16 bits (rounded to even by pushing the fraction out of a float's mantissa).
    static float quantize(float x) noexcept {
        const float steps = std::clamp(x, -127.f, 127.f) * 32768.f;
        return ((steps + 12582912.f) - 12582912.f) * (1.f / 32768.f);
    }

private:
    struct HighPass {
        float x = 0.f, y = 0.f;
    };
    std::vector<float> history_;
    size_t pos_ = 0;
    HighPass hp_[3];
};

// The compressor, one a side: it hears the side's echoes together (the
// plug-in's one line holds every repeat at once), and its gain goes on each.
class DigitalDynamics {
public:
    void reset() noexcept {
        shelf_.reset();
        level_ = 0.f;
    }
    // The gain for the side's echoes, `sum` of them now.
    float gain(const DigitalDesign& d, float sum) noexcept {
        const float w = shelf_.process(d.shelf, sum);
        const float square = w * w;
        level_ = square + (square > level_ ? d.attack : d.release) * (level_ - square);
        if (level_ < 1e-12f) return 1.f;
        // (1 + (level / threshold)^p)^(-1/p)
        const float ratio = level_ * (1.f / (DigitalDesign::kThreshold * DigitalDesign::kThreshold));
        const float u = fastmath::exp2(0.5f * DigitalDesign::kPower * fastmath::log2(ratio));
        return fastmath::exp2(-fastmath::log2(1.f + u) / DigitalDesign::kPower);
    }

private:
    Shelf shelf_;
    float level_ = 0.f;
};

// --- Tape -----------------------------------------------------------------------------------

// The tape's speed for a delay time: k, 1 (up to 200 ms) to 3 (600 ms and up).
inline float tapeSlowdown(double seconds) noexcept { return static_cast<float>(std::clamp(seconds / 0.2, 1.0, 3.0)); }

// The modulation, one for the device: how far the tape has wandered, in ms, at a slowdown k.
class TapeModulation {
public:
    static constexpr double kDriftHz = 0.12, kDriftMs = 2.6;
    static constexpr double kWowHz = 5.0, kWowMs = 0.318, kWowSlope = 0.86;
    static constexpr double kFlutterHz = 33.0, kFlutterMs = 0.0635, kFlutterSlope = 0.5;
    static constexpr double kLoopSeconds = 10.0, kFirstSplice = 9.3;  // at full speed
    static constexpr double kSpliceMs = 7.32, kSpliceSlow = 0.1875, kSpliceFast = 0.125;  // the impulse's (ms at k = 1)
    static constexpr float kSpliceDip = 0.12f;  // the echo's level at the bump's height: 1 - this

    void prepare(double sr) noexcept {
        sr_ = sr;
        driftStep_ = kDriftHz / sr;
        wowStep_ = kWowHz / sr;
        flutterStep_ = kFlutterHz / sr;
        slow_ = std::exp(-1.0 / (kSpliceSlow * sr));
        fast_ = std::exp(-1.0 / (kSpliceFast * sr));
        // The bump's height for an amplitude of 1 (the two exponentials' difference at its peak).
        const double peak = std::log(kSpliceSlow / kSpliceFast) / (1.0 / kSpliceFast - 1.0 / kSpliceSlow);
        bumpPeak_ = static_cast<float>(std::exp(-peak / kSpliceSlow) - std::exp(-peak / kSpliceFast));
        reset();
    }
    void reset() noexcept {
        drift_.reset(0x5eed1u);
        wow_.reset(0x5eed2u);
        flutter_.reset(0x5eed3u);
        loop_ = 1.0 - kFirstSplice / kLoopSeconds;
        spliceSlow_ = spliceFast_ = 0.0;
    }

    struct Wander {
        float driftMs, wowUnit, flutterUnit, bump;  // the parts; bump: the splice's shape, 0..1
    };
    // Moves on a sample, the tape running at slowdown k.
    Wander next(float k) noexcept {
        loop_ += 1.0 / (kLoopSeconds * k * sr_);
        if (loop_ >= 1.0) {
            loop_ -= 1.0;
            spliceSlow_ += 1.0;  // the splice passes: an impulse into both exponentials
            spliceFast_ += 1.0;
        }
        spliceSlow_ *= slow_;
        spliceFast_ *= fast_;
        const auto bump = static_cast<float>(spliceSlow_ - spliceFast_) / bumpPeak_;
        return {static_cast<float>(kDriftMs / RandomSpline::kSplineRms) * drift_.next(driftStep_),
                wow_.next(wowStep_) / RandomSpline::kSplineRms, flutter_.next(flutterStep_) / RandomSpline::kSplineRms,
                bump};
    }
    // The delay's offset in ms for an echo on tape at slowdown k.
    static float offsetMs(const Wander& w, float k) noexcept {
        return w.driftMs + static_cast<float>(kWowMs * (1.0 + kWowSlope * (k - 1.0))) * w.wowUnit +
               static_cast<float>(kFlutterMs * (1.0 + kFlutterSlope * (k - 1.0))) * w.flutterUnit +
               static_cast<float>(kSpliceMs * bumpPeak()) * k * w.bump;
    }
    static float gain(const Wander& w) noexcept { return 1.f - kSpliceDip * w.bump; }

private:
    // The bump's height at amplitude 1, for these time constants (offsetMs() is static).
    static constexpr float bumpPeak() noexcept { return 0.148148f; }

    double sr_ = 48000.0;
    double driftStep_ = 0, wowStep_ = 0, flutterStep_ = 0;
    RandomSpline drift_, wow_, flutter_;
    double loop_ = 0.0, slow_ = 0.0, fast_ = 0.0, spliceSlow_ = 0.0, spliceFast_ = 0.0;
    float bumpPeak_ = 1.f;
};

// The playback EQ at a slowdown, and what doesn't change with it.
struct TapeDesign {
    static constexpr int kSections = 7;
    static constexpr double kGain = 0.4995;
    static constexpr double kLatency = 4.86;  // samples, added to the delay
    // Saturation: the compressor's level weighting and times, its curve, then the tanh.
    static constexpr double kShelfHz = 10168.0, kShelfGain = 10.599, kAttackMs = 0.481, kReleaseMs = 0.2332;
    static constexpr double kGainSmoothMs = 0.0397;
    static constexpr float kThreshold = 0.476f, kPower = 1.918f;
    static constexpr float kDrive = 0.3681f, kBias = -1.1476f, kOutput = 1.2f;

    std::array<dsp::BiquadCoefficients, kSections> eq{};
    ShelfCoefficients treble;
    float slowdown = 1.f;
    ShelfCoefficients shelf;
    float attack = 0.f, release = 0.f, gainSmooth = 0.f, biasTanh = 0.f;
    dsp::BiquadCoefficients feedbackHighPass, feedbackPeak;

    void prepare(double sr) noexcept {
        sr_ = sr;
        shelf = ShelfCoefficients(kShelfHz, kShelfGain, sr);
        attack = static_cast<float>(std::exp(-1.0 / (kAttackMs * 0.001 * sr)));
        release = static_cast<float>(std::exp(-1.0 / (kReleaseMs * 0.001 * sr)));
        gainSmooth = static_cast<float>(std::exp(-1.0 / (kGainSmoothMs * 0.001 * sr)));
        biasTanh = std::tanh(kDrive * kBias);
        // The feedback path: a one-pole high-pass at 100 Hz (as a biquad), +2 dB at 8.5 kHz.
        const double k = std::tan(std::numbers::pi * 100.1 / sr);
        feedbackHighPass = {1 / (1 + k), -1 / (1 + k), 0.0, (k - 1) / (k + 1), 0.0};
        feedbackPeak = analogSection(Section::Peak, 8545.0, 1.868, 1.266, sr);
        time_ = -1.0;
        setTime(0.1);
    }
    // The EQ for an echo `seconds` long (recomputed only when the time moves).
    void setTime(double seconds) noexcept {
        if (std::abs(seconds - time_) < 1e-5) return;
        time_ = seconds;
        const double s = slowdown = tapeSlowdown(seconds);
        eq = {analogSection(Section::HighPass, 10.149, 0.4986, 1, sr_),
              analogSection(Section::LowShelf, 2193.1 / s, 0.165, 0.2922, sr_),
              analogSection(Section::Peak, 2557.5 / s, 0.1839, 3.2753, sr_),
              analogSection(Section::LowPass, 26487.0 / s, 0.4684, 1, sr_),
              analogSection(Section::LowPass, 65877.0 / s, 0.3658, 1, sr_),
              analogSection(Section::LowPass, 23718.5, 0.646, 1, sr_),
              analogSection(Section::LowPass, 23888.7, 6.986, 1, sr_)};
        treble = ShelfCoefficients(9000.0, std::pow(10.0, trebleDb(seconds) / 20.0), sr_);
    }

    // The plug-in's treble steps with the time, beyond the speed's EQ (its tape
    // buffer's, at the times measured; in between, interpolated): dB above 9 kHz.
    static double trebleDb(double seconds) noexcept {
        static constexpr double kMs[] = {1, 30, 100, 150, 200, 300, 400, 500, 700, 1000, 1500, 2000, 3000, 5000};
        static constexpr double kDb[] = {0.42, 0.08, -1.04, -1.04, -2.7, 0.58, 0.61, 1.45, 1.26, 1.22, -0.57, -0.54, 3.68, 3.51};
        constexpr int n = static_cast<int>(std::size(kMs));
        const double ms = std::clamp(seconds * 1000.0, kMs[0], kMs[n - 1]);
        int i = 0;
        while (i < n - 2 && ms > kMs[i + 1]) ++i;
        const double t = std::log(ms / kMs[i]) / std::log(kMs[i + 1] / kMs[i]);
        return kDb[i] + t * (kDb[i + 1] - kDb[i]);
    }

private:
    double sr_ = 48000.0, time_ = -1.0;  // what eq is for
};

// One echo's linear part: the playback EQ, and its feedback path.
class TapeEcho {
public:
    void reset() noexcept {
        for (auto& b : eq_) b.reset();
        treble_.reset();
        feedbackHighPass_.reset();
        feedbackPeak_.reset();
    }

    float process(const TapeDesign& d, float x) noexcept {
        float y = static_cast<float>(TapeDesign::kGain) * x;
        for (int i = 0; i < TapeDesign::kSections; ++i) y = eq_[i].process(d.eq[i], y);
        return treble_.process(d.treble, y);
    }
    // What this echo feeds back (before the feedback's gain).
    float feedback(const TapeDesign& d, float wet) noexcept {
        return feedbackPeak_.process(d.feedbackPeak, feedbackHighPass_.process(d.feedbackHighPass, wet));
    }

private:
    std::array<dsp::Biquad, TapeDesign::kSections> eq_;
    Shelf treble_;
    dsp::Biquad feedbackHighPass_, feedbackPeak_;
};

// The saturation, one a side, on the side's echoes together (as Digital's
// compressor): a fast compressor, then the tanh, as a gain for each echo (the
// tanh's output over its input), so the echoes add up to the saturated sum.
class TapeDynamics {
public:
    void reset() noexcept {
        shelf_.reset();
        level_ = 0.f;
        gain_ = 1.f;
    }

    float gain(const TapeDesign& d, float sum) noexcept {
        // The compressor: (1 + (level / threshold)^p)^(-1/p), level an RMS, the gain smoothed a little.
        const float w = shelf_.process(d.shelf, sum);
        const float square = w * w;
        level_ = square + (square > level_ ? d.attack : d.release) * (level_ - square);
        if (level_ < 1e-30f) level_ = 0.f;
        float g = 1.f;
        if (level_ > 1e-12f) {
            const float ratio = level_ * (1.f / (TapeDesign::kThreshold * TapeDesign::kThreshold));
            const float u = fastmath::exp2(0.5f * TapeDesign::kPower * fastmath::log2(ratio));
            g = fastmath::exp2(-fastmath::log2(1.f + u) / TapeDesign::kPower);
        }
        gain_ = g + d.gainSmooth * (gain_ - g);
        // The tanh, a little off centre (even harmonics too), over what goes in; its slope there near 0.
        const float v = gain_ * sum;
        constexpr float kDrive = TapeDesign::kDrive, kBias = TapeDesign::kBias;
        const float ratio = std::abs(v) > 1e-6f ? (dsp::fastTanh(kDrive * (v + kBias)) - d.biasTanh) / (kDrive * v)
                                                : 1.f - d.biasTanh * d.biasTanh;
        return TapeDesign::kOutput * gain_ * ratio;
    }

private:
    Shelf shelf_;
    float level_ = 0.f, gain_ = 1.f;
};

}  // namespace sub::delay_character
