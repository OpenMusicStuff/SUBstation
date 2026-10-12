#pragma once
// The Transient Shaper's maths, shared by the device (builtin/devices/Transient.cpp)
// and the application layer's TransientResponse.h, so the hit its editor draws is
// shaped by the same detector and gain law the audio is.
//
// The level it follows is the input's envelope: the magnitude of its analytic
// signal (a Hilbert pair of all-pass chains), which is smooth on a steady tone
// of any pitch where a rectified signal ripples at twice its frequency, so a
// bass note doesn't make the gain chatter. Three followers ride on it:
//
// - fast: rises at once (kFastAttackMs) and falls over kFastReleaseMs;
// - slow: rises over Attack Time, falls as the fast one does, so it lags only
//   while the level rises. fast / slow (in dB) is the transient: 0 dB on
//   anything steady or falling, up to the onset's whole rise at a hit;
// - held: rises as the fast one does and falls over Release, so it stays up
//   while the level falls. held / fast (in dB) is the sustain: 0 dB while the
//   level holds or rises, growing as a tail decays faster than Release.
//
// The gain in dB is Attack (as a fraction of ±100 %) times the transient plus
// Sustain times the sustain, faded out below the threshold (by the held level:
// over kKneeDb under it) and held to -kMaxCutDb..kMaxBoostDb. So Attack at
// +100 % doubles an onset's rise in dB, at -100 % slows it to Attack Time's;
// Sustain at +100 % lets a tail fall only as fast as Release, at -100 % makes
// it fall twice as fast in dB.

#include <algorithm>
#include <cmath>

#include "builtin/DspBlocks.h"
#include "rt/RtUtils.h"

namespace sub::transient {

// --- The parameters' ranges --------------------------------------------------------------

inline constexpr float kAmountMin = -100.f;  // Attack and Sustain, %
inline constexpr float kAmountMax = 100.f;
inline constexpr float kAttackTimeMinMs = 1.f;
inline constexpr float kAttackTimeMaxMs = 100.f;
inline constexpr float kReleaseMinMs = 20.f;
inline constexpr float kReleaseMaxMs = 2000.f;
inline constexpr float kThresholdMinDb = -80.f;
inline constexpr float kThresholdMaxDb = 0.f;
inline constexpr float kOutputMinDb = -24.f;
inline constexpr float kOutputMaxDb = 24.f;

// --- The detector and the gain -----------------------------------------------------------

inline constexpr float kFastAttackMs = 0.15f;  // the fast and held followers' rise
inline constexpr float kFastReleaseMs = 20.f;  // the fast and slow followers' fall
inline constexpr float kKneeDb = 12.f;         // below the threshold, shaping fades out over this
inline constexpr float kMaxBoostDb = 24.f;
inline constexpr float kMaxCutDb = 30.f;
// Below this the followers are taken as silence (-200 dB): their ratios are then 0 dB.
inline constexpr float kSilence = 1e-10f;

// The analytic signal's magnitude: two chains of four second-order all-passes whose outputs are
// 90° apart (within a degree) from about 0.001 to 0.499 of the sample rate (Olli Niemitalo's
// design), the first a sample later. On a steady tone from 40 Hz to 20 kHz (at 48 kHz) it holds
// within 0.06 dB. The input's own phase is never heard: only the envelope.
class Hilbert {
public:
    void reset() noexcept {
        for (Section& s : a_) s = {};
        for (Section& s : b_) s = {};
        delayed_ = 0.f;
    }
    float magnitude(float x) noexcept {
        float re = x, im = x;
        for (int k = 0; k < 4; ++k) re = a_[k].tick(re, kA[k] * kA[k]);
        for (int k = 0; k < 4; ++k) im = b_[k].tick(im, kB[k] * kB[k]);
        const float real = delayed_;
        delayed_ = re;
        return std::sqrt(real * real + im * im);
    }

private:
    // The design's coefficients; each section's is the square of one (the products fold at compile time).
    static constexpr float kA[4] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
    static constexpr float kB[4] = {0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};
    struct Section {
        float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
        float tick(float x, float c) noexcept {
            const float y = c * (x + y2) - x2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    Section a_[4], b_[4];
    float delayed_ = 0.f;
};

// What the followers make of the envelope, in dB: the transient (fast over slow, ≥ 0), the
// sustain (held over fast, ≥ 0) and the held level (what the threshold compares).
struct Levels {
    float transientDb = 0.f;
    float sustainDb = 0.f;
    float heldDb = -200.f;
};

class Detector {
public:
    void setTimes(float attackTimeMs, float releaseMs, double sampleRate) noexcept {
        fast_.setTimes(kFastAttackMs, kFastReleaseMs, sampleRate);
        slow_.setTimes(attackTimeMs, kFastReleaseMs, sampleRate);
        held_.setTimes(kFastAttackMs, releaseMs, sampleRate);
    }
    void reset() noexcept {
        fast_.reset();
        slow_.reset();
        held_.reset();
    }
    Levels step(float envelope) noexcept {
        const float fast = std::max(fast_.peak(envelope), kSilence);
        const float slow = std::max(slow_.peak(envelope), kSilence);
        const float held = std::max(held_.peak(envelope), kSilence);
        return {std::max(0.f, gainToDb(fast / slow)), std::max(0.f, gainToDb(held / fast)), gainToDb(held)};
    }

private:
    dsp::EnvelopeFollower fast_, slow_, held_;
};

// How much of the shaping applies at a held level: all of it at or above the threshold, none
// kKneeDb below it, and an S-curve between.
inline float weight(float heldDb, float thresholdDb) noexcept {
    const float t = std::clamp((heldDb - thresholdDb) / kKneeDb + 1.f, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// The gain in dB and the parts of it Attack and Sustain make (scaled down together where the
// whole is held to its range, so they always add up to it).
struct Gain {
    float attackDb = 0.f;
    float sustainDb = 0.f;
    float totalDb() const noexcept { return attackDb + sustainDb; }
};

inline Gain gain(float attackPercent, float sustainPercent, const Levels& levels, float weight) noexcept {
    Gain g{0.01f * attackPercent * levels.transientDb * weight, 0.01f * sustainPercent * levels.sustainDb * weight};
    const float total = g.totalDb();
    const float held = std::clamp(total, -kMaxCutDb, kMaxBoostDb);
    if (held != total) {
        const float scale = held / total;
        g.attackDb *= scale;
        g.sustainDb *= scale;
    }
    return g;
}

// --- The output --------------------------------------------------------------------------

// Clip: what is above kClipKnee bends smoothly towards full scale (a tanh with the line's
// slope at the knee); what is below passes untouched.
inline constexpr float kClipKnee = 0.5f;
inline float softClip(float x) noexcept {
    const float a = std::abs(x);
    if (a <= kClipKnee) return x;
    const float bent = kClipKnee + (1.f - kClipKnee) * dsp::fastTanh((a - kClipKnee) / (1.f - kClipKnee));
    return x < 0.f ? -bent : bent;
}

// Lookahead (latency): the audio is delayed while the detector hears it as it comes.
inline constexpr int kLookaheads = 3;
inline constexpr float kLookaheadMs[kLookaheads] = {0.f, 1.f, 5.f};
inline int lookaheadSamples(int choice, double sampleRate) noexcept {
    return static_cast<int>(std::lround(kLookaheadMs[std::clamp(choice, 0, kLookaheads - 1)] * 0.001 * sampleRate));
}

// --- The displays ------------------------------------------------------------------------

inline constexpr int kDisplaySamples = 128;  // audio per value, all five streams pushed together
inline constexpr float kDisplayFloorDb = -90.f;

// --- The editor's picture ----------------------------------------------------------------

// A drum hit as the editor draws one: a linear rise to kHitPeakDb over kHitRiseMs, then an
// exponential decay of 60 dB over kHitDecayMs.
inline constexpr double kHitPeakDb = -6.0;
inline constexpr double kHitRiseMs = 1.0;
inline constexpr double kHitDecayMs = 450.0;
inline constexpr double kHitWindowMs = 600.0;  // what the editor shows of it
inline constexpr double kHitRate = 24000.0;    // the rate it is worked out at

// The hit's envelope `ms` after it starts (linear).
inline double hitEnvelope(double ms) noexcept {
    const double peak = std::pow(10.0, kHitPeakDb / 20.0);
    if (ms <= 0.0) return 0.0;
    if (ms < kHitRiseMs) return peak * ms / kHitRiseMs;
    return peak * std::exp((ms - kHitRiseMs) / kHitDecayMs * std::log(1e-3));
}

// The hit through the detector and the gain at these settings, at `count` times (ms, rising):
// its level before (`inDb`) and after (`outDb`), and the parts of the gain (`attackDb`,
// `sustainDb`; any of the four may be null). Starts from silence, as the device does.
inline void hitResponse(float attackPercent, float sustainPercent, float attackTimeMs, float releaseMs,
                        float thresholdDb, const double* timesMs, int count, float* inDb, float* outDb,
                        float* attackDb, float* sustainDb) noexcept {
    Detector detector;
    detector.setTimes(attackTimeMs, releaseMs, kHitRate);
    const double msPerStep = 1000.0 / kHitRate;
    int step = 0;
    Gain g;
    double envelope = 0.0;
    for (int i = 0; i < count; ++i) {
        // On to the step at or after this time (the levels are what the step there makes).
        while (static_cast<double>(step) * msPerStep <= timesMs[i]) {
            envelope = hitEnvelope(static_cast<double>(step) * msPerStep);
            const Levels levels = detector.step(static_cast<float>(envelope));
            g = gain(attackPercent, sustainPercent, levels, weight(levels.heldDb, thresholdDb));
            ++step;
        }
        const float level = gainToDb(static_cast<float>(envelope));
        if (inDb) inDb[i] = level;
        if (outDb) outDb[i] = level + g.totalDb();
        if (attackDb) attackDb[i] = g.attackDb;
        if (sustainDb) sustainDb[i] = g.sustainDb;
    }
}

}  // namespace sub::transient
