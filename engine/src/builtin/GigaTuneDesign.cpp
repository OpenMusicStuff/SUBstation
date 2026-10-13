#include "builtin/GigaTuneDesign.h"

// GigaTune's tracker and corrector (see GigaTuneDesign.h). This is the only file that includes fastpitch.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <fastpitch/pyin.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "builtin/SwiftF0.h"
#include "rt/FastMath.h"

namespace sub::gigatune {

int frameLength(double sampleRate) {
    return 2 * std::max(8, static_cast<int>(std::lround(0.5 * kFrameSeconds * sampleRate)));
}

int hopLength(double sampleRate) { return std::max(1, static_cast<int>(std::lround(kHopSeconds * sampleRate))); }

// --- Keys -----------------------------------------------------------------------------------------

const std::vector<std::string>& keyLabels() {
    static const std::vector<std::string> kLabels = {"Song", "C",  "C#", "D",  "D#", "E", "F",
                                                     "F#",   "G",  "G#", "A",  "A#", "B"};
    return kLabels;
}

const std::vector<std::string>& qualityLabels() {
    static const std::vector<std::string> kLabels = {"Low Latency", "High Quality"};
    return kLabels;
}

const std::vector<std::string>& scaleLabels() {
    static const std::vector<std::string> kLabels = {
        "Song",   "Chromatic", "Major",      "Minor",   "Harmonic Minor",   "Melodic Minor",    "Dorian",
        "Phrygian", "Lydian",  "Mixolydian", "Locrian", "Major Pentatonic", "Minor Pentatonic", "Blues"};
    return kLabels;
}

int scaleNotes(int key, Scale scale, int songTonic, bool songMinor) {
    constexpr int kAll = 0xfff;
    const int index = std::clamp(static_cast<int>(scale), 0, static_cast<int>(Scale::Count) - 1);
    scale = static_cast<Scale>(index);
    if (scale == Scale::Chromatic) return kAll;
    key = std::clamp(key, 0, 12);
    if ((key == 0 || scale == Scale::Song) && songTonic < 0) return kAll;  // the song's key, and none known
    const int tonic = key == 0 ? songTonic % 12 : key - 1;
    if (scale == Scale::Song) scale = songMinor ? Scale::Minor : Scale::Major;
    // The scales' steps from the tonic, as bits (bit 0: the tonic).
    static constexpr std::array<int, static_cast<size_t>(Scale::Count)> kSteps = {
        0,
        0xfff,
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),  // major
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),  // natural minor
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),  // harmonic minor
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),  // melodic minor
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),  // dorian
        (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),  // phrygian
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),  // lydian
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),  // mixolydian
        (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),  // locrian
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                         // major pentatonic
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                        // minor pentatonic
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),             // blues
    };
    const int steps = kSteps[static_cast<size_t>(scale)];
    int bits = 0;
    for (int s = 0; s < 12; ++s)
        if (steps >> s & 1) bits |= 1 << ((tonic + s) % 12);
    return bits;
}

int targetNote(double pitch, int notes, int previous) {
    notes &= 0xfff;
    if (notes == 0) notes = 0xfff;
    const auto allowed = [notes](int note) { return (notes >> (((note % 12) + 12) % 12) & 1) != 0; };
    const int base = static_cast<int>(std::floor(pitch));
    int best = base;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (int note = base - 12; note <= base + 13; ++note) {  // upwards: of two as near, the lower
        const double distance = std::abs(pitch - note);
        if (allowed(note) && distance < bestDistance) {
            best = note;
            bestDistance = distance;
        }
    }
    if (previous >= 0 && allowed(previous) && std::abs(pitch - previous) <= bestDistance + kHysteresis) return previous;
    return best;
}

namespace {

constexpr double kPi = std::numbers::pi;
using Model = fastpitch::detail::Model;
using Yin = fastpitch::detail::YinWorker<double>;

size_t powerOfTwoAtLeast(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

fastpitch::PyinOptions pyinOptions(double sampleRate, bool center) {
    fastpitch::PyinOptions o;
    o.sample_rate = sampleRate;
    o.fmin = kFmin;
    o.fmax = std::min(kFmax, 0.5 * sampleRate);
    o.frame_length = frameLength(sampleRate);
    o.hop_length = hopLength(sampleRate);
    o.center = center;
    o.refine_f0 = true;
    o.num_threads = 1;
    return o;
}

// Grains on pulses: how sure the pulse must be at a voiced start to move the first mark onto it, how much
// of the way a grain's centre moves to its pulse at each mark (times the sureness), and how far it may be
// from its mark (of the period, or kUnvoicedPeriod if shorter: the centres stay in order).
constexpr double kOnsetSureness = 0.1;
constexpr double kOffsetFollow = 0.1;
constexpr double kOffsetShare = 0.4;

// Low Latency: how far ahead of the output a grain is started (its marks placed for good).
constexpr double kCommitAheadSeconds = 0.0005;

// A grain's pitch ratio, as its gain takes it, at the least (and 1 / it at the most): a little past kMaxMove's.
constexpr double kLowestRatio = 0.4;

// The pitches the tracker reports: a little past pYIN's range (a refined pitch may lie past its last bin).
constexpr double kRangeMargin = 1.03;
double lowestF0() { return kFmin / kRangeMargin; }

// Brent's minimisation of f over [a, b] from x (f(x) below f at both ends), to `tol` relative.
template <typename F>
double brentMinimum(F f, double a, double b, double x, double tol) {
    constexpr double kGolden = 0.3819660112501051;
    double v = x, w = x, fx = f(x), fv = fx, fw = fx, d = 0.0, e = 0.0;
    for (int iteration = 0; iteration < 100; ++iteration) {
        const double m = 0.5 * (a + b);
        const double tol1 = tol * std::abs(x) + 1e-12, tol2 = 2.0 * tol1;
        if (std::abs(x - m) <= tol2 - 0.5 * (b - a)) break;
        bool golden = true;
        if (std::abs(e) > tol1) {  // a parabola through x, w and v
            double r = (x - w) * (fx - fv), q = (x - v) * (fx - fw), p = (x - v) * q - (x - w) * r;
            q = 2.0 * (q - r);
            if (q > 0.0) p = -p;
            else q = -q;
            const double previousStep = e;
            e = d;
            if (std::abs(p) < std::abs(0.5 * q * previousStep) && p > q * (a - x) && p < q * (b - x)) {
                d = p / q;
                const double u = x + d;
                if (u - a < tol2 || b - u < tol2) d = x < m ? tol1 : -tol1;
                golden = false;
            }
        }
        if (golden) {
            e = (x < m ? b : a) - x;
            d = kGolden * e;
        }
        const double u = std::abs(d) >= tol1 ? x + d : x + (d > 0.0 ? tol1 : -tol1);
        const double fu = f(u);
        if (fu <= fx) {
            if (u < x) b = x;
            else a = x;
            v = w; fv = fw;
            w = x; fw = fx;
            x = u; fx = fu;
        } else {
            if (u < x) a = u;
            else b = u;
            if (fu <= fw || w == x) {
                v = w; fv = fw;
                w = u; fw = fu;
            } else if (fu <= fv || v == x || v == w) {
                v = u; fv = fu;
            }
        }
    }
    return x;
}

// --- The tracker ----------------------------------------------------------------------------------

class Tracker {
public:
    void prepare(double sampleRate, int lag) {
        rate_ = sampleRate;
        lag_ = std::max(0, lag);
        model_ = std::make_unique<Model>(pyinOptions(sampleRate, false));
        yin_ = std::make_unique<Yin>(*model_);
        const Model& m = *model_;
        n_ = m.nbins;
        frame_ = m.N;
        hop_ = m.hop;
        maxCandidates_ = m.K / 2 + 4;  // troughs alternate with peaks
        batch_.cands.reserve(static_cast<size_t>(m.K + 8));

        // Each destination's sources, lowest first, with the transition's log probability: same voicing
        // (stay) and the other (switch), only those librosa keeps (transition_min_prob).
        sameFrom_.clear(); sameWeight_.clear(); crossFrom_.clear(); crossWeight_.clear();
        sameAt_.assign(static_cast<size_t>(n_) + 1, 0);
        crossAt_.assign(static_cast<size_t>(n_) + 1, 0);
        for (int j = 0; j < n_; ++j) {
            sameAt_[static_cast<size_t>(j)] = static_cast<int>(sameFrom_.size());
            crossAt_[static_cast<size_t>(j)] = static_cast<int>(crossFrom_.size());
            for (int d = m.dmin; d <= m.dmax; ++d) {
                const int i = j + d;
                if (i < 0 || i >= n_) continue;
                const auto si = static_cast<size_t>(i);
                const double g = m.gk[static_cast<size_t>(d - m.dmin)] - m.logZ[si];
                if (d >= m.src_s_lo[si] && d <= m.src_s_hi[si]) {
                    sameFrom_.push_back(i);
                    sameWeight_.push_back(m.ls_stay + g);
                }
                if (d >= m.src_c_lo[si] && d <= m.src_c_hi[si]) {
                    crossFrom_.push_back(i);
                    crossWeight_.push_back(m.ls_switch + g);
                }
            }
        }
        sameAt_[static_cast<size_t>(n_)] = static_cast<int>(sameFrom_.size());
        crossAt_[static_cast<size_t>(n_)] = static_cast<int>(crossFrom_.size());

        const auto states = static_cast<size_t>(2 * n_);
        for (auto* v : {&voiced_, &unvoiced_, &nextVoiced_, &nextUnvoiced_, &obsVoiced_})
            v->assign(static_cast<size_t>(n_), 0.0);
        slots_ = lag_ + 1;
        back_.assign(static_cast<size_t>(slots_) * states, 0);
        candidates_.assign(static_cast<size_t>(slots_) * static_cast<size_t>(maxCandidates_), {});
        candidateCount_.assign(static_cast<size_t>(slots_), 0);
        voicedProb_.assign(static_cast<size_t>(slots_), 0.0);

        // The refinement's window: at least kMinWindow, and two periods; its lags as far as the candidate
        // may be off (searchOf()), and the interpolation's taps past them.
        minWindow_ = static_cast<int>(std::lround(kMinWindowSeconds * rate_));
        const int longest = static_cast<int>(std::ceil(rate_ / lowestF0())) + 2;
        const int reach = searchOf(longest, kSearchHigh) + kTaps;
        difference_.assign(static_cast<size_t>(2 * reach + 1), 0.0);
        span_.assign(static_cast<size_t>(std::max(2 * longest, minWindow_) + longest + reach + 8), 0.0);
        // It reads the input low-passed (Butterworth, eighth order: four biquads), so the difference function
        // has nothing near the rate's half, where the interpolation is least exact. A filter moves no period.
        const double cutoff = std::min(kRefineCutoff, kRefineCutoffShare * rate_);
        const double w0 = 2.0 * kPi * cutoff / rate_, cw = std::cos(w0), sw = std::sin(w0);
        for (size_t k = 0; k < lowpass_.size(); ++k) {
            const double q = 1.0 / (2.0 * std::sin(kPi * (2.0 * static_cast<double>(k) + 1.0) / 16.0));
            const double alpha = sw / (2.0 * q), a0 = 1.0 + alpha;
            Biquad& b = lowpass_[k];
            b.b0 = b.b2 = 0.5 * (1.0 - cw) / a0;
            b.b1 = (1.0 - cw) / a0;
            b.a1 = -2.0 * cw / a0;
            b.a2 = (1.0 - alpha) / a0;
        }
        // High Quality: SwiftF0, and how long after a frame's centre it is decided: when SwiftF0's frame after it
        // is final.
        swift_.prepare(rate_);
        swiftHop_ = swift_.hostHop();
        highDelay_ = static_cast<int>(std::ceil(swiftHop_)) + swift_.delay() + 1;
        // The input kept: a frame, the lag (or SwiftF0's wait), and the refinement's reach about a decided frame.
        const size_t history = std::max(static_cast<size_t>(frame_) + static_cast<size_t>(lag_ + 1) * static_cast<size_t>(hop_),
                                        static_cast<size_t>(highDelay_)) + span_.size() + 64;
        ring_.assign(powerOfTwoAtLeast(history), 0.0);
        low_.assign(ring_.size(), 0.0);
        mask_ = ring_.size() - 1;
        frameBuffer_.assign(static_cast<size_t>(frame_), 0.0);
        reset();
    }

    void reset() noexcept {
        std::fill(ring_.begin(), ring_.end(), 0.0);
        std::fill(low_.begin(), low_.end(), 0.0);
        for (Biquad& b : lowpass_) b.z1 = b.z2 = 0.0;
        written_ = 0;
        frames_ = 0;
        decidedUpTo_ = 0;
        decided_ = {};
        swift_.reset();
        for (auto& f : swiftFrames_) f = {-1, 0.0, 0.0};
        nextHigh_ = 0;
    }

    void setQuality(Quality quality) noexcept {
        if (quality == quality_) return;
        quality_ = quality;
        reset();
    }
    Quality quality() const noexcept { return quality_; }
    int delay(Quality quality) const noexcept {
        return quality == Quality::HighQuality ? highDelay_ : frame_ / 2 + lag_ * hop_;
    }

    int frameLength() const noexcept { return frame_; }
    int hop() const noexcept { return hop_; }
    int lag() const noexcept { return lag_; }
    double rate() const noexcept { return rate_; }
    int64_t written() const noexcept { return written_; }
    const PitchFrame& decided() const noexcept { return decided_; }
    // The input low-passed as the refinement reads it, at a sample still kept.
    double lowAt(int64_t n) const noexcept { return low_[static_cast<size_t>(n) & mask_]; }

    bool push(double x) noexcept {
        ring_[static_cast<size_t>(written_) & mask_] = x;
        double y = x;
        for (Biquad& b : lowpass_) y = b(y);
        low_[static_cast<size_t>(written_) & mask_] = y;
        ++written_;
        if (quality_ == Quality::HighQuality) {
            if (swift_.push(static_cast<float>(x))) {
                const swiftf0::Frame& f = swift_.frame();
                swiftFrames_[static_cast<size_t>(f.index) % swiftFrames_.size()] = {f.index, f.pitch, f.confidence};
            }
            if (written_ - highDelay_ < nextHigh_ * hop_) return false;
            decideHigh(nextHigh_++);
            return true;
        }
        // frame f covers [f hop - N/2, f hop + N/2): complete once that many samples are in
        const int64_t start = written_ - frame_ / 2;
        if (start < 0 || start % hop_ != 0) return false;
        analyse();
        const int64_t newest = frames_ - 1;
        if (newest < lag_) return false;
        int state = bestState();
        for (int64_t k = newest; k > newest - lag_; --k) state = back(k)[state];
        decide(newest - lag_, state);
        return true;
    }

    void flush(std::vector<PitchFrame>& out) {
        if (quality_ == Quality::HighQuality) {  // the input ends: silence until the frames it reached are decided
            const int64_t end = written_;
            while (nextHigh_ * hop_ < end)
                if (push(0.0)) out.push_back(decided_);
            reset();
            return;
        }
        const int64_t first = decidedUpTo_;
        if (frames_ > first) {
            std::vector<int> states(static_cast<size_t>(frames_ - first));
            int state = bestState();
            for (int64_t k = frames_ - 1;; --k) {
                states[static_cast<size_t>(k - first)] = state;
                if (k == first) break;
                state = back(k)[state];
            }
            for (int64_t g = first; g < frames_; ++g) {
                decide(g, states[static_cast<size_t>(g - first)]);
                out.push_back(decided_);
            }
        }
        reset();
    }

private:
    static constexpr double kMinWindowSeconds = 0.01;
    static constexpr int kTaps = 9;                   // lags past the search for the interpolation (8 a side)
    static constexpr double kRefineCutoff = 8000.0;   // Hz, the refinement's low-pass
    static constexpr double kRefineCutoffShare = 0.18;  // of the rate, at the most
    struct Candidate {
        int bin = 0;
        double f0 = 0.0;
    };
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double operator()(double x) noexcept {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // Whole lags either side of the candidate's period the difference function's minimum is looked for at: the
    // candidate is within a few cents (0.3 % of the period) and a sample of its rounding. (Wider, the refinement
    // finds other periods where notes overlap in reverb, and no better pitch where SwiftF0's 16 ms frames lag.)
    static constexpr double kSearchLow = 0.003, kSearchHigh = 0.003;
    // High Quality keeps SwiftF0's own pitch where the refinement finds no clear period (the normalised difference
    // function at it this high or more: noise, notes overlapping in reverb).
    static constexpr double kRefineGate = 0.3;
    static int searchOf(double period, double share) noexcept { return 2 + static_cast<int>(std::ceil(share * period)); }
    struct SwiftFrame {
        int64_t index = -1;
        double pitch = 0.0, confidence = 0.0;
    };

    const uint16_t* back(int64_t frame) const noexcept {
        return back_.data() + static_cast<size_t>(frame % slots_) * static_cast<size_t>(2 * n_);
    }
    uint16_t* back(int64_t frame) noexcept {
        return back_.data() + static_cast<size_t>(frame % slots_) * static_cast<size_t>(2 * n_);
    }

    // The newest frame's best state (librosa's argmax: the first, voiced states before unvoiced ones).
    int bestState() const noexcept {
        double best = -std::numeric_limits<double>::infinity();
        int arg = n_;
        for (int j = 0; j < n_; ++j)
            if (voiced_[static_cast<size_t>(j)] > best) best = voiced_[static_cast<size_t>(j)], arg = j;
        for (int j = 0; j < n_; ++j)
            if (unvoiced_[static_cast<size_t>(j)] > best) best = unvoiced_[static_cast<size_t>(j)], arg = n_ + j;
        return arg;
    }

    // The newest frame through pYIN's observation model and one Viterbi step.
    void analyse() noexcept {
        const int64_t from = written_ - frame_;
        for (int i = 0; i < frame_; ++i)
            frameBuffer_[static_cast<size_t>(i)] = ring_[static_cast<size_t>(from + i) & mask_];
        yin_->run(frameBuffer_.data(), static_cast<size_t>(frame_), 0, 1, batch_);
        const fastpitch::detail::FrameObs& obs = batch_.frames[0];
        const Model& m = *model_;

        const auto slot = static_cast<size_t>(frames_ % slots_);
        int count = 0;
        Candidate* candidates = candidates_.data() + slot * static_cast<size_t>(maxCandidates_);
        std::fill(obsVoiced_.begin(), obsVoiced_.end(), m.log_tiny);
        for (uint32_t c = obs.cbegin; c < obs.cend; ++c) {
            const fastpitch::detail::Cand& cand = batch_.cands[c];
            obsVoiced_[static_cast<size_t>(cand.bin)] = cand.logp;
            if (count < maxCandidates_) candidates[count++] = {cand.bin, cand.f0};
        }
        candidateCount_[slot] = count;
        voicedProb_[slot] = obs.voiced_prob;
        const double obsUnvoiced = obs.log_unvoiced;

        if (frames_ == 0) {
            for (size_t j = 0; j < static_cast<size_t>(n_); ++j) {
                voiced_[j] = obsVoiced_[j] + m.lpi;
                unvoiced_[j] = obsUnvoiced + m.lpi;
            }
        } else {
            uint16_t* toVoiced = back(frames_);
            uint16_t* toUnvoiced = toVoiced + n_;
            const double* v = voiced_.data();
            const double* u = unvoiced_.data();
            for (int j = 0; j < n_; ++j) {
                const auto sj = static_cast<size_t>(j);
                const int s0 = sameAt_[sj], s1 = sameAt_[sj + 1], c0 = crossAt_[sj], c1 = crossAt_[sj + 1];
                // into voiced bin j: from voiced bins (stay), then from unvoiced ones (switch)
                double best = -std::numeric_limits<double>::infinity();
                int arg = 0;
                for (int k = s0; k < s1; ++k) {
                    const int i = sameFrom_[static_cast<size_t>(k)];
                    const double value = v[i] + sameWeight_[static_cast<size_t>(k)];
                    if (value > best) best = value, arg = i;
                }
                for (int k = c0; k < c1; ++k) {
                    const int i = crossFrom_[static_cast<size_t>(k)];
                    const double value = u[i] + crossWeight_[static_cast<size_t>(k)];
                    if (value > best) best = value, arg = n_ + i;
                }
                nextVoiced_[sj] = best + obsVoiced_[sj];
                toVoiced[j] = static_cast<uint16_t>(arg);
                // into unvoiced bin j: from voiced bins (switch), then from unvoiced ones (stay)
                best = -std::numeric_limits<double>::infinity();
                arg = 0;
                for (int k = c0; k < c1; ++k) {
                    const int i = crossFrom_[static_cast<size_t>(k)];
                    const double value = v[i] + crossWeight_[static_cast<size_t>(k)];
                    if (value > best) best = value, arg = i;
                }
                for (int k = s0; k < s1; ++k) {
                    const int i = sameFrom_[static_cast<size_t>(k)];
                    const double value = u[i] + sameWeight_[static_cast<size_t>(k)];
                    if (value > best) best = value, arg = n_ + i;
                }
                nextUnvoiced_[sj] = best + obsUnvoiced;
                toUnvoiced[j] = static_cast<uint16_t>(arg);
            }
            voiced_.swap(nextVoiced_);
            unvoiced_.swap(nextUnvoiced_);
        }
        // The values only fall: brought back up now and then (a shift of all of them changes no decision).
        double top = -std::numeric_limits<double>::infinity();
        for (size_t j = 0; j < static_cast<size_t>(n_); ++j) top = std::max({top, voiced_[j], unvoiced_[j]});
        if (top < -1e6) {
            for (size_t j = 0; j < static_cast<size_t>(n_); ++j) {
                voiced_[j] -= top;
                unvoiced_[j] -= top;
            }
        }
        ++frames_;
    }

    // Frame g's state chosen: the frame as it is reported.
    void decide(int64_t g, int state) noexcept {
        const Model& m = *model_;
        const auto slot = static_cast<size_t>(g % slots_);
        PitchFrame f;
        f.index = g;
        f.voiced = state < n_;
        f.bin = f.voiced ? state : state - n_;
        f.voicedProb = voicedProb_[slot];
        const Candidate* candidates = candidates_.data() + slot * static_cast<size_t>(maxCandidates_);
        for (int c = 0; c < candidateCount_[slot]; ++c)
            if (candidates[c].bin == f.bin) f.candidate = candidates[c].f0;
        if (f.voiced) {
            const double start = f.candidate > 0.0 ? f.candidate : m.freqs[static_cast<size_t>(f.bin)];
            double periodicity;
            f.f0 = std::clamp(refine(g * hop_, start, kSearchLow, periodicity), lowestF0(), kFmax * kRangeMargin);
        }
        decided_ = f;
        decidedUpTo_ = g + 1;
    }

    // High Quality: frame g from SwiftF0's frames either side of its centre (both final by now).
    void decideHigh(int64_t g) noexcept {
        PitchFrame f;
        f.index = g;
        f.bin = -1;
        const double x = static_cast<double>(g * hop_) / swiftHop_;
        const auto k0 = static_cast<int64_t>(std::floor(x));
        const double t = x - static_cast<double>(k0);
        const SwiftFrame& a = swiftFrames_[static_cast<size_t>(k0) % swiftFrames_.size()];
        const SwiftFrame& b = swiftFrames_[static_cast<size_t>(k0 + 1) % swiftFrames_.size()];
        if (a.index != k0 || b.index != k0 + 1) {  // (not there: never, the wait covers them)
            decided_ = f;
            return;
        }
        const SwiftFrame& near = t < 0.5 ? a : b;
        f.voicedProb = near.confidence;
        const bool sure = near.confidence >= swiftf0::kVoiced;
        if (sure || near.confidence >= kRescueConfidence) {
            // the pitch between the two frames in log frequency, where both are as sure; else the nearer's
            double pitch = near.pitch;
            const SwiftFrame& far = t < 0.5 ? b : a;
            if (far.confidence >= kRescueConfidence) pitch = std::exp(std::log(a.pitch) + (std::log(b.pitch) - std::log(a.pitch)) * t);
            f.candidate = pitch;
            if (pitch >= lowestF0() && pitch <= kFmax * kRangeMargin) {  // within C2 to C7: the corrector's range
                double periodicity = 1.0;
                const double refined = refine(g * hop_, pitch, kSearchHigh, periodicity);
                if (sure || periodicity < kRescuePeriodicity) {
                    f.voiced = true;
                    f.f0 = std::clamp(periodicity < kRefineGate ? refined : pitch, lowestF0(), kFmax * kRangeMargin);
                }
            }
        }
        decided_ = f;
        decidedUpTo_ = g + 1;
    }

    // The period about `center` near the candidate's (rate / f0), to a small fraction of a sample: the
    // difference function at whole lags about it, interpolated (a windowed sinc) and its minimum found.
    // `periodicity`: the normalised difference function at the whole lag nearest the period (d over both segments'
    // energy: 0 periodic, about 1 noise), 1 where it gives up.
    double refine(int64_t center, double f0, double share, double& periodicity) noexcept {
        periodicity = 1.0;
        const double period = rate_ / f0;
        const int n0 = static_cast<int>(std::lround(period));
        const int search = searchOf(period, share), reach = search + kTaps;
        if (n0 - reach < 2 || static_cast<size_t>(2 * reach + 1) > difference_.size()) return f0;
        const int window = std::max(2 * n0, minWindow_);
        const int length = window + n0 + reach;
        if (static_cast<size_t>(length) > span_.size()) return f0;
        // about the centre, or as near it as the input goes (Low Latency: a low voice's window reaches past it)
        const int64_t from = std::min<int64_t>(center - (window + n0) / 2, written_ - length);
        if (from < written_ - static_cast<int64_t>(ring_.size())) return f0;
        for (int i = 0; i < length; ++i) span_[static_cast<size_t>(i)] = low_[static_cast<size_t>(from + i) & mask_];

        // d(m) = sum over the window of (x[j] - x[j + m])^2 for m = n0 - reach .. n0 + reach
        const double* x = span_.data();
        for (int k = -reach; k <= reach; ++k) {
            const double* y = x + n0 + k;
            double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
            int j = 0;
            for (; j + 4 <= window; j += 4) {
                const double d0 = x[j] - y[j], d1 = x[j + 1] - y[j + 1], d2 = x[j + 2] - y[j + 2], d3 = x[j + 3] - y[j + 3];
                s0 += d0 * d0;
                s1 += d1 * d1;
                s2 += d2 * d2;
                s3 += d3 * d3;
            }
            for (; j < window; ++j) {
                const double d0 = x[j] - y[j];
                s0 += d0 * d0;
            }
            difference_[static_cast<size_t>(k + reach)] = (s0 + s1) + (s2 + s3);
        }
        int best = -search;
        for (int k = 1 - search; k <= search; ++k)
            if (difference_[static_cast<size_t>(k + reach)] < difference_[static_cast<size_t>(best + reach)]) best = k;
        if (best == -search || best == search) return f0;  // the minimum isn't near (or d is flat): keep the candidate's
        {
            double own = 0.0, other = 0.0;
            const double* y = x + n0 + best;
            for (int j = 0; j < window; ++j) {
                own += x[j] * x[j];
                other += y[j] * y[j];
            }
            if (own + other > 0.0) periodicity = difference_[static_cast<size_t>(best + reach)] / (own + other);
        }

        // d between whole lags: 16 taps of a Blackman-Harris windowed sinc, normalised to a gain of 1 at 0 Hz.
        const auto at = [&](double tau) {
            const double position = tau - (n0 - reach);
            const double whole = std::floor(position);
            const double fraction = position - whole;
            const auto i0 = static_cast<int>(whole);
            if (fraction == 0.0) return difference_[static_cast<size_t>(i0)];
            const double sine = fastmath::sin(kPi * fraction);
            double sum = 0.0, gain = 0.0;
            for (int t = -7; t <= 8; ++t) {
                const double u = fraction - t;  // from the tap to tau
                const double c1 = fastmath::cos(kPi * u / 8.0), c2 = 2.0 * c1 * c1 - 1.0, c3 = c1 * (2.0 * c2 - 1.0);
                const double taper = 0.35875 + 0.48829 * c1 + 0.14128 * c2 + 0.01168 * c3;
                const double tap = ((t & 1) ? -sine : sine) / (kPi * u) * taper;
                sum += difference_[static_cast<size_t>(i0 + t)] * tap;
                gain += tap;
            }
            return sum / gain;
        };
        const double lag = n0 + best;
        const double tau = brentMinimum(at, lag - 1.0, lag + 1.0, lag, 1e-10);
        return rate_ / tau;
    }

    double rate_ = 48000.0;
    int lag_ = kLag, n_ = 0, frame_ = 0, hop_ = 0, slots_ = 1, maxCandidates_ = 0, minWindow_ = 0;
    std::unique_ptr<Model> model_;
    std::unique_ptr<Yin> yin_;
    Yin::BatchT batch_;
    std::vector<int> sameAt_, crossAt_, sameFrom_, crossFrom_;
    std::vector<double> sameWeight_, crossWeight_;
    std::vector<double> voiced_, unvoiced_, nextVoiced_, nextUnvoiced_, obsVoiced_;
    std::vector<uint16_t> back_;  // per frame (a ring of lag + 1): each state's predecessor
    std::vector<Candidate> candidates_;
    std::vector<int> candidateCount_;
    std::vector<double> voicedProb_;
    std::vector<double> ring_, low_, frameBuffer_, span_, difference_;
    std::array<Biquad, 4> lowpass_{};
    Quality quality_ = Quality::LowLatency;
    swiftf0::Detector swift_;
    std::array<SwiftFrame, 64> swiftFrames_{};  // SwiftF0's frames as they become final, by index
    double swiftHop_ = 768.0;
    int highDelay_ = 0;
    int64_t nextHigh_ = 0;  // the next frame High Quality decides
    size_t mask_ = 0;
    int64_t written_ = 0, frames_ = 0, decidedUpTo_ = 0;
    PitchFrame decided_;
};

// --- Reading between samples ----------------------------------------------------------------------

// A Kaiser-windowed sinc (beta 9, 16 zero crossings each side), tabulated over [0, 16] for linear
// interpolation. Zero at every whole number but 0: a read at a whole sample is that sample.
constexpr int kSincHalf = 16;
constexpr int kSincResolution = 1024;

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64 && term > 1e-17 * sum; ++k) {
        const double h = x / (2.0 * k);
        term *= h * h;
        sum += term;
    }
    return sum;
}

const std::vector<double>& sincTable() {
    static const std::vector<double> kTable = [] {
        constexpr double kBeta = 9.0;
        std::vector<double> table(static_cast<size_t>(kSincHalf * kSincResolution) + 2, 0.0);
        const double norm = besselI0(kBeta);
        for (int i = 0; i < kSincHalf * kSincResolution; ++i) {
            if (i % kSincResolution == 0) {
                table[static_cast<size_t>(i)] = i == 0 ? 1.0 : 0.0;
                continue;
            }
            const double x = static_cast<double>(i) / kSincResolution;
            const double r = x / kSincHalf;
            table[static_cast<size_t>(i)] =
                std::sin(kPi * x) / (kPi * x) * besselI0(kBeta * std::sqrt(1.0 - r * r)) / norm;
        }
        return table;
    }();
    return kTable;
}

inline double sincAt(const double* table, double x) noexcept {
    const double position = std::abs(x) * kSincResolution;
    if (!(position < kSincHalf * kSincResolution)) return 0.0;
    const auto i = static_cast<size_t>(position);
    const double f = position - static_cast<double>(i);
    return table[i] + (table[i + 1] - table[i]) * f;
}

}  // namespace

// --- PitchTracker -----------------------------------------------------------------------------------

struct PitchTracker::Impl {
    Tracker tracker;
};

PitchTracker::PitchTracker() : impl_(std::make_unique<Impl>()) {}
PitchTracker::~PitchTracker() = default;
void PitchTracker::prepare(double sampleRate, int lag) { impl_->tracker.prepare(sampleRate, lag); }
void PitchTracker::reset() { impl_->tracker.reset(); }
int PitchTracker::frameLength() const noexcept { return impl_->tracker.frameLength(); }
int PitchTracker::hop() const noexcept { return impl_->tracker.hop(); }
int PitchTracker::lag() const noexcept { return impl_->tracker.lag(); }
void PitchTracker::setQuality(Quality quality) noexcept { impl_->tracker.setQuality(quality); }
Quality PitchTracker::quality() const noexcept { return impl_->tracker.quality(); }
int PitchTracker::delay(Quality quality) const noexcept { return impl_->tracker.delay(quality); }
bool PitchTracker::push(double x) noexcept { return impl_->tracker.push(x); }
const PitchFrame& PitchTracker::decided() const noexcept { return impl_->tracker.decided(); }
void PitchTracker::flush(std::vector<PitchFrame>& out) { impl_->tracker.flush(out); }

std::vector<PitchFrame> offlinePyin(const std::vector<double>& x, double sampleRate) {
    fastpitch::Pyin<double> pyin(pyinOptions(sampleRate, true));
    fastpitch::PyinResult<double> result;
    pyin.analyze(x.data(), x.size(), result);
    std::vector<PitchFrame> frames(result.size());
    const std::vector<double>& freqs = pyin.bin_frequencies();
    for (size_t i = 0; i < frames.size(); ++i) {
        PitchFrame& f = frames[i];
        f.index = static_cast<int64_t>(i);
        f.voiced = result.voiced[i] != 0;
        f.voicedProb = result.voiced_prob[i];
        if (f.voiced) {
            f.candidate = result.f0[i];
            // the bin the frame was decoded in: its candidate's nearest (pYIN rounds the candidate to it)
            const double bin = 12.0 * std::ceil(1.0 / pyinOptions(sampleRate, true).resolution) * std::log2(f.candidate / kFmin);
            f.bin = std::clamp(static_cast<int>(std::nearbyint(bin)), 0, static_cast<int>(freqs.size()) - 1);
        }
    }
    return frames;
}

// --- Corrector --------------------------------------------------------------------------------------

struct Corrector::Impl {
    // A decided frame, as the marks read it: whether it is voiced, its pitch and the output's (semitones); and as
    // the editor shows it, where the correction takes it (before Pitch) and the note it is corrected to.
    struct Control {
        bool voiced = false;
        double pitch = 0.0, out = 0.0, corrected = 0.0;
        int note = -1;
    };
    // A grain being played: its marks (synthesis k, analysis j), its centres (s in the output, a in the input), its
    // window's halves (output samples), how fast it is read and how loud, the output samples it covers, and read at
    // speed 1, the read's offset from the output sample (whole and fraction) and its taps (one set for the grain).
    // Its right half (to the marks after its own) is settled when the output reaches its centre (`open` till then).
    struct Grain {
        double s = 0.0, a = 0.0, left = 0.0, right = 0.0, alpha = 1.0, gain = 1.0, fraction = 0.0;
        int64_t k = 0, j = 0, first = 0, last = -1, shift = 0;
        bool done = true, open = false, voiced = false;
        std::array<double, 2 * kSincHalf> taps{};
    };
    static constexpr int kMaxGrains = 128;  // playing at once, at the most (a few, but for a long one among short)
    static constexpr int kGrainMask = kMaxGrains - 1;

    Tracker tracker;
    double rate = 48000.0;
    int hop = 0, latency = 0;
    std::array<int, 2> latencies{};  // by quality
    Quality quality = Quality::LowLatency;
    double unvoicedPeriod = 240.0;  // samples
    double longest = 0.0;           // the longest analysis period (or unvoiced step)
    double reach = 0.0;             // how far before its mark a grain starts, at the most
    int commitAhead = 0;            // Low Latency: how far ahead of the output a grain is started
    Controls controls;
    const double* sinc = nullptr;

    std::vector<Control> control;
    size_t controlMask = 0;
    int64_t controlCount = 0;  // frames decided

    double correction = 0.0;  // semitones (before Strength)
    int target = -1;
    bool wasVoiced = false;

    // Marks (input sample positions), rings: analysis marks, whether each is voiced (its grain played as a
    // voiced one) and how far its pulse lies from it (its grain's centre: 0 unvoiced); synthesis marks.
    // They are placed as the grains need them. One placed past the frames decided (the last one's pitch held) or
    // with its pulse found in an earlier period (the input not there yet) is provisional, and so is every one after
    // it: when the next frame is decided they are placed again, from the last settled one, but for those a grain
    // has taken (committed). High Quality's are all settled (its latency is long enough).
    std::vector<double> analysis, offsets, synthesis;
    std::vector<uint8_t> analysisVoiced, synthesisVoiced;  // (whether the step to the mark was voiced)
    size_t markMask = 0;
    int64_t analysisCount = 0, synthesisCount = 0;
    int64_t analysisSettled = 0, synthesisSettled = 0, analysisCommitted = 0, synthesisCommitted = 0;
    int64_t nextGrain = 1;  // the synthesis mark to play next (0 is the one before the start)
    int64_t nearest = 0;    // analysis[nearest] <= the next grain's mark
    int64_t nearestCommitted = 0;
    int64_t lastFirst = 0;  // the first output sample of the last grain started
    // The next grain as planned, till the marks or Formant change (planned false)
    Grain plan;
    int64_t planJ = 0;
    double planFormant = 1.0;
    bool planned = false;
    double formantOf = 0.0, formantRatio = 1.0;  // Formant's read speed, for its value

    std::array<Grain, kMaxGrains> grains{};  // a ring, in the order they started
    int grainHead = 0, grainCount = 0;
    std::array<std::vector<float>, 2> input;
    size_t inputMask = 0;
    int64_t time = 0;  // samples in
    int channels = 1;

    int64_t late = 0, voicedCount = 0;

    void prepare(double sampleRate) {
        rate = sampleRate;
        sinc = sincTable().data();
        tracker.prepare(sampleRate, kLag);
        hop = tracker.hop();
        unvoicedPeriod = kUnvoicedPeriod * rate;
        longest = std::max(rate / lowestF0(), unvoicedPeriod);
        // A grain reaches back half its width: from its centre to the one before (a step, and the two centres'
        // offsets), read at half speed (Formant -12), its centre up to an offset from its mark.
        const double shortestRatio = std::exp2(-kMaxFormant / 12.0);
        const double longestSynthesis = std::max(longest, rate / lowestF0() * std::exp2(kMaxMove / 12.0));
        const double offsetMost = kOffsetShare * unvoicedPeriod;
        reach = std::max({(longest + 2.0 * offsetMost) / shortestRatio, 2.0 * unvoicedPeriod, longest}) + offsetMost;
        // High Quality: behind its input far enough for every mark a grain needs to be settled when it starts (the
        // bounds are the worst the settings allow): a frame is decided up to a hop after the tracker's delay; a
        // grain starts up to `reach` before its mark, and needs the frames past its mark and the next (half an
        // output period, or a period and a half for the analysis mark after its nearest one) and the input a
        // period past that (its pulse).
        const double behind = tracker.delay(Quality::HighQuality) + hop + std::max(2.5 * longest, 0.5 * longestSynthesis) + reach;
        latencies[static_cast<size_t>(Quality::HighQuality)] = static_cast<int>(std::ceil(behind)) + 2;
        // Low Latency: MAutoPitch's (1024 samples at 48 kHz), the same time at any rate. Its tracker decides a frame
        // well inside it; marks past that are placed from the last frame decided, and placed again with the next.
        latencies[static_cast<size_t>(Quality::LowLatency)] = static_cast<int>(std::lround(kLowLatencySeconds * rate));
        commitAhead = static_cast<int>(std::lround(kCommitAheadSeconds * rate));
        latency = std::max(latencies[0], latencies[1]);  // (the buffers are sized for the longer)

        const auto span = static_cast<size_t>(latency) + static_cast<size_t>(std::ceil(4.0 * longest)) + 4 * kSincHalf + 64;
        for (auto& c : input) c.assign(powerOfTwoAtLeast(span), 0.f);
        inputMask = input[0].size() - 1;
        const double shortestStep = std::min(unvoicedPeriod, rate / (kFmax * kRangeMargin) * std::exp2(-kMaxMove / 12.0));
        const auto marks = static_cast<size_t>(std::ceil(static_cast<double>(span) / std::max(1.0, shortestStep))) + 64;
        analysis.assign(powerOfTwoAtLeast(marks), 0.0);
        synthesis.assign(analysis.size(), 0.0);
        offsets.assign(analysis.size(), 0.0);
        analysisVoiced.assign(analysis.size(), 0);
        synthesisVoiced.assign(analysis.size(), 0);
        markMask = analysis.size() - 1;
        control.assign(powerOfTwoAtLeast(span / static_cast<size_t>(hop) + static_cast<size_t>(tracker.lag()) + 16), {});
        controlMask = control.size() - 1;
        tracker.setQuality(quality);
        latency = latencies[static_cast<size_t>(quality)];
        reset();
    }

    void reset() noexcept {
        tracker.reset();
        for (auto& c : input) std::fill(c.begin(), c.end(), 0.f);
        controlCount = 0;
        correction = 0.0;
        target = -1;
        wasVoiced = false;
        // a mark before the start, and one at it (both unvoiced: the input before the start is silence)
        analysis[0] = synthesis[0] = -unvoicedPeriod;
        analysis[1] = synthesis[1] = 0.0;
        offsets[0] = offsets[1] = 0.0;
        analysisVoiced[0] = analysisVoiced[1] = 0;
        synthesisVoiced[0] = synthesisVoiced[1] = 0;
        analysisCount = synthesisCount = 2;
        analysisSettled = synthesisSettled = analysisCommitted = synthesisCommitted = 2;
        nextGrain = 1;
        nearest = nearestCommitted = 0;
        lastFirst = std::numeric_limits<int64_t>::min();
        planned = false;
        grainHead = grainCount = 0;
        time = 0;
        late = 0;
        voicedCount = 0;
    }

    double& analysisAt(int64_t k) noexcept { return analysis[static_cast<size_t>(k) & markMask]; }
    double& synthesisAt(int64_t k) noexcept { return synthesis[static_cast<size_t>(k) & markMask]; }
    double& offsetAt(int64_t k) noexcept { return offsets[static_cast<size_t>(k) & markMask]; }
    double centreAt(int64_t k) noexcept { return analysisAt(k) + offsetAt(k); }

    // Where the pulse of the period about `p` lies from it (period T), and how sure that is (0..1): the
    // centroid of the low-passed signal's energy at the fundamental, over two periods (a Hann window, which
    // lets nothing of the energy's mean through at 1 / T). A pulse train is sure; a sine has no pulse.
    // Where the input doesn't reach p + T yet, the window is as many whole periods earlier as it takes (the pulse
    // is as far from p as from its centre): false.
    bool pulseAt(double p, double period, double& offset, double& confidence) const noexcept {
        const auto newest = static_cast<double>(tracker.written() - 1);
        bool whole = true;
        if (std::floor(p + period) > newest) {
            p -= period * std::ceil((std::floor(p + period) - newest) / period);
            whole = false;
        }
        const auto first = static_cast<int64_t>(std::ceil(p - period));
        const auto last = static_cast<int64_t>(std::floor(p + period));
        double s0, c0, s1, c1, sh, ch, s2, c2;
        fastmath::sincos(2.0 * kPi * (static_cast<double>(first) - p) / period, s0, c0);
        fastmath::sincos(kPi * (static_cast<double>(first) - p) / period, sh, ch);
        fastmath::sincos(2.0 * kPi / period, s1, c1);
        fastmath::sincos(kPi / period, s2, c2);
        double re = 0.0, im = 0.0, total = 0.0;
        for (int64_t n = first; n <= last; ++n) {
            const double x = tracker.lowAt(n), e = (0.5 + 0.5 * ch) * x * x;
            re += e * c0;
            im -= e * s0;
            total += e;
            const double c = c0 * c1 - s0 * s1, sn = s0 * c1 + c0 * s1;
            c0 = c;
            s0 = sn;
            const double hc = ch * c2 - sh * s2, hs = sh * c2 + ch * s2;
            ch = hc;
            sh = hs;
        }
        confidence = total > 0.0 ? std::hypot(re, im) / total : 0.0;
        offset = confidence > 0.0 ? -period * std::atan2(im, re) / (2.0 * kPi) : 0.0;
        return whole;
    }

    // A pitch's period in samples (pitch in semitones, MIDI).
    double periodOf(double pitch) const noexcept { return rate / (440.0 * std::exp2((pitch - 69.0) / 12.0)); }

    // The analysis and synthesis periods at a position and whether it is voiced: between the decided frames
    // either side of it (the voicing the nearer one's). Past the last decided frame, that one's (held): `exact`
    // false.
    void periodsAt(double position, double& analysisPeriod, double& synthesisPeriod, bool& voiced, bool& exact) const noexcept {
        exact = false;
        voiced = false;
        analysisPeriod = synthesisPeriod = unvoicedPeriod;
        if (controlCount == 0) return;
        const double x = position / hop;
        const double whole = std::floor(x);
        auto g0 = static_cast<int64_t>(whole);
        double f = x - whole;
        if (g0 < 0) {
            g0 = 0;
            f = 0.0;
        } else if (g0 + 1 < controlCount) {
            exact = true;
        } else {
            g0 = controlCount - 1;
            f = 0.0;
        }
        const Control& c0 = control[static_cast<size_t>(g0) & controlMask];
        const Control& c1 = exact ? control[static_cast<size_t>(g0 + 1) & controlMask] : c0;
        voiced = f < 0.5 ? c0.voiced : c1.voiced;
        if (!voiced) return;
        double pitch, out;
        if (c0.voiced && c1.voiced) {
            pitch = c0.pitch + (c1.pitch - c0.pitch) * f;
            out = c0.out + (c1.out - c0.out) * f;
        } else {
            const Control& c = c0.voiced ? c0 : c1;
            pitch = c.pitch;
            out = c.out;
        }
        analysisPeriod = periodOf(pitch);
        synthesisPeriod = periodOf(out);
    }

    // A decided frame: its correction; the provisional marks are placed again.
    void onFrame(const PitchFrame& frame) noexcept {
        Control c;
        if (frame.voiced) {
            const double pitch = 69.0 + 12.0 * std::log2(frame.f0 / 440.0);
            target = targetNote(pitch, controls.notes, target);
            if (!wasVoiced) correction = 0.0;
            const double k = controls.speedMs > 0.0 ? 1.0 - std::exp(-1000.0 * hop / rate / controls.speedMs) : 1.0;
            correction += (target - pitch - correction) * k;
            const double move = std::clamp(controls.strength * correction + controls.pitch, -kMaxMove, kMaxMove);
            c = {true, pitch, pitch + move, pitch + controls.strength * correction, target};
            ++voicedCount;
        } else {
            correction = 0.0;
            target = -1;
        }
        wasVoiced = frame.voiced;
        control[static_cast<size_t>(frame.index) & controlMask] = c;
        controlCount = frame.index + 1;
        analysisCount = std::min(analysisCount, std::max(analysisSettled, analysisCommitted));
        synthesisCount = std::min(synthesisCount, std::max(synthesisSettled, synthesisCommitted));
        nearest = std::min(nearest, nearestCommitted);
        planned = false;
    }

    // The next analysis mark: a period on (the period at the midpoint of the step). The first voiced one after
    // unvoiced ones is moved onto its period's pulse; after it the marks keep to the period, and their grains'
    // centres follow the pulse slowly (offsets). False when the ring is full.
    bool addAnalysis() noexcept {
        const int64_t m = analysisCount;
        if (m - nearestCommitted + 16 >= static_cast<int64_t>(analysis.size())) return false;
        const double a = analysisAt(m - 1);
        double t1, t2, unused;
        bool voiced, midVoiced, exact, midExact;
        periodsAt(a, t1, unused, voiced, exact);
        periodsAt(a + 0.5 * t1, t2, unused, midVoiced, midExact);
        exact = exact && midExact;
        double next = a + t2, offset = 0.0;
        if (midVoiced) {
            double pulse, sure;
            exact = pulseAt(next, t2, pulse, sure) && exact;
            // the first voiced mark after an unvoiced one (or the frames there unvoiced, or the first after a reset)
            if (!voiced || m == 2 || analysisVoiced[static_cast<size_t>(m - 1) & markMask] == 0) {
                // onto the pulse, the step no longer than the longest period and a quarter of one at least
                if (sure >= kOnsetSureness) {
                    if (next + pulse > a + longest) pulse -= t2;
                    if (next + pulse >= a + 0.25 * t2) next += pulse;
                }
            } else {
                const double previous = offsetAt(m - 1);
                const double towards = pulse - t2 * std::round((pulse - previous) / t2);  // the nearest to it
                const double limit = kOffsetShare * std::min(t2, unvoicedPeriod);
                offset = std::clamp(previous + kOffsetFollow * sure * (towards - previous), -limit, limit);
                // the centres in order: a quarter of a period apart at least
                offset = std::max(offset, a + previous + 0.25 * t2 - next);
            }
        }
        offsetAt(m) = offset;
        analysisVoiced[static_cast<size_t>(m) & markMask] = midVoiced ? 1 : 0;
        analysisAt(m) = next;
        analysisCount = m + 1;
        if (exact && analysisSettled == m) analysisSettled = m + 1;
        return true;
    }

    // The next synthesis mark: an output period on where voiced, else (and after an unvoiced step, as the next
    // analysis mark may have moved onto a pulse) the next analysis mark. False when a ring is full.
    bool addSynthesis() noexcept {
        const int64_t k = synthesisCount;
        if (k - nextGrain + 16 >= static_cast<int64_t>(synthesis.size())) return false;
        const double s = synthesisAt(k - 1);
        double unused, t1, t2;
        bool voiced, midVoiced, exact, midExact;
        periodsAt(s, unused, t1, voiced, exact);
        double next;
        uint8_t stepVoiced = 0;
        if (voiced && synthesisVoiced[static_cast<size_t>(k - 1) & markMask] != 0) {
            periodsAt(s + 0.5 * t1, unused, t2, midVoiced, midExact);
            next = s + t2;
            stepVoiced = midVoiced ? 1 : 0;
            exact = exact && midExact;
        } else {
            while (!(analysisAt(analysisCount - 1) > s))
                if (!addAnalysis()) return false;
            int64_t i = analysisCount - 1;
            while (i > nearestCommitted && analysisAt(i - 1) > s) --i;
            next = analysisAt(i);
            stepVoiced = analysisVoiced[static_cast<size_t>(i) & markMask];
            exact = exact && i < analysisSettled;
        }
        synthesisVoiced[static_cast<size_t>(k) & markMask] = stepVoiced;
        synthesisAt(k) = next;
        synthesisCount = k + 1;
        if (exact && synthesisSettled == k) synthesisSettled = k + 1;
        return true;
    }

    // Synthesis mark k playing analysis mark j's grain, read alpha samples a sample (voiced ones): where and how.
    void shape(int64_t k, int64_t j, double formantRatio, Grain& g) noexcept {
        const bool voicedGrain = analysisVoiced[static_cast<size_t>(j) & markMask] != 0;
        // Formant reads voiced grains faster or slower. Unvoiced ones (breath, sibilants) play as they were:
        // overlapping grains of noise read at another speed read different noise, and their sum is weaker by
        // an amount that depends on the noise.
        const double alpha = voicedGrain ? formantRatio : 1.0;
        // A voiced grain is centred on its pulse: read about the analysis mark's centre, played as far from the
        // synthesis mark (so at a ratio of 1 the read is the sample played, and the windows meet as before).
        const double offset = voicedGrain ? offsetAt(j) : 0.0;
        g.k = k;
        g.j = j;
        g.voiced = voicedGrain;
        g.s = synthesisAt(k) + offset;
        g.a = analysisAt(j) + offset;
        g.alpha = alpha;
        g.gain = 1.0;
        if (voicedGrain) {
            g.left = (g.a - centreAt(j - 1)) / alpha;
            // Grains reaching past their neighbours' marks are divided by the windows' sum (render()), so each
            // harmonic keeps its level; grains falling short of them (q < 1: a lower pitch, a higher Formant)
            // would lose q of it, and are raised by 1 / q. Then the pitch's ratio r: sqrt(r) keeps the power
            // of a flat spectrum as its harmonics thin out or crowd in. At r = 1 and Formant 0, exactly 1.
            const double r = std::clamp((analysisAt(j + 1) - analysisAt(j - 1)) / (synthesisAt(k + 1) - synthesisAt(k - 1)),
                                        kLowestRatio, 1.0 / kLowestRatio);
            const double q = r / alpha;
            g.gain = std::sqrt(r) * (q < 1.0 ? 1.0 / q : 1.0);
        } else {
            // to the marks either side and a centre's offset past them (a voiced neighbour's centre), and wider
            const double past = kOffsetShare * unvoicedPeriod;
            g.left = std::max(2.0 * unvoicedPeriod, std::min(g.s - synthesisAt(k - 1), longest) + past);
        }
        g.first = static_cast<int64_t>(std::floor(g.s - g.left)) + 1;
        shapeRight(g);
    }

    // A grain's right half, from the marks after its own.
    void shapeRight(Grain& g) noexcept {
        if (g.voiced) {
            g.right = (centreAt(g.j + 1) - g.a) / g.alpha;
        } else {
            const double past = kOffsetShare * unvoicedPeriod;
            g.right = std::max(2.0 * unvoicedPeriod, std::min(synthesisAt(g.k + 1) - g.s, longest) + past);
        }
        g.last = static_cast<int64_t>(std::ceil(g.s + g.right)) - 1;
    }

    // The output reaches a grain's centre: its right half from the marks after its own as they are now, taken for
    // good, and no further than the input will be in (read faster).
    bool settleRight(Grain& g) noexcept {
        while (analysisCount <= g.j + 1)
            if (!addAnalysis()) return false;
        while (synthesisCount <= g.k + 1)
            if (!addSynthesis()) return false;
        shapeRight(g);
        analysisCommitted = std::max(analysisCommitted, g.j + 2);
        synthesisCommitted = std::max(synthesisCommitted, g.k + 2);
        analysisSettled = std::max(analysisSettled, analysisCommitted);
        synthesisSettled = std::max(synthesisSettled, synthesisCommitted);
        if (g.alpha > 1.0 && lookahead(g) > latency) {
            const double taps = std::ceil(kSincHalf * g.alpha) + 1.0;
            g.right = std::max(0.0, (latency - taps - (g.a - g.s)) / (g.alpha - 1.0) - 1.0);
            g.last = static_cast<int64_t>(std::ceil(g.s + g.right)) - 1;
        }
        g.open = false;
        return true;
    }

    // How far past the output sample it plays a grain reads the input, at the most (the interpolation's taps too).
    static double lookahead(const Grain& g) noexcept {
        const double taps = g.alpha > 1.0 ? std::ceil(kSincHalf * g.alpha) + 1.0 : kSincHalf + 1.0;
        const double atFirst = g.a - g.s + (static_cast<double>(g.first) - g.s) * (g.alpha - 1.0);
        const double atLast = g.a - g.s + (static_cast<double>(g.last) - g.s) * (g.alpha - 1.0);
        return std::max(atFirst, atLast) + taps;
    }

    // Synthesis mark k's grain as it would play now, the marks placed as far as it needs: the analysis mark nearest
    // it, or (Low Latency) the nearest before that whose grain reads no input that won't be in when it is played.
    // False when a ring is full.
    bool planGrain(int64_t k, double formantRatio, Grain& g, int64_t& j) noexcept {
        while (synthesisCount <= k + 1)
            if (!addSynthesis()) return false;
        const double s = synthesisAt(k);
        for (;;) {
            while (analysisCount <= nearest + 1)
                if (!addAnalysis()) return false;
            if (!(analysisAt(nearest + 1) <= s)) break;
            ++nearest;
        }
        j = std::max<int64_t>(s - analysisAt(nearest) <= analysisAt(nearest + 1) - s ? nearest : nearest + 1, 1);
        while (analysisCount <= j + 1)
            if (!addAnalysis()) return false;
        for (;; --j) {
            shape(k, j, formantRatio, g);
            if (j <= std::max<int64_t>(1, nearestCommitted - 4) || lookahead(g) <= latency) break;
        }
        return true;
    }

    // A grain's rising half squeezed to begin at output sample `from` (it is no wider than that).
    static void squeeze(Grain& g, int64_t from) noexcept {
        g.left = std::max(0.5, g.s - static_cast<double>(from) + 0.5);
        g.first = static_cast<int64_t>(std::floor(g.s - g.left)) + 1;
    }

    // Starts the grains whose windows begin by output sample n. High Quality starts each as soon as it might
    // (its marks are settled long before); Low Latency as late as it can (commitAhead before its window), its
    // marks placed with the frames decided by then, and in order (a wider grain after narrower ones begins with
    // the one before it, its rising half squeezed).
    void startGrains(int64_t n) noexcept {
        if (controls.formant != formantOf) {
            formantOf = controls.formant;
            formantRatio = std::exp2(std::clamp(formantOf, -kMaxFormant, kMaxFormant) / 12.0);
        }
        const bool early = quality == Quality::HighQuality;
        while (grainCount < kMaxGrains) {
            if (early) {
                while (synthesisCount <= nextGrain)
                    if (!addSynthesis()) return;
                if (synthesisAt(nextGrain) - reach > static_cast<double>(n)) return;
            }
            if (!planned || planFormant != formantRatio) {
                if (!planGrain(nextGrain, formantRatio, plan, planJ)) return;
                if (!early && plan.first < lastFirst) squeeze(plan, lastFirst);
                planFormant = formantRatio;
                planned = true;
            }
            if (!early && plan.first > n + commitAhead) return;
            Grain& g = plan;
            const int64_t j = planJ;
            planned = false;
            analysisCommitted = std::max({analysisCommitted, j + 1, nearest + 1});
            synthesisCommitted = std::max(synthesisCommitted, nextGrain + 1);
            analysisSettled = std::max(analysisSettled, analysisCommitted);
            synthesisSettled = std::max(synthesisSettled, synthesisCommitted);
            nearestCommitted = nearest;
            ++nextGrain;
            if (g.first < n) {
                // its window would have begun already (Low Latency, where the frame just decided made it wider: the
                // voice ended, or a long period read slowly)
                late += n - g.first;
                squeeze(g, n);
            }
            if (lookahead(g) > latency) {  // (never past the start: planGrain chose an earlier grain)
                late += std::max<int64_t>(0, g.last + 1 - g.first);
                continue;
            }
            lastFirst = g.first;
            if (g.alpha == 1.0) {
                // the read is the output sample's, offset by a - s throughout: one set of taps for the grain
                const double offset = g.a - g.s;
                const double whole = std::floor(offset);
                g.fraction = offset - whole;
                g.shift = static_cast<int64_t>(whole);
                if (g.fraction != 0.0)
                    for (int t = 0; t < 2 * kSincHalf; ++t)
                        g.taps[static_cast<size_t>(t)] = sincAt(sinc, g.fraction - (t - (kSincHalf - 1)));
            }
            g.done = false;
            g.open = true;
            grains[static_cast<size_t>((grainHead + grainCount) & kGrainMask)] = g;
            ++grainCount;
        }
    }

    // Output sample n: the grains playing it, summed, divided by their windows' sum where it passes 1.
    void render(int64_t n, float* const* io, int i) noexcept {
        double sum[2] = {0.0, 0.0}, weights = 0.0;
        const int chs = channels;
        const double* table = sinc;
        for (int k = 0; k < grainCount; ++k) {
            Grain& g = grains[static_cast<size_t>((grainHead + k) & kGrainMask)];
            if (g.done || n < g.first) continue;
            if (g.open && static_cast<double>(n) + 1.0 > g.s && !settleRight(g)) g.last = n;
            if (n > g.last) {
                g.done = true;
                continue;
            }
            const double u = static_cast<double>(n) - g.s;
            const double w = window(u, g.left, g.right);
            if (g.alpha == 1.0) {
                const double heard = g.gain * w;
                const int64_t base = n + g.shift;
                for (int c = 0; c < chs; ++c) {
                    const float* x = input[static_cast<size_t>(c)].data();
                    double value;
                    if (g.fraction == 0.0) {
                        value = x[static_cast<size_t>(base) & inputMask];
                    } else {
                        value = 0.0;
                        for (int t = 0; t < 2 * kSincHalf; ++t)
                            value += g.taps[static_cast<size_t>(t)] * x[static_cast<size_t>(base + t - (kSincHalf - 1)) & inputMask];
                    }
                    sum[c] += heard * value;
                }
            } else {
                // read faster or slower: the taps follow the read, low-passed to 1 / alpha of the band read faster
                const double scale = g.alpha > 1.0 ? 1.0 / g.alpha : 1.0;
                const int half = g.alpha > 1.0 ? static_cast<int>(std::ceil(kSincHalf * g.alpha)) : kSincHalf;
                const double position = g.a + u * g.alpha;
                const double whole = std::floor(position);
                const double fraction = position - whole;
                const auto base = static_cast<int64_t>(whole);
                double values[2] = {0.0, 0.0};
                for (int t = 1 - half; t <= half; ++t) {
                    const double tap = sincAt(table, (fraction - t) * scale) * scale;
                    const size_t at = static_cast<size_t>(base + t) & inputMask;
                    for (int c = 0; c < chs; ++c) values[c] += tap * input[static_cast<size_t>(c)][at];
                }
                for (int c = 0; c < chs; ++c) sum[c] += g.gain * w * values[c];
            }
            weights += w;
            if (n >= g.last) g.done = true;
        }
        while (grainCount > 0 && grains[static_cast<size_t>(grainHead)].done) {
            grainHead = (grainHead + 1) & kGrainMask;
            --grainCount;
        }
        const double gain = weights > 1.0 ? 1.0 / weights : 1.0;
        for (int c = 0; c < chs; ++c) io[c][i] = static_cast<float>(sum[c] * gain);
    }

    // A grain's window at u output samples from its mark: Hann halves, `left` and `right` long.
    static double window(double u, double left, double right) noexcept {
        const double x = u < 0.0 ? -u / left : u / right;
        if (!(x < 1.0)) return 0.0;
        return 0.5 + 0.5 * fastmath::cos(kPi * x);
    }

    // The frame at the input's time of the output sample `ago` before the last one out.
    Heard heard(int ago) const noexcept {
        const int64_t n = time - 1 - ago - latency;
        if (n < 0 || controlCount == 0) return {};
        const int64_t index = std::min((n + hop / 2) / hop, controlCount - 1);
        if (index < controlCount - static_cast<int64_t>(control.size())) return {};
        const Control& c = control[static_cast<size_t>(index) & controlMask];
        if (!c.voiced) return {};
        return {true, c.pitch, c.corrected, c.note};
    }

    void process(float* const* io, int numChannels, int numFrames) noexcept {
        if (controls.quality != quality) {  // another tracker, another latency: from the start again
            quality = controls.quality;
            tracker.setQuality(quality);
            latency = latencies[static_cast<size_t>(quality)];
            reset();
        }
        channels = std::clamp(numChannels, 1, 2);
        if (numChannels < 1) return;
        for (int i = 0; i < numFrames; ++i) {
            const size_t in = static_cast<size_t>(time) & inputMask;
            double mono = 0.0;
            for (int c = 0; c < channels; ++c) {
                const float x = io[c][i];
                input[static_cast<size_t>(c)][in] = x;
                mono += x;
            }
            if (tracker.push(mono / channels)) onFrame(tracker.decided());
            const int64_t n = time - latency;
            startGrains(n);
            render(n, io, i);
            ++time;
        }
    }
};

Corrector::Corrector() : impl_(std::make_unique<Impl>()) {}
Corrector::~Corrector() = default;
void Corrector::prepare(double sampleRate) { impl_->prepare(sampleRate); }
void Corrector::reset() noexcept { impl_->reset(); }
int Corrector::latency() const noexcept { return impl_->latency; }
int Corrector::latency(Quality quality) const noexcept { return impl_->latencies[static_cast<size_t>(quality)]; }
void Corrector::setControls(const Controls& controls) noexcept { impl_->controls = controls; }
void Corrector::process(float* const* channels, int numChannels, int numFrames) noexcept {
    impl_->process(channels, numChannels, numFrames);
}
Heard Corrector::heard(int ago) const noexcept { return impl_->heard(ago); }
int64_t Corrector::lateSamples() const noexcept { return impl_->late; }
int64_t Corrector::voicedFrames() const noexcept { return impl_->voicedCount; }
const PitchFrame& Corrector::lastFrame() const noexcept { return impl_->tracker.decided(); }

}  // namespace sub::gigatune
