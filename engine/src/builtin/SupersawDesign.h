#pragma once
// The Supersaw's design: where each saw of a stack sits (its pitch, its place
// in the stereo field, its share of the stack's level), the curves of the
// controls that place them, and the band-limited step that keeps every saw
// free of aliasing.
//
// A stack is N saws (1 to 64) around the stack's pitch. From the flattest to
// the sharpest their places are N points spread evenly over -1..1, bent by
// Curve (x becomes sign(x) |x|^e, e = 2^(1.5 Curve): above 0 the saws gather
// towards the middle, as the JP-8000's do, which keeps their beating from
// lining up into a pattern that repeats; below 0 towards the edges), times
// the outermost saws' detune (Detune² × 100 cents: 50 % is 25 cents, 100 % a
// semitone). Blend weighs the middle saw (an odd stack's; an even stack's
// middle pair shares it) against the others: the middle at 1 - Blend / 3, each
// other saw at Blend (all alike at 75 %, the others louder above), then the
// whole made to unit power, so adding saws thickens a stack without making it
// louder. Width spreads them over the stereo field: each mirrored pair (the
// flattest with the sharpest, and so on inwards) goes to opposite sides, the
// outer pairs widest, the flat one to the left in one pair and to the right in
// the next, so each side hears flat and sharp saws alike; equal power, 1 at
// the middle.
//
// Shared by the device (builtin/devices/Supersaw.cpp) and its editor's graph
// (through the application layer's SupersawLayout.h), so the stack drawn is
// the stack that plays. All inline; the step's table is made once, on first
// use (not on the audio thread: the device asks for it in prepare()).

#include <algorithm>
#include <cmath>
#include <vector>

#include "builtin/DspBlocks.h"

namespace sub::supersaw {

using dsp::kPi;

inline constexpr int kStacks = 3;
inline constexpr int kMaxSaws = 64;
inline constexpr double kMaxDetuneCents = 100.0;  // Detune 100 %: the outermost saws a semitone off
inline constexpr double kCurveRange = 1.5;        // Curve ±100 %: the places' exponent 2^±1.5

// --- The controls ---------------------------------------------------------------------------

// The outermost saws' offset, in cents, for a Detune in percent: squared, so the
// range supersaws live in (10 to 40 cents) gets most of the knob.
inline double detuneCents(double detunePercent) noexcept {
    const double d = std::clamp(detunePercent / 100.0, 0.0, 1.0);
    return kMaxDetuneCents * d * d;
}

// The places' exponent for a Curve in percent (-100..100).
inline double curveExponent(double curvePercent) noexcept {
    return std::exp2(kCurveRange * std::clamp(curvePercent / 100.0, -1.0, 1.0));
}

// The middle saw's weight and each other saw's, for a Blend in percent, before
// the stack is made to unit power.
inline double middleWeight(double blendPercent) noexcept {
    return 1.0 - std::clamp(blendPercent / 100.0, 0.0, 1.0) / 3.0;
}
inline double sideWeight(double blendPercent) noexcept { return std::clamp(blendPercent / 100.0, 0.0, 1.0); }

// A place in the stereo field (-1 left .. 1 right) as two gains: equal power, 1
// each at the middle, √2 and 0 at the sides.
inline void panGains(double pan, double& left, double& right) noexcept {
    const double angle = (std::clamp(pan, -1.0, 1.0) + 1.0) * kPi / 4.0;
    left = std::sqrt(2.0) * std::cos(angle);
    right = std::sqrt(2.0) * std::sin(angle);
}

// --- A stack's saws -------------------------------------------------------------------------

// A stack's shape: its parameters, in their units (a count, percent).
struct Shape {
    int saws = 7;
    double detune = 45.0;
    double curve = 35.0;
    double blend = 75.0;
    double width = 100.0;
};

struct Saw {
    double cents = 0.0;  // from the stack's pitch
    double pan = 0.0;    // -1 (left) .. 1 (right)
    double gain = 0.0;   // its share of the stack's level: the gains' squares add up to 1
};

inline int sawCount(int saws) noexcept { return std::clamp(saws, 1, kMaxSaws); }

// The stack's saws, from the flattest to the sharpest, into out[0..count): the count.
inline int layout(const Shape& shape, Saw* out) noexcept {
    const int n = sawCount(shape.saws);
    const double spread = detuneCents(shape.detune);
    const double exponent = curveExponent(shape.curve);
    const double width = std::clamp(shape.width / 100.0, 0.0, 1.0);
    const double middle = middleWeight(shape.blend), side = sideWeight(shape.blend);
    double power = 0.0;
    for (int i = 0; i < n; ++i) {
        const double u = n == 1 ? 0.0 : -1.0 + 2.0 * i / (n - 1);  // evenly over -1..1
        const double x = std::copysign(std::pow(std::abs(u), exponent), u);
        Saw& saw = out[i];
        saw.cents = x * spread;
        // Mirrored pairs (i, n - 1 - i) go to opposite sides, the outermost (pair 0) widest; the flat
        // one of an even pair to the left, of an odd one to the right. The middle saw stays there.
        const int mirror = n - 1 - i;
        const int pair = std::min(i, mirror);
        const double flatSide = pair % 2 == 0 ? -1.0 : 1.0;
        saw.pan = i == mirror ? 0.0 : width * std::abs(u) * (i < mirror ? flatSide : -flatSide);
        // The middle: the middle saw, or an even stack's middle pair (sharing its weight).
        const bool isMiddle = n % 2 == 1 ? i == n / 2 : (i == n / 2 - 1 || i == n / 2);
        saw.gain = isMiddle ? (n % 2 == 1 ? middle : middle / std::sqrt(2.0)) : side;
        power += saw.gain * saw.gain;
    }
    const double scale = power > 0.0 ? 1.0 / std::sqrt(power) : 0.0;
    for (int i = 0; i < n; ++i) out[i].gain *= scale;
    return n;
}

// --- The band-limited step ------------------------------------------------------------------
//
// A saw jumps once a cycle. Sampled as it is, the jump is a step with every
// harmonic up to infinity, and those above half the rate fold back as
// inharmonic aliases (a plain saw's are 17 to 55 dB down). So around each jump
// the device adds the difference between a band-limited step and the plain one
// (Brandt's BLEP): the integral of a Kaiser-windowed sinc (β 13, ±9 samples),
// flat to a quarter of the oscillators' rate and about 125 dB down from three
// quarters of it, so what it lets through folds back only above 20 kHz.
// The oscillators run at twice the rate (below 88.2 kHz) and a half-band
// decimator takes them down, filtering away what folds there: the saws'
// aliases are more than 130 dB under their fundamental at any pitch.
//
// The table: for a jump `fraction` (0..1) of a sample before sample n, the
// correction at samples n - kStepHalf + k (k = 0..kStepTaps - 1), a row for
// each of kStepPhases + 1 fractions (linear between them: within 3e-7).

inline constexpr int kStepHalf = 9;              // samples a jump's correction reaches either side
inline constexpr int kStepTaps = 2 * kStepHalf;  // the samples it spans
inline constexpr int kStepPhases = 1024;         // fractions tabulated
inline constexpr double kStepBeta = 13.0;

class StepTable {
public:
    StepTable() {
        // The step's integral S at t = -kStepHalf + q / kStepPhases, q = 0 .. 2 kStepHalf kStepPhases,
        // by Simpson's rule over 4 steps a tabulated fraction; then scaled so S ends at exactly 1.
        constexpr int kPoints = kStepTaps * kStepPhases;
        constexpr int kSub = 4;
        const double i0Beta = dsp::besselI0(kStepBeta);
        const auto pulse = [&](double t) {
            const double r = t / kStepHalf;
            if (std::abs(r) >= 1.0) return 0.0;
            const double sinc = t == 0.0 ? 1.0 : std::sin(kPi * t) / (kPi * t);
            return sinc * dsp::besselI0(kStepBeta * std::sqrt(1.0 - r * r)) / i0Beta;
        };
        std::vector<double> integral(static_cast<size_t>(kPoints) + 1, 0.0);
        const double step = 1.0 / (kStepPhases * kSub);
        double sum = 0.0;
        for (int q = 0; q < kPoints; ++q) {
            const double from = -kStepHalf + static_cast<double>(q) / kStepPhases;
            for (int s = 0; s < kSub; ++s) {
                const double a = from + s * step;
                sum += (pulse(a) + 4.0 * pulse(a + 0.5 * step) + pulse(a + step)) * step / 6.0;
            }
            integral[static_cast<size_t>(q) + 1] = sum;
        }
        const double total = integral.back();
        values_.resize(static_cast<size_t>(kStepPhases + 1) * kStepTaps);
        for (int p = 0; p <= kStepPhases; ++p) {
            for (int k = 0; k < kStepTaps; ++k) {
                // Sample n - kStepHalf + k is k - kStepHalf + fraction after the jump; the plain step is 1 from n on.
                const double band = integral[static_cast<size_t>(k * kStepPhases + p)] / total;
                values_[static_cast<size_t>(p * kStepTaps + k)] =
                    static_cast<float>(band - (k >= kStepHalf ? 1.0 : 0.0));
            }
        }
    }

    // Row `phase` (0..kStepPhases): the correction for a jump of 1 phase / kStepPhases of a sample before n.
    const float* row(int phase) const noexcept { return values_.data() + static_cast<size_t>(phase) * kStepTaps; }

    // The correction for a jump of 1 `fraction` (0..1) of a sample before sample n, at its kStepTaps samples
    // (n - kStepHalf ...), interpolated between rows.
    void correction(float fraction, float* out) const noexcept {
        const float at = std::clamp(fraction, 0.f, 1.f) * kStepPhases;
        const int phase = std::min(kStepPhases - 1, static_cast<int>(at));
        const float t = at - static_cast<float>(phase);
        const float* a = row(phase);
        const float* b = a + kStepTaps;
        for (int k = 0; k < kStepTaps; ++k) out[k] = a[k] + t * (b[k] - a[k]);
    }

private:
    std::vector<float> values_;
};

// The table, made on first use.
inline const StepTable& stepTable() {
    static const StepTable table;
    return table;
}

}  // namespace sub::supersaw
