#pragma once
// Snarl's maths, shared by the device (builtin/devices/Snarl.cpp), its tests and,
// through the application layer's SnarlResponse.h, its editor: the lists' names
// and what they stand for (the growl's modes, the FM ratios, the filters, the
// dirt, the wobble's rates and shapes), the oscillator's shapes and how Wave
// morphs between them, the growl's four ways of bending them, the vowels the
// Vowel filter talks through, and the wobble's shapes.
//
// All inline and real-time safe.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "builtin/DspBlocks.h"
#include "rt/FastMath.h"

namespace sub::snarl {

// --- The lists ---------------------------------------------------------------------------

enum class GrowlMode { FM, Sync, Fold, Ring };
enum class FilterType { Low, Band, Vowel, Comb };
enum class DirtType { Tube, Hard, Rectify, Crush };
enum class WobbleShape { Sine, Triangle, RampDown, RampUp, Square, Random };

inline const std::vector<std::string>& growlModeLabels() {
    static const std::vector<std::string> kLabels = {"FM", "Sync", "Fold", "Ring"};
    return kLabels;
}
inline const std::vector<std::string>& filterTypeLabels() {
    static const std::vector<std::string> kLabels = {"Low", "Band", "Vowel", "Comb"};
    return kLabels;
}
inline const std::vector<std::string>& dirtTypeLabels() {
    static const std::vector<std::string> kLabels = {"Tube", "Hard", "Rectify", "Crush"};
    return kLabels;
}
inline const std::vector<std::string>& wobbleShapeLabels() {
    static const std::vector<std::string> kLabels = {"Sine", "Triangle", "Ramp Down", "Ramp Up", "Square", "Random"};
    return kLabels;
}

// The modulator's frequency for FM and Ring, as a ratio to the note's.
inline const std::vector<std::string>& ratioLabels() {
    static const std::vector<std::string> kLabels = {"0.5", "1", "1.5", "2", "3", "4", "5", "7"};
    return kLabels;
}
inline double ratio(int index) noexcept {
    static constexpr std::array<double, 8> kRatios = {0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 7.0};
    return kRatios[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(kRatios.size()) - 1))];
}

// The wobble's rates, the note values bass music wobbles at: whole to 1/32,
// dotted (D) and triplets (T) among them. Labels, and each one's cycle in beats.
inline const std::vector<std::string>& rateLabels() {
    static const std::vector<std::string> kLabels = {"1/1", "1/2",  "1/2T", "1/4D", "1/4",   "1/4T",
                                                     "1/8D", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
    return kLabels;
}
inline double rateBeats(int index) noexcept {
    static constexpr std::array<double, 12> kBeats = {4.0,  2.0, 4.0 / 3, 1.5,  1.0,       2.0 / 3,
                                                      0.75, 0.5, 1.0 / 3, 0.25, 1.0 / 6.0, 0.125};
    return kBeats[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(kBeats.size()) - 1))];
}

// --- The oscillator ----------------------------------------------------------------------

// PolyBLEP: smooths a step at phase 0 over one sample on each side (the Synth's).
inline double polyBlep(double t, double dt) noexcept {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

inline double wrap(double phase) noexcept { return phase - std::floor(phase); }

inline constexpr int kShapes = 5;         // Sine, Triangle, Saw, Square, Pulse
inline constexpr double kPulseWidth = 0.2;

// Shape `k` (0 Sine, 1 Triangle, 2 Saw, 3 Square, 4 Pulse) at phase `t` (0..1),
// stepping `dt` a sample (its steps smoothed: PolyBLEP). Each in -1..1, without DC.
inline double shapeAt(int k, double t, double dt) noexcept {
    dt = std::clamp(dt, 1e-9, 0.5);
    switch (k) {
    case 0: return fastmath::sin(dsp::kTwoPi * t);
    case 1: return 1.0 - 4.0 * std::abs(t - 0.5);
    case 2: return 2.0 * t - 1.0 - polyBlep(t, dt);
    case 3: return (t < 0.5 ? 1.0 : -1.0) + polyBlep(t, dt) - polyBlep(wrap(t + 0.5), dt);
    default: {  // a narrow pulse, its DC taken out and its peak at 1
        const double pulse = (t < kPulseWidth ? 1.0 : -1.0) + polyBlep(t, dt) - polyBlep(wrap(t + 1.0 - kPulseWidth), dt);
        return (pulse - (2.0 * kPulseWidth - 1.0)) / (2.0 - 2.0 * kPulseWidth);
    }
    }
}

// Wave (0..100 %) as a place among the shapes: 0 Sine, 25 Triangle, 50 Saw,
// 75 Square, 100 Pulse, crossfading between neighbours.
inline double morphPosition(double wavePercent) noexcept {
    return std::clamp(wavePercent, 0.0, 100.0) / 100.0 * (kShapes - 1);
}
inline double morph(double position, double t, double dt) noexcept {
    const int first = std::clamp(static_cast<int>(position), 0, kShapes - 2);
    const double fraction = std::clamp(position - first, 0.0, 1.0);
    if (fraction <= 0.0) return shapeAt(first, t, dt);
    if (fraction >= 1.0) return shapeAt(first + 1, t, dt);
    return (1.0 - fraction) * shapeAt(first, t, dt) + fraction * shapeAt(first + 1, t, dt);
}

// --- The growl ---------------------------------------------------------------------------
// Growl (0..1) bends the oscillator four ways:
// - FM: its phase pushed back and forth by a sine at Ratio times its frequency
//   (phase modulation), up to kMaxFmIndex radians.
// - Sync: a copy kSyncRange times as fast at most, restarted each cycle, faded
//   in and out over the cycle (a Hann window: windowed sync), so the restart
//   doesn't click and a formant rises through the sound as it turns.
// - Fold: driven into a sine wavefolder, up to kMaxFold times, anti-aliased
//   (first-order antiderivative anti-aliasing: the folder averaged over the line
//   from the last sample to this one, half a sample late).
// - Ring: multiplied by a sine at Ratio times its frequency.
inline constexpr double kMaxFmIndex = 8.0;
inline constexpr double kSyncRange = 7.0;  // Sync's copy runs 1 + 7 times as fast at 100 %
inline constexpr double kMaxFold = 8.0;

// How far `growl` brings in Sync's window and Fold's folder (all of it by a third of the way),
// so both start from the oscillator as it is at 0.
inline double growlBlend(double growl) noexcept { return std::clamp(3.0 * growl, 0.0, 1.0); }

// The folder sin(a x) averaged over the line from x0 to x: (cos(a x0) - cos(a x)) / (a (x - x0)), or at their
// midpoint where they are too close for the division.
inline double foldAveraged(double a, double x, double x0) noexcept {
    const double d = x - x0;
    if (std::abs(d) < 1e-6) return std::sin(a * 0.5 * (x + x0));
    return (std::cos(a * x0) - std::cos(a * x)) / (a * d);
}

// One oscillator through the growl: `read(t, dt)` the wave at phase `t` (0..1)
// stepping `dt` a sample (Wave's shapes, or a wavetable's frame), `t` and `dt`
// the oscillator's, `mod` the modulator's phase (FM, Ring) at `ratio` times its
// frequency. `foldIn` is Fold's last input (what it averages from): Fold
// updates it; another mode leaves it, so a switch to Fold primes it first.
template <typename Read>
double growled(GrowlMode mode, double growl, double t, double dt, double mod, double ratio, double& foldIn,
               Read&& read) noexcept {
    switch (mode) {
    case GrowlMode::FM: {
        double sine, cosine;
        fastmath::sincos(dsp::kTwoPi * mod, sine, cosine);
        const double index = kMaxFmIndex * growl;
        // The step the bent phase takes, the carrier's and the modulation's together, for the BLEPs.
        const double step = dt * (1.0 + index * ratio * cosine);
        return read(wrap(t + index * sine / dsp::kTwoPi), std::abs(step));
    }
    case GrowlMode::Sync: {
        const double speed = 1.0 + kSyncRange * growl;
        const double blend = growlBlend(growl);
        const double window = 0.5 - 0.5 * fastmath::cos(dsp::kTwoPi * t);
        return read(wrap(t * speed), dt * speed) * ((1.0 - blend) + blend * window);
    }
    case GrowlMode::Fold: {
        const double x = read(t, dt);
        const double blend = growlBlend(growl);
        const double a = 0.5 * dsp::kPi * (1.0 + (kMaxFold - 1.0) * growl);
        const double y = (1.0 - blend) * x + blend * foldAveraged(a, x, foldIn);
        foldIn = x;
        return y;
    }
    case GrowlMode::Ring: {
        const double x = read(t, dt);
        return x * ((1.0 - growl) + growl * fastmath::sin(dsp::kTwoPi * mod));
    }
    }
    return 0.0;
}

// The same through Wave's shapes, at `position` among them.
inline double growled(GrowlMode mode, double growl, double position, double t, double dt, double mod, double ratio,
                      double& foldIn) noexcept {
    return growled(mode, growl, t, dt, mod, ratio, foldIn,
                   [position](double at, double step) { return morph(position, at, step); });
}

// --- The dirt ----------------------------------------------------------------------------
// Each curve takes u, the sound times half of Drive's gain (6 dB of headroom: a
// full-scale sound bends only gently at Drive 0), and is anti-aliased as Fold is:
// the curve averaged over the line from the last u to this one. Tube is tanh
// (biased, its offset taken out), Hard a clip at ±1, Rectify 1 - sech 2u: a
// full-wave rectifier (even: an octave up) rounded at 0 (2u² there) where |u|
// would have a corner, whose harmonics alias, and saturating at 1.

inline constexpr double kLn2 = 0.69314718055994531;

// ln cosh x, without overflow: |x| + ln(1 + e^(-2|x|)) - ln 2.
inline double logCosh(double x) noexcept {
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - kLn2;
}

inline double dirtCurve(DirtType type, double u) noexcept {
    switch (type) {
    case DirtType::Tube: return std::tanh(u);
    case DirtType::Hard: return std::clamp(u, -1.0, 1.0);
    case DirtType::Rectify: return 1.0 - 1.0 / std::cosh(std::min(std::abs(2.0 * u), 700.0));
    case DirtType::Crush: return u;
    }
    return u;
}
// Its antiderivative.
inline double dirtIntegral(DirtType type, double u) noexcept {
    switch (type) {
    case DirtType::Tube: return logCosh(u);
    case DirtType::Hard: return std::abs(u) <= 1.0 ? 0.5 * u * u : std::abs(u) - 0.5;
    case DirtType::Rectify: return u - std::atan(std::tanh(u));  // (d/du atan tanh u = sech 2u)
    case DirtType::Crush: return 0.5 * u * u;
    }
    return 0.5 * u * u;
}
// The curve averaged over the line from u0 to u (at their midpoint where they are too close for the division).
inline double dirtAveraged(DirtType type, double u, double u0) noexcept {
    const double d = u - u0;
    if (std::abs(d) < 1e-6) return dirtCurve(type, 0.5 * (u + u0));
    return (dirtIntegral(type, u) - dirtIntegral(type, u0)) / d;
}

// Drive (0..1) as a gain: x1 to x16 (+24 dB).
inline double driveGain(double drive) noexcept { return std::exp(2.7725887 * std::clamp(drive, 0.0, 1.0)); }

// --- The Vowel filter --------------------------------------------------------------------
// Three formants per vowel (a man's voice, Peterson and Barney's averages),
// ordered dark to bright: U, O, A, E, I. The filter's frequency picks a place
// along them, its lowest at U and its highest at I, gliding between neighbours
// (the formants' frequencies in octaves, their levels in amplitude).

struct Formant {
    double hz;
    double gain;
};
inline constexpr int kVowels = 5;
inline constexpr int kFormants = 3;
inline constexpr std::array<char, kVowels> kVowelNames = {'U', 'O', 'A', 'E', 'I'};
inline const std::array<std::array<Formant, kFormants>, kVowels>& vowels() {
    static const std::array<std::array<Formant, kFormants>, kVowels> kTable = {{
        {{{300.0, 1.0}, {870.0, 0.35}, {2240.0, 0.1}}},   // U
        {{{570.0, 1.0}, {840.0, 0.5}, {2410.0, 0.15}}},   // O
        {{{730.0, 1.0}, {1090.0, 0.6}, {2440.0, 0.25}}},  // A
        {{{530.0, 1.0}, {1840.0, 0.45}, {2480.0, 0.3}}},  // E
        {{{270.0, 1.0}, {2290.0, 0.35}, {3010.0, 0.35}}}, // I
    }};
    return kTable;
}

// The filter's frequency range (Hz): its parameter's.
inline constexpr double kMinFreq = 30.0;
inline constexpr double kMaxFreq = 18000.0;

// A frequency's place along the range: 0 at kMinFreq, 1 at kMaxFreq (log), unclamped.
inline double freqPosition(double hz) noexcept {
    return std::log(std::max(hz, 1e-3) / kMinFreq) / std::log(kMaxFreq / kMinFreq);
}
// Its place among the vowels: 0 U .. 4 I.
inline double vowelPosition(double hz) noexcept { return std::clamp(freqPosition(hz), 0.0, 1.0) * (kVowels - 1); }

// The formants at a place among the vowels.
inline std::array<Formant, kFormants> formantsAt(double position) noexcept {
    position = std::clamp(position, 0.0, static_cast<double>(kVowels - 1));
    const int first = std::min(static_cast<int>(position), kVowels - 2);
    const double t = position - first;
    std::array<Formant, kFormants> out{};
    for (int f = 0; f < kFormants; ++f) {
        const Formant& a = vowels()[static_cast<size_t>(first)][static_cast<size_t>(f)];
        const Formant& b = vowels()[static_cast<size_t>(first + 1)][static_cast<size_t>(f)];
        out[static_cast<size_t>(f)] = {a.hz * std::pow(b.hz / a.hz, t), a.gain + t * (b.gain - a.gain)};
    }
    return out;
}

// --- The wobble --------------------------------------------------------------------------

// The wobble's shape at `phase` (0..1) of cycle `cycle`, from 0 to 1 (where the
// filter rests at its frequency, and the growl and pitch at theirs): Sine and
// Triangle rise from 0 and fall back, Ramp Down falls from 1, Ramp Up rises to
// it, Square is 1 for the first half, Random holds a new value each cycle (from
// the cycle's number: a render repeats exactly).
inline double wobble(WobbleShape shape, double phase, uint32_t cycle) noexcept {
    switch (shape) {
    case WobbleShape::Sine: return 0.5 - 0.5 * fastmath::cos(dsp::kTwoPi * phase);
    case WobbleShape::Triangle: return phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
    case WobbleShape::RampDown: return 1.0 - phase;
    case WobbleShape::RampUp: return phase;
    case WobbleShape::Square: return phase < 0.5 ? 1.0 : 0.0;
    case WobbleShape::Random: return 0.5 + 0.5 * dsp::hashToBipolar(cycle * 0x9E3779B9U + 0x5EEDU);
    }
    return 0.0;
}

// How far the wobble moves the filter at Wobble > Filter 100 %, in octaves (the
// Vowel filter's place along its vowels moves as far along the frequency range).
inline constexpr double kWobbleOctaves = 6.0;

}  // namespace sub::snarl
