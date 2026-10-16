// Signalsmith Linear's power-of-two FFTs on KFR's (its platform backend of
// SUBstation's, platform/fft-kfr.h) against the library's own (SimpleFFT, and
// SimpleRealFFT on it): what Signalsmith Stretch's and Spectral's transforms
// rest on. Nanoseconds per transform, float, each size, best of --repeat runs.
//
//     fft_bench [--repeat 5] [--min 64] [--max 16384]
//
// Qt-free: Signalsmith Linear and KFR alone.

#include <algorithm>
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "signalsmith-linear/fft.h"

namespace {

namespace linear = signalsmith::linear;
using Clock = std::chrono::steady_clock;
using Complex = std::complex<float>;

volatile float sink = 0.f;  // (what the transforms make is read: not optimised away)

// Nanoseconds per call of `run`, best of `repeat` runs of about 50 ms each.
template<class Run>
double timePerCall(int repeat, Run&& run) {
    int calls = 1;
    for (;;) {  // calls enough to last ~50 ms
        const auto start = Clock::now();
        for (int i = 0; i < calls; ++i) run();
        if (Clock::now() - start > std::chrono::milliseconds(50)) break;
        calls *= 2;
    }
    double best = 1e300;
    for (int r = 0; r < repeat; ++r) {
        const auto start = Clock::now();
        for (int i = 0; i < calls; ++i) run();
        best = std::min(best, std::chrono::duration<double, std::nano>(Clock::now() - start).count() / calls);
    }
    return best;
}

}  // namespace

int main(int argc, char** argv) {
    int repeat = 5;
    size_t minSize = 64, maxSize = 16384;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&] {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", arg.c_str());
                std::exit(2);
            }
            return std::string(argv[++i]);
        };
        if (arg == "--repeat") repeat = std::max(1, std::stoi(next()));
        else if (arg == "--min") minSize = std::stoul(next());
        else if (arg == "--max") maxSize = std::stoul(next());
        else {
            std::printf("fft_bench [--repeat 5] [--min 64] [--max 16384]\n");
            return arg == "--help" ? 0 : 2;
        }
    }

    std::printf("%-7s  %-28s  %12s  %12s  %8s\n", "size", "transform", "own (ns)", "KFR (ns)", "speed-up");
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> uni(-1.f, 1.f);
    for (size_t n = std::max<size_t>(minSize, 4); n <= maxSize; n *= 2) {
        std::vector<Complex> x(n), y(n);
        for (auto& v : x) v = {uni(rng), uni(rng)};
        std::vector<float> real(n), back(n);
        for (float& v : real) v = uni(rng);
        std::vector<Complex> spectrum(n / 2);

        linear::SimpleFFT<float> ownComplex(n);
        linear::Pow2FFT<float> kfrComplex(n);
        const double ownC = timePerCall(repeat, [&] { ownComplex.fft(x.data(), y.data()); sink = sink + y[1].real(); });
        const double kfrC = timePerCall(repeat, [&] { kfrComplex.fft(x.data(), y.data()); sink = sink + y[1].real(); });
        std::printf("%-7zu  %-28s  %12.0f  %12.0f  %7.1fx\n", n, "complex forward", ownC, kfrC, ownC / kfrC);

        linear::SimpleRealFFT<float, linear::_impl::SimplePow2FFT<float>> ownReal(n);
        linear::Pow2RealFFT<float> kfrReal(n);
        const double ownR = timePerCall(repeat, [&] {
            ownReal.fft(real.data(), spectrum.data());
            ownReal.ifft(spectrum.data(), back.data());
            sink = sink + back[1];
        });
        const double kfrR = timePerCall(repeat, [&] {
            kfrReal.fft(real.data(), spectrum.data());
            kfrReal.ifft(spectrum.data(), back.data());
            sink = sink + back[1];
        });
        std::printf("%-7zu  %-28s  %12.0f  %12.0f  %7.1fx\n", n, "real forward + inverse", ownR, kfrR, ownR / kfrR);
    }
    return 0;
}
