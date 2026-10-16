// Signalsmith Linear's FFTs on KFR's (third_party/signalsmith-linear/
// signalsmith-linear/platform/fft-kfr.h, SIGNALSMITH_USE_KFR): the power-of-two
// transforms against the library's own (SimpleFFT, SimpleRealFFT on it), every
// way they are called (interleaved, split, in place, forward and inverse, float
// and double), and what Signalsmith Stretch builds on them (RealFFT, of sizes
// with factors of 3 and 5 too, in steps) against a direct DFT.

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include "harness/Test.h"
#include "signalsmith-linear/fft.h"

namespace {

namespace linear = signalsmith::linear;

static_assert(std::is_base_of_v<linear::_impl::KfrComplexFFT<float>, linear::Pow2FFT<float>>,
              "Signalsmith Linear's power-of-two FFT is KFR's");
static_assert(std::is_base_of_v<linear::_impl::KfrRealFFT<double>, linear::Pow2RealFFT<double>>,
              "Signalsmith Linear's power-of-two real FFT is KFR's");

template<typename T>
std::vector<std::complex<T>> randomComplex(size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<T> uni(-1, 1);
    std::vector<std::complex<T>> x(n);
    for (auto& v : x) v = {uni(rng), uni(rng)};
    return x;
}

template<typename T>
std::vector<T> randomReal(size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<T> uni(-1, 1);
    std::vector<T> x(n);
    for (auto& v : x) v = uni(rng);
    return x;
}

// The largest difference, against the largest magnitude of the reference.
template<typename T>
double relativeError(const std::vector<std::complex<T>>& got, const std::vector<std::complex<T>>& want) {
    double worst = 0.0, scale = 1e-30;
    for (size_t i = 0; i < want.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(got[i] - want[i])));
        scale = std::max(scale, static_cast<double>(std::abs(want[i])));
    }
    return worst / scale;
}
template<typename T>
double relativeError(const std::vector<T>& got, const std::vector<T>& want) {
    double worst = 0.0, scale = 1e-30;
    for (size_t i = 0; i < want.size(); ++i) {
        worst = std::max(worst, std::abs(static_cast<double>(got[i]) - static_cast<double>(want[i])));
        scale = std::max(scale, std::abs(static_cast<double>(want[i])));
    }
    return worst / scale;
}

template<typename T>
constexpr double tolerance() {
    return std::is_same_v<T, float> ? 2e-5 : 1e-12;
}

template<typename T>
void checkComplex(size_t n) {
    using C = std::complex<T>;
    INFO("complex, size " + std::to_string(n) + (std::is_same_v<T, float> ? ", float" : ", double"));
    linear::Pow2FFT<T> kfr(n);
    linear::SimpleFFT<T> own(n);
    const std::vector<C> x = randomComplex<T>(n, static_cast<unsigned>(n));
    for (const bool inverse : {false, true}) {
        std::vector<C> want(n), got(n);
        if (inverse) {
            own.ifft(x.data(), want.data());
            kfr.ifft(x.data(), got.data());
        } else {
            own.fft(x.data(), want.data());
            kfr.fft(x.data(), got.data());
        }
        CHECK(relativeError(got, want) < tolerance<T>());
        // In place.
        std::vector<C> inPlace = x;
        if (inverse) kfr.ifft(inPlace.data(), inPlace.data());
        else kfr.fft(inPlace.data(), inPlace.data());
        CHECK(relativeError(inPlace, want) < tolerance<T>());
        // Split.
        std::vector<T> inR(n), inI(n), outR(n), outI(n);
        for (size_t i = 0; i < n; ++i) {
            inR[i] = x[i].real();
            inI[i] = x[i].imag();
        }
        if (inverse) kfr.ifft(inR.data(), inI.data(), outR.data(), outI.data());
        else kfr.fft(inR.data(), inI.data(), outR.data(), outI.data());
        std::vector<C> split(n);
        for (size_t i = 0; i < n; ++i) split[i] = {outR[i], outI[i]};
        CHECK(relativeError(split, want) < tolerance<T>());
    }
}

template<typename T>
void checkReal(size_t n) {
    using C = std::complex<T>;
    INFO("real, size " + std::to_string(n) + (std::is_same_v<T, float> ? ", float" : ", double"));
    linear::Pow2RealFFT<T> kfr(n);
    linear::SimpleRealFFT<T, linear::_impl::SimplePow2FFT<T>> own(n);
    const std::vector<T> x = randomReal<T>(n, static_cast<unsigned>(n) + 7);
    // Forward: bins 0..n/2-1, bin 0 holding DC and Nyquist.
    std::vector<C> want(n / 2), got(n / 2);
    own.fft(x.data(), want.data());
    kfr.fft(x.data(), got.data());
    CHECK(relativeError(got, want) < tolerance<T>());
    std::vector<T> outR(n / 2), outI(n / 2);
    kfr.fft(x.data(), outR.data(), outI.data());
    std::vector<C> split(n / 2);
    for (size_t i = 0; i < n / 2; ++i) split[i] = {outR[i], outI[i]};
    CHECK(relativeError(split, want) < tolerance<T>());
    // Inverse, of that spectrum (unscaled: n times the signal), leaving it as it was.
    std::vector<T> back(n), backOwn(n), backSplit(n);
    const std::vector<C> spectrum = want;
    own.ifft(spectrum.data(), backOwn.data());
    kfr.ifft(spectrum.data(), back.data());
    CHECK(spectrum == want);
    CHECK(relativeError(back, backOwn) < tolerance<T>());
    for (size_t i = 0; i < n / 2; ++i) {
        outR[i] = want[i].real();
        outI[i] = want[i].imag();
    }
    kfr.ifft(outR.data(), outI.data(), backSplit.data());
    CHECK(relativeError(backSplit, backOwn) < tolerance<T>());
}

}  // namespace

TEST_CASE("Signalsmith Linear's power-of-two FFTs on KFR's are the library's own") {
    for (size_t n = 1; n <= 16384; n *= 2) {
        checkComplex<float>(n);
        checkComplex<double>(n);
        if (n >= 2) {
            checkReal<float>(n);
            checkReal<double>(n);
        }
    }
    // Made again at another size (Stretch's configurations change it).
    linear::Pow2FFT<float> fft(64);
    fft.resize(2);
    fft.resize(4096);
    std::vector<std::complex<float>> x = randomComplex<float>(4096, 3), want(4096), got(4096);
    linear::SimpleFFT<float>(4096).fft(x.data(), want.data());
    fft.fft(x.data(), got.data());
    CHECK(relativeError(got, want) < 2e-5);
}

TEST_CASE("Signalsmith Stretch's real FFTs on KFR's are the DFT, in any number of steps") {
    // Sizes as Stretch picks them (fastSizeAbove: powers of two times 1, 3, 5 or 6).
    for (const size_t n : {size_t{256}, size_t{768}, size_t{1280}, size_t{1536}, size_t{4096}, size_t{6144}}) {
        INFO("size " + std::to_string(n));
        const std::vector<float> x = randomReal<float>(n, static_cast<unsigned>(n));
        // The DFT, in double: bins 0..n/2-1, the Nyquist in bin 0's imaginary part.
        std::vector<std::complex<float>> want(n / 2);
        for (size_t k = 0; k < n / 2; ++k) {
            std::complex<double> sum = 0.0;
            for (size_t t = 0; t < n; ++t)
                sum += static_cast<double>(x[t]) * std::polar(1.0, -2.0 * M_PI * double(k * t % n) / double(n));
            want[k] = std::complex<float>(sum);
        }
        double nyquist = 0.0;
        for (size_t t = 0; t < n; ++t) nyquist += (t % 2 ? -1.0 : 1.0) * x[t];
        want[0] = {want[0].real(), static_cast<float>(nyquist)};

        linear::RealFFT<float> whole(n);
        std::vector<std::complex<float>> got(n / 2);
        whole.fft(x.data(), got.data());
        CHECK(relativeError(got, want) < 1e-4);
        std::vector<float> back(n);
        whole.ifft(got.data(), back.data());
        for (float& v : back) v /= static_cast<float>(n);
        CHECK(relativeError(back, x) < 1e-4);

        linear::RealFFT<float, true> steps(n);  // (Spectral's, a step per block)
        std::vector<std::complex<float>> stepped(n / 2);
        for (size_t s = 0; s < steps.steps(); ++s) steps.fft(s, x.data(), stepped.data());
        CHECK(relativeError(stepped, want) < 1e-4);
    }
}
