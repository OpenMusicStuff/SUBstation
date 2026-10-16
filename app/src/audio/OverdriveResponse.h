#pragma once
// The Overdrive's band and Tone, as its editor draws them: worked out by
// the engine's own maths (engine/src/builtin/OverdriveDesign.h), so the band
// drawn is the filter that plays.

#include <QList>
#include <QPair>

namespace sub::app {

// Frequency's and Width's ranges (Hz, octaves): the device's (checked against the engine's in
// OverdriveResponse.cpp), for the editor's drags.
inline constexpr double kOverdriveMinFrequency = 50.0;
inline constexpr double kOverdriveMaxFrequency = 20000.0;
inline constexpr double kOverdriveMinWidth = 0.5;
inline constexpr double kOverdriveMaxWidth = 9.0;

// The band-pass's response (dB, floored at -120) at each of `frequencies` (Hz), at `sampleRate`.
QList<double> overdriveBandDb(double frequency, double width, double sampleRate, const QList<double>& frequencies);
// Tone's low-pass's, the same way.
QList<double> overdriveToneDb(double tone, double sampleRate, const QList<double>& frequencies);
// The band's -3 dB edges (Hz).
QPair<double, double> overdriveBandEdges(double frequency, double width, double sampleRate);
// Drive's gain (linear): how far the band's level is pushed into the curve.
double overdriveDriveGain(double drive);

}  // namespace sub::app
