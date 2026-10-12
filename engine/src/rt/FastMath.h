#pragma once
// Math functions for the audio thread, branch-free (so loops over them
// vectorise) and about as accurate as the C library's: exp, exp2, log2 and
// log10 in float; exp, exp2, log2, sin and cos in double.
//
// Why: MinGW's C library (the Qt MinGW toolchain's) computes expf by converting
// to double and calling an x87 exp, about 29 ns a value against 1.5 ns for
// these; log2, exp2 and sin are as slow. Devices calling them per sample or per
// bin spent most of their time there (Compressor and OTT 85 %, Spectral 60 %,
// Phaser 49 %, Multiband 48 %; see benchmarks/README.md).
//
// With MSVC, the library's own: they are as fast, and MSVC vectorises loops
// calling them (with its vector math library), which it doesn't do for these
// (2 times as slow there). The clamps and limits below hold either way.
//
// Accuracy (tests/engine/test_fast_math.cpp checks it against the library):
// within 2 ulp in float (2.5 with MSVC's log2f) and 4 ulp in double over the ranges noted, where the
// results are normal numbers. Outside them, as noted per function: what a gain
// or a level needs, not every IEEE special case.

#include <cmath>
#include <cstdint>
#include <cstring>

namespace sub::fastmath {

namespace detail {

#if defined(_MSC_VER) && !defined(__clang__)
inline constexpr bool kLibrary = true;
#else
inline constexpr bool kLibrary = false;
#endif

inline int32_t bitsOf(float x) noexcept {
    int32_t b;
    std::memcpy(&b, &x, sizeof b);
    return b;
}
inline float floatOf(int32_t b) noexcept {
    float x;
    std::memcpy(&x, &b, sizeof x);
    return x;
}
inline int64_t bitsOf(double x) noexcept {
    int64_t b;
    std::memcpy(&b, &x, sizeof b);
    return b;
}
inline double doubleOf(int64_t b) noexcept {
    double x;
    std::memcpy(&x, &b, sizeof x);
    return x;
}

// x rounded to the nearest integer, ties to even (as nearbyint in the default rounding mode),
// for |x| < 2^22 (float) or 2^51 (double): adding 1.5 2^23 (1.5 2^52) pushes the fraction out
// of the mantissa. Not std::nearbyint, nor std::fmin and std::fmax for the clamps: MSVC calls
// them as functions, which keeps its loops scalar (2.5 times as slow as its library's exp).
// (Strict floating point is assumed: -ffast-math or /fp:fast could cancel the two additions.)
inline float roundToInt(float x) noexcept { return (x + 12582912.f) - 12582912.f; }
inline double roundToInt(double x) noexcept { return (x + 6755399441055744.0) - 6755399441055744.0; }
inline float clampTo(float x, float lo, float hi) noexcept {
    x = x > lo ? x : lo;  // (NaN: lo)
    return x < hi ? x : hi;
}
inline double clampTo(double x, double lo, double hi) noexcept {
    x = x > lo ? x : lo;
    return x < hi ? x : hi;
}

// e^r for |r| <= ln 2 / 2: Taylor to r^7 (the first term left out is under 6e-9 of it).
inline float expReduced(float r) noexcept {
    float p = 1.f / 5040.f;
    p = p * r + 1.f / 720.f;
    p = p * r + 1.f / 120.f;
    p = p * r + 1.f / 24.f;
    p = p * r + 1.f / 6.f;
    p = p * r + 0.5f;
    p = p * r + 1.f;
    return p * r + 1.f;
}
// e^r for |r| <= ln 2 / 2: Taylor to r^13 (the first term left out is under 5e-18 of it).
inline double expReduced(double r) noexcept {
    double p = 1.0 / 6227020800.0;
    p = p * r + 1.0 / 479001600.0;
    p = p * r + 1.0 / 39916800.0;
    p = p * r + 1.0 / 3628800.0;
    p = p * r + 1.0 / 362880.0;
    p = p * r + 1.0 / 40320.0;
    p = p * r + 1.0 / 5040.0;
    p = p * r + 1.0 / 720.0;
    p = p * r + 1.0 / 120.0;
    p = p * r + 1.0 / 24.0;
    p = p * r + 1.0 / 6.0;
    p = p * r + 0.5;
    p = p * r + 1.0;
    return p * r + 1.0;
}

// p 2^n, p in [0.7, 1.42] and n in [-125, 127] (float) or [-1021, 1023] (double): the exponent added in.
inline float scale(float p, float n) noexcept { return floatOf(bitsOf(p) + (static_cast<int32_t>(n) << 23)); }
inline double scale(double p, double n) noexcept { return doubleOf(bitsOf(p) + (static_cast<int64_t>(n) << 52)); }

// ln m for m in [sqrt(1/2), sqrt(2)], f = m - 1, as fdlibm's log has it (Sun Microsystems,
// freely redistributable): f - f^2 / 2 + s (f^2 / 2 + R(s^2)), s = f / (2 + f).
inline double logReduced(double m) noexcept {
    const double f = m - 1.0, s = f / (2.0 + f), z = s * s, hfsq = 0.5 * f * f;
    double r = 1.479819860511658591e-01;
    r = r * z + 1.531383769920937332e-01;
    r = r * z + 1.818357216161805012e-01;
    r = r * z + 2.222219843214978396e-01;
    r = r * z + 2.857142874366239149e-01;
    r = r * z + 3.999999999940941908e-01;
    r = r * z + 6.666666666666735130e-01;
    return f - (hfsq - s * (hfsq + z * r));
}

}  // namespace detail

// e^x. Below -86.5: 0 (the true value is under 3e-38); above 88: e^88 (gains of +764 dB and more).
inline float exp(float x) noexcept {
    const bool underflow = x < -86.5f;
    x = detail::clampTo(x, -86.5f, 88.f);
    if constexpr (detail::kLibrary) return underflow ? 0.f : std::exp(x);
    const float n = detail::roundToInt(x * 1.44269504088896341f);
    const float r = (x - n * 0.693145751953125f) - n * 1.42860682030941723212e-6f;  // ln 2, in two parts
    const float y = detail::scale(detail::expReduced(r), n);
    return underflow ? 0.f : y;
}

// 2^x. Below -125: 0; above 127: 2^127.
inline float exp2(float x) noexcept {
    const bool underflow = x < -125.f;
    x = detail::clampTo(x, -125.f, 127.f);
    if constexpr (detail::kLibrary) return underflow ? 0.f : std::exp2(x);
    const float n = detail::roundToInt(x);
    const float y = detail::scale(detail::expReduced((x - n) * 0.693147180559945309f), n);
    return underflow ? 0.f : y;
}

// log2 x for x > 0 (subnormals too); 0 gives -inf, below it or NaN gives NaN, +inf gives +inf.
// (The reduced part in double: in float its roundings add up to 4 ulp.)
inline float log2(float x) noexcept {
    if constexpr (detail::kLibrary) return std::log2(x);
    const bool tiny = x < 1.17549435e-38f;  // subnormal: scaled up 2^23 first
    const float scaled = tiny ? x * 8388608.f : x;
    const int32_t bits = detail::bitsOf(scaled);
    int32_t e = ((bits >> 23) & 0xff) - 127;
    float m = detail::floatOf((bits & 0x007fffff) | 0x3f800000);  // [1, 2)
    const bool high = m > 1.41421356f;
    m = high ? 0.5f * m : m;
    e += high ? 1 : 0;
    const double y = static_cast<double>(e) - (tiny ? 23.0 : 0.0) +
                     detail::logReduced(static_cast<double>(m)) * 1.44269504088896340736;
    const auto yf = static_cast<float>(y);
    return x > 0.f ? (x < INFINITY ? yf : x) : (x == 0.f ? -INFINITY : NAN);
}

// log10 x, as log2 is.
inline float log10(float x) noexcept {
    if constexpr (detail::kLibrary) return std::log10(x);
    return log2(x) * 0.301029995663981195f;
}

// e^x. Below -707.5: 0 (the true value is under 1e-307); above 709: e^709.
inline double exp(double x) noexcept {
    const bool underflow = x < -707.5;
    x = detail::clampTo(x, -707.5, 709.0);
    if constexpr (detail::kLibrary) return underflow ? 0.0 : std::exp(x);
    const double n = detail::roundToInt(x * 1.44269504088896338700);
    const double r = (x - n * 6.93147180369123816490e-01) - n * 1.90821492927058770002e-10;  // ln 2, in two parts
    const double y = detail::scale(detail::expReduced(r), n);
    return underflow ? 0.0 : y;
}

// 2^x. Below -1021: 0; above 1023: 2^1023.
inline double exp2(double x) noexcept {
    const bool underflow = x < -1021.0;
    x = detail::clampTo(x, -1021.0, 1023.0);
    if constexpr (detail::kLibrary) return underflow ? 0.0 : std::exp2(x);
    const double n = detail::roundToInt(x);
    const double y = detail::scale(detail::expReduced((x - n) * 0.693147180559945309417), n);
    return underflow ? 0.0 : y;
}

// log2 x for x > 0 (subnormals too); 0 gives -inf, below it or NaN gives NaN, +inf gives +inf.
inline double log2(double x) noexcept {
    if constexpr (detail::kLibrary) return std::log2(x);
    const bool tiny = x < 2.2250738585072014e-308;  // subnormal: scaled up 2^54 first
    const double scaled = tiny ? x * 18014398509481984.0 : x;
    const int64_t bits = detail::bitsOf(scaled);
    int64_t e = ((bits >> 52) & 0x7ff) - 1023;
    double m = detail::doubleOf((bits & 0x000fffffffffffffLL) | 0x3ff0000000000000LL);  // [1, 2)
    const bool high = m > 1.4142135623730951;
    m = high ? 0.5 * m : m;
    e += high ? 1 : 0;
    const double y = static_cast<double>(e) - (tiny ? 54.0 : 0.0) + detail::logReduced(m) * 1.44269504088896340736;
    return x > 0.0 ? (x < INFINITY ? y : x) : (x == 0.0 ? -INFINITY : NAN);
}

// sin x and cos x, both from one reduction, for |x| up to 1e6 (beyond, the library's).
// fdlibm's kernels (Sun Microsystems, freely redistributable), reduced by pi / 2 in three parts.
inline void sincos(double x, double& sine, double& cosine) noexcept {
    if (detail::kLibrary || !(std::abs(x) < 1e6)) {
        sine = std::sin(x);
        cosine = std::cos(x);
        return;
    }
    const double k = detail::roundToInt(x * 0.636619772367581382433);
    const double r = ((x - k * 1.57079632673412561417e+00) - k * 6.07710050630396597660e-11) -
                     k * 2.02226624879595063154e-21;
    const double z = r * r;
    double ps = 1.58969099521155010221e-10;
    ps = ps * z - 2.50507602534068634195e-08;
    ps = ps * z + 2.75573137070700676789e-06;
    ps = ps * z - 1.98412698298579493134e-04;
    ps = ps * z + 8.33333333332248946124e-03;
    ps = ps * z - 1.66666666666666324348e-01;
    const double s = r + r * z * ps;
    double pc = -1.13596475577881948265e-11;
    pc = pc * z + 2.08757232129817482790e-09;
    pc = pc * z - 2.75573143513906633035e-07;
    pc = pc * z + 2.48015872894767294178e-05;
    pc = pc * z - 1.38888888888741095749e-03;
    pc = pc * z + 4.16666666666666019037e-02;
    const double c = 1.0 - 0.5 * z + z * z * pc;
    const auto quadrant = static_cast<int64_t>(k) & 3;
    const double sq = (quadrant & 1) ? c : s, cq = (quadrant & 1) ? s : c;
    sine = (quadrant & 2) ? -sq : sq;
    cosine = ((quadrant + 1) & 2) ? -cq : cq;
}
inline double sin(double x) noexcept {
    double s, c;
    sincos(x, s, c);
    return s;
}
inline double cos(double x) noexcept {
    double s, c;
    sincos(x, s, c);
    return c;
}

}  // namespace sub::fastmath
