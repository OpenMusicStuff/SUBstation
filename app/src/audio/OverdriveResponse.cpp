#include "audio/OverdriveResponse.h"

#include "builtin/OverdriveDesign.h"

namespace sub::app {

static_assert(kOverdriveMinFrequency == sub::overdrive::kMinFrequency &&
                  kOverdriveMaxFrequency == sub::overdrive::kMaxFrequency,
              "the editor's Frequency range is the device's");
static_assert(kOverdriveMinWidth == sub::overdrive::kMinWidth && kOverdriveMaxWidth == sub::overdrive::kMaxWidth,
              "the editor's Width range is the device's");

QList<double> overdriveBandDb(double frequency, double width, double sampleRate, const QList<double>& frequencies) {
    QList<double> out;
    out.reserve(frequencies.size());
    for (const double f : frequencies) out.append(sub::overdrive::bandResponseDb(f, frequency, width, sampleRate));
    return out;
}

QList<double> overdriveToneDb(double tone, double sampleRate, const QList<double>& frequencies) {
    QList<double> out;
    out.reserve(frequencies.size());
    for (const double f : frequencies) out.append(sub::overdrive::toneResponseDb(f, tone, sampleRate));
    return out;
}

QPair<double, double> overdriveBandEdges(double frequency, double width, double sampleRate) {
    const auto [low, high] = sub::overdrive::bandEdges(frequency, width, sampleRate);
    return {low, high};
}

double overdriveDriveGain(double drive) { return sub::overdrive::driveGain(drive); }

}  // namespace sub::app
