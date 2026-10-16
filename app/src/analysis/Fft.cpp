#include "analysis/Fft.h"

#include <kfr/dft.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

namespace sub::app::analysis {

namespace {

constexpr double kPi = 3.14159265358979323846;

static_assert(sizeof(Complex) == sizeof(kfr::complex<double>), "std::complex and KFR's complex share a layout");

// A plan per size, with the scratch it needs. Each thread has its own (the editors' on the UI thread, the
// Sidechain's fit on the pool's): a plan's scratch is used by one transform at a time.
template <typename Plan>
struct Planned {
    explicit Planned(std::size_t n) : plan(n), temp(plan.temp_size) {}
    Plan plan;
    std::vector<kfr::u8> temp;
};

template <typename Plan>
Planned<Plan>& planFor(std::size_t n) {
    thread_local std::unordered_map<std::size_t, std::unique_ptr<Planned<Plan>>> plans;
    std::unique_ptr<Planned<Plan>>& planned = plans[n];
    if (!planned)
        planned = std::make_unique<Planned<Plan>>(n);
    return *planned;
}

}  // namespace

bool isPowerOfTwo(std::size_t n) { return n > 0 && (n & (n - 1)) == 0; }

void fft(std::vector<Complex>& data, bool inverse) {
    const std::size_t n = data.size();
    if (n < 2 || !isPowerOfTwo(n))
        return;
    auto& [plan, temp] = planFor<kfr::dft_plan<double>>(n);
    auto* values = reinterpret_cast<kfr::complex<double>*>(data.data());
    plan.execute(values, values, temp.data(), inverse);
    if (inverse) {
        const double scale = 1.0 / double(n);
        for (Complex& value : data)
            value *= scale;
    }
}

std::vector<Complex> rfft(const double* x, std::size_t count, std::size_t n) {
    std::vector<Complex> bins(n / 2 + 1);
    const std::size_t used = std::min(count, n);
    if (n < 2) {  // (one value: its own transform)
        if (n == 1 && used == 1)
            bins[0] = Complex(x[0], 0.0);
        return bins;
    }
    auto& [plan, temp] = planFor<kfr::dft_plan_real<double>>(n);
    thread_local std::vector<double> padded;
    const double* in = x;
    if (used < n) {
        padded.assign(n, 0.0);
        std::copy(x, x + used, padded.begin());
        in = padded.data();
    }
    plan.execute(reinterpret_cast<kfr::complex<double>*>(bins.data()), in, temp.data());
    return bins;
}

std::vector<double> hann(std::size_t n) {
    if (n == 0)
        return {};
    if (n == 1)
        return {1.0};
    std::vector<double> window(n);
    for (std::size_t i = 0; i < n; ++i)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1));
    return window;
}

}  // namespace sub::app::analysis
