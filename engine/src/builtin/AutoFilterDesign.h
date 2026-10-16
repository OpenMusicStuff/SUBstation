#pragma once
// The Auto Filter's maths, after Ableton's: a resonant state-variable filter
// (Lowpass, Highpass, Bandpass, Notch, or Morph, sweeping from one to the next)
// at 12 or 24 dB/octave, its cutoff moved in octaves by an envelope follower
// and an LFO.
//
// The filter is a TPT state-variable section (dsp::Svf): 12 dB is one section
// at the resonance's Q; 24 dB puts a gentle section (Q 0.5412) before it and
// raises its Q by 1.8478, so at no resonance the pair is a fourth-order
// Butterworth. Each section's output is a weighted sum of its low-pass, its
// band-pass (unity at the centre) and its high-pass (`Weights`): every type is
// such a sum (a notch is the low-pass plus the high-pass), so changing type
// glides the weights rather than switching outputs, and Morph walks them round
// Lowpass, Bandpass, Highpass, Notch and back to Lowpass.
//
// The TPT section is the bilinear transform of the analog one with its cutoff
// prewarped, so its response at f is the analog one at tan(pi f / sr) /
// tan(pi fc / sr): responseDb() is exact for the Clean circuit (the others
// saturate inside the loop, which only a level-dependent drawing could show).
//
// Shared by the device (builtin/devices/AutoFilter.cpp) and its editor (through
// the application layer's AutoFilterResponse.h), so the curve drawn is the
// filter that plays. No engine headers: the application layer includes this one.

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace sub::autofilter {

enum class Type { Lowpass, Highpass, Bandpass, Notch, Morph };
enum class Circuit { Clean, Osr, Ms2, Smp, Prd };

constexpr double kPi = std::numbers::pi;
constexpr double kMinFrequency = 20.0, kMaxFrequency = 20000.0;  // Hz: Frequency's range, and the cutoff's
constexpr double kNyquistLimit = 0.45;  // of the sample rate: the highest the cutoff goes
constexpr double kMaxResonance = 125.0;  // %
constexpr double kEnvelopeOctaves = 6.0;  // Envelope Amount ±100 % with the envelope full: ±6 octaves
constexpr double kLfoOctaves = 4.0;       // LFO Amount 100 % at the shape's peaks: ±4 octaves
constexpr double kEnvelopeFloorDb = -48.0;  // the envelope reads 0 at this level, 1 at 0 dBFS (evenly in dB)
constexpr double kFirstSectionQ = 0.5411961001461970;  // 24 dB's gentle section (fourth-order Butterworth's)
constexpr double kSecondSectionScale = 1.8477590650225735;  // 24 dB's resonant section: Q times this (0.7071 -> 1.3066)

// The resonant section's Q for Resonance (%): 0.7071 (Butterworth) at 0, rising
// with the square to 25 at 125 %.
inline double resonanceQ(double resonancePercent) noexcept {
    const double r = std::clamp(resonancePercent, 0.0, kMaxResonance) / kMaxResonance;
    return std::numbers::sqrt2 / 2.0 + (25.0 - std::numbers::sqrt2 / 2.0) * r * r;
}

// The cutoff a sweep reaches: Frequency moved by `octaves`, held to the range
// and below kNyquistLimit of the rate.
inline double cutoffHz(double frequency, double octaves, double sampleRate) noexcept {
    const double hz = frequency * std::exp2(octaves);
    return std::clamp(hz, kMinFrequency, std::min(kMaxFrequency, kNyquistLimit * sampleRate));
}

// The envelope's 0..1 from a peak level (linear): evenly in dB from kEnvelopeFloorDb to 0 dBFS.
inline double envelopeValue(double peak) noexcept {
    if (!(peak > 0.0)) return 0.0;
    const double db = 20.0 * std::log10(peak);
    return std::clamp(1.0 - db / kEnvelopeFloorDb, 0.0, 1.0);
}

// How far the envelope and the LFO move the cutoff, in octaves: Envelope Amount
// (%, ±100) times the envelope (0..1), LFO Amount (%, 0..100) times the LFO's
// value (-1..1).
inline double sweepOctaves(double envelopeAmount, double envelope, double lfoAmount, double lfo) noexcept {
    return envelopeAmount / 100.0 * kEnvelopeOctaves * envelope + lfoAmount / 100.0 * kLfoOctaves * lfo;
}

// A section's output as a sum of its low-pass, band-pass (unity at the centre)
// and high-pass.
struct Weights {
    double lp = 1.0, bp = 0.0, hp = 0.0;
};

// Morph's position (0..100 %) round Lowpass, Bandpass, Highpass, Notch and back
// to Lowpass, a quarter each, crossfading straight between neighbours.
inline Weights morphWeights(double morphPercent) noexcept {
    const double at = std::clamp(morphPercent, 0.0, 100.0) / 25.0;
    const int segment = std::min(3, static_cast<int>(at));
    const double t = at - segment;
    switch (segment) {
    case 0: return {1.0 - t, t, 0.0};  // Lowpass to Bandpass
    case 1: return {0.0, 1.0 - t, t};  // Bandpass to Highpass
    case 2: return {t, 0.0, 1.0};      // Highpass to Notch (the low-pass joining)
    default: return {1.0, 0.0, 1.0 - t};  // Notch to Lowpass (the high-pass leaving)
    }
}

inline Weights weights(Type type, double morphPercent) noexcept {
    switch (type) {
    case Type::Lowpass: return {1.0, 0.0, 0.0};
    case Type::Highpass: return {0.0, 0.0, 1.0};
    case Type::Bandpass: return {0.0, 1.0, 0.0};
    case Type::Notch: return {1.0, 0.0, 1.0};
    case Type::Morph: return morphWeights(morphPercent);
    }
    return {};
}

// What a circuit does to the signal going into the filter and to the band-pass
// state inside it (which bounds the resonance: the louder it rings, the more
// it is squashed). Clean is linear. OSR clips hard and leans one way; MS2
// (Sallen-Key) is the most aggressive, its state squashed early; SMP sits
// between; PRD (a ladder's) is round and soft. Every curve has a slope of 1 at
// 0 and is never larger than its input (so the state it squashes can only lose
// energy: the loop stays stable).
inline float saturate(Circuit circuit, float x) noexcept {
    switch (circuit) {
    case Circuit::Clean: return x;
    case Circuit::Osr: return x / (1.f + (x > 0.f ? 1.f : 1.4f) * std::abs(x));
    case Circuit::Ms2: return std::tanh(x);
    case Circuit::Smp: return 1.5f * std::tanh(x / 1.5f);
    case Circuit::Prd: return 2.f * x / std::sqrt(4.f + x * x);
    }
    return x;
}

// The band-pass state's limit per circuit (the state is saturate()d over it), and whether a circuit hears
// Drive (Clean doesn't).
inline float stateLimit(Circuit circuit) noexcept {
    switch (circuit) {
    case Circuit::Clean: return 0.f;  // (none)
    case Circuit::Osr: return 1.5f;
    case Circuit::Ms2: return 0.9f;
    case Circuit::Smp: return 1.2f;
    case Circuit::Prd: return 2.f;
    }
    return 0.f;
}
inline bool hasDrive(Circuit circuit) noexcept { return circuit != Circuit::Clean; }

// One section's analog response at Ω (the frequency over the cutoff, prewarped)
// for damping k = 1/Q and output weights `w`.
inline std::complex<double> sectionResponse(double omega, double k, const Weights& w) noexcept {
    const std::complex<double> s(0.0, omega);
    return (w.lp + w.bp * k * s + w.hp * s * s) / (s * s + k * s + 1.0);
}

// The filter's response (dB) at `frequency` (Hz) for cutoff `cutoff` (Hz), as
// the device plays it at `sampleRate` with `slope24` (else 12 dB) and Resonance
// (%), output weights `w`. Floored at -120 dB.
inline double responseDb(double frequency, double cutoff, double resonancePercent, bool slope24, const Weights& w,
                         double sampleRate) noexcept {
    const double nyquist = 0.5 * sampleRate;
    const double f = std::clamp(frequency, 1.0, nyquist * 0.9999);
    const double fc = std::clamp(cutoff, 1.0, kNyquistLimit * sampleRate);
    const double omega = std::tan(kPi * f / sampleRate) / std::tan(kPi * fc / sampleRate);
    const double q = resonanceQ(resonancePercent);
    std::complex<double> h;
    if (slope24) {
        h = sectionResponse(omega, 1.0 / kFirstSectionQ, w) * sectionResponse(omega, 1.0 / (q * kSecondSectionScale), w);
    } else {
        h = sectionResponse(omega, 1.0 / q, w);
    }
    const double magnitude = std::abs(h);
    return magnitude > 1e-6 ? 20.0 * std::log10(magnitude) : -120.0;
}

}  // namespace sub::autofilter
