#pragma once
// The Vintage Hall device's design (devices/VintageHall.cpp): a 1970s digital
// concert hall, measured from a classic one (research/vintageverb has the
// renders and the analysis). The numbers below are what the measurements gave;
// lengths are in samples at kReferenceRate, the rate the reverb runs at with a
// 48 kHz host, and scale to the rate it runs at.
//
// The reverb runs at half the host's rate (a quarter at 88.2/96 kHz...), as the
// hardware it recalls ran at a low rate: an elliptic low-pass at a quarter of
// the host's rate band-limits it to 12 kHz at 48 kHz, ringing a little there.

#include <algorithm>
#include <array>
#include <cmath>

namespace sub::vintage_hall {

inline constexpr double kReferenceRate = 24000.0;

// --- Input ---------------------------------------------------------------------------------
// Each side comes into the ring after a fixed delay: the left at the ring's
// start, the right half way round, each before two Schroeder all-passes (gains
// 0.75 and 0.656 times Early Diffusion) that are part of the ring. Size scales
// the all-passes below 50 % (inputScale()), and the fixed delays (fixedDelay()).
inline constexpr int kFixedDelay[2] = {220, 134};
inline constexpr int kDiffuser[2][2] = {{168, 288}, {144, 240}};  // 7 and 12 ms, 6 and 10 ms
inline constexpr double kDiffuserGain[2] = {0.75, 0.65625};  // the shorter, the longer (times Early Diffusion)
inline constexpr double kInputGain = 0.9695;  // -0.27 dB

// --- The ring --------------------------------------------------------------------------------
// One loop of two halves. Half 1, from where the left input comes in:
//   + left, shelf, 168, 288 [L1] A [L2] 192 [R1] B [L3] 820 loss
// half 2, from where the right comes in:
//   + right, shelf, 144, 240 [R2] C [L4] 192 [R3] D [R4] 192 loss
// where 168..288 are the input's all-passes, A..D nested all-passes followed by
// the plain delays below, "loss" the decay's gain and the bass crossover,
// "shelf" the high shelf and [..] the output taps. The outermost all-pass of each has a gain of 0.5 times Late
// Diffusion, the ones inside 0.25 times it. B and D have three levels, and the
// innermost delay of each is the one the modulation moves.
struct Allpass {
    int levels;
    int length[3];  // outermost first; each contains the next (and a plain delay)
};
inline constexpr Allpass kA = {2, {1917, 1356, 0}};
inline constexpr Allpass kB = {3, {2737, 1685, 1033}};
inline constexpr Allpass kC = {2, {1807, 1430, 0}};
inline constexpr Allpass kD = {3, {2869, 1808, 1203}};
inline constexpr int kAfterA = 585, kAfterB = 408, kAfterC = 853, kAfterD = 30;
inline constexpr int kCross = 192;                    // from L2 to R1 and from L4 to R3
inline constexpr int kBeforeF1 = 820, kBeforeF2 = 192;
inline constexpr double kOuterGain = 0.5, kInnerGain = 0.25;

// The halves' lengths along the loop (what the decay is worked out from).
inline constexpr int kHalf1 = kDiffuser[0][0] + kDiffuser[0][1] + kA.length[0] + kAfterA + kCross + kB.length[0] +
                              kAfterB + kBeforeF1;  // 7115
inline constexpr int kHalf2 = kDiffuser[1][0] + kDiffuser[1][1] + kC.length[0] + kAfterC + kCross + kD.length[0] +
                              kAfterD + kBeforeF2;  // 6327

// The modulation: each moving delay wanders from its length to this much longer
// at full Mod Depth, at a steady speed (the whole range in 1 / Mod Rate seconds)
// towards targets picked at random.
inline constexpr double kModRangeSeconds = 64.0 / kReferenceRate;  // 2.67 ms

// The Size control (0..1): the ring's all-passes scale from 0.2 to 1; the
// input's from 0.4 at 0 to 1.2 just below 50 %, and are 1 from 50 % on.
inline double ringScale(double size) noexcept { return 0.2 + 0.8 * std::clamp(size, 0.0, 1.0); }
inline double inputScale(double size) noexcept { return size < 0.5 ? 2.0 * ringScale(size) : 1.0; }

// The plain delays (kAfterA, kCross, kAfterB, kBeforeF1, kAfterC, kCross,
// kAfterD, kBeforeF2, in the ring's order) for Size, as shares of their
// lengths at 100 %: as measured, straight lines below 50 % and from 50 % on
// (the crossings and those after B and D scale as the ring does). The whole
// loop always scales as the ring: these make up for the input's all-passes.
// (Below 50 % L2 comes before A has ended: the delay after A is then a sample.)
inline double plainScale(int segment, double size) noexcept {
    size = std::clamp(size, 0.0, 1.0);
    const bool low = size < 0.5;
    switch (segment) {
        case 0: return low ? (-22.0 - 90.2 * size) / 1167.0 : (188.0 + 1958.0 * (size - 0.5)) / 1167.0;
        case 3: return low ? (366.8 + 1468.9 * size) / 1641.0 : (1063.0 + 1156.0 * (size - 0.5)) / 1641.0;
        case 4: return low ? (149.2 + 595.6 * size) / 1705.0 : (637.6 + 2134.8 * (size - 0.5)) / 1705.0;
        case 7: return low ? (149.2 + 605.3 * size) / 386.0 : (378.0 + 16.0 * (size - 0.5)) / 386.0;
        default: return ringScale(size);
    }
}

// A side's fixed delay (samples at kReferenceRate) for Size: shorter below 50 %.
inline double fixedDelay(int side, double size) noexcept {
    size = std::clamp(size, 0.0, 1.0);
    if (size >= 0.5) return kFixedDelay[side];
    return side == 0 ? 106.6 + 270.3 * size : 72.9 + 157.8 * size;
}

// The output taps' weights for Attack (0..1): the first three taps of each side
// and the other side's tap (L4 on the left, R1 on the right), straight lines
// between these at 0, 1/3, 2/3 and 1 (in 31sts). Low, the first tap leads (the
// sound starts at once); high, only the third is left (it swells).
inline constexpr float kTapWeights[4][4] = {
    {31.f, 10.f, 10.f, -2.f},
    {29.f, 15.f, 5.f, -10.f},
    {18.f, 20.f, 15.f, -12.f},
    {0.f, 0.f, 31.f, 0.f},
};
inline std::array<float, 4> tapWeights(double attack) noexcept {
    const double t = std::clamp(attack, 0.0, 1.0) * 3.0;
    const int i = std::min(2, static_cast<int>(t));
    const auto f = static_cast<float>(t - i);
    std::array<float, 4> w{};
    for (int k = 0; k < 4; ++k) w[k] = (kTapWeights[i][k] + f * (kTapWeights[i + 1][k] - kTapWeights[i][k])) / 31.f;
    return w;
}

// A half's gain (dB) in the middle band, so the loop decays 60 dB in `decaySeconds`.
inline double halfGainDb(double halfSeconds, double decaySeconds) noexcept {
    return -60.0 * halfSeconds / std::max(0.05, decaySeconds);
}

// The ring's map for Size (0..1): where along the loop (samples at kReferenceRate from where the
// left comes in) each part starts, as the lengths are set (its editor draws the loop from this).
struct RingMap {
    enum Point {
        LeftIn = 0, L1, A, L2, R1, B, L3, RightIn, R2, C, L4, R3, D, R4, kPoints
    };
    double at[kPoints] = {};
    double half1 = 0.0, length = 0.0;  // where the second half starts, and the whole loop
};
inline RingMap ringMap(double size) noexcept {
    const double ring = ringScale(size), input = inputScale(size);
    const auto plain = [&](int segment, int samples) { return std::max(1.0, samples * plainScale(segment, size)); };
    RingMap m;
    double p = 0.0;
    m.at[RingMap::LeftIn] = p;
    p += (kDiffuser[0][0] + kDiffuser[0][1]) * input;
    m.at[RingMap::L1] = m.at[RingMap::A] = p;
    p += kA.length[0] * ring + plain(0, kAfterA);
    m.at[RingMap::L2] = p;
    p += plain(1, kCross);
    m.at[RingMap::R1] = m.at[RingMap::B] = p;
    p += kB.length[0] * ring + plain(2, kAfterB);
    m.at[RingMap::L3] = p;
    p += plain(3, kBeforeF1);
    m.half1 = m.at[RingMap::RightIn] = p;
    p += (kDiffuser[1][0] + kDiffuser[1][1]) * input;
    m.at[RingMap::R2] = m.at[RingMap::C] = p;
    p += kC.length[0] * ring + plain(4, kAfterC);
    m.at[RingMap::L4] = p;
    p += plain(5, kCross);
    m.at[RingMap::R3] = m.at[RingMap::D] = p;
    p += kD.length[0] * ring + plain(6, kAfterD);
    m.at[RingMap::R4] = p;
    p += plain(7, kBeforeF2);
    m.length = p;
    return m;
}

// --- Its editor's displays -------------------------------------------------------------------
// One value per kDisplaySamples host samples: the input's and the wet output's peaks (dB, floored
// at kDisplayFloorDb), each tap's peak (L1..L4, R1..R4) and the two moving delays' positions (0..1).
inline constexpr int kDisplaySamples = 256;
inline constexpr float kDisplayFloorDb = -96.f;

// --- Rates -----------------------------------------------------------------------------------
// The reverb's rate is the host's over this (a power of two): about 24 kHz.
inline int rateDivider(double hostRate) noexcept {
    int divider = 1;
    while (hostRate / divider > 40000.0 && divider < 8) divider *= 2;
    return divider;
}

// The 2:1 resampler's low-pass: a 12th-order elliptic (0.01 dB ripple, 90 dB
// down, the band's edge at 0.2495 of the rate it runs at), as second-order
// sections {b0, b1, b2, a1, a2}. Each halving runs it once going down and once
// coming back up.
inline constexpr double kResampler[6][5] = {
    {0.0054777625774177037, 0.010447764323088426, 0.0054777625774177028, -0.73272344416138535, 0.17323673382647581},
    {1.0, 1.3742087325383796, 1.0, -0.54106318773636131, 0.35703619725201785},
    {1.0, 0.85154577869672576, 1.0, -0.30427160658974489, 0.5862374642805539},
    {1.0, 0.52815650383383539, 1.0, -0.12432668916483286, 0.76595705083329746},
    {1.0, 0.36060084677106363, 1.0, -0.017628890596476793, 0.88478477671584943},
    {1.0, 0.29115924818321104, 1.0, 0.030634947616462602, 0.96530461732956341},
};

// --- Filters ---------------------------------------------------------------------------------
// A first-order section y = b0 x + b1 x[-1] - a1 y[-1].
struct FirstOrder {
    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
};

// The high shelf (in each half of the loop, after where the input comes in): (G s + w) / (s + w),
// w = 2 pi `freq`, by the bilinear transform without prewarping, so it reaches
// G at half the rate it runs at.
inline FirstOrder highShelf(double freq, double gainDb, double rate) noexcept {
    const double k = 2.0 * rate, w = 2.0 * 3.14159265358979323846 * freq, g = std::pow(10.0, gainDb / 20.0);
    return {(g * k + w) / (k + w), (w - g * k) / (k + w), (w - k) / (k + w)};
}

// High Cut: a one-pole low-pass, w / (s + w) by the bilinear transform.
inline FirstOrder highCut(double freq, double rate) noexcept {
    const double k = 2.0 * rate, w = 2.0 * 3.14159265358979323846 * freq;
    return {w / (k + w), w / (k + w), (w - k) / (k + w)};
}

// Low Cut: a DC blocker, y = x - x[-1] + r y[-1] (it lifts the top a little: +0.8 dB at a quarter of
// the rate at 1.5 kHz).
inline FirstOrder lowCut(double freq, double rate) noexcept {
    const double r = std::exp(-2.0 * 3.14159265358979323846 * freq / rate);
    return {1.0, -1.0, -r};
}

// The bass crossover's one-pole low-pass coefficient: y = (1 - a) x + a y[-1].
inline double bassPole(double freq, double rate) noexcept { return std::exp(-2.0 * 3.14159265358979323846 * freq / rate); }

}  // namespace sub::vintage_hall
