#pragma once
// The Transient Shaper device's maths, as its editor draws it: from the
// engine's own (engine/src/builtin/TransientDesign.h), so the hit drawn is
// shaped by the detector and gain law that shape the audio, and what the
// editor's handles drag stays in the parameters' ranges.

#include <algorithm>
#include <vector>

namespace sub::app {

// A parameter's range, as the device has it.
struct TransientRange {
    double min = 0.0, max = 0.0;
    double clamp(double value) const { return std::clamp(value, min, max); }
};
TransientRange transientAmountRange();      // Attack and Sustain (%)
TransientRange transientAttackTimeRange();  // ms
TransientRange transientReleaseRange();     // ms

// The editor's hit: its level before and after the shaping (dB) and the parts of the gain Attack and
// Sustain make, at each of `timesMs` (rising, from the hit's start).
struct TransientHit {
    std::vector<float> inDb, outDb, attackDb, sustainDb;
};
TransientHit transientHit(double attackPercent, double sustainPercent, double attackTimeMs, double releaseMs,
                          double thresholdDb, const std::vector<double>& timesMs);
double transientHitWindowMs();  // how much of the hit the editor shows
double transientHitPeakDb();    // the hit's peak, unshaped

// The gain's range (dB): the most it turns up, and down.
double transientMaxBoostDb();
double transientMaxCutDb();
// Audio samples per value of the device's displays (128), and their floor (dB).
int transientDisplaySamples();
double transientDisplayFloorDb();

}  // namespace sub::app
