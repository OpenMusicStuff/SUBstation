#pragma once
// The Vocoder device's bands, as its editor draws them: worked out from the engine's own layout
// (engine/src/builtin/VocoderDesign.h), so the bands drawn are the filters that play.

#include <QList>

namespace sub::app {

struct VocoderBands {
    QList<double> centres;  // Hz, the modulator's, lowest first
    QList<double> gainsDb;  // each band's level (Retro tilts them)
};

// The bands for the Bands choice `bandsChoice` (0: 4 bands .. 9: 40) from `low` to `high` Hz, `bandwidth` wide
// (1: the spacing between centres), Precise or Retro.
VocoderBands vocoderBands(int bandsChoice, double low, double high, double bandwidth, bool retro);

// Band `band`'s response in dB at each of `frequencies` (Hz), its centre moved by `shiftSemitones` (Formant, for
// the carrier's), at `sampleRate`.
QList<double> vocoderBandResponseDb(int bandsChoice, double low, double high, double bandwidth, bool retro, int band,
                                    double shiftSemitones, double sampleRate, const QList<double>& frequencies);

}  // namespace sub::app
