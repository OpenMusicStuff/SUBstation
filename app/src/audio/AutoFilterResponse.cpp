#include "audio/AutoFilterResponse.h"

#include "builtin/AutoFilterDesign.h"
#include "builtin/DspBlocks.h"

#include <algorithm>
#include <cmath>

namespace sub::app {

static_assert(kAutoFilterMinFrequency == sub::autofilter::kMinFrequency &&
                  kAutoFilterMaxFrequency == sub::autofilter::kMaxFrequency,
              "the editor's Frequency range is the device's");
static_assert(kAutoFilterMaxResonance == sub::autofilter::kMaxResonance, "the editor's Resonance is the device's");
static_assert(kAutoFilterEnvelopeOctaves == sub::autofilter::kEnvelopeOctaves &&
                  kAutoFilterLfoOctaves == sub::autofilter::kLfoOctaves,
              "the editor's sweep is the device's");

QList<double> autoFilterResponseDb(const AutoFilterCurve& curve, double sampleRate, const QList<double>& frequencies) {
    const auto type = static_cast<sub::autofilter::Type>(std::clamp(curve.type, 0, 4));
    const sub::autofilter::Weights weights = sub::autofilter::weights(type, curve.morph);
    QList<double> out;
    out.reserve(frequencies.size());
    for (const double f : frequencies) {
        out.append(sub::autofilter::responseDb(f, curve.cutoffHz, curve.resonance, curve.slope == 1, weights,
                                               sampleRate));
    }
    return out;
}

double autoFilterCutoffHz(double frequency, double octaves, double sampleRate) {
    return sub::autofilter::cutoffHz(frequency, octaves, sampleRate);
}

double autoFilterLfoValue(int wave, double phase, unsigned cycle) {
    const auto shape = static_cast<sub::dsp::LfoShape>(std::clamp(wave, 0, 6));
    return sub::dsp::Lfo::shape(shape, phase - std::floor(phase), cycle);
}

bool autoFilterHasDrive(int circuit) {
    return sub::autofilter::hasDrive(static_cast<sub::autofilter::Circuit>(std::clamp(circuit, 0, 4)));
}

}  // namespace sub::app
