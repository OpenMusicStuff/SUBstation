#include "builtin/SwiftF0.h"

// SwiftF0 run a frame at a time (see SwiftF0.h). Its weights are SwiftF0Data.h, written from the model.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <kfr/dft.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "builtin/SwiftF0Data.h"
#include "rt/FastMath.h"

namespace sub::swiftf0 {
namespace {

constexpr int kBands = 128;
constexpr int kClasses = 95;
constexpr int kHeadBands = 96;  // the pitch head reads the lowest 96 bands
constexpr int kLongest = 2048;
constexpr std::array<int, 3> kSizes = {1024, 2048, 512};  // the spectra in the network's channel order
constexpr float kFloor = 1e-8f;
constexpr float kSilencePeak = 1e-3f;
constexpr double kPi = std::numbers::pi;

// A real FFT (KFR's, its N / 2 + 1 bins).
class RealDft {
public:
    void resize(size_t size) {
        plan_ = std::make_unique<kfr::dft_plan_real<float>>(size, kfr::dft_pack_format::CCs);
        temp_.assign(plan_->temp_size, 0);
    }
    void forward(const float* time, std::complex<float>* bins) noexcept {
        plan_->execute(reinterpret_cast<kfr::complex<float>*>(bins), time, temp_.data());
    }

private:
    std::unique_ptr<kfr::dft_plan_real<float>> plan_;
    std::vector<kfr::u8> temp_;
};

// One of the network's convolutions, run a column at a time: an input column in (time s), an output column out
// for time s - (timeTaps - 1) / 2 once it can be made (the columns before the start are zeros).
class Layer {
public:
    void prepare(const Conv& conv, int heightIn) {
        c_ = &conv;
        hIn_ = heightIn;
        hOut_ = heightIn + conv.padLow + conv.padHigh - conv.freqDilation * (conv.freqTaps - 1);
        history_.assign(static_cast<size_t>(conv.timeTaps) * static_cast<size_t>(conv.in * hIn_), 0.f);
        reset();
    }
    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.f);
        pushed_ = 0;
    }
    int heightOut() const noexcept { return hOut_; }
    int channelsOut() const noexcept { return c_->out; }
    size_t sizeOut() const noexcept { return static_cast<size_t>(c_->out) * static_cast<size_t>(hOut_); }

    // `in`: in x heightIn values (channel-major). Returns true when `out` (out x heightOut) was made.
    bool push(const float* in, float* out) noexcept {
        const int taps = c_->timeTaps;
        const size_t column = static_cast<size_t>(c_->in) * static_cast<size_t>(hIn_);
        // history_: the last `taps` columns, oldest first
        std::copy(history_.begin() + static_cast<std::ptrdiff_t>(column), history_.end(), history_.begin());
        std::copy(in, in + column, history_.end() - static_cast<std::ptrdiff_t>(column));
        ++pushed_;
        const int64_t lag = (taps - 1) / 2;
        if (pushed_ <= lag) return false;  // its output's time is before the start
        compute(out);
        return true;
    }

private:
    void compute(float* out) const noexcept {
        const Conv& c = *c_;
        const size_t column = static_cast<size_t>(c.in) * static_cast<size_t>(hIn_);
        for (int o = 0; o < c.out; ++o) {
            float* y = out + static_cast<size_t>(o) * static_cast<size_t>(hOut_);
            std::fill(y, y + hOut_, c.bias[o]);
            for (int ch = 0; ch < c.in; ++ch) {
                for (int i = 0; i < c.freqTaps; ++i) {
                    // output row r reads input row r + i * dilation - padLow
                    const int shift = i * c.freqDilation - c.padLow;
                    const int lo = std::max(0, -shift), hi = std::min(hOut_, hIn_ - shift);
                    if (lo >= hi) continue;
                    for (int j = 0; j < c.timeTaps; ++j) {
                        const float w = c.weights[((static_cast<size_t>(o) * static_cast<size_t>(c.in) + static_cast<size_t>(ch)) *
                                                       static_cast<size_t>(c.freqTaps) + static_cast<size_t>(i)) *
                                                          static_cast<size_t>(c.timeTaps) + static_cast<size_t>(j)];
                        const float* x = history_.data() + static_cast<size_t>(j) * column + static_cast<size_t>(ch) * static_cast<size_t>(hIn_) + shift;
                        for (int r = lo; r < hi; ++r) y[r] += w * x[r];
                    }
                }
            }
        }
    }

    const Conv* c_ = nullptr;
    int hIn_ = 0, hOut_ = 0;
    std::vector<float> history_;
    int64_t pushed_ = 0;
};

// A column held back `delay` columns (to meet another path's later output).
class Delay {
public:
    void prepare(size_t size, int delay) {
        size_ = size;
        delay_ = delay;
        ring_.assign(size * static_cast<size_t>(delay + 1), 0.f);
        reset();
    }
    void reset() noexcept {
        std::fill(ring_.begin(), ring_.end(), 0.f);
        at_ = 0;
    }
    // Stores `in` and returns the column pushed `delay` pushes ago.
    const float* push(const float* in) noexcept {
        std::copy(in, in + size_, ring_.data() + at_ * size_);
        const size_t slots = static_cast<size_t>(delay_ + 1);
        const size_t old = (at_ + 1) % slots;
        at_ = (at_ + 1) % slots;
        return ring_.data() + old * size_;
    }

private:
    std::vector<float> ring_;
    size_t size_ = 0, at_ = 0;
    int delay_ = 0;
};

}  // namespace

// --- The network, a column at a time ---------------------------------------------------------------------

class Network {
public:
    void prepare() {
        const Conv* c = kConvs;
        layers_[0].prepare(c[0], kBands);
        for (int k = 1; k <= 12; ++k) layers_[static_cast<size_t>(k)].prepare(c[k], kBands);
        layers_[13].prepare(c[13], kHeadBands);
        layers_[14].prepare(c[14], layers_[13].heightOut());
        layers_[15].prepare(c[15], layers_[13].heightOut());
        layers_[16].prepare(c[16], 1);
        layers_[17].prepare(c[17], 1);
        const size_t column = 12 * kBands;
        for (auto& b : bufs_) b.assign(column, 0.f);
        residual_[0].prepare(column, 2);  // the first pair's convolutions see a frame either side: 2 late
        residual_[1].prepare(column, 2);  // the last pair's
        head_.assign(layers_[13].sizeOut(), 0.f);
        headIn_.assign(12 * kHeadBands, 0.f);
        top_.assign(layers_[14].sizeOut(), 0.f);
        logits_.assign(kClasses, 0.f);
        logitsLate_.prepare(kClasses, 1);  // the confidence is a frame later than the pitch
        reset();
    }
    void reset() noexcept {
        for (auto& l : layers_) l.reset();
        for (auto& r : residual_) r.reset();
        logitsLate_.reset();
        means_.fill(0.f);
        pushed_ = 0;
    }

    // A column of spectra in (3 x 128, time t). True when frame t - 7 is out: its class scores and confidence logit.
    bool push(const float* spectra, const float*& logits, float& confidenceLogit) noexcept {
        float mean = 0.f;
        for (int i = 0; i < 3 * kBands; ++i) mean += spectra[i];
        mean /= 3 * kBands;
        // the input's mean, for the confidence head: that of frame t - 6
        means_[static_cast<size_t>(pushed_ % 7)] = mean;
        const float meanLate = means_[static_cast<size_t>((pushed_ + 1) % 7)];
        ++pushed_;

        float* a = bufs_[0].data();
        float* b = bufs_[1].data();
        float* h = bufs_[2].data();
        if (!layers_[0].push(spectra, h)) return false;  // t - 1
        relu(h);
        // the residual pairs: h = relu(h + conv(relu(conv(h))))
        for (int pair = 0; pair < 6; ++pair) {
            Layer& first = layers_[static_cast<size_t>(1 + 2 * pair)];
            Layer& second = layers_[static_cast<size_t>(2 + 2 * pair)];
            // the first and last pairs see a frame either side twice: what skips them waits two frames
            const float* skip = h;
            if (pair == 0 || pair == 5) skip = residual_[pair == 0 ? 0 : 1].push(h);
            if (!first.push(h, a)) return false;
            relu(a);
            if (!second.push(a, b)) return false;
            for (size_t i = 0; i < bufs_[2].size(); ++i) h[i] = std::max(0.f, skip[i] + b[i]);
        }
        // h: frame t - 5. The heads read its lowest 96 bands.
        for (int ch = 0; ch < 12; ++ch)
            std::copy(h + ch * kBands, h + ch * kBands + kHeadBands, headIn_.data() + ch * kHeadBands);
        if (!layers_[13].push(headIn_.data(), head_.data())) return false;  // t - 6
        relu(head_);
        layers_[14].push(head_.data(), top_.data());
        layers_[15].push(head_.data(), logits_.data());
        const int rows = layers_[14].heightOut();
        float summary[5];
        for (int ch = 0; ch < 4; ++ch) {
            const float* row = top_.data() + ch * rows;
            summary[ch] = *std::max_element(row, row + rows);
        }
        summary[4] = meanLate;
        float hidden[16];
        const float* pitchLate = logitsLate_.push(logits_.data());
        if (!layers_[16].push(summary, hidden)) return false;  // t - 7
        for (float& v : hidden) v = std::max(0.f, v);
        float out = 0.f;
        layers_[17].push(hidden, &out);
        logits = pitchLate;
        confidenceLogit = out;
        return true;
    }

private:
    static void relu(float* x, size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) x[i] = x[i] > 0.f ? x[i] : 0.f;
    }
    static void relu(std::vector<float>& x) noexcept { relu(x.data(), x.size()); }
    static void relu(float* x) noexcept { relu(x, 12 * kBands); }

    std::array<Layer, 18> layers_;
    std::array<std::vector<float>, 3> bufs_;
    std::array<Delay, 2> residual_;
    Delay logitsLate_;
    std::array<float, 7> means_{};  // the last seven frames' means
    int64_t pushed_ = 0;
    std::vector<float> head_, headIn_, top_, logits_;
};

// The decoded frame from the class scores and the confidence logit (fmin and fmax: the whole range).
inline void decode(const float* logits, float confidenceLogit, double& pitch, double& confidence) noexcept {
    const int best = static_cast<int>(std::max_element(logits, logits + kClasses) - logits);
    double scores[3], weights[3], sum = 0.0;
    double top = -INFINITY;
    for (int k = -1; k <= 1; ++k) {
        const int c = best + k;
        scores[k + 1] = (c < 0 || c >= kClasses) ? -INFINITY : static_cast<double>(logits[c]);
        top = std::max(top, scores[k + 1]);
    }
    for (int k = 0; k < 3; ++k) {
        weights[k] = std::isinf(scores[k]) ? 0.0 : std::exp(scores[k] - top);
        sum += weights[k];
    }
    double logHz = 0.0;
    for (int k = -1; k <= 1; ++k) {
        const int c = std::clamp(best + k, 0, kClasses - 1);
        logHz += weights[k + 1] / sum * static_cast<double>(kClassLogHz[c]);
    }
    pitch = std::exp(logHz);
    confidence = 1.0 / (1.0 + std::exp(-static_cast<double>(confidenceLogit)));
}

// --- The spectra --------------------------------------------------------------------------------------------

class Spectra {
public:
    void prepare() {
        for (size_t s = 0; s < kSizes.size(); ++s) {
            const int n = kSizes[s];
            dft_[s].resize(static_cast<size_t>(n));
            window_[s].resize(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) window_[s][static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / n));
        }
        time_.assign(kLongest, 0.f);
        bins_.assign(kLongest / 2 + 1, {});
        magnitude_.assign(kLongest / 2 + 1, 0.f);
    }
    // The column for a frame: `audio` holds the 2048 samples centred on it (the shorter frames' in its middle).
    void column(const float* audio, float* out) noexcept {
        const FilterTap* taps[3] = {kFilter1024, kFilter2048, kFilter512};
        const size_t counts[3] = {std::size(kFilter1024), std::size(kFilter2048), std::size(kFilter512)};
        for (size_t s = 0; s < kSizes.size(); ++s) {
            const int n = kSizes[s];
            const float* from = audio + (kLongest - n) / 2;
            for (int i = 0; i < n; ++i) time_[static_cast<size_t>(i)] = from[i] * window_[s][static_cast<size_t>(i)];
            dft_[s].forward(time_.data(), bins_.data());
            for (int k = 0; k <= n / 2; ++k) magnitude_[static_cast<size_t>(k)] = std::abs(bins_[static_cast<size_t>(k)]);
            float* band = out + s * kBands;
            std::fill(band, band + kBands, 0.f);
            for (size_t t = 0; t < counts[s]; ++t) band[taps[s][t].band] += taps[s][t].weight * magnitude_[taps[s][t].bin];
            for (int k = 0; k < kBands; ++k) band[k] = fastmath::log2(band[k] + kFloor) * 0.6931471805599453f;
        }
    }

private:
    std::array<RealDft, 3> dft_;
    std::array<std::vector<float>, 3> window_;
    std::vector<float> time_, magnitude_;
    std::vector<std::complex<float>> bins_;
};

// --- Detector -------------------------------------------------------------------------------------------------

struct Detector::Impl {
    static constexpr int kZeros = 16;          // the resampler's zero crossings either side
    static constexpr int kTableRes = 512;      // its kernel's table points per zero crossing
    static constexpr double kCutoff = 0.94;    // of 8 kHz

    double hostRate = 48000.0, step = 3.0, scale = 3.0;
    std::vector<float> kernel;  // h(u) for u = i / kTableRes, u in [0, kZeros]
    std::vector<float> host;    // host samples (a ring)
    size_t hostMask = 0;
    int64_t hostCount = 0, nextOut = 0;  // host samples in; the next 16 kHz sample to make
    std::vector<float> audio;   // 16 kHz samples (a ring)
    size_t audioMask = 0;
    int64_t audioCount = 0;
    std::vector<float> peaks;   // each hop's peak (a ring of 64)
    std::vector<float> frameAudio, spectra;
    Spectra front;
    Network net;
    Frame last;

    void prepare(double rate) {
        hostRate = rate;
        step = rate / kRate;
        scale = std::max(1.0, step);
        kernel.assign(static_cast<size_t>(kZeros * kTableRes) + 2, 0.f);
        const double beta = 8.6, norm = besselI0(beta);
        for (int i = 0; i <= kZeros * kTableRes; ++i) {
            const double u = static_cast<double>(i) / kTableRes, r = u / kZeros;
            const double x = kPi * kCutoff * u;
            const double sinc = u == 0.0 ? 1.0 : std::sin(x) / x;
            kernel[static_cast<size_t>(i)] = static_cast<float>(kCutoff * sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / norm);
        }
        size_t size = 1;
        while (size < static_cast<size_t>(2 * kZeros * scale + 8)) size <<= 1;
        host.assign(size, 0.f);
        hostMask = size - 1;
        audio.assign(4096, 0.f);
        audioMask = audio.size() - 1;
        peaks.assign(64, 0.f);
        frameAudio.assign(kLongest, 0.f);
        spectra.assign(3 * kBands, 0.f);
        front.prepare();
        net.prepare();
        reset();
    }

    void reset() noexcept {
        std::fill(host.begin(), host.end(), 0.f);
        std::fill(audio.begin(), audio.end(), 0.f);
        std::fill(peaks.begin(), peaks.end(), 0.f);
        hostCount = nextOut = audioCount = 0;
        net.reset();
        last = {};
    }

    static double besselI0(double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64 && term > 1e-17 * sum; ++k) {
            const double h = x / (2.0 * k);
            term *= h * h;
            sum += term;
        }
        return sum;
    }

    float kernelAt(double u) const noexcept {
        const double p = std::abs(u) * kTableRes;
        if (!(p < kZeros * kTableRes)) return 0.f;
        const auto i = static_cast<size_t>(p);
        const auto f = static_cast<float>(p - static_cast<double>(i));
        return kernel[i] + (kernel[i + 1] - kernel[i]) * f;
    }

    bool push(float x) noexcept {
        host[static_cast<size_t>(hostCount) & hostMask] = x;
        ++hostCount;
        bool out = false;
        // 16 kHz samples whose kernel the host samples now cover
        for (;;) {
            const double centre = static_cast<double>(nextOut) * step;
            const double reach = kZeros * scale;
            const auto lastIn = static_cast<int64_t>(std::floor(centre + reach));
            if (lastIn >= hostCount) break;
            const auto firstIn = static_cast<int64_t>(std::ceil(centre - reach));
            double sum = 0.0;
            for (int64_t n = firstIn; n <= lastIn; ++n)
                if (n >= 0) sum += kernelAt((static_cast<double>(n) - centre) / scale) * host[static_cast<size_t>(n) & hostMask];
            ++nextOut;
            out = push16k(static_cast<float>(sum / scale)) || out;
        }
        return out;
    }

    // A 16 kHz sample in; true when a frame became final.
    bool push16k(float y) noexcept {
        audio[static_cast<size_t>(audioCount) & audioMask] = y;
        float& peak = peaks[static_cast<size_t>(audioCount / kHop) & 63];
        peak = (audioCount % kHop == 0) ? std::abs(y) : std::max(peak, std::abs(y));
        ++audioCount;
        // frame t's spectra once its 2048 samples are in: [t hop - 1024, t hop + 1024)
        const int64_t end = audioCount - kLongest / 2;
        if (end < 0 || end % kHop != 0) return false;
        const int64_t t = end / kHop;
        const int64_t from = t * kHop - kLongest / 2;
        for (int i = 0; i < kLongest; ++i) {
            const int64_t n = from + i;
            frameAudio[static_cast<size_t>(i)] = n < 0 ? 0.f : audio[static_cast<size_t>(n) & audioMask];
        }
        front.column(frameAudio.data(), spectra.data());
        const float* logits = nullptr;
        float confidenceLogit = 0.f;
        if (!net.push(spectra.data(), logits, confidenceLogit)) return false;
        const int64_t f = t - 7;
        last.index = f;
        decode(logits, confidenceLogit, last.pitch, last.confidence);
        if (peaks[static_cast<size_t>(f) & 63] < kSilencePeak) last.confidence = 0.0;
        return true;
    }
};

Detector::Detector() : impl_(std::make_unique<Impl>()) {}
Detector::~Detector() = default;
void Detector::prepare(double hostRate) { impl_->prepare(hostRate); }
void Detector::reset() noexcept { impl_->reset(); }
bool Detector::push(float x) noexcept { return impl_->push(x); }
const Frame& Detector::frame() const noexcept { return impl_->last; }
double Detector::hostHop() const noexcept { return kHop * impl_->step; }
int Detector::delay() const noexcept {
    // final once 16 kHz sample (centre + kLookahead) is made, which needs host samples to its kernel's end
    return static_cast<int>(std::ceil((kLookahead + Impl::kZeros * std::max(1.0, 1.0 / impl_->step)) * impl_->step)) + 2;
}

Analysis analyse16k(const float* audio, int64_t samples) {
    // swift-f0 pads the audio to whole hops (at least one), and each convolution's input with zeros past the
    // last frame; here the network is fed columns of zeros past it instead, so the last 7 frames can differ a
    // little from its batch analysis (the first ones and all the rest are the same: zeros before the start)
    const int64_t frames = std::max<int64_t>(1, samples / kHop);
    Spectra front;
    front.prepare();
    Network net;
    net.prepare();
    std::vector<float> padded(static_cast<size_t>((frames + 8) * kHop + kLongest), 0.f);
    std::copy(audio, audio + samples, padded.begin() + kLongest / 2);
    Analysis out;
    out.pitch = std::make_unique<double[]>(static_cast<size_t>(frames));
    out.confidence = std::make_unique<double[]>(static_cast<size_t>(frames));
    out.frames = frames;
    std::vector<float> column(3 * kBands);
    const std::vector<float> zeros(3 * kBands, 0.f);
    for (int64_t t = 0; t < frames + 7; ++t) {
        const float* in = zeros.data();
        if (t < frames) {
            front.column(padded.data() + t * kHop, column.data());
            in = column.data();
        }
        const float* logits = nullptr;
        float logit = 0.f;
        if (!net.push(in, logits, logit)) continue;
        const int64_t f = t - 7;
        if (f < 0 || f >= frames) continue;
        decode(logits, logit, out.pitch[static_cast<size_t>(f)], out.confidence[static_cast<size_t>(f)]);
        float peak = 0.f;
        for (int64_t n = f * kHop; n < std::min<int64_t>(samples, (f + 1) * kHop); ++n) peak = std::max(peak, std::abs(audio[n]));
        if (peak < kSilencePeak) out.confidence[static_cast<size_t>(f)] = 0.0;
    }
    return out;
}

}  // namespace sub::swiftf0
