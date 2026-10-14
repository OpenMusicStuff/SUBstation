#include "Warp.h"

#include <algorithm>
#include <cmath>

#include "Snapshot.h"

#if defined(__AVX__)
#include <immintrin.h>
#endif

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "signalsmith-stretch.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace sub {
namespace {

// Shorter blocks keep drum transients tight; longer ones give smoother tones
// and textures at the cost of smearing attacks.
constexpr std::array<StretchTiming, kNumStretchConfigs> kTimings = {{
    {0.080, 0.020},  // Transient (Transients mode)
    {0.120, 0.030},  // Standard (Standard and Formants modes): Signalsmith's default preset
    {0.200, 0.050},  // Smooth (Smooth mode)
}};

// A clip's source as the stretcher's input: `reader[channel][i]` is source frame
// `start + i`, with silence outside the file. Mono sources feed both channels.
struct SourceInput {
    const AudioSource* source;
    int64_t start;

    struct Channel {
        const float* data;
        int64_t frames;
        int64_t start;
        float operator[](int i) const noexcept {
            const int64_t k = start + i;
            return k >= 0 && k < frames ? data[k] : 0.f;
        }
    };
    Channel operator[](int c) const noexcept {
        return {source->stereoChannel(static_cast<uint32_t>(c)), source->frames(), start};
    }
};

// Windowed-sinc (Lanczos) kernel, tabulated. kHalfWidth zero crossings each
// side; linear interpolation between kResolution points per zero crossing.
constexpr int kHalfWidth = 8;
constexpr int kResolution = 512;
constexpr double kMaxRateFiltered = 4.0;  // beyond this, the kernel stops widening (cost cap)

struct SincTable {
    std::array<float, kHalfWidth * kResolution + 2> values{};
    SincTable() {
        constexpr double pi = 3.14159265358979323846;
        values[0] = 1.f;
        for (size_t i = 1; i < values.size(); ++i) {
            const double x = static_cast<double>(i) / kResolution;
            if (x >= kHalfWidth) {
                values[i] = 0.f;
                continue;
            }
            const double px = pi * x;
            values[i] = static_cast<float>(std::sin(px) / px * std::sin(px / kHalfWidth) / (px / kHalfWidth));
        }
    }
    float operator()(double x) const noexcept {  // x >= 0
        const double scaled = x * kResolution;
        const auto index = static_cast<size_t>(scaled);
        if (index + 1 >= values.size()) return 0.f;
        const auto frac = static_cast<float>(scaled - static_cast<double>(index));
        return values[index] + frac * (values[index + 1] - values[index]);
    }
};

// Built during static initialisation, never on the audio thread.
const SincTable kSinc;

// The same kernel laid out by phase, for rates up to 1 (the full cutoff). There
// a frame's 2 * kHalfWidth taps lie whole source frames apart, so all of them
// sit the same fraction between two table points. Row q holds the taps at
// fraction q / kResolution past a source frame. A frame's weights are two
// neighbouring rows mixed by what is left of the fraction: kSinc's values,
// without a lookup per tap.
struct SincPhases {
    static constexpr int kTaps = 2 * kHalfWidth;  // source frames floor(pos) - kHalfWidth + 1 .. floor(pos) + kHalfWidth
    std::array<std::array<float, kTaps>, kResolution + 1> rows{};
    SincPhases() {
        for (int q = 0; q <= kResolution; ++q) {
            for (int m = 0; m < kTaps; ++m)  // tap m is (kHalfWidth - 1 - m) + q / kResolution frames from pos
                rows[static_cast<size_t>(q)][static_cast<size_t>(m)] =
                    kSinc.values[static_cast<size_t>(std::abs((kHalfWidth - 1 - m) * kResolution + q))];
        }
    }
};

const SincPhases kSincPhases;

#if defined(__AVX__)
// The sums of eight lanes each: lanes k and k + 4, then neighbours, then the
// two pairs.
inline void sumLanes(__m256 left, __m256 right, float& outL, float& outR) noexcept {
    const __m128 l4 = _mm_add_ps(_mm256_castps256_ps128(left), _mm256_extractf128_ps(left, 1));
    const __m128 r4 = _mm_add_ps(_mm256_castps256_ps128(right), _mm256_extractf128_ps(right, 1));
    const __m128 both = _mm_hadd_ps(_mm_hadd_ps(l4, r4), _mm_setzero_ps());  // left in lane 0, right in lane 1
    outL = _mm_cvtss_f32(both);
    outR = _mm_cvtss_f32(_mm_shuffle_ps(both, both, 1));
}
#endif

// One frame from the phase table: the taps from source frames `l` and `r` on,
// weighted by rows `low` and `high` mixed by `frac`. With AVX, a vector of the
// eight taps of each half; elsewhere the plain loop (compilers leave it scalar:
// the frame's sum ends in one value). Both sum in the same order: the halves
// lane by lane, then the lanes pairwise.
inline void phasedFrame(const float* low, const float* high, float frac, const float* l, const float* r,
                        float& outL, float& outR) noexcept {
    constexpr int kLanes = SincPhases::kTaps / 2;
    static_assert(kLanes == 8);
#if defined(__AVX__)
    const __m256 f = _mm256_set1_ps(frac);
    const __m256 low0 = _mm256_loadu_ps(low), low1 = _mm256_loadu_ps(low + kLanes);
    const __m256 w0 = _mm256_add_ps(low0, _mm256_mul_ps(f, _mm256_sub_ps(_mm256_loadu_ps(high), low0)));
    const __m256 w1 = _mm256_add_ps(low1, _mm256_mul_ps(f, _mm256_sub_ps(_mm256_loadu_ps(high + kLanes), low1)));
    sumLanes(_mm256_add_ps(_mm256_mul_ps(_mm256_loadu_ps(l), w0), _mm256_mul_ps(_mm256_loadu_ps(l + kLanes), w1)),
             _mm256_add_ps(_mm256_mul_ps(_mm256_loadu_ps(r), w0), _mm256_mul_ps(_mm256_loadu_ps(r + kLanes), w1)),
             outL, outR);
#else
    float sumL[kLanes], sumR[kLanes];
    for (int lane = 0; lane < kLanes; ++lane) {
        const float w0 = low[lane] + frac * (high[lane] - low[lane]);
        const float w1 = low[lane + kLanes] + frac * (high[lane + kLanes] - low[lane + kLanes]);
        sumL[lane] = l[lane] * w0 + l[lane + kLanes] * w1;
        sumR[lane] = r[lane] * w0 + r[lane + kLanes] * w1;
    }
    float l4[4], r4[4];
    for (int k = 0; k < 4; ++k) {
        l4[k] = sumL[k] + sumL[k + 4];
        r4[k] = sumR[k] + sumR[k + 4];
    }
    outL = (l4[0] + l4[1]) + (l4[2] + l4[3]);
    outR = (r4[0] + r4[1]) + (r4[2] + r4[3]);
#endif
}

// One frame at any rate: source frames first..last (at least one) around `pos`,
// the kernel scaled by `cutoff`; the sums before the gain. With AVX2, eight taps
// at a time. Each tap's position in the table is computed in float from its
// whole frames and the fraction apart, within a thousandth of a table step of
// kSinc's (a weight within about 1e-6). Its two table values are loaded as one
// pair: no gather instructions, which some CPUs run very slowly. Elsewhere,
// kSinc tap by tap.
inline void kernelFrame(const float* srcL, const float* srcR, double pos, int64_t first, int64_t last, double cutoff,
                        float& outL, float& outR) noexcept {
#if defined(__AVX2__)
    const auto taps = static_cast<int>(last - first + 1);
    const double whole = std::floor(pos);
    const __m256 n0 = _mm256_set1_ps(static_cast<float>(whole - static_cast<double>(first)));
    const __m256 step = _mm256_set1_ps(static_cast<float>(cutoff * kResolution));
    const __m256 fraction = _mm256_set1_ps(static_cast<float>((pos - whole) * cutoff * kResolution));
    const __m256 sign = _mm256_set1_ps(-0.f);
    const __m256i end = _mm256_set1_epi32(kHalfWidth * kResolution);  // the kernel is 0 from there on
    const float* values = kSinc.values.data();
    __m256 sumL = _mm256_setzero_ps(), sumR = _mm256_setzero_ps();
    for (int j = 0; j < taps; j += 8) {
        const __m256i jv = _mm256_add_epi32(_mm256_set1_epi32(j), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
        const __m256 offset = _mm256_sub_ps(n0, _mm256_cvtepi32_ps(jv));  // whole frames from pos (exact)
        const __m256 at = _mm256_andnot_ps(sign, _mm256_add_ps(_mm256_mul_ps(offset, step), fraction));
        const __m256i index = _mm256_min_epi32(_mm256_cvttps_epi32(at), end);
        const __m256 frac = _mm256_sub_ps(at, _mm256_cvtepi32_ps(index));
        alignas(32) int32_t indices[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(indices), index);
        auto pairs = [&](int k0, int k1) {  // (values[i], values[i + 1]) of taps k0 and k1
            const __m128d low = _mm_load_sd(reinterpret_cast<const double*>(values + indices[k0]));
            return _mm_castpd_ps(_mm_loadh_pd(low, reinterpret_cast<const double*>(values + indices[k1])));
        };
        const __m256 pairs0 = _mm256_insertf128_ps(_mm256_castps128_ps256(pairs(0, 1)), pairs(4, 5), 1);
        const __m256 pairs1 = _mm256_insertf128_ps(_mm256_castps128_ps256(pairs(2, 3)), pairs(6, 7), 1);
        const __m256 a = _mm256_shuffle_ps(pairs0, pairs1, _MM_SHUFFLE(2, 0, 2, 0));
        const __m256 b = _mm256_shuffle_ps(pairs0, pairs1, _MM_SHUFFLE(3, 1, 3, 1));
        // Taps past `last` weigh nothing and aren't read.
        const __m256i inside = _mm256_cmpgt_epi32(_mm256_set1_epi32(taps), jv);
        const __m256 w = _mm256_and_ps(_mm256_add_ps(a, _mm256_mul_ps(frac, _mm256_sub_ps(b, a))),
                                       _mm256_castsi256_ps(inside));
        sumL = _mm256_add_ps(sumL, _mm256_mul_ps(_mm256_maskload_ps(srcL + first + j, inside), w));
        sumR = _mm256_add_ps(sumR, _mm256_mul_ps(_mm256_maskload_ps(srcR + first + j, inside), w));
    }
    sumLanes(sumL, sumR, outL, outR);
#else
    float l = 0.f, r = 0.f;
    for (int64_t k = first; k <= last; ++k) {
        const float w = kSinc(std::abs(pos - static_cast<double>(k)) * cutoff);
        l += srcL[k] * w;
        r += srcR[k] * w;
    }
    outL = l;
    outR = r;
#endif
}

}  // namespace

StretchTiming stretchTiming(StretchConfig config) noexcept { return kTimings[static_cast<size_t>(config)]; }

StretchConfig stretchConfigFor(WarpMode mode) noexcept {
    switch (mode) {
        case WarpMode::Transients: return StretchConfig::Transient;
        case WarpMode::Smooth: return StretchConfig::Smooth;
        default: return StretchConfig::Standard;
    }
}

// ---------------------------------------------------------------------------
// Stretch voices

struct WarpVoice::State {
    signalsmith::stretch::SignalsmithStretch<float> stretch{kStretchSeed};
    int64_t inputPos = 0;  // next source frame to feed
    double lead = 0.0;     // source frames between the fed input and the frame being output
    float transpose = 0.f;
    bool formants = false;
    bool primed = false;
};

WarpVoice::WarpVoice(StretchConfig config, double sampleRate)
    : config_(config), state_(std::make_unique<State>()) {
    configureStretcher(state_->stretch, config, sampleRate);
}

WarpVoice::~WarpVoice() = default;

bool WarpVoice::carriesOn(const ClipRender& clip, int64_t position) const noexcept {
    const State& st = *state_;
    const double playing = static_cast<double>(st.inputPos) - st.lead;  // where the output is now
    // A tempo change rescales timeline positions but keeps the source position
    // (to within a frame or two), so it carries on without a re-seek.
    const double tolerance = 2.0 + 2.0 * clip.rate;
    return st.primed && std::abs(clip.sourceAt(position) - playing) <= tolerance;
}

void WarpVoice::render(const ClipRender& clip, int64_t position, int frames, bool continuing, float* outL,
                       float* outR) noexcept {
    State& st = *state_;
    auto& stretch = st.stretch;

    if (!st.primed || clip.transpose != st.transpose || clip.preserveFormants != st.formants) {
        stretch.setTransposeSemitones(clip.transpose);
        stretch.setFormantFactor(1.f, clip.preserveFormants);  // compensatePitch: keep the formants in place
        st.transpose = clip.transpose;
        st.formants = clip.preserveFormants;
    }

    // Input runs ahead of the output by the stretcher's latency: inputLatency()
    // source frames, plus outputLatency() timeline frames' worth at this rate.
    const double rate = clip.rate;
    const double lead = stretch.inputLatency() + rate * stretch.outputLatency();
    if (!continuing || !carriesOn(clip, position)) {
        // outputSeek() pre-computes the pre-roll so the very next output frame is
        // source frame `from`: sample-accurate starts, locates and loop jumps.
        const auto from = static_cast<int64_t>(std::floor(clip.sourceAt(position)));
        const int seekLength = stretch.outputSeekLength(static_cast<float>(rate));
        SourceInput seekInput{clip.source.get(), from};
        stretch.outputSeek(seekInput, seekLength);
        st.inputPos = from + seekLength;
        st.primed = true;
    }
    st.lead = lead;

    // Feed exactly enough input to reach the source position the block should
    // end on; rounding never accumulates because the target is absolute.
    const int64_t target = std::llround(clip.sourceAt(position + frames) + lead);
    const auto count = static_cast<int>(std::max<int64_t>(0, target - st.inputPos));
    SourceInput input{clip.source.get(), st.inputPos};
    float* outputs[2] = {outL, outR};
    stretch.process(input, count, outputs, frames);
    st.inputPos += count;
}

// ---------------------------------------------------------------------------
// Re-Pitch

void renderResampled(const ClipRender& clip, int64_t position, int frames, float* outL, float* outR) noexcept {
    const AudioSource& source = *clip.source;
    const float* srcL = source.stereoChannel(0);
    const float* srcR = source.stereoChannel(1);
    const int64_t total = source.frames();

    // Speeding up moves content above the output Nyquist: lower the cutoff (and
    // widen the kernel) by the rate so it is filtered out instead of aliasing.
    const double cutoff = 1.0 / std::clamp(clip.rate, 1.0, kMaxRateFiltered);
    const double reach = kHalfWidth / cutoff;
    const auto gainScale = static_cast<float>(cutoff);
    const bool phased = cutoff == 1.0;  // at most the source's speed: the phase table

    for (int i = 0; i < frames; ++i) {
        const double pos = clip.sourceAt(position + i);
        const double whole = std::floor(pos);
        const auto lowest = static_cast<int64_t>(whole) - (kHalfWidth - 1);  // the phase table's first tap
        if (phased && lowest >= 0 && lowest + SincPhases::kTaps <= total) {
            const double scaled = (pos - whole) * kResolution;
            const auto q = static_cast<size_t>(scaled);  // below kResolution: the fraction is below 1
            const auto frac = static_cast<float>(scaled - static_cast<double>(q));
            phasedFrame(kSincPhases.rows[q].data(), kSincPhases.rows[q + 1].data(), frac, srcL + lowest,
                        srcR + lowest, outL[i], outR[i]);
            continue;
        }
        const auto first = std::max<int64_t>(0, static_cast<int64_t>(std::floor(pos - reach)) + 1);
        const auto last = std::min<int64_t>(total - 1, static_cast<int64_t>(std::floor(pos + reach)));
        float l = 0.f, r = 0.f;
        if (first <= last) kernelFrame(srcL, srcR, pos, first, last, cutoff, l, r);
        outL[i] = l * gainScale;
        outR[i] = r * gainScale;
    }
}

}  // namespace sub
