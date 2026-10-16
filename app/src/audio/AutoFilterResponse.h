#pragma once
// The Auto Filter's response and modulation, as its editor draws them: worked
// out by the engine's own maths (engine/src/builtin/AutoFilterDesign.h), so the
// curve drawn is the filter that plays and the cutoff the modulation reaches is
// the one the engine reaches.

#include <QList>
#include <QStringList>

namespace sub::app {

// Frequency's and Resonance's ranges, and how far the envelope and the LFO
// sweep at their most (octaves): the device's (checked against the engine's in
// AutoFilterResponse.cpp), for the editor's drags and its sweep band.
inline constexpr double kAutoFilterMinFrequency = 20.0;
inline constexpr double kAutoFilterMaxFrequency = 20000.0;
inline constexpr double kAutoFilterMaxResonance = 125.0;
inline constexpr double kAutoFilterEnvelopeOctaves = 6.0;
inline constexpr double kAutoFilterLfoOctaves = 4.0;

// The filter as it stands: its type (0 Lowpass, 1 Highpass, 2 Bandpass, 3 Notch, 4 Morph), slope (0: 12 dB,
// 1: 24 dB), cutoff (Hz), Resonance (%) and Morph (%).
struct AutoFilterCurve {
    int type = 0;
    int slope = 0;
    double cutoffHz = 1000.0;
    double resonance = 0.0;
    double morph = 0.0;
};

// The response in dB (floored at -120) at each of `frequencies` (Hz), at `sampleRate`.
QList<double> autoFilterResponseDb(const AutoFilterCurve& curve, double sampleRate, const QList<double>& frequencies);

// The cutoff Frequency moved by `octaves` reaches (held to the range and below 0.45 of the rate).
double autoFilterCutoffHz(double frequency, double octaves, double sampleRate);

// The LFO's value (-1..1) of waveform `wave` (the parameter's index) at `phase` (0..1) of cycle `cycle`.
double autoFilterLfoValue(int wave, double phase, unsigned cycle = 0);

// Whether the circuit (its index) hears Drive (Clean doesn't).
bool autoFilterHasDrive(int circuit);

}  // namespace sub::app
