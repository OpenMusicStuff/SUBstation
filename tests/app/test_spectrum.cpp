// The editors' analyzers (analysis/Spectrum) by the time: FallingSpectrum and
// EqAnalyzer hold through a gap between blocks of audio and fall back as fast a
// second however often the displays refresh (60 times a second or 120, 144,
// 240), as they did at 60.

#include <QTest>

#include <algorithm>
#include <cmath>
#include <vector>

#include "analysis/Spectrum.h"

namespace an = sub::app::analysis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> tone(double hz, std::size_t count) {
    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i)
        samples[i] = float(0.5 * std::sin(2 * kPi * hz * double(i) / kRate));
    return samples;
}

float loudest(const std::vector<float>& levels) { return *std::max_element(levels.begin(), levels.end()); }

// A tone, then `seconds` without samples, refreshed `hz` times a second: the loudest level then.
double fallingAfter(double seconds, double hz) {
    an::FallingSpectrum spectrum;
    const std::vector<float> samples = tone(1000.0, an::FallingSpectrum::kFftSize);
    spectrum.add(samples.data(), samples.size(), kRate, 1.0 / hz);
    for (int i = 0; i < int(std::lround(seconds * hz)); ++i)
        spectrum.add(nullptr, 0, kRate, 1.0 / hz);
    return loudest(spectrum.levels());
}

double eqAfter(double seconds, double hz) {
    an::EqAnalyzer analyzer;
    const std::vector<float> samples = tone(1000.0, an::EqAnalyzer::kFftSize);
    for (int i = 0; i < 8; ++i)  // (up to the tone's level)
        analyzer.feed(an::EqAnalyzer::Input, samples.data(), samples.size(), kRate, 1.0 / hz);
    // (Whole refreshes, then what is left of `seconds`: the same time at any rate.)
    const int ticks = int(std::floor(seconds * hz + 1e-9));
    for (int i = 0; i < ticks; ++i)
        analyzer.feed(an::EqAnalyzer::Input, nullptr, 0, kRate, 1.0 / hz);
    if (seconds - ticks / hz > 1e-9)
        analyzer.feed(an::EqAnalyzer::Input, nullptr, 0, kRate, seconds - ticks / hz);
    return loudest(analyzer.levels(an::EqAnalyzer::Input));
}

}  // namespace

class TestSpectrum : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fallingSpectrumHoldsThenFallsBySeconds() {
        const double tick = 0.016;  // (a 16 ms refresh, as at 60 Hz)
        const double peak = fallingAfter(0.0, 1.0 / tick);
        QVERIFY(std::abs(peak - 20 * std::log10(0.5)) < 1.0);
        // Held for 0.24 s (15 refreshes of 16 ms), then 1 dB a 16 ms refresh: 62.5 dB a second.
        QVERIFY(std::abs(fallingAfter(0.24, 1.0 / tick) - peak) < 1e-4);
        QVERIFY(std::abs(fallingAfter(0.256, 1.0 / tick) - (peak - 1.0)) < 1e-4);
        QVERIFY(std::abs(fallingAfter(0.4, 1.0 / tick) - (peak - 10.0)) < 1e-4);
    }

    void fallingSpectrumFallsAsFastAtAnyRate() {
        const double peak = fallingAfter(0.0, 62.5);
        for (const double hz : {30.0, 62.5, 120.0, 144.0, 240.0}) {
            for (const double seconds : {0.1, 0.2, 0.25, 0.4, 0.6, 1.0}) {
                // The refreshes' time (whole refreshes), past the hold: 62.5 dB a second of it.
                const double elapsed = double(std::lround(seconds * hz)) / hz;
                const double expected = peak - an::FallingSpectrum::kFallDbPerSecond * std::max(0.0, elapsed - an::kHoldSeconds);
                const double found = fallingAfter(seconds, hz);
                QVERIFY2(std::abs(found - expected) < 1e-3,
                         qPrintable(QStringLiteral("%1 Hz, %2 s: %3 dB, not %4").arg(hz).arg(seconds).arg(found).arg(expected)));
            }
        }
        // As far at 60, 120 and 240 refreshes a second (whole refreshes at each).
        QVERIFY(std::abs(fallingAfter(0.4, 62.5) - fallingAfter(0.4, 125.0)) < 1e-3);
        QVERIFY(std::abs(fallingAfter(0.4, 62.5) - fallingAfter(0.4, 250.0)) < 1e-3);
    }

    void eqAnalyzerFallsAsFastAtAnyRate() {
        const double peak = eqAfter(0.0, 60.0);
        QVERIFY(peak > -20.0);
        QVERIFY(std::abs(eqAfter(0.24, 62.5) - peak) < 1e-4);  // held through a gap
        QVERIFY(eqAfter(0.6, 62.5) < peak - 10.0);             // then falling
        // As fast at 120, 144 and 240 refreshes a second as at 60, within what easing in steps differs by (under a
        // tenth of the way fallen, early on: the level follows a target that falls within each step).
        for (const double hz : {120.0, 144.0, 240.0}) {
            for (const double seconds : {0.4, 0.6, 1.0}) {
                const double at60 = eqAfter(seconds, 62.5), atRate = eqAfter(seconds, hz);
                QVERIFY2(std::abs(at60 - atRate) < 0.08 * (peak - at60) + 0.1,
                         qPrintable(QStringLiteral("%1 Hz, %2 s: %3 dB against %4").arg(hz).arg(seconds).arg(atRate).arg(at60)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestSpectrum)
#include "test_spectrum.moc"
