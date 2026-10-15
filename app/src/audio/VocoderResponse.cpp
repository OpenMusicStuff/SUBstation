#include "audio/VocoderResponse.h"

#include "builtin/VocoderDesign.h"

namespace sub::app {

VocoderBands vocoderBands(int bandsChoice, double low, double high, double bandwidth, bool retro) {
    const sub::vocoder::Layout l = sub::vocoder::layout(sub::vocoder::bandCount(bandsChoice), low, high, bandwidth, retro);
    VocoderBands out;
    for (int k = 0; k < l.count; ++k) {
        out.centres.append(l.bands[size_t(k)].centre);
        out.gainsDb.append(l.bands[size_t(k)].gainDb);
    }
    return out;
}

QList<double> vocoderBandResponseDb(int bandsChoice, double low, double high, double bandwidth, bool retro, int band,
                                    double shiftSemitones, double sampleRate, const QList<double>& frequencies) {
    const sub::vocoder::Layout l = sub::vocoder::layout(sub::vocoder::bandCount(bandsChoice), low, high, bandwidth, retro);
    QList<double> out;
    out.reserve(frequencies.size());
    if (band < 0 || band >= l.count || !(sampleRate > 0.0)) return out;
    for (const double f : frequencies)
        out.append(sub::vocoder::bandResponseDb(l, band, shiftSemitones, std::min(f, 0.499 * sampleRate), sampleRate));
    return out;
}

}  // namespace sub::app
