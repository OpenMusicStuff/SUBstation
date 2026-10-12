// The built-in Vital's FFT (third_party/vital/compat/kfr_fourier_transform.cpp):
// in the layout Vital's IPP build has, against a direct DFT, and one transform
// shared by threads at once, as voices on parallel tracks share it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include "fourier_transform.h"
#include "harness/Test.h"

namespace {

constexpr int kBits = 11;  // (Vital's wavetables': 2048 samples)
constexpr int kSize = 1 << kBits;

// 2N floats (the transforms' buffers), 16 bytes past a 64-byte boundary: as Vital's.
struct Buffer {
    std::vector<float> storage = std::vector<float>(2 * kSize + 32);
    float* data() {
        auto p = reinterpret_cast<std::uintptr_t>(storage.data());
        return reinterpret_cast<float*>((p + 63) & ~std::uintptr_t(63)) + 4;
    }
};

std::vector<float> randomSignal(unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni(-1.f, 1.f);
    std::vector<float> x(kSize);
    for (float& v : x) v = uni(rng);
    return x;
}

// Bins 0..N/2 interleaved (as the forward transform gives them), falling off as 1/k.
std::vector<float> randomSpectrum(unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni(-1.f, 1.f);
    std::vector<float> s(kSize + 2, 0.f);
    s[0] = 0.25f;
    for (int k = 1; k < kSize / 2; ++k) {
        s[2 * k] = uni(rng) / k;
        s[2 * k + 1] = uni(rng) / k;
    }
    s[kSize] = 0.01f;  // Nyquist
    return s;
}

// The inverse Vital's layout means: Re(DC) + Re(Nyquist)(-1)^n + 2 sum (Re cos - Im sin), over N.
std::vector<double> referenceInverse(const std::vector<float>& s) {
    std::vector<double> x(kSize);
    for (int n = 0; n < kSize; ++n) {
        double v = s[0] + s[kSize] * ((n & 1) ? -1.0 : 1.0);
        for (int k = 1; k < kSize / 2; ++k) {
            const double a = 2.0 * M_PI * double(k) * n / kSize;
            v += 2.0 * (s[2 * k] * std::cos(a) - s[2 * k + 1] * std::sin(a));
        }
        x[n] = v / kSize;
    }
    return x;
}

double peakOf(const std::vector<double>& x) {
    double p = 0.0;
    for (double v : x) p = std::max(p, std::abs(v));
    return p;
}

}  // namespace

TEST_CASE("vital fft: forward, Vital's layout, as a direct DFT") {
    const std::vector<float> signal = randomSignal(1);
    Buffer buffer;
    float* data = buffer.data();
    std::fill(data, data + 2 * kSize, 0.f);
    std::copy(signal.begin(), signal.end(), data);
    vital::FFT<kBits>::transform()->transformRealForward(data);

    std::vector<double> reference(kSize + 2);
    for (int k = 0; k <= kSize / 2; ++k) {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < kSize; ++n) {
            const double a = 2.0 * M_PI * double(k) * n / kSize;
            re += signal[n] * std::cos(a);
            im -= signal[n] * std::sin(a);
        }
        reference[2 * k] = re;
        reference[2 * k + 1] = (k == 0 || k == kSize / 2) ? 0.0 : im;
    }
    const double peak = peakOf(reference);
    double error = 0.0;
    for (int i = 0; i < kSize + 2; ++i) error = std::max(error, std::abs(reference[i] - data[i]));
    CHECK(error / peak < 1e-5);
    CHECK(data[1] == 0.f);          // DC's imaginary part
    CHECK(data[kSize + 1] == 0.f);  // Nyquist's
}

TEST_CASE("vital fft: inverse, Vital's layout, scaled, the upper half zeroed") {
    const std::vector<float> spectrum = randomSpectrum(2);
    const std::vector<double> reference = referenceInverse(spectrum);
    Buffer buffer;
    float* data = buffer.data();
    std::copy(spectrum.begin(), spectrum.end(), data);
    data[1] = 7.f;           // ignored: DC's imaginary part,
    data[kSize + 1] = -3.f;  // Nyquist's,
    std::fill(data + kSize + 2, data + 2 * kSize, 5.f);  // and what lies beyond
    vital::FFT<kBits>::transform()->transformRealInverse(data);

    double error = 0.0;
    for (int i = 0; i < kSize; ++i) error = std::max(error, std::abs(reference[i] - data[i]));
    CHECK(error / peakOf(reference) < 1e-5);
    bool zeroed = true;
    for (int i = kSize; i < 2 * kSize; ++i) zeroed = zeroed && data[i] == 0.f;
    CHECK(zeroed);
}

TEST_CASE("vital fft: a forward and an inverse give the signal back") {
    const std::vector<float> signal = randomSignal(3);
    Buffer buffer;
    float* data = buffer.data();
    std::fill(data, data + 2 * kSize, 0.f);
    std::copy(signal.begin(), signal.end(), data);
    vital::FourierTransform* transform = vital::FFT<kBits>::transform();
    transform->transformRealForward(data);
    transform->transformRealInverse(data);
    double error = 0.0;
    for (int i = 0; i < kSize; ++i) error = std::max(error, double(std::abs(signal[i] - data[i])));
    CHECK(error < 1e-5);
}

TEST_CASE("vital fft: one transform, used by threads at once") {
    const std::vector<float> spectrum = randomSpectrum(4);
    const std::vector<double> reference = referenceInverse(spectrum);
    const double peak = peakOf(reference);
    vital::FourierTransform* transform = vital::FFT<kBits>::transform();
    std::vector<int> wrong(4, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            Buffer buffer;
            float* data = buffer.data();
            for (int round = 0; round < 300; ++round) {
                std::copy(spectrum.begin(), spectrum.end(), data);
                transform->transformRealInverse(data);
                double error = 0.0;
                for (int i = 0; i < kSize; ++i) error = std::max(error, std::abs(reference[i] - data[i]));
                if (error / peak >= 1e-5) ++wrong[static_cast<size_t>(t)];
            }
        });
    }
    for (std::thread& thread : threads) thread.join();
    for (int t = 0; t < 4; ++t) CHECK(wrong[static_cast<size_t>(t)] == 0);
}
