// What the device editors' spectra cost the GUI thread: the FFT (analysis/Fft,
// KFR's) against the radix-2 transform it replaced (kept here), and a display
// tick of each analyzer as the editors run it: EqAnalyzer (the EQ, Hue, Erosion,
// the Saturator: two 8192-point spectra a tick) and FallingSpectrum (the Delay,
// the Reverb, Vital: one of 4096). The application layer alone, no UI.
//
//   display_fft_bench [--rounds 2000] [--rate 60]
//
// --rate: display ticks a second, so each tick brings sampleRate / rate samples.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "analysis/Fft.h"
#include "analysis/Spectrum.h"

namespace an = sub::app::analysis;
using an::Complex;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

// The transform as it was before KFR: radix-2, its twiddles made on every call; rfft as a complex transform.
void radix2(std::vector<Complex>& data) {
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    std::vector<Complex> twiddles(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        const double angle = -2.0 * kPi * double(k) / double(n);
        twiddles[k] = Complex(std::cos(angle), std::sin(angle));
    }
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
}

std::vector<Complex> oldRfft(const double* x, std::size_t n) {
    std::vector<Complex> data(n);
    for (std::size_t i = 0; i < n; ++i)
        data[i] = Complex(x[i], 0.0);
    radix2(data);
    data.resize(n / 2 + 1);
    return data;
}

// Microseconds a call of `run`, the best of five batches of `rounds`.
template <typename Run>
double timeUs(int rounds, Run&& run) {
    double best = 1e300;
    for (int batch = 0; batch < 5; ++batch) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < rounds; ++i)
            run();
        const std::chrono::duration<double, std::micro> took = std::chrono::steady_clock::now() - start;
        best = std::min(best, took.count() / rounds);
    }
    return best;
}

volatile double sink = 0.0;  // (so the work isn't optimised away)

}  // namespace

int main(int argc, char** argv) {
    int rounds = 2000;
    double rate = 60.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
            rounds = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            rate = std::clamp(std::atof(argv[++i]), 1.0, kSampleRate);  // (at least a sample a tick)
        } else {
            std::printf("display_fft_bench [--rounds 2000] [--rate 60]\n");
            return std::strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }

    std::mt19937 random(1);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    std::vector<double> x(16384);
    for (double& value : x)
        value = uniform(random);

    std::printf("%-34s %12s %12s %8s\n", "transform", "radix-2 us", "KFR us", "faster");
    for (const std::size_t n : {std::size_t(1024), std::size_t(4096), std::size_t(8192), std::size_t(16384)}) {
        const double before = timeUs(rounds, [&] { sink = sink + oldRfft(x.data(), n)[1].real(); });
        const double after = timeUs(rounds, [&] { sink = sink + an::rfft(x.data(), n, n)[1].real(); });
        std::printf("rfft %-29zu %12.2f %12.2f %7.1fx\n", n, before, after, before / after);
    }
    for (const std::size_t n : {std::size_t(4096), std::size_t(8192)}) {
        std::vector<Complex> data(n);
        for (std::size_t i = 0; i < n; ++i)
            data[i] = Complex(x[i], x[n - 1 - i]);
        const double before = timeUs(rounds, [&] {
            std::vector<Complex> copy = data;
            radix2(copy);
            sink = sink + copy[1].real();
        });
        const double after = timeUs(rounds, [&] {
            std::vector<Complex> copy = data;
            an::fft(copy);
            sink = sink + copy[1].real();
        });
        std::printf("fft (complex) %-20zu %12.2f %12.2f %7.1fx\n", n, before, after, before / after);
    }

    // A display tick: the samples that came since the last (a tone and some noise), into each analyzer.
    const auto perTick = std::size_t(std::lround(kSampleRate / rate));
    std::vector<float> audio(perTick * 64);
    for (std::size_t i = 0; i < audio.size(); ++i)
        audio[i] = float(0.5 * std::sin(2 * kPi * 440.0 * double(i) / kSampleRate) + 0.05 * uniform(random));
    std::size_t at = 0;
    const auto next = [&] {
        const float* samples = audio.data() + at;
        at = (at + perTick) % (audio.size() - perTick);
        return samples;
    };
    const double seconds = 1.0 / rate;
    an::EqAnalyzer eq;
    const double eqTick = timeUs(std::max(1, rounds / 4), [&] {
        eq.feed(an::EqAnalyzer::Input, next(), perTick, kSampleRate, seconds);
        eq.feed(an::EqAnalyzer::Output, next(), perTick, kSampleRate, seconds);
        sink = sink + eq.levels(an::EqAnalyzer::Input)[10];
    });
    an::FallingSpectrum falling;
    const double fallingTick = timeUs(rounds, [&] {
        falling.add(next(), perTick, kSampleRate, seconds);
        sink = sink + falling.levels()[10];
    });
    std::printf("\nA display tick at %.0f Hz (%zu samples a channel at 48 kHz):\n", rate, perTick);
    std::printf("%-34s %12.1f us  (%.1f%% of a core at %.0f ticks a second)\n", "EqAnalyzer, input and output", eqTick,
                eqTick * rate / 1e4, rate);
    std::printf("%-34s %12.1f us  (%.1f%% of a core)\n", "FallingSpectrum", fallingTick, fallingTick * rate / 1e4);
    return sink == 12345.678 ? 1 : 0;
}
