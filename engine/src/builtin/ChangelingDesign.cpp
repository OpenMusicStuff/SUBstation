#include "builtin/ChangelingDesign.h"

// Changeling's shifter and drive (see ChangelingDesign.h). The grains are GigaTune's corrector's (its marks, windows
// and the reading between samples, GigaTuneDesign.cpp), centred on the voice's glottal closures as SEDREAMS finds
// them (Closures) rather than GigaTune's pulses, in voices: each voice has synthesis marks and grains of its own and
// plays one channel (the left and right's main voices, and their twins), all of them reading the analysis marks the
// tracker's frames place.

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "rt/FastMath.h"

namespace sub::changeling {

const std::vector<std::string>& modeLabels() {
    static const std::vector<std::string> kLabels = {"Transpose", "Quantize", "Robot"};
    return kLabels;
}

const std::vector<std::string>& qualityLabels() {
    static const std::vector<std::string> kLabels = {"Live", "Studio", "High Quality"};
    return kLabels;
}

namespace {

constexpr double kPi = std::numbers::pi;
using gigatune::kSincHalf;
using gigatune::sincAt;

// The tracker's range, as GigaTune's (a refined pitch may lie a little past pYIN's last bin).
constexpr double kRangeMargin = 1.03;
double lowestF0() { return gigatune::kFmin / kRangeMargin; }
double highestF0() { return gigatune::kFmax * kRangeMargin; }

// Grains on the closures, as GigaTune's on its pulses: how sure the closure must be at a voiced start to move the
// first mark onto it, how much of the way a grain's centre moves to its closure at each mark (times the sureness), and
// how far it may be from its mark (of the period, or the unvoiced step if shorter).
constexpr double kOnsetSureness = 0.1;
constexpr double kOffsetFollow = 0.1;
constexpr double kOffsetShare = 0.4;
constexpr double kUnvoicedPeriod = 0.005;  // seconds between unvoiced grains
constexpr double kCommitAheadSeconds = 0.0005;
// A grain's pitch ratio, as its gain takes it, at the least (and 1 / it at the most); how much a grain shorter than
// its step is raised at the most.
constexpr double kLowestRatio = 0.4;
constexpr double kMostRaise = 2.0;
// Elastic grains (Shifter::Impl::bend()): the points of an overlap compared (at the most), the shortest overlap bent,
// the first search's steps each way, what the warp's size, slope and curvature cost against the mismatch (each as a
// share of what its limits allow: the same cost for each at the limits; measured, below), and how much better than
// unbent a warp must be.
constexpr int kBendPoints = 48;
constexpr int kBendSteps = 4;
constexpr int64_t kBendLeast = 8;
constexpr double kBendSize = 0.01, kBendSlope = 0.01, kBendCurve = 0.01;
constexpr double kBendBetter = 1e-4;
constexpr double kCorrelateReach = 0.5;
constexpr double kCorrelateUnsure = 0.5;  // Correlate::Unsure: a closure's sureness under which its grain is moved

double midiHz(double pitch) { return 440.0 * std::exp2((pitch - 69.0) / 12.0); }

size_t powerOfTwoAtLeast(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// A small, fast generator for Smear (xorshift32), as uniform in [0, 1).
struct Random {
    uint32_t state = 0x9e3779b9u;
    double next() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state >> 8) * (1.0 / 16777216.0);
    }
};

}  // namespace

// --- Quadrature -------------------------------------------------------------------------------------

namespace {
// Each branch cascades (c - z^-2) / (1 - c z^-2) sections; the second is delayed a sample more. Fitted for a
// 90 degree difference (the second behind) from 30 Hz to 20 kHz at 48 kHz: within 0.18 degrees.
constexpr std::array<double, 5> kBranchI = {0.099802060100, 0.569076280421, 0.860434291157, 0.960840410601,
                                            0.990739726534};
constexpr std::array<double, 5> kBranchQ = {0.330433452155, 0.747759774769, 0.925245186905, 0.980091132693,
                                            0.997309649911};
}  // namespace

void Quadrature::reset() noexcept {
    iState1_ = iState2_ = qState1_ = qState2_ = iIn1_ = iIn2_ = qIn1_ = qIn2_ = {};
    qDelay_ = 0.0;
}

void Quadrature::process(double x, double& i, double& q) noexcept {
    double a = x;
    for (int s = 0; s < kSections; ++s) {
        const auto k = static_cast<size_t>(s);
        const double y = kBranchI[k] * (a + iState2_[k]) - iIn2_[k];
        iIn2_[k] = iIn1_[k];
        iIn1_[k] = a;
        iState2_[k] = iState1_[k];
        iState1_[k] = y;
        a = y;
    }
    i = a;
    double b = x;
    for (int s = 0; s < kSections; ++s) {
        const auto k = static_cast<size_t>(s);
        const double y = kBranchQ[k] * (b + qState2_[k]) - qIn2_[k];
        qIn2_[k] = qIn1_[k];
        qIn1_[k] = b;
        qState2_[k] = qState1_[k];
        qState1_[k] = y;
        b = y;
    }
    q = qDelay_;
    qDelay_ = b;
    // (denormals: the engine renders with them flushed to zero)
}

// --- EdgeWarp ---------------------------------------------------------------------------------------

void EdgeWarp::layout(int n, double* at, double* limits) noexcept {
    if (n == 3) {
        for (size_t k = 0; k < 3; ++k) at[k] = kKnots[k], limits[k] = kLimits[k];
        return;
    }
    for (int k = 0; k < n; ++k) {
        const double x = kDead + (1.0 - kDead) * (k + 1) / n;
        at[k] = x;
        // (the line through (kDead, 0) and kLimits' points)
        limits[k] = x <= kKnots[0] ? kLimits[0] * (x - kDead) / (kKnots[0] - kDead)
                    : x <= kKnots[1] ? kLimits[0] + (kLimits[1] - kLimits[0]) * (x - kKnots[0]) / (kKnots[1] - kKnots[0])
                                     : kLimits[1] + (kLimits[2] - kLimits[1]) * (x - kKnots[1]) / (kKnots[2] - kKnots[1]);
    }
}

void EdgeWarp::set(double half, const std::array<double, 3>& d) noexcept {
    set(half, 3, kKnots.data(), d.data());
}

void EdgeWarp::set(double half, int n, const double* at, const double* d) noexcept {
    count = std::clamp(n, 1, kMaxKnots);
    width = half;
    knots[0] = kDead * half;
    values[0] = 0.0;
    places[0] = kDead;
    on = false;
    for (int k = 1; k <= count; ++k) {
        const auto i = static_cast<size_t>(k);
        places[i] = at[k - 1];
        knots[i] = at[k - 1] * half;
        values[i] = d[k - 1];
        on = on || d[k - 1] != 0.0;
    }
    on = on && half > 0.0;
    slopes[0] = 0.0;
    for (int k = 1; k < count; ++k) {
        const auto i = static_cast<size_t>(k);
        slopes[i] = (values[i + 1] - values[i - 1]) / (knots[i + 1] - knots[i - 1]);
    }
    const auto last = static_cast<size_t>(count);
    slopes[last] = (values[last] - values[last - 1]) / (knots[last] - knots[last - 1]);
}

double EdgeWarp::at(double u) const noexcept {
    if (!on || !(u > knots[0])) return 0.0;
    const auto last = static_cast<size_t>(count);
    if (u >= knots[last]) return values[last];
    size_t i = 0;
    while (i + 1 < last && u >= knots[i + 1]) ++i;
    const double span = knots[i + 1] - knots[i], t = (u - knots[i]) / span;
    const double t2 = t * t, t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * values[i] + (t3 - 2.0 * t2 + t) * slopes[i] * span +
           (3.0 * t2 - 2.0 * t3) * values[i + 1] + (t3 - t2) * slopes[i + 1] * span;
}

// --- Closures ---------------------------------------------------------------------------------------

namespace {
// SEDREAMS' settings: the mean-based signal's window (periods), the interval the closure is looked for in (of the
// minimum-to-maximum distance, either side of the ratio), the ratio before any is learnt and how each closure moves
// it; the residual's analysis, and how long its skewness is followed for.
constexpr double kMeanWindow = 1.75;
constexpr double kInterval = 0.35;
constexpr double kFirstRatio = 0.2;
constexpr int kFirstClosures = 8;  // closures learnt from before the interval is used (the cycle's strongest until then)
constexpr double kRatioFollow = 0.1, kRatioStep = 0.03;
constexpr double kRatioLowest = -0.25, kRatioHighest = 1.25;
constexpr double kResidualWindowSeconds = 0.025, kResidualHopSeconds = 0.005;
constexpr double kSkewSeconds = 0.5;
// How sure a closure is: its residual peak over the cycle's RMS, from kSureFrom (none) to kSureFrom + kSureSpan.
// A closure looked for again within this many samples of a place, with a period as near, is the one found there.
constexpr double kSureFrom = 2.5, kSureSpan = 3.0;
constexpr double kSamePlace = 0.5;
}  // namespace

void Closures::prepare(double sampleRate, int keep, double longest) {
    order_ = std::clamp(static_cast<int>(std::lround(sampleRate / 1000.0)) + 2, 8, kMaxOrder);
    window_ = static_cast<int>(std::lround(kResidualWindowSeconds * sampleRate));
    hop_ = std::max(1, static_cast<int>(std::lround(kResidualHopSeconds * sampleRate)));
    hann_.resize(static_cast<size_t>(window_));
    for (int n = 0; n < window_; ++n)
        hann_[static_cast<size_t>(n)] = 0.5 - 0.5 * std::cos(2.0 * kPi * (n + 0.5) / window_);
    windowed_.assign(static_cast<size_t>(window_) + 4, 0.0);
    const size_t size = powerOfTwoAtLeast(static_cast<size_t>(std::max(keep, window_ + hop_ + order_)) + 16);
    input_.assign(size, 0.0);
    history_.assign(static_cast<size_t>(hop_ + order_), 0.0);
    residual_.assign(size, 0.0);
    mask_ = size - 1;
    // find() reads the mean-based signal over two periods and a window's half either side of them
    const auto span = static_cast<size_t>(std::ceil((2.0 + kMeanWindow) * longest)) + 16;
    for (auto* v : {&sums0_, &sums1Re_, &sums1Im_, &sums2Re_, &sums2Im_}) v->assign(span + 1, 0.0);
    mean_.assign(span, 0.0);
    momentRate_ = 1.0 - std::exp(-1.0 / (kSkewSeconds * sampleRate));
    reset();
}

void Closures::reset() noexcept {
    std::fill(input_.begin(), input_.end(), 0.0);
    std::fill(residual_.begin(), residual_.end(), 0.0);
    lpc_ = {};
    written_ = ready_ = 0;
    moment3_ = 0.0;
    ratio_ = kFirstRatio;
    learnt_ = 0;
    found_ = {};
    for (Found& f : found_) f.p = -std::numeric_limits<double>::infinity();
}

void Closures::push(double x) noexcept {
    input_[static_cast<size_t>(written_) & mask_] = x;
    ++written_;
    if (written_ % hop_ == 0) analyse();
}

// The predictor from the window ending at the newest sample (the autocorrelation method, Levinson-Durbin), and the
// last hop filtered by it.
void Closures::analyse() noexcept {
    const int64_t end = written_;
    for (int n = 0; n < window_; ++n)
        windowed_[static_cast<size_t>(n)] = hann_[static_cast<size_t>(n)] * inputAt(end - window_ + n);
    std::array<double, kMaxOrder + 1> r{};
    // (four sums at a time, so the compiler may vectorise them; the window is zero-padded to a multiple of four)
    const double* w = windowed_.data();
    for (int k = 0; k <= order_; ++k) {
        double sums[4] = {0.0, 0.0, 0.0, 0.0};
        for (int n = k; n < window_; n += 4)
            for (int j = 0; j < 4; ++j) sums[j] += w[n + j] * w[n + j - k];
        r[static_cast<size_t>(k)] = (sums[0] + sums[1]) + (sums[2] + sums[3]);
    }
    lpc_ = {};
    lpc_[0] = 1.0;
    if (r[0] > 1e-20) {
        double error = r[0] * (1.0 + 1e-9);  // (a little white noise: the predictor stays stable)
        std::array<double, kMaxOrder + 1> before{};
        for (int i = 1; i <= order_; ++i) {
            double acc = r[static_cast<size_t>(i)];
            for (int j = 1; j < i; ++j) acc += lpc_[static_cast<size_t>(j)] * r[static_cast<size_t>(i - j)];
            const double k = -acc / error;
            before = lpc_;
            for (int j = 1; j < i; ++j) lpc_[static_cast<size_t>(j)] = before[static_cast<size_t>(j)] + k * before[static_cast<size_t>(i - j)];
            lpc_[static_cast<size_t>(i)] = k;
            error *= 1.0 - k * k;
            if (!(error > 0.0)) break;
        }
    }
    // The hop and the order before it, in order (the window's last samples, before windowing).
    double* h = history_.data();
    for (int i = 0; i < hop_ + order_; ++i) h[i] = inputAt(end - hop_ - order_ + i);
    for (int64_t n = end - hop_; n < end; ++n) {
        const double* x = h + (n - (end - hop_)) + order_;
        double sums[4] = {0.0, 0.0, 0.0, 0.0};
        int k = 0;
        for (; k + 3 <= order_; k += 4)
            for (int j = 0; j < 4; ++j) sums[j] += lpc_[static_cast<size_t>(k + j)] * x[-(k + j)];
        for (; k <= order_; ++k) sums[0] += lpc_[static_cast<size_t>(k)] * x[-k];
        const double e = (sums[0] + sums[1]) + (sums[2] + sums[3]);
        residual_[static_cast<size_t>(n) & mask_] = e;
        moment3_ += momentRate_ * (e * e * e - moment3_);
    }
    ready_ = end;
}

void Closures::learn(double relative) noexcept {
    // (the first ones' mean; then each move held to kRatioStep: a stray peak barely moves it, and a voice whose
    // closures lie elsewhere moves it there within a few tens of periods)
    if (!std::isfinite(relative)) return;
    if (learnt_ < kFirstClosures) {
        ratio_ = std::clamp((ratio_ * learnt_ + relative) / (learnt_ + 1), kRatioLowest, kRatioHighest);
        ++learnt_;
        return;
    }
    ratio_ = std::clamp(ratio_ + std::clamp(kRatioFollow * (relative - ratio_), -kRatioStep, kRatioStep), kRatioLowest,
                        kRatioHighest);
}

bool Closures::find(double p, double period, double& offset, double& sureness, double& relative) const noexcept {
    offset = 0.0;
    sureness = 0.0;
    relative = std::numeric_limits<double>::quiet_NaN();
    const double T = period;
    const auto half = static_cast<int64_t>(std::max(2L, std::lround(0.5 * kMeanWindow * T)));
    // The mean-based signal over [p - 1.25 T, p + 0.5 T]: the minima before the closures about p, the input a window's
    // half past them; the residual to p + 0.5 T.
    const double newest = static_cast<double>(written_ - 1), newestResidual = static_cast<double>(ready_ - 1);
    const double past = std::max(std::floor(p + 0.5 * T) + static_cast<double>(half) - newest,
                                 std::floor(p + 0.5 * T) - newestResidual);
    bool whole = true;
    if (past > 0.0) {
        p -= T * std::ceil(past / T);
        whole = false;
    }
    // Looked for about the same place with the same period already (a mark placed again as frames come, or one
    // looked for whole periods back): the same closure, its input being the same.
    const double sign = moment3_ < 0.0 ? -1.0 : 1.0;
    for (const Found& f : found_) {
        if (std::abs(f.p - p) <= kSamePlace && std::abs(f.period - T) <= kSamePlace && f.ratio == ratio_ &&
            f.learnt == learnt_ && f.sign == sign) {
            if (std::isfinite(f.closure)) offset = f.closure - p;
            sureness = f.sureness;
            relative = f.relative;
            return whole;
        }
    }
    Found& f = found_[nextFound_];
    nextFound_ = (nextFound_ + 1) % found_.size();
    f = {p, T, ratio_, sign, std::numeric_limits<double>::quiet_NaN(), 0.0, relative, learnt_};
    look(f, half);
    if (std::isfinite(f.closure)) offset = f.closure - p;
    sureness = f.sureness;
    relative = f.relative;
    return whole;
}

void Closures::look(Found& f, int64_t half) const noexcept {
    const double p = f.p, T = f.period, sign = f.sign;
    const auto a = static_cast<int64_t>(std::ceil(p - 1.25 * T));
    const auto b = static_cast<int64_t>(std::floor(p + 0.5 * T));
    const int64_t from = a - half, to = b + half;
    const auto count = static_cast<size_t>(b - a + 1);
    if (static_cast<size_t>(to - from + 1) >= sums0_.size() || count < 4 || from < written_ - static_cast<int64_t>(mask_))
        return;

    // Running sums of x, x e^(i pi j / half) and x e^(2 i pi j / half) (j from `from`): each point's Blackman-windowed
    // sum is then three differences, turned back by the point's own phase.
    double z1Re = 1.0, z1Im = 0.0, s1, c1;
    fastmath::sincos(kPi / static_cast<double>(half), s1, c1);
    sums0_[0] = sums1Re_[0] = sums1Im_[0] = sums2Re_[0] = sums2Im_[0] = 0.0;
    for (int64_t j = 0; j <= to - from; ++j) {
        const auto i = static_cast<size_t>(j);
        const double x = inputAt(from + j);
        const double z2Re = z1Re * z1Re - z1Im * z1Im, z2Im = 2.0 * z1Re * z1Im;
        sums0_[i + 1] = sums0_[i] + x;
        sums1Re_[i + 1] = sums1Re_[i] + x * z1Re;
        sums1Im_[i + 1] = sums1Im_[i] + x * z1Im;
        sums2Re_[i + 1] = sums2Re_[i] + x * z2Re;
        sums2Im_[i + 1] = sums2Im_[i] + x * z2Im;
        const double re = z1Re * c1 - z1Im * s1;
        z1Im = z1Im * c1 + z1Re * s1;
        z1Re = re;
    }
    // (the first point is `half` on from `from`: its phase is pi)
    double zRe = -1.0, zIm = 0.0;
    double sumT = 0.0, sumTT = 0.0, sumY = 0.0, sumTY = 0.0;
    const double centre = 0.5 * static_cast<double>(count - 1);
    for (size_t n = 0; n < count; ++n) {
        const size_t lo = n, hi = n + 2 * static_cast<size_t>(half) + 1;
        const double d0 = sums0_[hi] - sums0_[lo];
        const double d1Re = sums1Re_[hi] - sums1Re_[lo], d1Im = sums1Im_[hi] - sums1Im_[lo];
        const double d2Re = sums2Re_[hi] - sums2Re_[lo], d2Im = sums2Im_[hi] - sums2Im_[lo];
        const double z2Re = zRe * zRe - zIm * zIm, z2Im = 2.0 * zRe * zIm;
        const double y = 0.42 * d0 + 0.5 * (d1Re * zRe + d1Im * zIm) + 0.08 * (d2Re * z2Re + d2Im * z2Im);
        mean_[n] = y;
        const double t = static_cast<double>(n) - centre;
        sumT += t;
        sumTT += t * t;
        sumY += y;
        sumTY += t * y;
        const double re = zRe * c1 - zIm * s1;
        zIm = zIm * c1 + zRe * s1;
        zRe = re;
    }
    // Its drift taken off (SEDREAMS high-passes it at 50 Hz): a straight line through it.
    // Both it and the residual turned the way the residual's skewness says (SEDREAMS takes the voice the right way up).
    const double slope = sumTT > 0.0 ? sumTY / sumTT : 0.0, level = sumY / static_cast<double>(count);
    for (size_t n = 0; n < count; ++n) mean_[n] = sign * (mean_[n] - level - slope * (static_cast<double>(n) - centre));

    const auto peakIn = [&](double lo, double hi, int64_t& at) {
        const auto first = static_cast<int64_t>(std::ceil(lo));
        const auto last = std::min(static_cast<int64_t>(std::floor(hi)), ready_ - 1);
        double best = -std::numeric_limits<double>::infinity();
        at = -1;
        for (int64_t n = first; n <= last; ++n) {
            const double e = sign * residualAt(n);
            if (e > best) best = e, at = n;
        }
        return best;
    };

    double nearest = std::numeric_limits<double>::infinity();
    int64_t closure = -1;
    double minimum = 0.0, distance = 0.0;
    for (size_t n = 1; n + 1 < count; ++n) {
        if (!(mean_[n] < mean_[n - 1] && mean_[n] <= mean_[n + 1])) continue;
        size_t m = n + 1;
        while (m + 1 < count && !(mean_[m] > mean_[m - 1] && mean_[m] >= mean_[m + 1])) ++m;
        const double interval = std::clamp(m + 1 < count ? static_cast<double>(m - n) : 0.5 * T, 0.25 * T, 0.75 * T);
        const double low = static_cast<double>(a) + static_cast<double>(n);
        int64_t at;
        if (learnt_ < kFirstClosures) peakIn(low - 0.5 * interval, low + 1.5 * interval, at);
        else peakIn(low + (ratio_ - kInterval) * interval, low + (ratio_ + kInterval) * interval, at);
        if (at < 0) continue;
        const double d = std::abs(static_cast<double>(at) - p);
        if (d < nearest) nearest = d, closure = at, minimum = low, distance = interval;
    }
    if (closure < 0) return;

    // Between samples: a parabola through the peak and its neighbours.
    const double y0 = sign * residualAt(closure - 1), y1 = sign * residualAt(closure);
    const double y2 = closure + 1 < ready_ ? sign * residualAt(closure + 1) : y0;
    const double curve = y0 - 2.0 * y1 + y2;
    const double between = curve < 0.0 ? std::clamp(0.5 * (y0 - y2) / curve, -0.5, 0.5) : 0.0;
    f.closure = static_cast<double>(closure) + between;
    // How it stands out of its cycle (a period about its minimum), and where the cycle's strongest peak lies.
    const double lo = minimum - 0.5 * distance, hi = minimum + 1.5 * distance;
    double energy = 0.0;
    int64_t points = 0;
    for (auto n = static_cast<int64_t>(std::ceil(lo)); n <= std::min(static_cast<int64_t>(std::floor(hi)), ready_ - 1); ++n) {
        energy += residualAt(n) * residualAt(n);
        ++points;
    }
    if (!(points > 0 && energy > 0.0)) return;
    const double rms = std::sqrt(energy / static_cast<double>(points));
    if (y1 > 0.0) f.sureness = std::clamp((y1 / rms - kSureFrom) / kSureSpan, 0.0, 1.0);
    // (learnt from only where it stands out as a closure would: found in the interval or not)
    int64_t strongest;
    const double peak = peakIn(lo, hi, strongest);
    if (strongest >= 0 && peak / rms >= kSureFrom + 0.5 * kSureSpan)
        f.relative = (static_cast<double>(strongest) - minimum) / distance;
}

// --- Shifter ----------------------------------------------------------------------------------------

struct Shifter::Impl {
    static constexpr int kVoices = 4;  // the left's and the right's main voices, then their twins
    static constexpr int kMaxGrains = 128;
    static constexpr int kGrainMask = kMaxGrains - 1;
    static constexpr int kMidiEvents = 64;

    // A decided frame, as the marks read it: whether it is voiced, its pitch and the main voices' output pitch
    // (semitones), the formants' move (semitones), and for the editor the note it is held on.
    struct Control {
        bool voiced = false;
        double pitch = 0.0, out = 0.0, formant = 0.0;
        int note = -1;
    };
    // What a position on the input's timeline reads from the frames either side of it.
    struct At {
        double analysisPeriod = 0.0, out = 0.0, formant = 0.0;
        bool voiced = false, exact = false;
    };
    // A grain being played (GigaTune's, and: its gain, and for Harmonics its rotation and how much it is heard).
    struct Grain {
        double s = 0.0, a = 0.0, left = 0.0, right = 0.0, alpha = 1.0, gain = 1.0, fraction = 0.0;
        double rotRe = 1.0, rotIm = 0.0, stepRe = 1.0, stepIm = 0.0, shiftMix = 0.0;
        int64_t k = 0, j = 0, first = 0, last = -1, shift = 0;
        bool done = true, open = false, voiced = false;
        EdgeWarp leftWarp, rightWarp;  // its edges bent (elastic: set as its neighbours start)
        std::array<double, 2 * kSincHalf> taps{};
    };
    // A voice: its synthesis marks and grains, and what it plays.
    struct Voice {
        int input = 0, output = 0;
        bool twin = false;
        double offset = 0.0;   // semitones on the output pitch (Detune, the twin's interval)
        double formant = 0.0;  // semitones on the formants (the twin's interval)
        double voicedGain = 1.0, unvoicedGain = 1.0;
        int thinned = 0;       // Width thins its grains of this parity (0 even, 1 odd)
        std::vector<double> synthesis;
        std::vector<uint8_t> synthesisVoiced;  // (whether the step to the mark was voiced)
        int64_t synthesisCount = 0, synthesisSettled = 0, synthesisCommitted = 0;
        int64_t nextGrain = 1, nearest = 0, nearestCommitted = 0, lastFirst = 0;
        Grain plan;
        int64_t planJ = 0;
        bool planned = false;
        std::array<Grain, kMaxGrains> grains{};
        int grainHead = 0, grainCount = 0;
        Random random;
        // The main voices run on their channels throughout; a twin from when Twin comes up (taking its main voice's
        // marks, so it starts in step) until its last grain ends after Twin goes to 0. Only running voices commit
        // marks: with Twin at 0 the main voices place them as GigaTune's corrector does (a ratio of 1 is the input).
        bool running = false;
        std::array<double, 16> profile{};  // (tests: its last bent grain's left warp, for warpChange())
        bool hasProfile = false;
    };
    struct Midi {
        int64_t time = 0;
        int note = -1;
    };

    std::array<gigatune::PitchTracker, 2> trackers;  // lag 0 (Live, High Quality), kStudioLag (Studio)
    Closures closures;  // where the grains are centred
    gigatune::PitchTracker* tracker = &trackers[0];
    double rate = 48000.0;
    int hop = 0, latency = 0;
    std::array<int, 3> latencies{};  // by quality
    Quality quality = Quality::Studio;
    double unvoicedPeriod = 240.0, longest = 0.0, reach = 0.0, smearMarks = 0.0;
    int commitAhead = 0;
    Controls controls;
    const double* sinc = nullptr;
    // The reading between samples at kPhases + 1 fractions of one, 2 kSincHalf taps each (the fast path's): a bent
    // grain read at speed 1 takes its taps between the two nearest (within 1e-4 of the sinc's own: -90 dB).
    static constexpr int kPhases = 256;
    static constexpr int kTaps = 2 * kSincHalf;
    std::vector<double> phases;

    std::vector<Control> control;
    size_t controlMask = 0;
    int64_t controlCount = 0;

    // The output pitch's glides: of the transposition (Pitch and MIDI), and of the note Quantize or Robot holds.
    dsp::Glide shiftGlide, noteGlide;
    int target = -1;
    bool wasVoiced = false;
    bool robotSung = false;  // Robot has sung a frame since the reset (its first starts on its note)
    std::array<Midi, kMidiEvents> midi{};
    int midiCount = 0, midiHead = 0;
    int midiBefore = -1;  // the note held before the oldest one kept

    // The analysis marks, shared by the voices: positions, whether each grain is voiced, and its closure's offset.
    std::vector<double> analysis, offsets, sureness;
    std::vector<uint8_t> analysisVoiced;
    size_t markMask = 0;
    int64_t analysisCount = 0, analysisSettled = 0, analysisCommitted = 0;

    std::array<Voice, kVoices> voices;
    int channels = 1;
    // The input per channel, and its quadrature pair (Harmonics).
    std::array<std::vector<float>, 2> input, inputI, inputQ;
    std::array<Quadrature, 2> quadrature;
    size_t inputMask = 0;
    int64_t time = 0;
    int64_t late = 0, voicedCount = 0, dropped = 0, elasticCount = 0, sharedCount = 0, correlatedCount = 0;
    double correlatedShift = 0.0, changeSum = 0.0;
    int64_t changeCount = 0;

    void prepare(double sampleRate) {
        rate = sampleRate;
        sinc = gigatune::sincTable();
        phases.assign(static_cast<size_t>((kPhases + 1) * kTaps), 0.0);
        for (int p = 0; p <= kPhases; ++p)
            for (int t = 0; t < kTaps; ++t)
                phases[static_cast<size_t>(p * kTaps + t)] =
                    sincAt(sinc, static_cast<double>(p) / kPhases - (t - (kSincHalf - 1)));
        trackers[0].prepare(sampleRate, 0);
        trackers[1].prepare(sampleRate, kStudioLag);
        // a short note pYIN's path leaves unvoiced would come out unshifted: rescued where clearly periodic
        for (auto& t : trackers) t.setRescue(true);
        hop = trackers[0].hop();
        unvoicedPeriod = kUnvoicedPeriod * rate;
        longest = std::max(rate / lowestF0(), unvoicedPeriod);
        // A grain reaches back half its width: from its centre to the one before (a step, and the two centres'
        // offsets), read at the slowest (formants down kMaxRead), its centre up to an offset from its mark.
        const double slowest = std::exp2(-kMaxRead / 12.0);
        const double longestSynthesis = std::max(longest, rate / midiHz(kLowestOut));
        const double offsetMost = kOffsetShare * unvoicedPeriod;
        reach = std::max({(longest + 2.0 * offsetMost) / slowest, 2.0 * unvoicedPeriod, longest}) + offsetMost;
        // High Quality: behind its input far enough for every mark a grain needs to be settled when it starts.
        const double behind = trackers[0].delay(gigatune::Quality::HighQuality) + hop +
                              std::max(2.5 * longest, 0.5 * longestSynthesis) + reach;
        latencies[static_cast<size_t>(Quality::HighQuality)] = static_cast<int>(std::ceil(behind)) + 2;
        latencies[static_cast<size_t>(Quality::Live)] = static_cast<int>(std::lround(kLiveSeconds * rate));
        latencies[static_cast<size_t>(Quality::Studio)] = latencies[0] + kStudioLag * hop;
        commitAhead = static_cast<int>(std::lround(kCommitAheadSeconds * rate));
        const int most = *std::max_element(latencies.begin(), latencies.end());

        const double smearSamples = kSmearSeconds * rate;
        const auto span = static_cast<size_t>(most) + static_cast<size_t>(std::ceil(4.0 * longest / slowest + smearSamples)) +
                          static_cast<size_t>(4 * kSincHalf * std::exp2(kMaxRead / 12.0)) + 64;
        for (auto* rings : {&input, &inputI, &inputQ})
            for (auto& c : *rings) c.assign(powerOfTwoAtLeast(span), 0.f);
        inputMask = input[0].size() - 1;
        closures.prepare(rate, static_cast<int>(span), longest);
        const double shortestStep = std::min(unvoicedPeriod, rate / midiHz(kHighestOut));
        smearMarks = smearSamples / std::min(unvoicedPeriod, rate / highestF0()) + 2.0;
        const auto marks = static_cast<size_t>(std::ceil(static_cast<double>(span) / std::max(1.0, shortestStep))) +
                           static_cast<size_t>(smearMarks) + 64;
        analysis.assign(powerOfTwoAtLeast(marks), 0.0);
        offsets.assign(analysis.size(), 0.0);
        sureness.assign(analysis.size(), 0.0);
        analysisVoiced.assign(analysis.size(), 0);
        markMask = analysis.size() - 1;
        for (Voice& v : voices) {
            v.synthesis.assign(analysis.size(), 0.0);
            v.synthesisVoiced.assign(analysis.size(), 0);
        }
        for (int i = 0; i < kVoices; ++i) {
            voices[static_cast<size_t>(i)].input = voices[static_cast<size_t>(i)].output = i % 2;
            voices[static_cast<size_t>(i)].twin = i >= 2;
            voices[static_cast<size_t>(i)].thinned = i % 2;
        }
        control.assign(powerOfTwoAtLeast(span / static_cast<size_t>(hop) + kStudioLag + 16), {});
        controlMask = control.size() - 1;
        selectTracker();
        latency = latencies[static_cast<size_t>(quality)];
        reset();
    }

    void selectTracker() noexcept {
        tracker = &trackers[quality == Quality::Studio ? 1 : 0];
        tracker->setQuality(quality == Quality::HighQuality ? gigatune::Quality::HighQuality
                                                            : gigatune::Quality::LowLatency);
    }

    void reset() noexcept {
        for (auto& t : trackers) t.reset();
        closures.reset();
        for (auto* rings : {&input, &inputI, &inputQ})
            for (auto& c : *rings) std::fill(c.begin(), c.end(), 0.f);
        for (auto& q : quadrature) q.reset();
        controlCount = 0;
        shiftGlide.snap(0.0);
        noteGlide.snap(kRobotNote);
        target = -1;
        wasVoiced = false;
        robotSung = false;
        midiCount = midiHead = 0;
        midiBefore = -1;
        // a mark before the start, and one at it (both unvoiced: the input before the start is silence)
        analysis[0] = -unvoicedPeriod;
        analysis[1] = 0.0;
        offsets[0] = offsets[1] = 0.0;
        sureness[0] = sureness[1] = 0.0;
        analysisVoiced[0] = analysisVoiced[1] = 0;
        analysisCount = analysisSettled = analysisCommitted = 2;
        for (Voice& v : voices) {
            v.synthesis[0] = -unvoicedPeriod;
            v.synthesis[1] = 0.0;
            v.synthesisVoiced[0] = v.synthesisVoiced[1] = 0;
            v.synthesisCount = v.synthesisSettled = v.synthesisCommitted = 2;
            v.nextGrain = 1;
            v.nearest = v.nearestCommitted = 0;
            v.lastFirst = std::numeric_limits<int64_t>::min();
            v.planned = false;
            v.grainHead = v.grainCount = 0;
            v.running = !v.twin && v.output < channels;
            v.hasProfile = false;
        }
        time = 0;
        late = voicedCount = dropped = elasticCount = sharedCount = correlatedCount = 0;
        correlatedShift = changeSum = 0.0;
        changeCount = 0;
    }

    double& analysisAt(int64_t k) noexcept { return analysis[static_cast<size_t>(k) & markMask]; }
    double& offsetAt(int64_t k) noexcept { return offsets[static_cast<size_t>(k) & markMask]; }
    double centreAt(int64_t k) noexcept { return analysisAt(k) + offsetAt(k); }
    bool voicedAt(int64_t k) const noexcept { return analysisVoiced[static_cast<size_t>(k) & markMask] != 0; }
    double& synthesisAt(Voice& v, int64_t k) noexcept { return v.synthesis[static_cast<size_t>(k) & markMask]; }
    double periodOf(double pitch) const noexcept { return rate / midiHz(pitch); }

    // The oldest analysis mark any voice may still read (a grain may take one Smear's reach before its nearest).
    int64_t oldestNeeded() const noexcept {
        int64_t oldest = std::numeric_limits<int64_t>::max();
        for (const Voice& v : voices)
            if (v.running) oldest = std::min(oldest, v.nearestCommitted);
        return oldest - static_cast<int64_t>(smearMarks) - 4;
    }

    // The frames either side of a position: the analysis period, the main voices' output pitch, the formants' move
    // and whether it is voiced (the nearer frame's voicing). Past the last decided frame, that one's: `exact` false.
    At at(double position) const noexcept {
        At r;
        r.analysisPeriod = unvoicedPeriod;
        if (controlCount == 0) return r;
        const double x = position / hop;
        const double whole = std::floor(x);
        auto g0 = static_cast<int64_t>(whole);
        double f = x - whole;
        if (g0 < 0) {
            g0 = 0;
            f = 0.0;
        } else if (g0 + 1 < controlCount) {
            r.exact = true;
        } else {
            g0 = controlCount - 1;
            f = 0.0;
        }
        const Control& c0 = control[static_cast<size_t>(g0) & controlMask];
        const Control& c1 = r.exact ? control[static_cast<size_t>(g0 + 1) & controlMask] : c0;
        r.voiced = f < 0.5 ? c0.voiced : c1.voiced;
        if (!r.voiced) return r;
        double pitch;
        if (c0.voiced && c1.voiced) {
            pitch = c0.pitch + (c1.pitch - c0.pitch) * f;
            r.out = c0.out + (c1.out - c0.out) * f;
            r.formant = c0.formant + (c1.formant - c0.formant) * f;
        } else {
            const Control& c = c0.voiced ? c0 : c1;
            pitch = c.pitch;
            r.out = c.out;
            r.formant = c.formant;
        }
        r.analysisPeriod = periodOf(pitch);
        return r;
    }

    // The note held at an input time by the MIDI played (-1: none yet).
    int midiAt(int64_t t) const noexcept {
        int note = midiBefore;
        for (int i = 0; i < midiCount; ++i) {
            const Midi& m = midi[static_cast<size_t>((midiHead + i) % kMidiEvents)];
            if (m.time > t) break;
            note = m.note;
        }
        return note;
    }

    void addMidi(int note) noexcept {
        if (midiCount == kMidiEvents) {  // the oldest goes: its note is the one held before the others
            midiBefore = midi[static_cast<size_t>(midiHead)].note;
            midiHead = (midiHead + 1) % kMidiEvents;
            --midiCount;
        }
        midi[static_cast<size_t>((midiHead + midiCount) % kMidiEvents)] = {time, note};
        ++midiCount;
    }

    double glideCoefficient() const noexcept {
        // Two one-poles in a row (dsp::Glide): 10 % to 90 % of a step in about 3.36 time constants.
        if (!(controls.glideMs > 0.0)) return 1.0;
        const double tau = controls.glideMs / 3.36 * 0.001 * rate;
        return 1.0 - std::exp(-static_cast<double>(hop) / tau);
    }

    // A decided frame: the output's pitch at its time; the provisional marks are placed again.
    void onFrame(const gigatune::PitchFrame& frame) noexcept {
        const int note = midiAt(frame.index * hop);
        const double k = glideCoefficient();
        const double transpose = controls.pitch + (note >= 0 && controls.mode != Mode::Robot ? note - kRobotNote : 0);
        // From a reset (playback starting, an offline render) the glides start where they are going: no scoop into
        // the first note.
        if (controlCount == 0) shiftGlide.snap(transpose);
        const double shift = shiftGlide.next(transpose, k, 1e-6);
        Control c;
        if (frame.voiced) {
            const double pitch = 69.0 + 12.0 * std::log2(frame.f0 / 440.0);
            double out = pitch + shift;
            int held = -1;
            if (controls.mode == Mode::Quantize) {
                target = gigatune::targetNote(pitch, controls.notes, target);
                if (!wasVoiced) noteGlide.snap(target);
                out = noteGlide.next(target, k, 1e-6) + shift;
                held = target;
            } else if (controls.mode == Mode::Robot) {
                const double robot = (note >= 0 ? note : kRobotNote) + controls.pitch;
                if (!robotSung) noteGlide.snap(robot);
                robotSung = true;
                out = noteGlide.next(robot, k, 1e-6);
                held = static_cast<int>(std::lround(robot));
            }
            out = std::clamp(out, std::max(kLowestOut, pitch - kMaxShift), std::min(kHighestOut, pitch + kMaxShift));
            c = {true, pitch, out, controls.formant + (controls.link ? out - pitch : 0.0), held};
            ++voicedCount;
        } else {
            target = -1;
        }
        wasVoiced = frame.voiced;
        control[static_cast<size_t>(frame.index) & controlMask] = c;
        controlCount = frame.index + 1;
        analysisCount = std::min(analysisCount, std::max(analysisSettled, analysisCommitted));
        for (Voice& v : voices) {
            v.synthesisCount = std::min(v.synthesisCount, std::max(v.synthesisSettled, v.synthesisCommitted));
            v.nearest = std::min(v.nearest, v.nearestCommitted);
            v.planned = false;
        }
    }

    // The next analysis mark: GigaTune's (a period on; the first voiced one after unvoiced ones moved onto its
    // closure, the grains' centres following the closures slowly after it; each settled mark's closure teaching
    // Closures where they lie). False when the ring is full.
    bool addAnalysis() noexcept {
        const int64_t m = analysisCount;
        if (m - oldestNeeded() + 16 >= static_cast<int64_t>(analysis.size())) return false;
        const double a = analysisAt(m - 1);
        const At here = at(a);
        const At mid = at(a + 0.5 * here.analysisPeriod);
        const double t2 = mid.analysisPeriod;
        bool exact = here.exact && mid.exact;
        double next = a + t2, offset = 0.0;
        bool learn = false;
        double relative = 0.0, closureSure = 0.0;
        if (mid.voiced) {
            double pulse, sure;  // (the closure's offset)
            exact = closures.find(next, t2, pulse, sure, relative) && exact;
            learn = true;
            closureSure = sure;
            if (!here.voiced || m == 2 || !voicedAt(m - 1)) {
                if (sure >= kOnsetSureness) {
                    if (next + pulse > a + longest) pulse -= t2;
                    if (next + pulse >= a + 0.25 * t2) next += pulse;
                }
            } else {
                const double previous = offsetAt(m - 1);
                const double towards = pulse - t2 * std::round((pulse - previous) / t2);
                const double limit = kOffsetShare * std::min(t2, unvoicedPeriod);
                offset = std::clamp(previous + kOffsetFollow * sure * (towards - previous), -limit, limit);
                offset = std::max(offset, a + previous + 0.25 * t2 - next);
            }
        }
        offsetAt(m) = offset;
        sureness[static_cast<size_t>(m) & markMask] = closureSure;
        analysisVoiced[static_cast<size_t>(m) & markMask] = mid.voiced ? 1 : 0;
        analysisAt(m) = next;
        analysisCount = m + 1;
        if (exact && analysisSettled == m) {
            analysisSettled = m + 1;
            if (learn) closures.learn(relative);  // (once a mark, as it settles)
        }
        return true;
    }

    // A voice's next synthesis mark: an output period on where voiced (its own pitch: the main voices' moved by its
    // offset; Smear moves it at random), else the next analysis mark. False when a ring is full.
    bool addSynthesis(Voice& v) noexcept {
        const int64_t k = v.synthesisCount;
        if (k - v.nextGrain + 16 >= static_cast<int64_t>(v.synthesis.size())) return false;
        const double s = synthesisAt(v, k - 1);
        const At here = at(s);
        double next;
        uint8_t stepVoiced = 0;
        bool exact = here.exact;
        if (here.voiced && v.synthesisVoiced[static_cast<size_t>(k - 1) & markMask] != 0) {
            const double t1 = periodOf(here.out + v.offset);
            const At mid = at(s + 0.5 * t1);
            double t2 = mid.voiced ? periodOf(mid.out + v.offset) : unvoicedPeriod;  // (as the analysis marks step)
            // (moved in proportion, up or down alike: the pitch it averages to is the voice's)
            if (controls.smear > 0.0)
                t2 *= std::exp2(controls.smear * std::log2(1.0 + kSmearJitter) * (2.0 * v.random.next() - 1.0));
            next = s + t2;
            stepVoiced = mid.voiced ? 1 : 0;
            exact = exact && mid.exact;
        } else {
            while (!(analysisAt(analysisCount - 1) > s))
                if (!addAnalysis()) return false;
            int64_t i = analysisCount - 1;
            while (i > v.nearestCommitted && analysisAt(i - 1) > s) --i;
            next = analysisAt(i);
            stepVoiced = voicedAt(i) ? 1 : 0;
            exact = exact && i < analysisSettled;
        }
        v.synthesisVoiced[static_cast<size_t>(k) & markMask] = stepVoiced;
        synthesisAt(v, k) = next;
        v.synthesisCount = k + 1;
        if (exact && v.synthesisSettled == k) v.synthesisSettled = k + 1;
        return true;
    }

    // Synthesis mark k of a voice playing analysis mark j's grain: where, how fast it is read, how loud.
    void shape(Voice& v, int64_t k, int64_t j, Grain& g) noexcept {
        const bool voicedGrain = voicedAt(j);
        const double offset = voicedGrain ? offsetAt(j) : 0.0;
        g.k = k;
        g.j = j;
        g.voiced = voicedGrain;
        g.leftWarp = g.rightWarp = {};
        g.s = synthesisAt(v, k) + offset;
        g.a = analysisAt(j) + offset;
        g.shiftMix = 0.0;
        if (voicedGrain) {
            // Formant reads it faster or slower (the frame's move, and the twin's interval); unvoiced grains play as
            // they were.
            const At here = at(analysisAt(j));
            const double move = std::clamp(here.formant + v.formant, -kMaxRead, kMaxRead);
            g.alpha = std::exp2(move / 12.0);
            g.left = (g.a - centreAt(j - 1)) / g.alpha;
            const double outPeriod = 0.5 * (synthesisAt(v, k + 1) - synthesisAt(v, k - 1));
            const double r = std::clamp((analysisAt(j + 1) - analysisAt(j - 1)) / (synthesisAt(v, k + 1) - synthesisAt(v, k - 1)),
                                        kLowestRatio, 1.0 / kLowestRatio);
            const double q = r / g.alpha;
            // (raised by kMostRaise at the most: grains read far faster than they play, a short burst each period,
            // would come out spiky)
            g.gain = std::sqrt(r) * (q < 1.0 ? std::min(1.0 / q, kMostRaise) : 1.0) * v.voicedGain;
            if (controls.width > 0.0 && (k & 1) == v.thinned) g.gain *= 1.0 - controls.width;
            // Harmonics: the content shifted by h of the output's harmonics, about the grain's centre.
            const double h = controls.harmonics;
            if (h != 0.0 && outPeriod > 0.0) {
                g.shiftMix = std::min(1.0, 2.0 * std::abs(h));
                const double step = 2.0 * kPi * h / outPeriod;
                fastmath::sincos(step, g.stepIm, g.stepRe);
            }
        } else {
            g.alpha = 1.0;
            g.gain = v.unvoicedGain;
            const double past = kOffsetShare * unvoicedPeriod;
            g.left = std::max(2.0 * unvoicedPeriod, std::min(g.s - synthesisAt(v, k - 1), longest) + past);
        }
        g.first = static_cast<int64_t>(std::floor(g.s - g.left)) + 1;
        shapeRight(v, g);
    }

    void shapeRight(Voice& v, Grain& g) noexcept {
        if (g.voiced) {
            g.right = (centreAt(g.j + 1) - g.a) / g.alpha;
        } else {
            const double past = kOffsetShare * unvoicedPeriod;
            g.right = std::max(2.0 * unvoicedPeriod, std::min(synthesisAt(v, g.k + 1) - g.s, longest) + past);
        }
        g.last = static_cast<int64_t>(std::ceil(g.s + g.right)) - 1;
    }

    bool settleRight(Voice& v, Grain& g) noexcept {
        while (analysisCount <= g.j + 1)
            if (!addAnalysis()) return false;
        while (v.synthesisCount <= g.k + 1)
            if (!addSynthesis(v)) return false;
        shapeRight(v, g);
        analysisCommitted = std::max(analysisCommitted, g.j + 2);
        v.synthesisCommitted = std::max(v.synthesisCommitted, g.k + 2);
        analysisSettled = std::max(analysisSettled, analysisCommitted);
        v.synthesisSettled = std::max(v.synthesisSettled, v.synthesisCommitted);
        if (g.alpha > 1.0 && lookahead(g) > latency) {
            const double taps = std::ceil(kSincHalf * g.alpha) + 1.0;
            g.right = std::max(0.0, (latency - taps - (g.a - g.s) - g.rightWarp.most()) / (g.alpha - 1.0) - 1.0);
            g.last = static_cast<int64_t>(std::ceil(g.s + g.right)) - 1;
        }
        // (a right edge bent later still reads only input that is in: its warp is held to what is left; it starts
        // past the dead zone, which the output hasn't reached yet)
        if (g.rightWarp.on && lookahead(g) > latency) holdWarp(g.rightWarp, g.rightWarp.most() - (lookahead(g) - latency));
        g.open = false;
        return true;
    }

    static double lookahead(const Grain& g) noexcept {
        const double taps = g.alpha > 1.0 ? std::ceil(kSincHalf * g.alpha) + 1.0 : kSincHalf + 1.0;
        const double atFirst = g.a - g.s + (static_cast<double>(g.first) - g.s) * (g.alpha - 1.0) + g.leftWarp.most();
        const double atLast = g.a - g.s + (static_cast<double>(g.last) - g.s) * (g.alpha - 1.0) + g.rightWarp.most();
        return std::max(atFirst, atLast) + taps;
    }

    // A warp's knots held to read no later than `most` samples past where it would unbent.
    static void holdWarp(EdgeWarp& w, double most) noexcept {
        if (!w.on) return;
        const double cap = std::max(0.0, most);
        std::array<double, EdgeWarp::kMaxKnots> d{};
        for (int k = 0; k < w.count; ++k) d[static_cast<size_t>(k)] = std::min(w.values[static_cast<size_t>(k + 1)], cap);
        const auto at = w.places;
        w.set(w.width, w.count, at.data() + 1, d.data());
    }

    // Synthesis mark k's grain as it would play now: the analysis mark nearest it (Smear: one further back, at
    // random), or (Live, Studio) the nearest before that whose grain reads no input that won't be in.
    bool planGrain(Voice& v, int64_t k, Grain& g, int64_t& j) noexcept {
        while (v.synthesisCount <= k + 1)
            if (!addSynthesis(v)) return false;
        const double s = synthesisAt(v, k);
        for (;;) {
            while (analysisCount <= v.nearest + 1)
                if (!addAnalysis()) return false;
            if (!(analysisAt(v.nearest + 1) <= s)) break;
            ++v.nearest;
        }
        j = std::max<int64_t>(s - analysisAt(v.nearest) <= analysisAt(v.nearest + 1) - s ? v.nearest : v.nearest + 1, 1);
        if (controls.smear > 0.0 && voicedAt(j)) {
            const At here = at(analysisAt(j));
            const double back = controls.smear * kSmearSeconds * rate / here.analysisPeriod * v.random.next();
            const int64_t oldest = std::max<int64_t>(1, analysisCount - static_cast<int64_t>(analysis.size()) + 32);
            int64_t smeared = std::max(oldest, j - static_cast<int64_t>(back));
            while (smeared < j && !voicedAt(smeared)) ++smeared;  // a voiced grain for a voiced one
            j = smeared;
        }
        while (analysisCount <= j + 1)
            if (!addAnalysis()) return false;
        for (;; --j) {
            shape(v, k, j, g);
            if (j <= std::max<int64_t>(1, v.nearestCommitted - 4 - static_cast<int64_t>(smearMarks)) || lookahead(g) <= latency)
                break;
        }
        return true;
    }

    static void squeeze(Grain& g, int64_t from) noexcept {
        g.left = std::max(0.5, g.s - static_cast<double>(from) + 0.5);
        g.first = static_cast<int64_t>(std::floor(g.s - g.left)) + 1;
    }

    // Elastic PSOLA: a grain starting (g, at output sample `now`) and the one before it (p) bent to agree where they
    // overlap. g's left edge is bent, and while p's right edge hasn't been played past its dead zone, p's right edge
    // too, the other way (each half of it); else g's alone. The knots' values c minimise
    //     J(c) = E(c) + c' M c
    // (Measured on a synthetic vowel with vibrato and jitter, -7 to +9 semitones, 110 to 220 Hz: E is about 1 unbent,
    // the overlapping grains' contents uncorrelated, a closure's tail against the next one's head. Bent, the level
    // lost to cancelling goes from 1.1 dB to 0.95 on average (1.0 to 0.4 at best), Robot's harmonics 0.1 dB clearer on
    // average and 0.3 dB less clear at worst. A cost a fifth of this bends twice as far: twice the level back, but a
    // jittered voice's warps then differ enough from period to period to roughen it, 1.3 dB less clear at worst.)
    // E: the two grains' mismatch over what is left of the overlap, each point weighted by both windows (where the
    // two are alike loud, and what differs cancels), against their energy there; M: the warps' size, slope and
    // curvature (d, d' and d'' squared, over each bent side), each as a share of what its limits allow. Searched
    // whole samples at a time with the knots in proportion to their limits, a parabola through the best, then knot by
    // knot in halving steps (a sample's half, quarter and eighth); left unbent where that doesn't do better. The reads
    // here are linear (where a grain is, not how it sounds).
    void bend(Voice& v, Grain& p, Grain& g, int64_t now) noexcept {
        // both reading the same input (a ratio of 1): they agree already, and the input stays bit for bit
        if (g.alpha == 1.0 && p.alpha == 1.0 && std::abs((g.a - g.s) - (p.a - p.s)) < 1e-9) return;
        const int64_t lo = std::max({g.first, p.first, now});
        const int64_t hi = std::min(p.last, g.last);
        if (hi - lo < kBendLeast) return;
        const bool shared = static_cast<double>(now) - p.s <= EdgeWarp::kDead * p.right;
        const double shareG = shared ? 0.5 : 1.0, shareP = shared ? 0.5 : 0.0;
        const double period = g.left * g.alpha;  // (input samples from the closure before to this one)
        using Knots = std::array<double, EdgeWarp::kMaxKnots>;
        const int K = std::clamp(controls.warpKnots, 1, EdgeWarp::kMaxKnots);
        const auto L = static_cast<size_t>(K - 1);  // the edge's knot
        Knots place{}, limit{};
        EdgeWarp::layout(K, place.data(), limit.data());
        for (size_t k = 0; k <= L; ++k) limit[k] *= period;
        if (!(limit[L] > 0.25)) return;

        // Each knot's own warp (its value 1, the others 0), for the points' bases and M.
        std::array<EdgeWarp, EdgeWarp::kMaxKnots> unitG, unitP;
        for (size_t k = 0; k <= L; ++k) {
            Knots e{};
            e[k] = 1.0;
            unitG[k].set(g.left, K, place.data(), e.data());
            unitP[k].set(p.right, K, place.data(), e.data());
        }
        std::array<Knots, EdgeWarp::kMaxKnots> M{};
        const auto addCost = [&](const std::array<EdgeWarp, EdgeWarp::kMaxKnots>& unit, double half, double share) {
            if (!(share > 0.0) || !(half > 0.0)) return;
            const int kSteps = std::max(24, 4 * K);
            const double from = EdgeWarp::kDead * half, du = (half - from) / kSteps;
            const double size = kBendSize / (limit[L] * limit[L]) / kSteps;
            const double slope = kBendSlope * (half * half) / (limit[L] * limit[L]) / kSteps;
            const double curve = kBendCurve * (half * half * half * half) / (limit[L] * limit[L]) / kSteps;
            for (int i = 0; i <= kSteps; ++i) {
                const double u = from + du * i;
                Knots b{}, d1{}, d2{};
                for (size_t k = 0; k <= L; ++k) {
                    const double before = unit[k].at(u - du), here = unit[k].at(u), after = unit[k].at(u + du);
                    b[k] = here;
                    d1[k] = (after - before) / (2.0 * du);
                    d2[k] = (after - 2.0 * here + before) / (du * du);
                }
                for (size_t k = 0; k <= L; ++k)
                    for (size_t l = 0; l <= L; ++l)
                        M[k][l] += share * share * (size * b[k] * b[l] + slope * d1[k] * d1[l] + curve * d2[k] * d2[l]);
            }
        };
        addCost(unitG, g.left, shareG);
        addCost(unitP, p.right, shareP);

        // The overlap's points: where each grain reads unbent, how each knot moves it, and how much the point counts.
        struct Point {
            double q, readP, readG;
            Knots moveP, moveG;
        };
        std::array<Point, kBendPoints> points;
        const int count = static_cast<int>(std::min<int64_t>(kBendPoints, hi - lo + 1));
        int used = 0;
        for (int i = 0; i < count; ++i) {
            const int64_t n = lo + (count > 1 ? (hi - lo) * i / (count - 1) : 0);
            const double uP = static_cast<double>(n) - p.s, uG = static_cast<double>(n) - g.s;
            const double q = window(uP, p.left, p.right) * window(uG, g.left, g.right);
            if (!(q > 0.0)) continue;
            Point& pt = points[static_cast<size_t>(used++)];
            pt.q = q;
            pt.readP = p.a + p.alpha * uP + (uP < 0.0 ? p.leftWarp.at(-uP) : p.rightWarp.at(uP));
            pt.readG = g.a + g.alpha * uG;
            for (size_t k = 0; k <= L; ++k) {
                pt.moveP[k] = uP > 0.0 ? -shareP * unitP[k].at(uP) : 0.0;
                pt.moveG[k] = uG < 0.0 ? shareG * unitG[k].at(-uG) : 0.0;
            }
        }
        if (used < 4) return;
        const float* x = input[static_cast<size_t>(v.input)].data();
        const auto read = [&](double position) {
            const double whole = std::floor(position);
            const double f = position - whole;
            const auto i = static_cast<size_t>(static_cast<int64_t>(whole));
            return x[i & inputMask] + f * (x[(i + 1) & inputMask] - x[i & inputMask]);
        };
        const auto cost = [&](const Knots& c) {
            double mismatch = 0.0, energy = 0.0;
            for (int i = 0; i < used; ++i) {
                const Point& pt = points[static_cast<size_t>(i)];
                double mp = 0.0, mg = 0.0;
                for (size_t k = 0; k <= L; ++k) mp += pt.moveP[k] * c[k], mg += pt.moveG[k] * c[k];
                const double xp = read(pt.readP + mp), xg = read(pt.readG + mg);
                mismatch += pt.q * (xp - xg) * (xp - xg);
                energy += pt.q * (xp * xp + xg * xg);
            }
            double warp = 0.0;
            for (size_t k = 0; k <= L; ++k)
                for (size_t l = 0; l <= L; ++l) warp += c[k] * M[k][l] * c[l];
            return (energy > 0.0 ? mismatch / energy : 0.0) + warp;
        };
        // WSOLA's correlation shift (Controls::correlate, an experiment): the whole of g read up to half a period later
        // or earlier, where the overlap agrees best (no cost for the move: it moves the closure too). Searched in 64ths
        // of a period, then whole samples about the best, a parabola through it.
        const bool unsure = sureness[static_cast<size_t>(g.j) & markMask] < kCorrelateUnsure;
        if (controls.correlate == Correlate::Always || (controls.correlate == Correlate::Unsure && unsure)) {
            const auto moved = [&](double sigma) {
                double mismatch = 0.0, energy = 0.0;
                for (int i = 0; i < used; ++i) {
                    const Point& pt = points[static_cast<size_t>(i)];
                    const double xp = read(pt.readP), xg = read(pt.readG + sigma);
                    mismatch += pt.q * (xp - xg) * (xp - xg);
                    energy += pt.q * (xp * xp + xg * xg);
                }
                return energy > 0.0 ? mismatch / energy : 0.0;
            };
            const double reach = kCorrelateReach * period, room = std::min(reach, latency - lookahead(g));
            const double coarse = std::max(1.0, std::floor(period / 64.0));
            const double still = moved(0.0);
            double bestShift = 0.0, bestCost = still;
            for (double sigma = -std::floor(reach / coarse) * coarse; sigma <= room; sigma += coarse) {
                const double j = moved(sigma);
                if (j < bestCost) bestCost = j, bestShift = sigma;
            }
            const double centre = bestShift;
            for (double sigma = centre - coarse + 1.0; sigma < centre + coarse; sigma += 1.0) {
                if (sigma > room || sigma < -reach) continue;
                const double j = moved(sigma);
                if (j < bestCost) bestCost = j, bestShift = sigma;
            }
            if (bestShift - 1.0 >= -reach && bestShift + 1.0 <= room) {
                const double before = moved(bestShift - 1.0), after = moved(bestShift + 1.0);
                const double curve = before - 2.0 * bestCost + after;
                if (curve > 0.0) {
                    const double between = bestShift + std::clamp(0.5 * (before - after) / curve, -0.5, 0.5);
                    const double j = moved(between);
                    if (j < bestCost) bestCost = j, bestShift = between;
                }
            }
            if (bestShift != 0.0 && bestCost < still - kBendBetter) {
                g.a += bestShift;
                for (int i = 0; i < used; ++i) points[static_cast<size_t>(i)].readG += bestShift;
                ++correlatedCount;
                correlatedShift += std::abs(bestShift);
            }
        }
        if (!controls.elastic) return;

        const auto clamped = [&](Knots c) {
            for (size_t k = 0; k <= L; ++k) c[k] = std::clamp(c[k], -limit[k], limit[k]);
            return c;
        };

        // The knots in proportion to their limits, the edge's moved in kBendSteps steps either way (a sample each at
        // the least); a parabola through the best.
        const auto alongBy = [&](double t) {
            Knots c{};
            for (size_t k = 0; k <= L; ++k) c[k] = limit[k] / limit[L] * t;
            return clamped(c);
        };
        const double unbent = cost(Knots{});
        const double unit = std::max(1.0, limit[L] / kBendSteps);
        const int steps = std::min(kBendSteps, static_cast<int>(std::floor(limit[L] / unit)));
        int bestStep = 0;
        double best = unbent;
        std::array<double, 2 * kBendSteps + 1> tried{};
        for (int t = -steps; t <= steps; ++t) {
            const double j = t == 0 ? unbent : cost(alongBy(t * unit));
            tried[static_cast<size_t>(t + kBendSteps)] = j;
            if (j < best) best = j, bestStep = t;
        }
        Knots c = alongBy(bestStep * unit);
        if (std::abs(bestStep) < steps) {
            const double before = tried[static_cast<size_t>(bestStep + kBendSteps - 1)];
            const double after = tried[static_cast<size_t>(bestStep + kBendSteps + 1)];
            const double curve = before - 2.0 * best + after;
            if (curve > 0.0) {
                const Knots between = alongBy(unit * (bestStep + std::clamp(0.5 * (before - after) / curve, -0.5, 0.5)));
                const double j = cost(between);
                if (j < best) best = j, c = between;
            }
        }
        // Then (Controls::gaussNewton) all the knots together: Gauss-Newton on E's residuals (each point's two reads'
        // difference, its weight's root, over the energy's root; the reads' slopes linear), M as it is, damped
        // (Levenberg-Marquardt), each step clamped to the limits and kept only where J falls.
        if (controls.gaussNewton) {
            double damping = 1e-3;
            for (int iteration = 0; iteration < 8; ++iteration) {
                double energy = 0.0;
                for (int i = 0; i < used; ++i) {
                    const Point& pt = points[static_cast<size_t>(i)];
                    double mp = 0.0, mg = 0.0;
                    for (size_t k = 0; k <= L; ++k) mp += pt.moveP[k] * c[k], mg += pt.moveG[k] * c[k];
                    const double xp = read(pt.readP + mp), xg = read(pt.readG + mg);
                    energy += pt.q * (xp * xp + xg * xg);
                }
                if (!(energy > 0.0)) break;
                std::array<Knots, EdgeWarp::kMaxKnots> A{};
                Knots gradient{};
                for (int i = 0; i < used; ++i) {
                    const Point& pt = points[static_cast<size_t>(i)];
                    double mp = 0.0, mg = 0.0;
                    for (size_t k = 0; k <= L; ++k) mp += pt.moveP[k] * c[k], mg += pt.moveG[k] * c[k];
                    const double at = pt.readP + mp, ag = pt.readG + mg;
                    const auto slopeAt = [&](double position) {
                        const auto i0 = static_cast<size_t>(static_cast<int64_t>(std::floor(position)));
                        return static_cast<double>(x[(i0 + 1) & inputMask]) - x[i0 & inputMask];
                    };
                    const double w = std::sqrt(pt.q / energy);
                    const double r = w * (read(at) - read(ag));
                    const double sp = slopeAt(at), sg = slopeAt(ag);
                    Knots jac{};
                    for (size_t k = 0; k <= L; ++k) jac[k] = w * (sp * pt.moveP[k] - sg * pt.moveG[k]);
                    for (size_t k = 0; k <= L; ++k) {
                        gradient[k] += jac[k] * r;
                        for (size_t l = 0; l <= L; ++l) A[k][l] += jac[k] * jac[l];
                    }
                }
                for (size_t k = 0; k <= L; ++k) {
                    for (size_t l = 0; l <= L; ++l) {
                        A[k][l] += M[k][l];
                        gradient[k] += M[k][l] * c[l];
                    }
                }
                double scale = 0.0;
                for (size_t k = 0; k <= L; ++k) scale = std::max(scale, A[k][k]);
                bool improved = false;
                for (int attempt = 0; attempt < 4 && !improved; ++attempt) {
                    // (A + damping max(diag) I) step = -gradient, by Gaussian elimination with partial pivoting
                    std::array<Knots, EdgeWarp::kMaxKnots> B = A;
                    Knots step{};
                    for (size_t k = 0; k <= L; ++k) B[k][k] += damping * scale, step[k] = -gradient[k];
                    bool singular = false;
                    for (size_t col = 0; col <= L && !singular; ++col) {
                        size_t pivot = col;
                        for (size_t row = col + 1; row <= L; ++row)
                            if (std::abs(B[row][col]) > std::abs(B[pivot][col])) pivot = row;
                        if (!(std::abs(B[pivot][col]) > 1e-300)) {
                            singular = true;
                            break;
                        }
                        std::swap(B[col], B[pivot]);
                        std::swap(step[col], step[pivot]);
                        for (size_t row = col + 1; row <= L; ++row) {
                            const double f = B[row][col] / B[col][col];
                            for (size_t k = col; k <= L; ++k) B[row][k] -= f * B[col][k];
                            step[row] -= f * step[col];
                        }
                    }
                    if (singular) break;
                    for (size_t row = L + 1; row-- > 0;) {
                        for (size_t k = row + 1; k <= L; ++k) step[row] -= B[row][k] * step[k];
                        step[row] /= B[row][row];
                    }
                    Knots trial = c;
                    for (size_t k = 0; k <= L; ++k) trial[k] += step[k];
                    trial = clamped(trial);
                    const double j = cost(trial);
                    if (j < best) {
                        best = j;
                        c = trial;
                        damping = std::max(1e-6, damping / 3.0);
                        improved = true;
                    } else {
                        damping *= 8.0;
                    }
                }
                if (!improved) break;
            }
        }
        // Then each knot on its own, in halving steps from half the first search's.
        for (const double step : {0.5 * unit, 0.25 * unit, 0.125 * unit, 0.0625 * unit}) {
            for (int pass = 0; pass < 2; ++pass) {
                bool moved = false;
                for (size_t k = 0; k <= L; ++k) {
                    for (const double sign : {1.0, -1.0}) {
                        Knots trial = c;
                        trial[k] += sign * step;
                        trial = clamped(trial);
                        if (trial == c) continue;
                        const double j = cost(trial);
                        if (j < best) {
                            best = j;
                            c = trial;
                            moved = true;
                            break;
                        }
                    }
                }
                if (!moved) break;
            }
        }
        if (!(best < unbent - kBendBetter)) return;

        // (each side held to read only input that is in by then)
        const double roomG = latency - lookahead(g), roomP = latency - lookahead(p);
        Knots dg{}, dp{};
        for (size_t k = 0; k <= L; ++k) dg[k] = shareG * c[k], dp[k] = -shareP * c[k];
        g.leftWarp.set(g.left, K, place.data(), dg.data());
        holdWarp(g.leftWarp, roomG);
        if (shared) {
            p.rightWarp.set(p.right, K, place.data(), dp.data());
            holdWarp(p.rightWarp, roomP);
            ++sharedCount;
        }
        ++elasticCount;
        // (for tests: the left warp's change from the voice's last, over 16 places of the bent side, in periods)
        std::array<double, 16> profile{};
        for (size_t i = 0; i < profile.size(); ++i)
            profile[i] = g.leftWarp.at(g.left * (EdgeWarp::kDead + (1.0 - EdgeWarp::kDead) * (i + 1) / 16.0)) / period;
        if (v.hasProfile) {
            double change = 0.0;
            for (size_t i = 0; i < profile.size(); ++i) change += (profile[i] - v.profile[i]) * (profile[i] - v.profile[i]);
            changeSum += std::sqrt(change / 16.0);
            ++changeCount;
        }
        v.profile = profile;
        v.hasProfile = true;
    }

    // Starts a voice's grains whose windows begin by output sample n (GigaTune's: High Quality as soon as it might,
    // Live and Studio as late as they can, their marks placed with the frames decided by then).
    void startGrains(Voice& v, int64_t n) noexcept {
        const bool early = quality == Quality::HighQuality;
        while (v.grainCount < kMaxGrains) {
            if (early) {
                while (v.synthesisCount <= v.nextGrain)
                    if (!addSynthesis(v)) return;
                if (synthesisAt(v, v.nextGrain) - reach > static_cast<double>(n)) return;
            }
            if (!v.planned) {
                if (!planGrain(v, v.nextGrain, v.plan, v.planJ)) return;
                if (!early && v.plan.first < v.lastFirst) squeeze(v.plan, v.lastFirst);
                v.planned = true;
            }
            if (!early && v.plan.first > n + commitAhead) return;
            Grain& g = v.plan;
            const int64_t j = v.planJ;
            v.planned = false;
            analysisCommitted = std::max({analysisCommitted, j + 1, v.nearest + 1});
            v.synthesisCommitted = std::max(v.synthesisCommitted, v.nextGrain + 1);
            analysisSettled = std::max(analysisSettled, analysisCommitted);
            v.synthesisSettled = std::max(v.synthesisSettled, v.synthesisCommitted);
            v.nearestCommitted = v.nearest;
            ++v.nextGrain;
            if (g.first < n) {
                late += n - g.first;
                squeeze(g, n);
            }
            if (lookahead(g) > latency) {
                late += std::max<int64_t>(0, g.last + 1 - g.first);
                continue;
            }
            v.lastFirst = g.first;
            if ((controls.elastic || controls.correlate != Correlate::Off) && g.voiced && v.grainCount > 0) {
                Grain& previous = v.grains[static_cast<size_t>((v.grainHead + v.grainCount - 1) & kGrainMask)];
                if (!previous.done && previous.voiced) bend(v, previous, g, n);
            }
            if (g.alpha == 1.0) {
                const double offset = g.a - g.s;
                const double whole = std::floor(offset);
                g.fraction = offset - whole;
                g.shift = static_cast<int64_t>(whole);
                if (g.fraction != 0.0)
                    for (int t = 0; t < 2 * kSincHalf; ++t)
                        g.taps[static_cast<size_t>(t)] = sincAt(sinc, g.fraction - (t - (kSincHalf - 1)));
            }
            if (g.shiftMix > 0.0) {  // the rotation at the grain's first sample (phase 0 at its centre)
                const double phase = std::atan2(g.stepIm, g.stepRe) * (static_cast<double>(g.first) - g.s);
                fastmath::sincos(phase, g.rotIm, g.rotRe);
            }
            g.done = false;
            g.open = true;
            v.grains[static_cast<size_t>((v.grainHead + v.grainCount) & kGrainMask)] = g;
            ++v.grainCount;
        }
        if (v.planned || v.grainCount == kMaxGrains) ++dropped;
    }

    // Output sample n of a voice: its grains summed, divided by their windows' sum where it passes 1.
    double render(Voice& v, int64_t n) noexcept {
        double sum = 0.0, weights = 0.0;
        const float* x = input[static_cast<size_t>(v.input)].data();
        const float* xi = inputI[static_cast<size_t>(v.input)].data();
        const float* xq = inputQ[static_cast<size_t>(v.input)].data();
        const double* table = sinc;
        for (int k = 0; k < v.grainCount; ++k) {
            Grain& g = v.grains[static_cast<size_t>((v.grainHead + k) & kGrainMask)];
            if (g.done || n < g.first) continue;
            if (g.open && static_cast<double>(n) + 1.0 > g.s && !settleRight(v, g)) g.last = n;
            if (n > g.last) {
                g.done = true;
                continue;
            }
            const double u = static_cast<double>(n) - g.s;
            const double w = window(u, g.left, g.right);
            double value = 0.0, vi = 0.0, vq = 0.0;
            const bool shifted = g.shiftMix > 0.0;
            const double bent = u < 0.0 ? g.leftWarp.at(-u) : g.rightWarp.at(u);
            if (g.alpha == 1.0 && bent == 0.0) {
                const int64_t base = n + g.shift;
                if (g.fraction == 0.0) {
                    const size_t at = static_cast<size_t>(base) & inputMask;
                    value = x[at];
                    if (shifted) {
                        vi = xi[at];
                        vq = xq[at];
                    }
                } else {
                    for (int t = 0; t < 2 * kSincHalf; ++t) {
                        const size_t at = static_cast<size_t>(base + t - (kSincHalf - 1)) & inputMask;
                        const double tap = g.taps[static_cast<size_t>(t)];
                        value += tap * x[at];
                        if (shifted) {
                            vi += tap * xi[at];
                            vq += tap * xq[at];
                        }
                    }
                }
            } else if (g.alpha == 1.0) {
                // bent: the taps between the two nearest of the bank's fractions
                const double position = g.a + u + bent;
                const double whole = std::floor(position);
                const double at = (position - whole) * kPhases;
                const auto phase = std::min(static_cast<int>(at), kPhases - 1);
                const double mix = at - phase;
                const double* t0 = phases.data() + static_cast<size_t>(phase * kTaps);
                const double* t1 = t0 + kTaps;
                const auto base = static_cast<int64_t>(whole) - (kSincHalf - 1);
                for (int t = 0; t < kTaps; ++t) {
                    const double tap = t0[t] + mix * (t1[t] - t0[t]);
                    const size_t i = static_cast<size_t>(base + t) & inputMask;
                    value += tap * x[i];
                    if (shifted) {
                        vi += tap * xi[i];
                        vq += tap * xq[i];
                    }
                }
            } else {
                // read faster or slower (and bent alike): the taps follow the read, low-passed to 1 / alpha of the band
                // read faster
                const double scale = g.alpha > 1.0 ? 1.0 / g.alpha : 1.0;
                const int half = g.alpha > 1.0 ? static_cast<int>(std::ceil(kSincHalf * g.alpha)) : kSincHalf;
                const double position = g.a + u * g.alpha + bent;
                const double whole = std::floor(position);
                const double fraction = position - whole;
                const auto base = static_cast<int64_t>(whole);
                for (int t = 1 - half; t <= half; ++t) {
                    const double tap = sincAt(table, (fraction - t) * scale) * scale;
                    const size_t at = static_cast<size_t>(base + t) & inputMask;
                    value += tap * x[at];
                    if (shifted) {
                        vi += tap * xi[at];
                        vq += tap * xq[at];
                    }
                }
            }
            if (shifted) {
                const double rotated = vi * g.rotRe - vq * g.rotIm;
                value += g.shiftMix * (rotated - value);
                const double re = g.rotRe * g.stepRe - g.rotIm * g.stepIm;
                g.rotIm = g.rotIm * g.stepRe + g.rotRe * g.stepIm;
                g.rotRe = re;
            }
            sum += g.gain * w * value;
            weights += w;
            if (n >= g.last) g.done = true;
        }
        while (v.grainCount > 0 && v.grains[static_cast<size_t>(v.grainHead)].done) {
            v.grainHead = (v.grainHead + 1) & kGrainMask;
            --v.grainCount;
        }
        return weights > 1.0 ? sum / weights : sum;
    }

    static double window(double u, double left, double right) noexcept {
        const double x = u < 0.0 ? -u / left : u / right;
        if (!(x < 1.0)) return 0.0;
        return 0.5 + 0.5 * fastmath::cos(kPi * x);
    }

    Heard heard(int ago) const noexcept {
        const int64_t n = time - 1 - ago - latency;
        if (n < 0 || controlCount == 0) return {};
        const int64_t index = std::min((n + hop / 2) / hop, controlCount - 1);
        if (index < controlCount - static_cast<int64_t>(control.size())) return {};
        const Control& c = control[static_cast<size_t>(index) & controlMask];
        if (!c.voiced) return {};
        return {true, c.pitch, c.out, c.note};
    }

    // The voices' settings from the controls: Detune's offsets, the twin's interval, the levels.
    // A twin starts: from its main voice's marks as they are (those placed for good, and the next grain), its grains
    // its own from there.
    void startTwin(Voice& twin, const Voice& main) noexcept {
        std::copy(main.synthesis.begin(), main.synthesis.end(), twin.synthesis.begin());
        std::copy(main.synthesisVoiced.begin(), main.synthesisVoiced.end(), twin.synthesisVoiced.begin());
        twin.synthesisCount = twin.synthesisSettled = twin.synthesisCommitted = std::max(main.synthesisCommitted, main.nextGrain + 1);
        twin.nextGrain = main.nextGrain;
        twin.nearest = twin.nearestCommitted = main.nearestCommitted;
        twin.lastFirst = main.lastFirst;
        twin.planned = false;
        twin.grainHead = twin.grainCount = 0;
        twin.running = true;
    }

    void applyControls() noexcept {
        const double detune = std::clamp(controls.detune, 0.0, kDetuneMax) / 100.0;
        const double twin = std::clamp(controls.twin, 0.0, 1.0);
        const double width = std::clamp(controls.width, 0.0, 1.0);
        // Width leaves each side half of the grains at 1: kept as loud (a thinned grain's window still counts in the
        // windows' sum, so the gaps stay gaps).
        const double widthGain = std::sqrt(2.0 / (1.0 + (1.0 - width) * (1.0 - width)));
        const double main = 1.0 / std::sqrt(1.0 + twin * twin);
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = voices[static_cast<size_t>(i)];
            const double side = (i % 2 == 0) ? -detune : detune;
            v.offset = side + (v.twin ? controls.interval : 0.0);
            v.formant = v.twin ? controls.interval : 0.0;
            v.voicedGain = (v.twin ? twin * main : main) * widthGain;
            v.unvoicedGain = v.twin ? 0.0 : 1.0;
        }
    }

    void process(float* const* io, int numChannels, int numFrames, const NoteEvent* notes, int numNotes) noexcept {
        if (controls.quality != quality) {  // another tracker, another latency: from the start again
            quality = controls.quality;
            selectTracker();
            latency = latencies[static_cast<size_t>(quality)];
            reset();
        }
        applyControls();
        if (numChannels < 1) return;
        if (std::min(numChannels, 2) != channels) {  // (a voice that wasn't playing would start from long ago)
            channels = std::min(numChannels, 2);
            reset();
        }
        const bool twins = controls.twin > 0.0;
        int nextNote = 0;
        for (int i = 0; i < numFrames; ++i) {
            while (nextNote < numNotes && notes[nextNote].offset <= i) addMidi(notes[nextNote++].key);
            const size_t in = static_cast<size_t>(time) & inputMask;
            double mono = 0.0;
            for (int c = 0; c < channels; ++c) {
                const float x = io[c][i];
                input[static_cast<size_t>(c)][in] = x;
                double qi, qq;
                quadrature[static_cast<size_t>(c)].process(x, qi, qq);
                inputI[static_cast<size_t>(c)][in] = static_cast<float>(qi);
                inputQ[static_cast<size_t>(c)][in] = static_cast<float>(qq);
                mono += x;
            }
            closures.push(mono / channels);
            if (tracker->push(mono / channels)) onFrame(tracker->decided());
            const int64_t n = time - latency;
            double out[2] = {0.0, 0.0};
            for (int vi = 0; vi < kVoices; ++vi) {
                Voice& v = voices[static_cast<size_t>(vi)];
                if (v.output >= channels) continue;
                if (v.twin) {
                    if (twins && !v.running) startTwin(v, voices[static_cast<size_t>(v.output)]);
                    if (!twins && v.running && v.grainCount == 0) v.running = false;
                    if (!v.running) continue;
                }
                if (!v.twin || twins) startGrains(v, n);
                out[v.output] += render(v, n);
            }
            for (int c = 0; c < channels; ++c) io[c][i] = static_cast<float>(out[c]);
            ++time;
        }
        while (nextNote < numNotes) addMidi(notes[nextNote++].key);
    }
};

Shifter::Shifter() : impl_(std::make_unique<Impl>()) {}
Shifter::~Shifter() = default;
void Shifter::prepare(double sampleRate) { impl_->prepare(sampleRate); }
void Shifter::reset() noexcept { impl_->reset(); }
int Shifter::latency() const noexcept { return impl_->latency; }
int Shifter::latency(Quality quality) const noexcept { return impl_->latencies[static_cast<size_t>(quality)]; }
void Shifter::setControls(const Controls& controls) noexcept { impl_->controls = controls; }
void Shifter::process(float* const* channels, int numChannels, int numFrames, const NoteEvent* notes,
                      int numNotes) noexcept {
    impl_->process(channels, numChannels, numFrames, notes, numNotes);
}
Heard Shifter::heard(int ago) const noexcept { return impl_->heard(ago); }
int64_t Shifter::voicedFrames() const noexcept { return impl_->voicedCount; }
int64_t Shifter::lateSamples() const noexcept { return impl_->late; }
int64_t Shifter::droppedGrains() const noexcept { return impl_->dropped; }
int64_t Shifter::elasticJunctions() const noexcept { return impl_->elasticCount; }
int64_t Shifter::sharedJunctions() const noexcept { return impl_->sharedCount; }
int64_t Shifter::correlatedGrains() const noexcept { return impl_->correlatedCount; }
double Shifter::correlatedSamples() const noexcept { return impl_->correlatedShift; }
double Shifter::warpChange() const noexcept {
    return impl_->changeCount > 0 ? impl_->changeSum / static_cast<double>(impl_->changeCount) : 0.0;
}

// --- Drive ------------------------------------------------------------------------------------------

namespace {

// Little AlterBoy's drive at each of its settings, fitted to noise through it: y = A (s(B x + b) - s(b)),
// s(u) = u / (1 + |u|^h)^(1/h). The A's are LAB's divided by its gain at 0 (0.759: LAB is 2.4 dB quieter there),
// so 0 is unity and each step up 1.69 dB louder, as in LAB.
struct DriveShape {
    double a, b, bias, hardness;
};
constexpr double kLabGainAtZero = 0.759;
constexpr std::array<DriveShape, 11> kDriveShapes = {{
    {2.95834, 0.2753, -0.7984, 12.000},
    {1.34121, 0.7150, -0.6274, 7.309},
    {0.61068, 1.8588, -0.1264, 2.535},
    {0.50648, 2.7183, -0.0765, 2.512},
    {0.42942, 3.8999, -0.0481, 2.555},
    {0.37275, 5.5521, -0.0423, 2.688},
    {0.30149, 8.2616, -0.0704, 3.379},
    {0.24445, 11.9138, -0.1055, 4.809},
    {0.20057, 17.2062, -0.1140, 5.670},
    {0.16641, 25.9025, -0.0802, 4.688},
    {0.13882, 38.2658, -0.0563, 3.933},
}};
// Above 6 LAB's drive compresses: the gain before the clipper is 1 / (1 + kappa (rms - kCompressFrom)) of an input
// at `rms` (RMS), kappa rising as kCompressMost ((drive - 6) / 4)^1.5.
// LAB's wet path's colour, fitted to noise through it at Drive 0 (within 0.6 dB RMS from 25 Hz to 22 kHz): a
// first-order high-pass at 22 Hz, a shelf of +1.6 dB from 5.3 kHz, and a low-pass at 22 kHz, as RBJ's at 48 kHz.
constexpr double kColourHighPassHz = 22.2;
constexpr double kColourShelfHz = 5277.0, kColourShelfQ = 0.83, kColourShelfDb = 1.6;
constexpr double kColourTopHz = 22022.0, kColourTopQ = 0.30;
constexpr double kCompressFrom = 0.07;
constexpr double kCompressMost = 5.0;
constexpr double kCompressAttackMs = 30.0, kCompressReleaseMs = 200.0;

double clip(double u, double h) noexcept {
    // u / (1 + |u|^h)^(1/h), its powers as exp2 and log2 (fast forms: the shape, not the last ulp, matters)
    const double a = std::abs(u);
    if (a < 1e-12) return u;
    const double p = fastmath::exp2(h * fastmath::log2(a));
    return u * fastmath::exp2(-fastmath::log2(1.0 + p) / h);
}

DriveShape shapeAt(double drive) noexcept {
    const double d = std::clamp(drive, 0.0, kDriveMax);
    const auto i = std::min(static_cast<int>(d), 9);
    const double t = d - i;
    const DriveShape& p = kDriveShapes[static_cast<size_t>(i)];
    const DriveShape& q = kDriveShapes[static_cast<size_t>(i + 1)];
    return {p.a + (q.a - p.a) * t, p.b + (q.b - p.b) * t, p.bias + (q.bias - p.bias) * t,
            p.hardness + (q.hardness - p.hardness) * t};
}

}  // namespace

// The curve at a drive, worked out once for many samples.
struct PreparedCurve {
    double drive = 0.0, a = 1.0, b = 1.0, bias = 0.0, hardness = 2.0, atBias = 0.0, kappa = 0.0;

    explicit PreparedCurve(double d) noexcept : drive(d) {
        const DriveShape s = shapeAt(d);
        a = s.a / kLabGainAtZero;
        b = s.b;
        bias = s.bias;
        hardness = s.hardness;
        atBias = clip(bias, hardness);
        kappa = d > 6.0 ? kCompressMost * std::pow((std::min(d, kDriveMax) - 6.0) / 4.0, 1.5) : 0.0;
    }
    double operator()(double x, double envelope) const noexcept {
        if (!(drive > 0.0)) return x;
        const double gain = kappa > 0.0 ? 1.0 / (1.0 + kappa * std::max(0.0, envelope - kCompressFrom)) : 1.0;
        const double y = a * (clip(b * gain * x + bias, hardness) - atBias);
        // From 0 to 1 the curve fades in from the input itself (LAB's 0 colours a little; Changeling's 0 doesn't).
        return drive < 1.0 ? x + (y - x) * drive : y;
    }
};

double Drive::curve(double drive, double x, double envelope) noexcept { return PreparedCurve(drive)(x, envelope); }

void Drive::prepare(double sampleRate, int maxBlock) {
    for (int c = 0; c < kMaxChannels; ++c) {
        oversamplers_[static_cast<size_t>(c)].prepare(maxBlock, 1);
        oversamplers_[static_cast<size_t>(c)].setFactorLog2(1);
        dc_[static_cast<size_t>(c)].prepare(2.0 * sampleRate, 5.0);
    }
    latency_ = dsp::Oversampler::latencyFor(1);
    for (auto& d : delays_) d.prepare(maxBlock + latency_ + 4);
    highPass_ = dsp::onePoleCutoff(kColourHighPassHz, sampleRate);
    shelf_ = dsp::BiquadCoefficients::highShelf(kColourShelfHz, kColourShelfQ, kColourShelfDb, sampleRate);
    top_ = dsp::BiquadCoefficients::lowpass(kColourTopHz, kColourTopQ, sampleRate);
    attack_ = std::exp(-1.0 / (kCompressAttackMs * 0.001 * 2.0 * sampleRate));
    release_ = std::exp(-1.0 / (kCompressReleaseMs * 0.001 * 2.0 * sampleRate));
    reset();
}

void Drive::reset() noexcept {
    for (auto& o : oversamplers_) o.reset();
    for (auto& d : delays_) d.reset();
    for (auto& d : dc_) d.reset();
    for (auto& b : shelves_) b.reset();
    for (auto& b : tops_) b.reset();
    for (auto& l : lows_) l.reset();
    power_ = {};
}

void Drive::setDrive(double drive) noexcept { drive_ = std::clamp(drive, 0.0, kDriveMax); }

int Drive::latency() const noexcept { return latency_; }

void Drive::process(float* const* channels, int numChannels, int numFrames) noexcept {
    const int chs = std::clamp(numChannels, 0, kMaxChannels);
    const double before = active_;
    active_ = drive_;
    const bool none = !(active_ > 0.0) && !(before > 0.0);
    for (int c = 0; c < chs; ++c) {
        float* io = channels[c];
        dsp::DelayLine& delay = delays_[static_cast<size_t>(c)];
        dsp::Oversampler& os = oversamplers_[static_cast<size_t>(c)];
        // The oversampling runs at 0 too (its output unused), so turning the drive up starts it with its history.
        for (int i = 0; i < numFrames; ++i) delay.push(io[i]);
        // LAB's colour (before the clipper: nearer LAB at high drives than after it), in as the drive comes up
        // from 0 to 1, kept running at 0 so it starts with its history
        dsp::Biquad& shelf = shelves_[static_cast<size_t>(c)];
        dsp::Biquad& top = tops_[static_cast<size_t>(c)];
        dsp::OnePole& low = lows_[static_cast<size_t>(c)];
        for (int i = 0; i < numFrames; ++i) {
            const float x = io[i];
            const float coloured = top.process(top_, shelf.process(shelf_, low.highpass(x, highPass_)));
            const double d = before + (active_ - before) * (static_cast<double>(i + 1) / numFrames);
            const float share = static_cast<float>(std::min(1.0, d));
            io[i] = x + share * (coloured - x);
        }
        float* up = os.up(io, numFrames);
        double& power = power_[static_cast<size_t>(c)];
        dsp::DcBlocker& dc = dc_[static_cast<size_t>(c)];
        const int n2 = 2 * numFrames;
        constexpr int kStretch = 16;  // the drive glides from the last block's to this one's, a stretch at a time
        for (int from = 0; from < n2; from += kStretch) {
            const int to = std::min(n2, from + kStretch);
            const PreparedCurve shape(before + (active_ - before) * (static_cast<double>(to) / n2));
            for (int i = from; i < to; ++i) {
                const double x = up[i];
                const double p = x * x;
                power = p + (p > power ? attack_ : release_) * (power - p);
                up[i] = dc.process(static_cast<float>(none ? x : shape(x, std::sqrt(power))));
            }
        }
        os.down(up, numFrames, io);
        // none: the input exactly, as late as the oversampling makes it
        if (none)
            for (int i = 0; i < numFrames; ++i) io[i] = delay.tap(numFrames - 1 - i + latency_);
    }
}

}  // namespace sub::changeling
