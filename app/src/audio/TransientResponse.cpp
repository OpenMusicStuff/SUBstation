#include "audio/TransientResponse.h"

#include "builtin/TransientDesign.h"

namespace sub::app {

TransientRange transientAmountRange() { return {sub::transient::kAmountMin, sub::transient::kAmountMax}; }

TransientRange transientAttackTimeRange() {
    return {sub::transient::kAttackTimeMinMs, sub::transient::kAttackTimeMaxMs};
}

TransientRange transientReleaseRange() { return {sub::transient::kReleaseMinMs, sub::transient::kReleaseMaxMs}; }

TransientHit transientHit(double attackPercent, double sustainPercent, double attackTimeMs, double releaseMs,
                          double thresholdDb, const std::vector<double>& timesMs) {
    TransientHit hit;
    const size_t n = timesMs.size();
    for (auto* v : {&hit.inDb, &hit.outDb, &hit.attackDb, &hit.sustainDb})
        v->resize(n);
    sub::transient::hitResponse(static_cast<float>(attackPercent), static_cast<float>(sustainPercent),
                                static_cast<float>(attackTimeMs), static_cast<float>(releaseMs),
                                static_cast<float>(thresholdDb), timesMs.data(), static_cast<int>(n),
                                hit.inDb.data(), hit.outDb.data(), hit.attackDb.data(), hit.sustainDb.data());
    return hit;
}

double transientHitWindowMs() { return sub::transient::kHitWindowMs; }

double transientHitPeakDb() { return sub::transient::kHitPeakDb; }

double transientMaxBoostDb() { return sub::transient::kMaxBoostDb; }

double transientMaxCutDb() { return sub::transient::kMaxCutDb; }

int transientDisplaySamples() { return sub::transient::kDisplaySamples; }

double transientDisplayFloorDb() { return sub::transient::kDisplayFloorDb; }

}  // namespace sub::app
