// Built-in "Prisma" device: a spectral pitch quantizer for basses and other
// harmonic sounds. It keeps the parts of the spectrum that sit on the notes of
// a chosen scale (or the MIDI notes it is played), moves the notes a semitone
// off them onto them, takes out the other notes and lets what lies between
// notes through, softer: a growl's partials pulled into key, its texture left.
//
// - Bands: the input is split in halves of its rate (two-path polyphase IIR
//   half-band filters, as a perfect-reconstruction QMF: the upper half passes
//   untouched) down to a core of 22.05-24 kHz, where Linkwitz-Riley crossovers
//   (24 dB/octave) at Start and End make a low, a mid and a high band. Only
//   the mid band is processed; the others pass (or are muted).
// - The mid band goes through three filters at once: the notes kept, and the
//   notes a semitone below and above enabled ones, which two pitch shifters
//   then move up and down onto them. Each filter is minimum-phase (no latency
//   of its own, no pre-ringing), made from a magnitude mask over the pitch of
//   each frequency: an enabled note's gain (measured per set of notes:
//   PrismaData.h) falling off over a semitone or two, exact zeros on the notes
//   taken out, and between notes a floor that depends on how many notes are
//   enabled. The masks are designed in steps across audio callbacks whenever
//   the notes change (a cepstrum of 16384 points, then 32 partitions), and the
//   new filters fade in over one block.
// - The convolution is uniformly partitioned: blocks of 256 core samples (the
//   latency, with the half-band filters' delay; the other bands wait with it).
// - The shifters: two read heads per shifter on a delay line, crossfaded with
//   Hann windows over 220 ms grains born every 110 ms, their delay sweeping
//   at 1 - ratio (a semitone, or Pre-Shift's cents ahead of the notes).
// - Color fades from the plain mid band (0 %) to the processed one (100 %);
//   above, the notes get louder and narrower and the floor between them lower.
//   Wet Gain scales the processed band; Post-Shift tunes the note grid.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/PrismaData.h"
#include "rt/FastMath.h"
#include "rt/RtUtils.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "signalsmith-linear/fft.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace sub {
namespace {

using Complex = std::complex<float>;
using prisma::kKernelPoints;
using prisma::kNoteGainCentiDb;
using prisma::kNoteKernel;

constexpr int kChannels = 2;
constexpr int kBlock = 256;           // convolution block (core samples)
constexpr int kDesignSize = 16384;    // the masks' frequency grid
constexpr int kTaps = kDesignSize / 2;
constexpr int kParts = kTaps / kBlock;
constexpr int kFilters = 3;           // kept, from below (moved up), from above (moved down)
constexpr float kGrainSeconds = 0.11f;
constexpr float kMinDelay = 3.f;      // core samples a head reads behind at the least
constexpr float kMaskFloor = 1e-5f;   // -100 dB: where a mask is zero (the log needs a floor)
constexpr float kMinStart = 160.f;    // Start and End work within these (as the reference's do), whatever
constexpr float kMaxEnd = 4000.f;     // they are set to

// Half-band all-pass coefficients (polyphase, four: a transition 0.1075 of the rate wide), chosen so the
// bands' delay follows the reference's across the spectrum (its 34 samples at a quarter of the rate).
constexpr std::array<double, 4> kHalfband = {0.0761664965, 0.2733758376, 0.5325590391, 0.8277213729};

// A first-order all-pass in the decimated domain: (c + z^-1) / (1 + c z^-1).
struct Allpass {
    double c = 0, x1 = 0, y1 = 0;
    double operator()(double x) noexcept {
        const double y = c * (x - y1) + x1;
        x1 = x;
        y1 = y;
        return y;
    }
};

struct AllpassChain {
    std::array<Allpass, kHalfband.size() / 2> stages;
    explicit AllpassChain(int parity = 0) {
        for (size_t i = 0; i < stages.size(); ++i) stages[i].c = kHalfband[2 * i + static_cast<size_t>(parity)];
    }
    double operator()(double x) noexcept {
        for (auto& s : stages) x = s(x);
        return x;
    }
    void clear() noexcept {
        for (auto& s : stages) s.x1 = s.y1 = 0;
    }
};

// One split in half of the rate: two input samples give a low and a high half-rate sample, and back.
struct Qmf {
    AllpassChain a0{0}, a1{1}, s0{0}, s1{1};
    double oddDelay = 0;  // x[2n-1]
    void analyse(double even, double odd, double& low, double& high) noexcept {
        const double p0 = a0(even), p1 = a1(oddDelay);
        oddDelay = odd;
        low = 0.5 * (p0 + p1);
        high = 0.5 * (p0 - p1);
    }
    void synthesise(double low, double high, double& even, double& odd) noexcept {
        even = s0(low - high);
        odd = s1(low + high);
    }
    void clear() noexcept {
        a0.clear(); a1.clear(); s0.clear(); s1.clear();
        oddDelay = 0;
    }
    // Its delay at low frequencies, in full-rate samples: one, and each all-pass's twice.
    static double delay() noexcept {
        double d = 1;
        for (const double c : kHalfband) d += 2 * (1 - c) / (1 + c);
        return d;
    }
};

// A biquad (transposed direct form II), and Linkwitz-Riley 4 as two of them.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double operator()(double x) noexcept {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
struct Lr4 {
    Biquad a, b;
    void set(double fc, double rate, bool high) noexcept {
        const double w = std::tan(std::numbers::pi * std::min(fc, 0.45 * rate) / rate);
        const double q = std::numbers::sqrt2 / 2, n = 1 / (1 + w / q + w * w);
        Biquad c;
        if (high) {
            c.b0 = n; c.b1 = -2 * n; c.b2 = n;
        } else {
            c.b0 = w * w * n; c.b1 = 2 * c.b0; c.b2 = c.b0;
        }
        c.a1 = 2 * (w * w - 1) * n;
        c.a2 = (1 - w / q + w * w) * n;
        for (Biquad* f : {&a, &b}) {
            f->b0 = c.b0; f->b1 = c.b1; f->b2 = c.b2; f->a1 = c.a1; f->a2 = c.a2;
        }
    }
    double operator()(double x) noexcept { return b(a(x)); }
    void clear() noexcept { a.z1 = a.z2 = b.z1 = b.z2 = 0; }
};

// Two read heads on a delay line: a pitch shift by `ratio`. The heads crossfade with Hann windows (their
// gains summing to one) or, for the notes moved a semitone, sine windows (their powers summing to one).
class Shifter {
public:
    void prepare(double rate, bool constantPower = false) {
        constantPower_ = constantPower;
        period_ = std::max(16, static_cast<int>(std::lround(kGrainSeconds * rate)));
        // The longest sweep: Pre-Shift's 50 cents or a semitone's (6 %) over two periods.
        size_t size = 1;
        while (size < static_cast<size_t>(0.07 * 2 * period_ + kMinDelay + 8)) size <<= 1;
        buffer_.assign(size, 0.f);
        mask_ = size - 1;
        clear();
    }
    void clear() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.f);
        write_ = 0;
        age_ = 0;
    }
    float process(float x, double ratio) noexcept {
        buffer_[write_] = x;
        const double slope = 1.0 - ratio;
        const double twoP = 2.0 * period_;
        double out = 0;
        for (int head = 0; head < 2; ++head) {
            double age = age_ + head * period_;
            if (age >= twoP) age -= twoP;
            const double s = std::sin(std::numbers::pi * age / twoP);
            const double gain = constantPower_ ? s : s * s;
            const double delay = slope > 0 ? kMinDelay + slope * age : kMinDelay + (-slope) * (twoP - age);
            out += gain * read(delay);
        }
        write_ = (write_ + 1) & mask_;
        if (++age_ >= 2 * period_) age_ = 0;
        return static_cast<float>(out);
    }

private:
    double read(double delay) const noexcept {
        const double pos = static_cast<double>(write_) - delay;
        const double fl = std::floor(pos);
        const auto i = static_cast<size_t>(static_cast<int64_t>(fl)) & mask_;
        const double f = pos - fl;
        return buffer_[i] * (1 - f) + buffer_[(i + 1) & mask_] * f;
    }
    std::vector<float> buffer_;
    size_t mask_ = 0, write_ = 0;
    int period_ = 2640, age_ = 0;
    bool constantPower_ = false;
};

// What the filters are designed for: they are designed again when it changes.
struct DesignKey {
    int notes = 0;            // bit per pitch class (C = bit 0)
    int postCents = 0;        // tenths of a cent
    int colorExponent = 100;  // percent, above 100 only
    int start = 240, end = 2400;  // Hz: where the kept mask goes flat
    bool operator==(const DesignKey&) const = default;
};

// A filter in the frequency domain: its partitions' spectra (packed: DC and Nyquist in [0]).
struct Partitions {
    std::vector<Complex> spectra = std::vector<Complex>(static_cast<size_t>(kParts * kBlock));
    int used = 0;  // partitions with anything in them; 0: the filter is silent
};

// Pack-aware multiply-accumulate of one partition.
inline void multiplyAdd(const Complex* x, const Complex* h, Complex* y) noexcept {
    y[0] += Complex(x[0].real() * h[0].real(), x[0].imag() * h[0].imag());
    for (int k = 1; k < kBlock; ++k) y[k] += x[k] * h[k];
}

class PrismaProcessor final : public BuiltinProcessor {
public:
    enum Param {
        Start = 0, End, Color, WetGain, PreShift, PostShift, MuteLow, MuteHigh, Midi,
        NoteC, NumParams = NoteC + 12
    };

    PrismaProcessor() : BuiltinProcessor(infos()) {}

    std::string typeId() const override { return "builtin:prisma"; }
    std::string name() const override { return "Prisma"; }
    bool acceptsMidi() const override { return true; }
    int latencySamples() const override { return latency_; }
    int tailSamples() const override { return latency_ + kTaps * factor_; }

    void prepare(double sampleRate, int) override {
        rate_ = sampleRate;
        stages_ = 0;
        double core = sampleRate;
        while (core >= 40000.0 && stages_ < 3) {
            core *= 0.5;
            ++stages_;
        }
        coreRate_ = core;
        factor_ = 1 << stages_;
        double qmf = 0;
        for (int s = 0; s < stages_; ++s) qmf += Qmf::delay() * (1 << s);
        // the block, the half-band filters' delay, and a frame of factor_ host samples gathered before it goes down
        latency_ = kBlock * factor_ + static_cast<int>(std::lround(qmf)) + (factor_ > 1 ? factor_ : 0);

        fft_.resize(2 * kBlock);
        designFft_.resize(kDesignSize);
        for (auto& c : ch_) {
            c.history.assign(static_cast<size_t>(kParts * kBlock), Complex{});
            for (int s = 0; s < 3; ++s) {
                // a stage's upper half waits for the processing beneath it: the block, at its rate
                // (and as the half-band filters of the stages beneath delay it, rounded)
                const int shift = s < stages_ ? stages_ - 1 - s : 0;
                double deeper = 0;
                for (int d = s + 1; d < stages_; ++d) deeper += Qmf::delay() * (1 << (d - s - 1));
                c.highDelay[static_cast<size_t>(s)].assign((static_cast<size_t>(kBlock) << shift) +
                                                               static_cast<size_t>(std::lround(deeper)),
                                                           0.0);
            }
            c.pre.prepare(coreRate_);
            c.up.prepare(coreRate_);
            c.down.prepare(coreRate_);
        }
        for (auto& set : filters_)
            for (auto& f : set) f.spectra.assign(static_cast<size_t>(kParts * kBlock), Complex{});
        mag_.assign(static_cast<size_t>(kFilters), std::vector<float>(kDesignSize / 2 + 1));
        work_.assign(kDesignSize, 0.f);
        spec_.assign(kDesignSize / 2, Complex{});
        timeBuf_.assign(2 * kBlock, 0.f);
        freqBuf_.assign(kBlock, Complex{});
        accum_.assign(static_cast<size_t>(kFilters) * 2, std::vector<Complex>(kBlock));
        blockOut_.assign(static_cast<size_t>(kFilters) * 2, std::vector<float>(kBlock));

        // The filters for the current settings, now (no audio is running).
        key_ = currentKey();
        designStep_ = 0;
        designing_ = true;
        while (designing_) runDesignStep();
        active_ = pending_;
        fade_ = 0;
        wetNotes_ = key_.notes;
        wetAmount_.reset(coreRate_, 0.02);
        wetAmount_.snapTo(wetTarget());
        reset();
    }

    void reset() override {
        std::fill(std::begin(held_), std::end(held_), 0);
        for (auto& c : ch_) c.clear();
        pos_ = 0;
        frameFill_ = 0;
    }

protected:
    void render(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) override {
        for (size_t e = 0; e < ctx.inEvents.count; ++e) {
            const ProcessEvent& ev = ctx.inEvents.events[e];
            if (ev.startsNote()) ++held_[ev.key() & 127];
            else if (ev.endsNote() && held_[ev.key() & 127] > 0) --held_[ev.key() & 127];
        }
        // The filters follow the settings: a new design is started when they change, one step per call.
        const DesignKey key = currentKey();
        if (!designing_ && !(key == key_) && fade_ == 0) {
            key_ = key;
            designStep_ = 0;
            designing_ = true;
        }
        if (designing_) runDesignStep();

        updateCrossovers();
        const int chs = std::min(numChannels, kChannels);
        if (factor_ == 1) {
            for (int i = 0; i < numFrames; ++i) {
                for (int c = 0; c < chs; ++c) {
                    auto& st = ch_[static_cast<size_t>(c)];
                    channels[c][i] = static_cast<float>(core(st, channels[c][i]));
                }
                advanceCore(chs);
            }
            return;
        }
        // host samples are gathered into frames of factor_, which go down the half-band stages together
        for (int i = 0; i < numFrames; ++i) {
            for (int c = 0; c < chs; ++c) {
                auto& st = ch_[static_cast<size_t>(c)];
                st.frameIn[frameFill_] = channels[c][i];
                channels[c][i] = static_cast<float>(st.frameOut[frameFill_]);
            }
            if (++frameFill_ == factor_) {
                frameFill_ = 0;
                for (int c = 0; c < chs; ++c) {
                    auto& st = ch_[static_cast<size_t>(c)];
                    processFrame(st, st.frameIn, st.frameOut);
                }
                advanceCore(chs);
            }
        }
    }

private:
    struct Channel {
        Qmf qmf[3];
        std::vector<double> highDelay[3];
        size_t highPos[3] = {};
        Lr4 lowLp, midHp, midLp, highHp;
        Shifter pre, up, down;
        std::vector<Complex> history;  // the mid band's last blocks, as spectra
        int historyPos = 0;
        float prevIn[kBlock] = {};
        float inMid[kBlock] = {}, inLow[kBlock] = {}, inHigh[kBlock] = {};
        float outBlock[kBlock] = {};
        double frameIn[8] = {}, frameOut[8] = {};
        void clear() {
            for (auto& q : qmf) q.clear();
            for (int s = 0; s < 3; ++s) {
                std::fill(highDelay[s].begin(), highDelay[s].end(), 0.0);
                highPos[s] = 0;
            }
            lowLp.clear(); midHp.clear(); midLp.clear(); highHp.clear();
            pre.clear(); up.clear(); down.clear();
            std::fill(history.begin(), history.end(), Complex{});
            historyPos = 0;
            std::fill(std::begin(prevIn), std::end(prevIn), 0.f);
            std::fill(std::begin(inMid), std::end(inMid), 0.f);
            std::fill(std::begin(inLow), std::end(inLow), 0.f);
            std::fill(std::begin(inHigh), std::end(inHigh), 0.f);
            std::fill(std::begin(outBlock), std::end(outBlock), 0.f);
            std::fill(std::begin(frameIn), std::end(frameIn), 0.0);
            std::fill(std::begin(frameOut), std::end(frameOut), 0.0);
        }
    };

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            const auto& onOff = offOnLabels();
            std::vector<ParamInfo> list = {
                {"start", "Start", "Hz", 80.f, 1800.f, 240.f, true},
                {"end", "End", "Hz", 400.f, 8000.f, 2400.f, true},
                {"color", "Color", "%", 0.f, 200.f, 100.f},
                {"wet_gain", "Wet Gain", "dB", -6.f, 6.f, 0.f},
                {"pre_shift", "Pre-Shift", "cents", -50.f, 50.f, 0.f},
                {"post_shift", "Post-Shift", "cents", -50.f, 50.f, 0.f},
                {"mute_low", "Mute Low", "", 0.f, 1.f, 0.f, false, onOff},
                {"mute_high", "Mute High", "", 0.f, 1.f, 0.f, false, onOff},
                {"midi", "MIDI Notes", "", 0.f, 1.f, 0.f, false, onOff},
            };
            static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            static const char* const kIds[12] = {"note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f",
                                                 "note_fs", "note_g", "note_gs", "note_a", "note_as", "note_b"};
            for (int c = 0; c < 12; ++c) list.push_back({kIds[c], kNames[c], "", 0.f, 1.f, 0.f, false, onOff});
            return list;
        }();
        return kInfos;
    }

    int enabledNotes() const noexcept {
        int bits = 0;
        if (isOn(Midi)) {
            for (int k = 0; k < 128; ++k)
                if (held_[k] > 0) bits |= 1 << (k % 12);
        } else {
            for (int c = 0; c < 12; ++c)
                if (isOn(NoteC + c)) bits |= 1 << c;
        }
        return bits;
    }
    DesignKey currentKey() const noexcept {
        DesignKey k;
        k.notes = enabledNotes();
        k.postCents = static_cast<int>(std::lround(param(PostShift) * 10));
        k.colorExponent = std::max(100, static_cast<int>(std::lround(param(Color))));
        k.start = static_cast<int>(std::lround(std::max(param(Start), kMinStart)));
        k.end = static_cast<int>(std::lround(std::min(param(End), kMaxEnd)));
        return k;
    }
    // How much of the processed band is heard (0..1): Color up to 100 %, and nothing with no notes or all twelve.
    float wetTarget() const noexcept {
        const int n = wetNotes_;
        if (n == 0 || n == 0xfff) return 0.f;
        return std::clamp(param(Color) / 100.f, 0.f, 1.f);
    }

    void updateCrossovers() noexcept {
        const double start = std::max(param(Start), kMinStart);
        const double end = std::max(static_cast<double>(std::min(param(End), kMaxEnd)), start * 1.01);
        if (start == lastStart_ && end == lastEnd_) return;
        lastStart_ = start;
        lastEnd_ = end;
        for (auto& c : ch_) {
            c.lowLp.set(start, coreRate_, false);
            c.midHp.set(start, coreRate_, true);
            c.midLp.set(end, coreRate_, false);
            c.highHp.set(end, coreRate_, true);
        }
    }

    // After each core sample: a full block is processed, and a finished crossfade hands the new filters over.
    void advanceCore(int chs) noexcept {
        if (++pos_ < kBlock) return;
        pos_ = 0;
        wetAmount_.setTarget(wetTarget());
        for (int k = 0; k < kBlock; ++k) wetRamp_[static_cast<size_t>(k)] = wetAmount_.next();
        for (int c = 0; c < chs; ++c) processBlock(ch_[static_cast<size_t>(c)]);
        if (fade_ > 0 && --fade_ == 0) {
            active_ = 1 - active_;
            wetNotes_ = key_.notes;
        }
    }

    // One host frame group (factor_ samples) down the half-band stages, through the core, and back up.
    void processFrame(Channel& c, const double* in, double* out) noexcept {
        double hi[3][4] = {};
        double cur[8] = {};
        std::copy(in, in + factor_, cur);
        int len = factor_;
        for (int s = 0; s < stages_; ++s) {
            for (int k = 0; k < len / 2; ++k) {
                double low, high;
                c.qmf[s].analyse(cur[2 * k], cur[2 * k + 1], low, high);
                cur[k] = low;
                // the upper half waits as long as the processing beneath it
                auto& d = c.highDelay[s];
                const double delayed = d[c.highPos[s]];
                d[c.highPos[s]] = high;
                c.highPos[s] = (c.highPos[s] + 1) % d.size();
                hi[s][k] = delayed;
            }
            len /= 2;
        }
        cur[0] = core(c, cur[0]);
        for (int s = stages_ - 1; s >= 0; --s) {
            double up[8];
            for (int k = 0; k < len; ++k) c.qmf[s].synthesise(cur[k], hi[s][k], up[2 * k], up[2 * k + 1]);
            len *= 2;
            std::copy(up, up + len, cur);
        }
        std::copy(cur, cur + factor_, out);
    }

    // One core sample: the bands now, what the block before them made out.
    double core(Channel& c, double x) noexcept {
        const double low = c.lowLp(x);
        const double mid = c.midLp(c.midHp(x));
        const double high = c.highHp(x);
        const double pre = param(PreShift);
        const float m = pre != 0 ? c.pre.process(static_cast<float>(mid), std::exp2(pre / 1200.0)) : static_cast<float>(mid);
        c.inLow[pos_] = static_cast<float>(low);
        c.inHigh[pos_] = static_cast<float>(high);
        c.inMid[pos_] = m;
        return c.outBlock[pos_];
    }

    // A block of the mid band: convolved with the three filters (and the outgoing ones while they fade),
    // shifted, mixed with the other bands into the next block's output.
    void processBlock(Channel& c) noexcept {
        // the input spectrum: the last two blocks (overlap-save)
        std::copy(std::begin(c.prevIn), std::end(c.prevIn), timeBuf_.begin());
        std::copy(std::begin(c.inMid), std::end(c.inMid), timeBuf_.begin() + kBlock);
        std::copy(std::begin(c.inMid), std::end(c.inMid), std::begin(c.prevIn));
        fft_.fft(timeBuf_.data(), freqBuf_.data());
        c.historyPos = (c.historyPos + kParts - 1) % kParts;
        std::copy(freqBuf_.begin(), freqBuf_.end(), c.history.begin() + c.historyPos * kBlock);

        const bool fading = fade_ > 0;
        const float scale = 1.f / (2 * kBlock);
        for (int set = 0; set < (fading ? 2 : 1); ++set) {
            const int which = set == 0 ? active_ : 1 - active_;
            for (int f = 0; f < kFilters; ++f) {
                const Partitions& p = filters_[static_cast<size_t>(which)][static_cast<size_t>(f)];
                auto& out = blockOut_[static_cast<size_t>(set * kFilters + f)];
                if (p.used == 0) {
                    std::fill(out.begin(), out.end(), 0.f);
                    continue;
                }
                auto& acc = accum_[static_cast<size_t>(set * kFilters + f)];
                std::fill(acc.begin(), acc.end(), Complex{});
                for (int part = 0; part < p.used; ++part) {
                    const Complex* x = c.history.data() + ((c.historyPos + part) % kParts) * kBlock;
                    multiplyAdd(x, p.spectra.data() + part * kBlock, acc.data());
                }
                fft_.ifft(acc.data(), timeBuf_.data());
                for (int k = 0; k < kBlock; ++k) out[static_cast<size_t>(k)] = timeBuf_[static_cast<size_t>(kBlock + k)] * scale;
            }
        }
        const float wetGain = dbToGain(param(WetGain));
        const bool muteLow = isOn(MuteLow), muteHigh = isOn(MuteHigh);
        const double upRatio = std::exp2(1.0 / 12), downRatio = std::exp2(-1.0 / 12);
        for (int k = 0; k < kBlock; ++k) {
            const auto i = static_cast<size_t>(k);
            float kept = blockOut_[0][i], fromBelow = blockOut_[1][i], fromAbove = blockOut_[2][i];
            if (fading) {
                // the outgoing filters (set 0) fade out as the new ones (set 1) fade in
                const float t = (k + 1) / static_cast<float>(kBlock);
                kept = kept * (1 - t) + blockOut_[3][i] * t;
                fromBelow = fromBelow * (1 - t) + blockOut_[4][i] * t;
                fromAbove = fromAbove * (1 - t) + blockOut_[5][i] * t;
            }
            const float wet = kept + c.up.process(fromBelow, upRatio) + c.down.process(fromAbove, downRatio);
            const float a = wetRamp_[i];
            const float midOut = (1 - a) * c.inMid[k] + a * wetGain * wet;
            c.outBlock[k] = (muteLow ? 0.f : c.inLow[k]) + (muteHigh ? 0.f : c.inHigh[k]) + midOut;
        }
    }

    // --- Filter design (in steps on the rendering thread; nothing allocated) -------------------------

    void runDesignStep() noexcept {
        if (designStep_ == 0) {
            computeMasks();
            ++designStep_;
            return;
        }
        const int f = designStep_ - 1;
        designFilter(f, filters_[static_cast<size_t>(1 - active_)][static_cast<size_t>(f)]);
        if (++designStep_ > kFilters) {
            designing_ = false;
            pending_ = 1 - active_;
            fade_ = 1;  // the next block crossfades from the old filters to these
        }
    }

    // The three magnitude masks over the design grid's bins (core rate).
    void computeMasks() noexcept {
        const int bits = key_.notes;
        float v[12] = {};
        int count = 0;
        for (int c = 0; c < 12; ++c) {
            if (bits >> c & 1) {
                v[c] = std::pow(10.f, kNoteGainCentiDb[bits][c] / 2000.f);
                ++count;
            }
        }
        // Color past 100 % sharpens (measured at 125, 150, 175 and 200 %): the notes louder (to +7.1 dB) and
        // narrower (to 2.5 times), the floor between them lower (to -21.3 dB).
        static constexpr float kPeakDb[5] = {0.f, 1.8f, 5.3f, 7.1f, 7.1f};
        static constexpr float kFloorDb[5] = {0.f, -4.3f, -9.7f, -15.4f, -21.3f};
        static constexpr float kNarrow[5] = {1.f, 1.2f, 1.9f, 2.1f, 2.5f};
        const float over = std::clamp(key_.colorExponent / 100.f - 1.f, 0.f, 1.f) * 4.f;
        const int oi = std::min(3, static_cast<int>(over));
        const float of = over - static_cast<float>(oi);
        auto lerp = [&](const float* t) { return t[oi] + (t[oi + 1] - t[oi]) * of; };
        const float peak = std::pow(10.f, lerp(kPeakDb) / 20.f);
        const double narrow = lerp(kNarrow);
        for (float& g : v) g *= peak;
        const float floorGain = std::max(0.f, -0.016f + 0.0507f * static_cast<float>(12 - count)) *
                                std::pow(10.f, lerp(kFloorDb) / 20.f);
        const double post = key_.postCents / 1000.0;  // tenths of a cent -> semitones
        // The notes' comb reaches ten semitones past Start and End; beyond, the kept mask is flat, at a gain
        // that falls 1.89 dB per enabled note (+19.8 dB with one). The crossovers take most of it away, but
        // what is left overlaps the low and high bands, and a minimum-phase filter's phase depends on its
        // magnitude everywhere.
        const double combLow = 12.0 * std::log2(key_.start / 440.0) + 9.0 - 10.0;
        const double combHigh = 12.0 * std::log2(key_.end / 440.0) + 9.0 + 10.0;
        const double outsideDb = 19.8 - 1.89 * (count - 1);
        auto kernel = [](double dist) noexcept {
            const double x = std::abs(dist) * 8.0;
            const int i = static_cast<int>(x);
            if (i >= kKernelPoints - 1) return 0.0;
            const double t = x - i;
            return kNoteKernel[i] * (1 - t) + kNoteKernel[i + 1] * t;
        };
        const int bins = kDesignSize / 2 + 1;
        for (int k = 0; k < bins; ++k) {
            const double f = std::max(20.0, k * coreRate_ / kDesignSize);
            const double p = 12.0 * std::log2(f / 440.0) + 9.0 - post;  // semitones above C
            const double nearest = std::round(p);
            const double d = p - nearest;
            const int cls = static_cast<int>(((static_cast<int64_t>(nearest) % 12) + 12) % 12);
            double sum = 0, cover = 0;
            for (int c = 0; c < 12; ++c) {
                if (!(bits >> c & 1)) continue;
                double dist = std::fmod(p - c, 12.0);
                if (dist < -6) dist += 12;
                if (dist >= 6) dist -= 12;
                const double kv = kernel(dist * narrow);
                sum += v[c] * kv;
                cover += kv;
            }
            const bool on = bits >> cls & 1;
            const double s = std::sin(std::numbers::pi * d);
            const double notch = on ? 1.0 : s * s;
            double kept = notch * (sum + floorGain * std::max(0.0, 1.0 - cover));
            // the notes moved: around each removed note with an enabled neighbour (the lower one if both are),
            // a peak falling exponentially with the distance from it (13.5 dB in 0.3 semitone)
            double fromBelow = 0, fromAbove = 0;
            for (int src = 0; src < 12; ++src) {
                if (bits >> src & 1) continue;
                const bool lower = bits >> ((src + 11) % 12) & 1, upper = bits >> ((src + 1) % 12) & 1;
                if (!lower && !upper) continue;
                double dist = std::fmod(p - src, 12.0);
                if (dist < -6) dist += 12;
                if (dist >= 6) dist -= 12;
                if (std::abs(dist) > 1.5) continue;
                const double shape = 0.85 * std::exp(-std::abs(dist) * narrow / 0.19);
                if (lower) fromAbove += v[(src + 11) % 12] * shape;
                else fromBelow += v[(src + 1) % 12] * shape;
            }
            // across a semitone at each end of the comb, from it to the flat gain (in dB)
            const double past = std::max(combLow - p, p - combHigh);  // semitones outside the comb
            if (past > -0.5) {
                const double t = std::min(1.0, past + 0.5);
                const double outside = std::pow(10.0, outsideDb / 20.0);
                kept = std::exp((1 - t) * std::log(std::max(kept, 1e-5)) + t * std::log(outside));
                fromBelow *= 1 - t;
                fromAbove *= 1 - t;
            }
            mag_[0][static_cast<size_t>(k)] = static_cast<float>(kept);
            mag_[1][static_cast<size_t>(k)] = static_cast<float>(fromBelow);
            mag_[2][static_cast<size_t>(k)] = static_cast<float>(fromAbove);
        }
    }

    // A minimum-phase filter from mask f (real cepstrum), partitioned into `out`.
    void designFilter(int f, Partitions& out) noexcept {
        const auto& mag = mag_[static_cast<size_t>(f)];
        const int half = kDesignSize / 2;
        float peak = 0;
        for (const float m : mag) peak = std::max(peak, m);
        if (peak <= kMaskFloor * 10) {
            out.used = 0;
            return;
        }
        // log magnitude -> cepstrum
        spec_[0] = Complex(std::log(std::max(mag[0], kMaskFloor)), std::log(std::max(mag[static_cast<size_t>(half)], kMaskFloor)));
        for (int k = 1; k < half; ++k) spec_[static_cast<size_t>(k)] = Complex(std::log(std::max(mag[static_cast<size_t>(k)], kMaskFloor)), 0.f);
        designFft_.ifft(spec_.data(), work_.data());
        const float inv = 1.f / kDesignSize;
        // fold onto the causal half
        work_[0] *= inv;
        for (int n = 1; n < half; ++n) work_[static_cast<size_t>(n)] *= 2 * inv;
        work_[static_cast<size_t>(half)] *= inv;
        std::fill(work_.begin() + half + 1, work_.end(), 0.f);
        designFft_.fft(work_.data(), spec_.data());
        // exp -> the minimum-phase spectrum
        spec_[0] = Complex(std::exp(spec_[0].real()), std::exp(spec_[0].imag()));
        for (int k = 1; k < half; ++k) spec_[static_cast<size_t>(k)] = std::exp(spec_[static_cast<size_t>(k)]);
        designFft_.ifft(spec_.data(), work_.data());
        // the impulse response's first kTaps samples, its tail faded
        const int fadeStart = kTaps * 7 / 8;
        int last = 0;
        for (int n = 0; n < kTaps; ++n) {
            float h = work_[static_cast<size_t>(n)] * inv;
            if (n >= fadeStart) {
                const float t = static_cast<float>(n - fadeStart) / (kTaps - fadeStart);
                h *= 0.5f + 0.5f * std::cos(std::numbers::pi_v<float> * t);
            }
            work_[static_cast<size_t>(n)] = h;
            if (std::abs(h) > 1e-7f) last = n;
        }
        out.used = std::min(kParts, last / kBlock + 1);
        for (int part = 0; part < out.used; ++part) {
            std::fill(timeBuf_.begin(), timeBuf_.end(), 0.f);
            std::copy(work_.begin() + part * kBlock, work_.begin() + (part + 1) * kBlock, timeBuf_.begin());
            fft_.fft(timeBuf_.data(), out.spectra.data() + part * kBlock);
        }
    }

    double rate_ = 48000, coreRate_ = 24000;
    int stages_ = 1, factor_ = 2, latency_ = 0;
    std::array<Channel, kChannels> ch_;
    int pos_ = 0;
    int held_[128] = {};
    double lastStart_ = -1, lastEnd_ = -1;

    signalsmith::linear::RealFFT<float> fft_, designFft_;
    std::array<std::array<Partitions, kFilters>, 2> filters_;
    int active_ = 0, pending_ = 0, fade_ = 0;
    DesignKey key_;
    bool designing_ = false;
    int designStep_ = 0;
    std::vector<std::vector<float>> mag_;
    std::vector<float> work_, timeBuf_;
    std::vector<Complex> spec_, freqBuf_;
    std::vector<std::vector<Complex>> accum_;
    std::vector<std::vector<float>> blockOut_;
    SmoothedValue wetAmount_;
    std::array<float, kBlock> wetRamp_{};
    int wetNotes_ = 0;    // the notes of the filters heard
    int frameFill_ = 0;   // host samples gathered into the current frame
};

}  // namespace

SUB_REGISTER_BUILTIN(PrismaProcessor, AudioEffect);

}  // namespace sub
