#pragma once
// The Overdrive's maths, after Ableton's Overdrive (a pedal's): the input
// through a band-pass (Frequency, Width), then Drive into an asymmetric soft
// clipper, Tone's low-pass after it, and Dynamics putting back some of the
// level changes the clipping takes out.
//
// - The band-pass is a 12 dB/octave Butterworth high-pass at Frequency /
//   2^(Width/2) and low-pass at Frequency * 2^(Width/2) (TPT sections, which
//   are the analog ones under the bilinear transform: bandResponseDb() is
//   exact).
// - Drive (0..100 %) is a gain of 0 to kMaxDriveDb into the curve: tanh, its
//   negative side clipping lower (kNegative), so it adds even harmonics as a
//   diode pair that doesn't match does. It is anti-aliased with its
//   antiderivative (first-order ADAA: the curve averaged over the line from
//   one input to the next).
// - Dynamics (0..100 %): the clipped signal is scaled by (band level / clipped
//   level)^Dynamics, held to at most 1: at 0 the clipper's flat level, at 100 %
//   the quiet parts as much quieter as they came in.
// - Tone (0..100 %): a 12 dB/octave low-pass from 1 kHz to 20 kHz, evenly in
//   log.
//
// Shared by the device (builtin/devices/Overdrive.cpp) and its editor (through
// the application layer's OverdriveResponse.h). No engine headers.

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>

namespace sub::overdrive {

constexpr double kPi = std::numbers::pi;
constexpr double kMinFrequency = 50.0, kMaxFrequency = 20000.0;  // Hz: Frequency's range
constexpr double kMinWidth = 0.5, kMaxWidth = 9.0;              // octaves: Width's range
constexpr double kNyquistLimit = 0.45;                           // of the rate: the filters' highest
constexpr double kMaxDriveDb = 42.0;                             // Drive 100 %
constexpr double kNegative = 0.75;                               // where the curve's negative side clips
constexpr double kToneLowHz = 1000.0, kToneHighHz = 20000.0;     // Tone 0 and 100 %

// The band's -3 dB edges (Hz) for Frequency and Width, held to 20 Hz..the filters' highest.
inline std::pair<double, double> bandEdges(double frequency, double width, double sampleRate) noexcept {
    const double half = std::exp2(0.5 * std::clamp(width, kMinWidth, kMaxWidth));
    const double top = kNyquistLimit * sampleRate;
    return {std::clamp(frequency / half, 20.0, top), std::clamp(frequency * half, 20.0, top)};
}

// Drive's gain (linear).
inline double driveGain(double drivePercent) noexcept {
    return std::pow(10.0, kMaxDriveDb * std::clamp(drivePercent, 0.0, 100.0) / 100.0 / 20.0);
}

// Tone's cutoff (Hz), below the filters' highest.
inline double toneHz(double tonePercent, double sampleRate) noexcept {
    const double t = std::clamp(tonePercent, 0.0, 100.0) / 100.0;
    return std::min(kToneLowHz * std::pow(kToneHighHz / kToneLowHz, t), kNyquistLimit * sampleRate);
}

// The curve: tanh on the positive side, and on the negative side tanh scaled
// to clip at kNegative (a diode pair that doesn't match), so it adds even
// harmonics however hard it is driven. Its slope is 1 at 0 and it is 0 there:
// silence stays silence.
inline double shape(double x) noexcept { return x >= 0.0 ? std::tanh(x) : kNegative * std::tanh(x / kNegative); }

// ln cosh u without overflow: |u| + ln(1 + e^(-2|u|)) - ln 2.
inline double logCosh(double u) noexcept {
    const double a = std::abs(u);
    return a + std::log1p(std::exp(-2.0 * a)) - std::numbers::ln2;
}

// shape()'s antiderivative, 0 at 0 (both sides' are, and meet there with the same slope).
inline double shapeIntegral(double x) noexcept {
    return x >= 0.0 ? logCosh(x) : kNegative * kNegative * logCosh(x / kNegative);
}

// First-order antiderivative anti-aliasing of shape(): (F(x) - F(x0)) / (x - x0),
// or shape() at the midpoint where the two are too close to divide.
struct Adaa {
    double x0 = 0.0, f0 = 0.0;

    void reset() noexcept { x0 = f0 = 0.0; }
    double process(double x) noexcept {
        const double f = shapeIntegral(x);
        const double d = x - x0;
        const double y = std::abs(d) < 1e-5 ? shape(0.5 * (x0 + x)) : (f - f0) / d;
        x0 = x;
        f0 = f;
        return y;
    }
};

// A Butterworth second-order section's analog response at Ω = f / fc.
inline std::complex<double> lowpass(double omega) noexcept {
    const std::complex<double> s(0.0, omega);
    return 1.0 / (s * s + std::numbers::sqrt2 * s + 1.0);
}
inline std::complex<double> highpass(double omega) noexcept {
    const std::complex<double> s(0.0, omega);
    return s * s / (s * s + std::numbers::sqrt2 * s + 1.0);
}

// The prewarped Ω of `f` against a cutoff `fc`, at `sampleRate` (the TPT section's bilinear transform).
inline double warped(double f, double fc, double sampleRate) noexcept {
    const double nyquist = 0.5 * sampleRate;
    return std::tan(kPi * std::clamp(f, 1.0, 0.9999 * nyquist) / sampleRate) / std::tan(kPi * fc / sampleRate);
}

// The band-pass's response (dB) at `f` (Hz), as the device plays it. Floored at -120.
inline double bandResponseDb(double f, double frequency, double width, double sampleRate) noexcept {
    const auto [low, high] = bandEdges(frequency, width, sampleRate);
    const double magnitude =
        std::abs(highpass(warped(f, low, sampleRate)) * lowpass(warped(f, high, sampleRate)));
    return magnitude > 1e-6 ? 20.0 * std::log10(magnitude) : -120.0;
}

// Tone's response (dB) at `f`.
inline double toneResponseDb(double f, double tonePercent, double sampleRate) noexcept {
    const double magnitude = std::abs(lowpass(warped(f, toneHz(tonePercent, sampleRate), sampleRate)));
    return magnitude > 1e-6 ? 20.0 * std::log10(magnitude) : -120.0;
}

// Dynamics' gain for the band's level and the clipped signal's (both linear, followed alike).
inline double dynamicsGain(double bandLevel, double clippedLevel, double dynamicsPercent) noexcept {
    const double d = std::clamp(dynamicsPercent, 0.0, 100.0) / 100.0;
    if (d <= 0.0 || !(clippedLevel > 1e-9)) return 1.0;
    const double ratio = std::clamp(bandLevel / clippedLevel, 1e-6, 1.0);
    return std::pow(ratio, d);
}

}  // namespace sub::overdrive
