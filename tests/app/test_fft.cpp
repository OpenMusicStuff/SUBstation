// The editors' Fourier transform (analysis/Fft, KFR's): fft, its inverse and
// rfft against the definition (a direct DFT) and against the radix-2 transform
// it replaced (kept here), for every power of two from 1 to 16384; rfft's
// padding and cutting; the Hann window.

#include <QTest>

#include <cmath>
#include <complex>
#include <random>
#include <thread>
#include <vector>

#include "analysis/Fft.h"

namespace an = sub::app::analysis;
using an::Complex;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kLargest = 16384;

// The transform as it was (radix-2, decimation in time, twiddles from cos and sin): the reference.
void radix2(std::vector<Complex>& data, bool inverse) {
    const std::size_t n = data.size();
    if (n < 2)
        return;
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    const double sign = inverse ? 1.0 : -1.0;
    std::vector<Complex> twiddles(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k)
        twiddles[k] = std::polar(1.0, sign * 2.0 * kPi * double(k) / double(n));
    for (std::size_t span = 2; span <= n; span <<= 1) {
        const std::size_t half = span / 2, stride = n / span;
        for (std::size_t start = 0; start < n; start += span) {
            for (std::size_t k = 0; k < half; ++k) {
                const Complex t = twiddles[k * stride] * data[start + k + half];
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
        }
    }
    if (inverse) {
        for (Complex& value : data)
            value /= double(n);
    }
}

// Bin k of the transform of `x`, by its definition.
Complex dft(const std::vector<Complex>& x, std::size_t k, bool inverse = false) {
    const double sign = inverse ? 1.0 : -1.0;
    const std::size_t n = x.size();
    Complex sum;
    for (std::size_t i = 0; i < n; ++i)
        sum += x[i] * std::polar(1.0, sign * 2.0 * kPi * double((k * i) % n) / double(n));
    return inverse ? sum / double(n) : sum;
}

std::vector<Complex> noise(std::size_t n, unsigned seed) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    std::vector<Complex> values(n);
    for (Complex& value : values)
        value = Complex(uniform(random), uniform(random));
    return values;
}

double largest(const std::vector<Complex>& values) {
    double peak = 0.0;
    for (const Complex& value : values)
        peak = std::max(peak, std::abs(value));
    return std::max(peak, 1e-300);
}

// Whether `found` is `expected` to within 1e-9 of the largest value (each bin's error relative to the whole).
bool close(const std::vector<Complex>& found, const std::vector<Complex>& expected, double tolerance = 1e-9) {
    if (found.size() != expected.size())
        return false;
    const double scale = largest(expected);
    for (std::size_t k = 0; k < found.size(); ++k) {
        if (std::abs(found[k] - expected[k]) > tolerance * scale)
            return false;
    }
    return true;
}

}  // namespace

class TestFft : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fftIsTheDefinition() {
        for (std::size_t n = 1; n <= kLargest; n *= 2) {
            const std::vector<Complex> x = noise(n, unsigned(n));
            std::vector<Complex> forward = x, backward = x;
            an::fft(forward);
            an::fft(backward, true);
            std::vector<Complex> reference = x, inverseReference = x;
            radix2(reference, false);
            radix2(inverseReference, true);
            QVERIFY2(close(forward, reference), qPrintable(QStringLiteral("fft, n = %1").arg(n)));
            QVERIFY2(close(backward, inverseReference), qPrintable(QStringLiteral("ifft, n = %1").arg(n)));
            // The definition itself: every bin up to 1024 points, a few spread across the larger.
            const std::size_t step = n <= 1024 ? 1 : n / 61 + 1;
            const double scale = largest(reference), inverseScale = largest(inverseReference);
            for (std::size_t k = 0; k < n; k += step) {
                QVERIFY2(std::abs(forward[k] - dft(x, k)) < 1e-9 * scale, qPrintable(QStringLiteral("n = %1").arg(n)));
                QVERIFY(std::abs(backward[k] - dft(x, k, true)) < 1e-9 * inverseScale);
            }
            // And back again.
            an::fft(forward, true);
            QVERIFY2(close(forward, x, 1e-12), qPrintable(QStringLiteral("round trip, n = %1").arg(n)));
        }
    }

    void rfftIsTheFirstHalfOfTheTransform() {
        for (std::size_t n = 1; n <= kLargest; n *= 2) {
            std::vector<double> x(n);
            std::mt19937 random(unsigned(n) + 7);
            std::uniform_real_distribution<double> uniform(-1.0, 1.0);
            for (double& value : x)
                value = uniform(random);
            std::vector<Complex> full(x.begin(), x.end());
            radix2(full, false);
            full.resize(n / 2 + 1);
            const std::vector<Complex> bins = an::rfft(x.data(), x.size(), n);
            QCOMPARE(bins.size(), n / 2 + 1);
            QVERIFY2(close(bins, full), qPrintable(QStringLiteral("rfft, n = %1").arg(n)));
            // (numpy's for one value: itself)
            if (n == 1)
                QCOMPARE(bins[0], Complex(x[0], 0.0));
        }
    }

    void rfftPadsAndCuts() {
        const std::vector<double> x{1.0, 2.0, -1.0, 0.5, 0.25, 0.0, 3.0, -2.0, 4.0, 5.0};
        for (const std::size_t n : {std::size_t(4), std::size_t(8), std::size_t(16), std::size_t(64)}) {
            for (const std::size_t count : {std::size_t(0), std::size_t(3), std::size_t(8), x.size()}) {
                std::vector<Complex> padded(n);
                for (std::size_t i = 0; i < std::min(count, n); ++i)
                    padded[i] = x[i];
                const std::vector<Complex> bins = an::rfft(x.data(), count, n);
                QCOMPARE(bins.size(), n / 2 + 1);
                for (std::size_t k = 0; k < bins.size(); ++k)
                    QVERIFY(std::abs(bins[k] - dft(padded, k)) < 1e-12);
            }
        }
    }

    // Each thread keeps its own plans: transforms on several at once agree with one alone.
    void threadsKeepTheirOwnPlans() {
        const std::vector<Complex> x = noise(4096, 3);
        std::vector<Complex> alone = x;
        an::fft(alone);
        std::vector<std::vector<Complex>> results(4, x);
        std::vector<std::thread> threads;
        for (std::vector<Complex>& result : results) {
            threads.emplace_back([&result] {
                for (int i = 0; i < 20; ++i) {
                    std::vector<Complex> data = result;
                    an::fft(data);
                    if (i == 19)
                        result = data;
                }
            });
        }
        for (std::thread& thread : threads)
            thread.join();
        for (const std::vector<Complex>& result : results)
            QVERIFY(result == alone);
    }

    void hannIsNumpys() {
        const std::vector<double> window = an::hann(5);
        QCOMPARE(window.size(), std::size_t(5));
        QVERIFY(std::abs(window[0]) < 1e-15 && std::abs(window[1] - 0.5) < 1e-15 && std::abs(window[2] - 1.0) < 1e-15);
        QVERIFY(std::abs(window[3] - 0.5) < 1e-15 && std::abs(window[4]) < 1e-15);
        QCOMPARE(an::hann(1), std::vector<double>{1.0});
        QVERIFY(an::hann(0).empty());
        QVERIFY(an::isPowerOfTwo(1) && an::isPowerOfTwo(16384) && !an::isPowerOfTwo(0) && !an::isPowerOfTwo(12));
    }
};

QTEST_GUILESS_MAIN(TestFft)
#include "test_fft.moc"
