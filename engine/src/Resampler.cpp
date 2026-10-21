#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>

#if defined(__AVX__)
#include <immintrin.h>
#endif

namespace sub {

namespace {

// The modified Bessel function of the first kind, order 0 (Kaiser's window), by its series.
double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double quarter = x * x / 4.0;
    for (int k = 1; k < 200 && term > sum * 1e-17; ++k) {
        term *= quarter / (static_cast<double>(k) * k);
        sum += term;
    }
    return sum;
}

// The lanes of a sum added pairwise: ((0 + 4) + (1 + 5)) + ((2 + 6) + (3 + 7)).
float sumLanes(const float* lanes) {
    return ((lanes[0] + lanes[4]) + (lanes[1] + lanes[5])) + ((lanes[2] + lanes[6]) + (lanes[3] + lanes[7]));
}

// Taps times the frames of C channels (`frames[c]`) into out[c], each summed in eight lanes: the
// even blocks of eight taps in one set of them and the odd in another (sums that don't wait on each
// other), the sets added lane by lane and the lanes pairwise at the end. The same order whatever the
// chunks the frames came in. With AVX, a vector a set; elsewhere the plain loops. `n` is a multiple of 8.
template <int C>
void dots(const float* taps, const float* const* frames, int n, float* out) {
    int i = 0;
#if defined(__AVX__)
    __m256 even[C], odd[C];
    for (int c = 0; c < C; ++c) even[c] = odd[c] = _mm256_setzero_ps();
    for (; i + 16 <= n; i += 16) {
        const __m256 low = _mm256_loadu_ps(taps + i), high = _mm256_loadu_ps(taps + i + 8);
        for (int c = 0; c < C; ++c) {
            even[c] = _mm256_add_ps(even[c], _mm256_mul_ps(low, _mm256_loadu_ps(frames[c] + i)));
            odd[c] = _mm256_add_ps(odd[c], _mm256_mul_ps(high, _mm256_loadu_ps(frames[c] + i + 8)));
        }
    }
    if (i < n) {
        const __m256 low = _mm256_loadu_ps(taps + i);
        for (int c = 0; c < C; ++c) even[c] = _mm256_add_ps(even[c], _mm256_mul_ps(low, _mm256_loadu_ps(frames[c] + i)));
    }
    for (int c = 0; c < C; ++c) {
        alignas(32) float lanes[8];
        _mm256_store_ps(lanes, _mm256_add_ps(even[c], odd[c]));
        out[c] = sumLanes(lanes);
    }
#else
    float even[C][8] = {}, odd[C][8] = {};
    for (; i + 16 <= n; i += 16) {
        for (int c = 0; c < C; ++c) {
            for (int l = 0; l < 8; ++l) even[c][l] += taps[i + l] * frames[c][i + l];
            for (int l = 0; l < 8; ++l) odd[c][l] += taps[i + 8 + l] * frames[c][i + 8 + l];
        }
    }
    if (i < n) {
        for (int c = 0; c < C; ++c) {
            for (int l = 0; l < 8; ++l) even[c][l] += taps[i + l] * frames[c][i + l];
        }
    }
    for (int c = 0; c < C; ++c) {
        float lanes[8];
        for (int l = 0; l < 8; ++l) lanes[l] = even[c][l] + odd[c][l];
        out[c] = sumLanes(lanes);
    }
#endif
}

constexpr int64_t kOutChunk = 4096;     // output frames handed on at a time
constexpr int64_t kDropAfter = 1 << 16;  // input frames no longer needed, let go of once this many

}  // namespace

Resampler::Resampler(uint32_t channels, uint32_t inRate, uint32_t outRate) : channels_(channels) {
    if (channels == 0 || inRate == 0 || outRate == 0) throw std::invalid_argument("Resampler: no channels, or a rate of 0");
    const uint32_t common = std::gcd(inRate, outRate);
    up_ = outRate / common;
    down_ = inRate / common;
    // The cutoff, of the input's Nyquist frequency: the lower rate's.
    const double cutoff = std::min(1.0, static_cast<double>(outRate) / inRate);
    half_ = static_cast<int>(std::ceil(kZeroCrossings / cutoff));
    half_ = (half_ + 3) / 4 * 4;
    taps_ = 2 * half_;
    exact_ = up_ <= kMaxPhases;
    // (Interpolated: fewer phases for the long filters of steep downsampling, which are smooth.)
    phases_ = exact_ ? up_ : std::clamp<int64_t>((int64_t{1} << 21) / taps_, 256, kMaxPhases);
    const int64_t rows = exact_ ? phases_ : phases_ + 1;
    table_.resize(static_cast<size_t>(rows * taps_));
    const double beta = 0.1102 * (kStopbandDb - 8.7);
    const double windowScale = 1.0 / besselI0(beta);
    std::vector<double> weights(static_cast<size_t>(taps_));
    for (int64_t r = 0; r < rows; ++r) {
        const double past = static_cast<double>(r) / static_cast<double>(phases_);  // the place past its frame
        double sum = 0.0;
        for (int m = 0; m < taps_; ++m) {
            const double x = static_cast<double>(m - half_ + 1) - past;  // tap m's frame, from the place
            const double u = x / half_;
            const double window = std::abs(u) >= 1.0 ? 0.0 : besselI0(beta * std::sqrt(1.0 - u * u)) * windowScale;
            const double y = std::numbers::pi * cutoff * x;
            const double sinc = y == 0.0 ? 1.0 : std::sin(y) / y;
            weights[static_cast<size_t>(m)] = sinc * window;
            sum += weights[static_cast<size_t>(m)];
        }
        float* row = table_.data() + r * taps_;
        for (int m = 0; m < taps_; ++m) row[m] = static_cast<float>(weights[static_cast<size_t>(m)] / sum);
    }
    // Before the input's start: silence, so the first frames' taps are all held.
    held_.assign(channels_, std::vector<float>(static_cast<size_t>(half_), 0.f));
    base_ = -half_;
    scratch_.resize(static_cast<size_t>(taps_));
    step_ = down_ / up_;
    stepPhase_ = down_ % up_;
}

int64_t Resampler::outputFrames(int64_t inputFrames, uint32_t inRate, uint32_t outRate) {
    if (inputFrames <= 0 || inRate == 0 || outRate == 0) return 0;
    if (inRate == outRate) return inputFrames;
    const uint32_t common = std::gcd(inRate, outRate);
    const int64_t up = outRate / common, down = inRate / common;
    return (inputFrames * up + down - 1) / down;
}

const float* Resampler::row(int64_t phase, float* scratch) const {
    if (exact_) return table_.data() + phase * taps_;
    const double where = static_cast<double>(phase) / static_cast<double>(up_) * static_cast<double>(phases_);
    const auto below = std::min(static_cast<int64_t>(where), phases_ - 1);
    const auto f = static_cast<float>(where - static_cast<double>(below));
    const float* a = table_.data() + below * taps_;
    const float* b = a + taps_;
    for (int m = 0; m < taps_; ++m) scratch[m] = a[m] + f * (b[m] - a[m]);
    return scratch;
}

void Resampler::push(const float* interleaved, int64_t frames, const Sink& out) {
    if (frames <= 0) return;
    for (uint32_t c = 0; c < channels_; ++c) {
        std::vector<float>& held = held_[c];
        const size_t at = held.size();
        held.resize(at + static_cast<size_t>(frames));
        for (int64_t i = 0; i < frames; ++i) held[at + static_cast<size_t>(i)] = interleaved[i * channels_ + c];
    }
    pushed_ += frames;
    produce(INT64_MAX, out);
}

void Resampler::finish(const Sink& out) {
    // After the input's end: silence, as far as the last frame's taps reach.
    for (std::vector<float>& held : held_) held.resize(held.size() + static_cast<size_t>(half_), 0.f);
    produce(outputFrames(pushed_, static_cast<uint32_t>(down_), static_cast<uint32_t>(up_)), out);
}

void Resampler::produce(int64_t until, const Sink& out) {
    const auto end = base_ + static_cast<int64_t>(held_[0].size());  // past the last frame held
    outBuffer_.clear();
    const auto flush = [&] {
        if (outBuffer_.empty()) return;
        out(outBuffer_.data(), static_cast<int64_t>(outBuffer_.size() / channels_));
        outBuffer_.clear();
    };
    std::vector<const float*> frames(channels_);
    float sums[2];
    for (; next_ < until; ++next_) {
        if (at_ + half_ >= end) break;  // (its last tap isn't here yet)
        const float* taps = row(phase_, scratch_.data());
        const auto first = static_cast<size_t>(at_ - half_ + 1 - base_);
        if (channels_ == 2) {
            const float* const pair[2] = {held_[0].data() + first, held_[1].data() + first};
            dots<2>(taps, pair, taps_, sums);
            outBuffer_.push_back(sums[0]);
            outBuffer_.push_back(sums[1]);
        } else {
            for (uint32_t c = 0; c < channels_; ++c) {
                frames[0] = held_[c].data() + first;
                dots<1>(taps, frames.data(), taps_, sums);
                outBuffer_.push_back(sums[0]);
            }
        }
        if (static_cast<int64_t>(outBuffer_.size()) >= kOutChunk * channels_) flush();
        // The next frame's place: `step_` input frames on, and `stepPhase_` phases (of up_ a frame).
        at_ += step_;
        phase_ += stepPhase_;
        if (phase_ >= up_) {
            phase_ -= up_;
            ++at_;
        }
    }
    flush();
    // Frames no output frame reaches any more go (a long stretch of them at a time).
    const int64_t needed = at_ - half_ + 1;
    if (needed - base_ >= kDropAfter) {
        for (std::vector<float>& held : held_) held.erase(held.begin(), held.begin() + (needed - base_));
        base_ = needed;
    }
}

}  // namespace sub
