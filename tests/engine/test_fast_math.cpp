// rt/FastMath.h against the C library's long double functions (a reference
// more precise than either): the error in units in the last place, over each
// function's range, and the edge cases a gain or a level meets.

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>

#include "harness/Test.h"
#include "rt/FastMath.h"

namespace fm = sub::fastmath;

namespace {

constexpr int kCount = 1'000'000;

// |value - reference| in units of the last place of the reference's type at the reference.
template <typename T>
double ulps(T value, long double reference) {
    const T rounded = static_cast<T>(reference);
    const T next = std::nextafter(std::abs(rounded), std::numeric_limits<T>::infinity());
    const long double ulp = static_cast<long double>(next) - static_cast<long double>(std::abs(rounded));
    return static_cast<double>(std::abs(static_cast<long double>(value) - reference) / ulp);
}

// The largest error over kCount values from `draw`, with the worst input reported.
template <typename T, typename Draw, typename Fast, typename Reference>
double worst(const char* name, Draw draw, Fast fast, Reference reference) {
    std::mt19937_64 rng(7);
    double most = 0.0;
    T at = 0;
    for (int i = 0; i < kCount; ++i) {
        const T x = draw(rng);
        const double e = ulps<T>(fast(x), reference(static_cast<long double>(x)));
        if (e > most) {
            most = e;
            at = x;
        }
    }
    std::printf("  %-28s %6.2f ulp (at %.17g)\n", name, most, static_cast<double>(at));
    return most;
}

template <typename T>
auto uniform(T lo, T hi) {
    return [lo, hi](std::mt19937_64& rng) { return std::uniform_real_distribution<T>(lo, hi)(rng); };
}
// Positive values spread evenly in log: every exponent from `loExp` to `hiExp`.
template <typename T>
auto logUniform(T loExp, T hiExp) {
    return [loExp, hiExp](std::mt19937_64& rng) {
        return std::exp2(std::uniform_real_distribution<T>(loExp, hiExp)(rng));
    };
}

// sin and cos to long double precision for |x| up to 1e6: reduced by pi / 2 in four parts
// first (fdlibm's: each product with k exact), as the x87's own reduction, with its 66-bit pi,
// is off by up to thousands of double ulp out there.
long double reducePiHalves(long double x, long long& quadrant) {
    const long double k = std::nearbyint(x / 1.57079632679489661923132169163975144L);
    quadrant = static_cast<long long>(k) & 3;
    return (((x - k * 1.57079632673412561417e+00L) - k * 6.07710050630396597660e-11L) -
            k * 2.02226624871116645580e-21L) - k * 8.47842766036889956997e-32L;
}
long double referenceSin(long double x) {
    long long q;
    const long double r = reducePiHalves(x, q);
    const long double s = (q & 1) ? std::cos(r) : std::sin(r);
    return (q & 2) ? -s : s;
}
long double referenceCos(long double x) {
    long long q;
    const long double r = reducePiHalves(x, q);
    const long double c = (q & 1) ? std::sin(r) : std::cos(r);
    return ((q + 1) & 2) ? -c : c;
}

}  // namespace

// (2.5 ulp: with MSVC these are its library's, and its log2f comes to 2.02; FastMath.h's own
// come to 1.84 at most.)
TEST_CASE("fast math: float exp, exp2, log2, log10 within 2.5 ulp") {
    CHECK(worst<float>("exp(float)", uniform(-86.5f, 88.f), [](float x) { return fm::exp(x); },
                       [](long double x) { return std::exp(x); }) <= 2.5);
    CHECK(worst<float>("exp(float), gains", uniform(-23.f, 5.f), [](float x) { return fm::exp(x); },
                       [](long double x) { return std::exp(x); }) <= 2.5);
    CHECK(worst<float>("exp2(float)", uniform(-125.f, 127.f), [](float x) { return fm::exp2(x); },
                       [](long double x) { return std::exp2(x); }) <= 2.5);
    CHECK(worst<float>("log2(float)", logUniform(-149.f, 127.9f), [](float x) { return fm::log2(x); },
                       [](long double x) { return std::log2(x); }) <= 2.5);
    CHECK(worst<float>("log2(float), near 1", uniform(0.5f, 2.f), [](float x) { return fm::log2(x); },
                       [](long double x) { return std::log2(x); }) <= 2.5);
    CHECK(worst<float>("log10(float)", logUniform(-149.f, 127.9f), [](float x) { return fm::log10(x); },
                       [](long double x) { return std::log10(x); }) <= 2.5);
}

TEST_CASE("fast math: double exp, exp2, log2, sin, cos within 4 ulp") {
    CHECK(worst<double>("exp(double)", uniform(-707.5, 709.0), [](double x) { return fm::exp(x); },
                        [](long double x) { return std::exp(x); }) <= 4.0);
    CHECK(worst<double>("exp2(double)", uniform(-1021.0, 1023.0), [](double x) { return fm::exp2(x); },
                        [](long double x) { return std::exp2(x); }) <= 4.0);
    CHECK(worst<double>("log2(double)", logUniform(-1074.0, 1023.9), [](double x) { return fm::log2(x); },
                        [](long double x) { return std::log2(x); }) <= 4.0);
    CHECK(worst<double>("log2(double), near 1", uniform(0.5, 2.0), [](double x) { return fm::log2(x); },
                        [](long double x) { return std::log2(x); }) <= 4.0);
    // (Away from sin's and cos's zeros, where a last place is very small: there, below.)
    const auto awayFromZeros = [](auto f) {
        return [f](long double x) {
            const long double y = f(x);
            return std::abs(y) < 1e-3L ? std::numeric_limits<long double>::quiet_NaN() : y;
        };
    };
    for (const double range : {7.0, 1000.0, 999999.0}) {
        const std::string sinName = "sin(double), |x| < " + std::to_string(static_cast<int>(range));
        const std::string cosName = "cos(double), |x| < " + std::to_string(static_cast<int>(range));
        const double s = worst<double>(sinName.c_str(), uniform(-range, range), [](double x) { return fm::sin(x); },
                                       awayFromZeros(referenceSin));
        const double c = worst<double>(cosName.c_str(), uniform(-range, range), [](double x) { return fm::cos(x); },
                                       awayFromZeros(referenceCos));
        CHECK(s <= 4.0);
        CHECK(c <= 4.0);
    }
}

TEST_CASE("fast math: sin and cos near their zeros, within 2e-16 absolutely") {
    std::mt19937_64 rng(3);
    double most = 0.0;
    for (int i = 0; i < kCount; ++i) {
        const double k = std::floor(std::uniform_real_distribution<double>(-300000.0, 300000.0)(rng));
        const double x = k * 1.5707963267948966 + std::uniform_real_distribution<double>(-1e-3, 1e-3)(rng);
        double s, c;
        fm::sincos(x, s, c);
        most = std::max(most, static_cast<double>(std::abs(s - referenceSin(x))));
        most = std::max(most, static_cast<double>(std::abs(c - referenceCos(x))));
    }
    std::printf("  sin/cos near zeros: %.2e\n", most);
    CHECK(most <= 2e-16);
}

TEST_CASE("fast math: edge cases") {
    constexpr float inf = std::numeric_limits<float>::infinity();
    CHECK(fm::exp(0.f) == 1.f);
    CHECK(fm::exp2(0.f) == 1.f);
    CHECK(fm::exp2(10.f) == 1024.f);
    CHECK(fm::exp(-100.f) == 0.f);
    CHECK(fm::exp(-inf) == 0.f);
    CHECK(std::isfinite(fm::exp(inf)));  // (clamped: e^88)
    CHECK(fm::log2(1.f) == 0.f);
    CHECK(fm::log2(1024.f) == 10.f);
    CHECK(fm::log2(0.f) == -inf);
    CHECK(std::isnan(fm::log2(-1.f)));
    CHECK(std::isnan(fm::log2(std::numeric_limits<float>::quiet_NaN())));
    CHECK(fm::log2(inf) == inf);
    CHECK(std::abs(fm::log2(1e-40f) - std::log2(1e-40f)) < 1e-5f);  // subnormal
    CHECK(std::abs(fm::log10(1e-9f) + 9.f) < 1e-6f);

    constexpr double dinf = std::numeric_limits<double>::infinity();
    CHECK(fm::exp(0.0) == 1.0);
    CHECK(fm::exp2(-3.0) == 0.125);
    CHECK(fm::exp(-1000.0) == 0.0);
    CHECK(fm::log2(1.0) == 0.0);
    CHECK(fm::log2(0.0) == -dinf);
    CHECK(std::isnan(fm::log2(-2.0)));
    CHECK(fm::log2(dinf) == dinf);
    CHECK(std::abs(fm::log2(1e-310) - std::log2(1e-310)) < 1e-12);  // subnormal
    CHECK(fm::sin(0.0) == 0.0);
    CHECK(fm::cos(0.0) == 1.0);
    CHECK(fm::sin(1e7) == std::sin(1e7));  // (beyond 1e6: the library's)
}
