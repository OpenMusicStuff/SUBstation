// Built-in "Hue" device: a spectral pitch quantizer for basses and other
// harmonic sounds. It keeps the parts of the spectrum that sit on the notes of
// a chosen scale (or the MIDI notes it is played), moves the notes a semitone
// off them onto them, takes out the other notes and lets what lies between
// notes through, softer: a growl's partials pulled into key, its texture left.
//
// - Rates: the input is split in halves of its rate (two-path polyphase IIR
//   half-band filters, as a perfect-reconstruction QMF: the upper half passes
//   untouched) down to a core of 22.05-24 kHz, where everything else happens.
// - Masks: the notes are filters made from magnitude masks over the pitch of
//   each frequency: an enabled note's gain (measured per set of notes:
//   HueData.h) falling off over a semitone or two, exact zeros on the notes
//   taken out, and between notes a floor that depends on how many notes are
//   enabled. A note a semitone off an enabled one (the lower one if both are)
//   is moved onto it: a band about it, then a pitch shifter.
// - The bands: Linkwitz-Riley crossovers at Start and End make a low, a mid
//   and a high band; the mid band goes through the kept notes' filter and the
//   moved notes' bands, all minimum-phase (no latency of their own). The latency
//   is one convolution block (256 core samples) and the half-band filters'.
// - Quality: Low Latency moves the notes through two
//   shifters, up and down, whose heads drift in and out of phase on each note:
//   it lands a little off its pitch, with sidebands. High Quality gives each
//   pitch class moved a shifter of its own, its heads a whole number of that
//   note's periods apart: the note lands on its pitch exactly, clean. Both have
//   the same latency; switching crossfades.
// - The convolutions are uniformly partitioned (blocks of 256 core samples, 32
//   partitions). The masks are designed in steps across audio callbacks whenever the notes change (a
//   cepstrum of 16384 points for the minimum-phase ones), and the new filters
//   fade in over one block.
// - The shifters: two read heads on a delay line, crossfaded with Hann windows
//   over grains two periods long, their delay sweeping at 1 - ratio.
// - Color fades from the plain mid band (0 %) to the processed one (100 %);
//   above (tuned by how many notes are enabled), the
//   notes get narrower and louder and the floor between them lower.
//   Wet Gain scales the processed band; Pre-Shift shifts the mid band before the
//   notes, Post-Shift tunes the notes.
// - Auto (on at first): the notes are the song's key's scale (major or natural
//   minor; ProcessContext::keyTonic, the project's key or the one its notes are
//   in), else (off, or no key known) the twelve note buttons.
// - MIDI Notes (on at first): once MIDI notes come (from MIDI From, or the
//   track's own) they choose the notes instead, each chord held until the
//   next; until then, and after a reset, Auto's or the buttons'.
// - Its editor reads the input and the output (mono) for a chromagram, and the
//   notes in use (and whether they are MIDI's).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/HueData.h"
#include "rt/RtUtils.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <kfr/dft.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace sub {
namespace {

using Complex = std::complex<float>;
using hue::kColorCountPoints;
using hue::kColorCounts;
using hue::kColorFloorDb;
using hue::kColorKernel;
using hue::kColorLiftDb;
using hue::kKernelPoints;
using hue::kNoteGainCentiDb;
using hue::kNoteKernel;

constexpr int kChannels = 2;
constexpr int kBlock = 256;           // convolution block (core samples)
constexpr int kBins = kBlock + 1;     // a block's spectrum (its transform's size is two blocks)
constexpr int kDesignSize = 16384;    // the masks' frequency grid
constexpr int kTaps = kDesignSize / 2;
constexpr int kParts = kTaps / kBlock;
constexpr int kMaskChunk = 1024;  // the masks' bins designed in an audio callback
constexpr int kMaskSteps = (kDesignSize / 2 + 1 + kMaskChunk - 1) / kMaskChunk;
constexpr float kGrainSeconds = 0.11f;
constexpr float kMinDelay = 3.f;      // core samples a head reads behind at the least
constexpr float kMaskFloor = 1e-5f;   // -100 dB: where a mask is zero (the log needs a floor)
constexpr float kMinStart = 160.f;    // Start and End work within these, whatever
constexpr float kMaxEnd = 4000.f;     // they are set to
// The notes moved: a band about each, its gain 0.85 times the note's it lands on,
// falling exponentially away from it (13.5 dB in 0.3 semitone).
constexpr double kMoveGain = 0.85;
constexpr double kMoveWidth = 0.19;

// The filters, by slot.
enum Slot {
    Kept = 0,         // the notes kept
    FromBelow,        // Low Latency: the notes moved up, before the shifter
    FromAbove,        // Low Latency: the notes moved down
    Moved0,           // High Quality: each pitch class moved, before its own shifter (12)
    kSlots = Moved0 + 12
};

// Half-band all-pass coefficients (polyphase, four: a transition 0.1075 of the rate wide), chosen so the
// bands' delay is the same across the spectrum (34 samples at a quarter of the rate).
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

// Two read heads on a delay line, crossfaded with Hann windows over grains two periods long and born a period
// apart: a pitch shift, the delay sweeping by `slope` (1 - ratio) per sample. The heads are slope * period
// apart: a whole number of a note's periods keeps them in phase on it (High Quality's shifters).
class Shifter {
public:
    void prepare(int period, int maxPeriod) {
        maxPeriod = std::max(maxPeriod, period);
        // The longest sweep: Pre-Shift's 50 cents or a semitone's (6 %) over two periods.
        size_t size = 1;
        while (size < static_cast<size_t>(0.07 * 2 * maxPeriod + kMinDelay + 8)) size <<= 1;
        buffer_.assign(size, 0.f);
        mask_ = size - 1;
        window_.reserve(static_cast<size_t>(2 * maxPeriod));
        setPeriod(period);
    }
    // A period of another length (no more than prepare()'s longest): the shifter starts again, silent.
    void setPeriod(int period) {
        period_ = std::max(16, period);
        window_.resize(static_cast<size_t>(2 * period_));
        for (size_t i = 0; i < window_.size(); ++i) {
            const double s = std::sin(std::numbers::pi * static_cast<double>(i) / (2.0 * period_));
            window_[i] = static_cast<float>(s * s);
        }
        clear();
    }
    int period() const noexcept { return period_; }
    void clear() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.f);
        write_ = 0;
        age_ = 0;
        filled_ = 0;
    }
    float process(float x, double slope) noexcept {
        buffer_[write_] = x;
        const double twoP = 2.0 * period_;
        double out = 0;
        for (int head = 0; head < 2; ++head) {
            int age = age_ + head * period_;
            if (age >= 2 * period_) age -= 2 * period_;
            const double delay = slope > 0 ? kMinDelay + slope * age : kMinDelay + (-slope) * (twoP - age);
            double w = window_[static_cast<size_t>(age)];
            // started again: a head comes in over kWarmUp samples once what it reads has been written
            if (filled_ < delay + kWarmUp) w *= std::clamp((filled_ - delay) / kWarmUp, 0.0, 1.0);
            out += w * read(delay);
        }
        if (filled_ < static_cast<double>(mask_)) filled_ += 1;
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
    static constexpr double kWarmUp = 64;
    std::vector<float> buffer_, window_;
    size_t mask_ = 0, write_ = 0;
    double filled_ = 0;  // samples written since it started (up to the buffer's length)
    int period_ = 2640, age_ = 0;
};

// What the filters are designed for: they are designed again when it changes.
struct DesignKey {
    int notes = 0;            // bit per pitch class (C = bit 0)
    int postCents = 0;        // tenths of a cent
    int colorExponent = 100;  // percent, above 100 only
    int start = 240, end = 2400;
    bool highQuality = false;
    bool operator==(const DesignKey&) const = default;
};

// A real FFT (KFR's: its N / 2 + 1 bins, unscaled both ways) with the scratch it needs.
class RealDft {
public:
    void resize(size_t size) {
        plan_ = std::make_unique<kfr::dft_plan_real<float>>(size, kfr::dft_pack_format::CCs);
        temp_.assign(plan_->temp_size, 0);
    }
    void forward(const float* time, Complex* bins) noexcept {
        plan_->execute(reinterpret_cast<kfr::complex<float>*>(bins), time, temp_.data());
    }
    void inverse(const Complex* bins, float* time) noexcept {
        plan_->execute(time, reinterpret_cast<const kfr::complex<float>*>(bins), temp_.data(), kfr::cinvert_t{});
    }

private:
    std::unique_ptr<kfr::dft_plan_real<float>> plan_;
    std::vector<kfr::u8> temp_;
};

// Spectra as separate real and imaginary parts, so the multiply-adds vectorise: kBins per block.
struct Spectra {
    std::vector<float> re, im;
    void assign(size_t blocks) {
        re.assign(blocks * kBins, 0.f);
        im.assign(blocks * kBins, 0.f);
    }
    void set(size_t block, const Complex* bins) noexcept {
        float* r = re.data() + block * kBins;
        float* i = im.data() + block * kBins;
        for (int k = 0; k < kBins; ++k) {
            r[k] = bins[k].real();
            i[k] = bins[k].imag();
        }
    }
    void clear() noexcept {
        std::fill(re.begin(), re.end(), 0.f);
        std::fill(im.begin(), im.end(), 0.f);
    }
    const float* real(size_t block) const noexcept { return re.data() + block * kBins; }
    const float* imag(size_t block) const noexcept { return im.data() + block * kBins; }
};

// A filter in the frequency domain: its partitions' spectra.
struct Partitions {
    Spectra spectra;
    int used = 0;  // partitions with anything in them; 0: the filter is silent
};

// A signal's last blocks, as spectra (each block with the one before it: overlap-save), newest at `pos`.
struct History {
    Spectra spectra;
    int pos = 0;
    float previous[kBlock] = {};
    void clear() noexcept {
        spectra.clear();
        pos = 0;
        std::fill(std::begin(previous), std::end(previous), 0.f);
    }
};

// y += x h, over one partition's bins.
inline void multiplyAdd(const float* __restrict xr, const float* __restrict xi, const float* __restrict hr,
                        const float* __restrict hi, float* __restrict yr, float* __restrict yi) noexcept {
    for (int k = 0; k < kBins; ++k) {
        yr[k] += xr[k] * hr[k] - xi[k] * hi[k];
        yi[k] += xr[k] * hi[k] + xi[k] * hr[k];
    }
}

class HueProcessor final : public BuiltinProcessor {
public:
    enum Param {
        Start = 0, End, Color, WetGain, PreShift, PostShift, MuteLow, MuteHigh, Quality, Midi, Auto,
        NoteC, NumParams = NoteC + 12
    };
    // Its editor's displays: the input and the output (mono, a value a sample: its chromagram), and the notes
    // in use (their bits, C the lowest; kFromMidi while they are MIDI notes, kFromKey while the song key's), a
    // value every kNotesSamples.
    enum Display { InputDisplay = 0, OutputDisplay, NotesDisplay, NumDisplays };
    static constexpr int kNotesSamples = 1024;
    static constexpr int kFromMidi = 1 << 12;
    static constexpr int kFromKey = 1 << 13;

    HueProcessor() : BuiltinProcessor(infos(), displayInfos()) {}

    std::string typeId() const override { return "builtin:hue"; }
    std::string name() const override { return "Hue"; }
    bool acceptsMidi() const override { return true; }
    int latencySamples() const override { return latency_; }
    int tailSamples() const override { return latency_ + kTaps * factor_; }

    void prepare(double sampleRate, int) override {
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
        grain_ = static_cast<int>(std::lround(kGrainSeconds * coreRate_));
        for (auto& c : ch_) {
            c.mid.spectra.assign(kParts);
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
            c.pre.prepare(grain_, grain_);
            c.up.prepare(grain_, grain_);
            c.down.prepare(grain_, grain_);
            for (auto& m : c.moved) m.prepare(grain_, 3 * grain_);
        }
        for (auto& set : filters_)
            for (auto& f : set) f.spectra.assign(kParts);
        mag_.assign(kSlots, std::vector<float>(kDesignSize / 2 + 1));
        work_.assign(kDesignSize, 0.f);
        spec_.assign(kDesignSize / 2 + 1, Complex{});
        timeBuf_.assign(2 * kBlock, 0.f);
        freqBuf_.assign(kBins, Complex{});
        accumRe_.assign(kBins, 0.f);
        accumIm_.assign(kBins, 0.f);
        for (auto& b : out_) b.assign(2 * kBlock, 0.f);

        // The filters for the current settings, now (no audio is running).
        key_ = currentKey();
        designStep_ = 0;
        designing_ = true;
        while (designing_) runDesignStep();
        active_ = pending_;
        fade_ = 0;
        heard_ = key_;
        wetAmount_.reset(coreRate_, 0.02);
        wetAmount_.snapTo(wetTarget());
        reset();
    }

    void reset() override {
        std::fill(std::begin(held_), std::end(held_), 0);
        midiNotes_ = 0;
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
        keyTonic_ = ctx.keyTonic;
        keyMinor_ = ctx.keyMinor;
        // MIDI notes, once any come, choose the notes, each chord held until the next (the gaps between chords
        // don't fall back to the key).
        if (ctx.inEvents.count > 0) {
            int bits = 0;
            for (int k = 0; k < 128; ++k)
                if (held_[k] > 0) bits |= 1 << (k % 12);
            if (bits != 0) midiNotes_ = bits;
        }
        // The filters follow the settings: a new design is started when they change, one step per call.
        const DesignKey key = currentKey();
        if (!designing_ && !(key == key_) && fade_ == 0) {
            key_ = key;
            designStep_ = 0;
            designing_ = true;
        }
        if (designing_) runDesignStep();

        const int chs = std::min(numChannels, kChannels);
        const auto mono = [&](int i) { return chs > 1 ? 0.5f * (channels[0][i] + channels[1][i]) : channels[0][i]; };
        if (chs > 0)
            for (int i = 0; i < numFrames; ++i) publish(InputDisplay, mono(i));
        process(channels, chs, numFrames);
        if (chs > 0)
            for (int i = 0; i < numFrames; ++i) publish(OutputDisplay, mono(i));
        for (notesCount_ += numFrames; notesCount_ >= kNotesSamples; notesCount_ -= kNotesSamples)
            publish(NotesDisplay, static_cast<float>(enabledNotes() | (fromMidi() ? kFromMidi : fromKey() ? kFromKey : 0)));
    }

private:
    void process(float* const* channels, int chs, int numFrames) noexcept {
        updateCrossovers();
        preRatio_ = std::exp2(param(PreShift) / 1200.0);
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

    struct Channel {
        Qmf qmf[3];
        std::vector<double> highDelay[3];
        size_t highPos[3] = {};
        Lr4 lowLp, midHp, midLp, highHp;
        Shifter pre, up, down;
        std::array<Shifter, 12> moved;  // High Quality: a shifter per pitch class moved
        History mid;                    // the mid band (as Pre-Shift has it)
        float inMid[kBlock] = {}, inLow[kBlock] = {}, inHigh[kBlock] = {};
        float outBlock[kBlock] = {};
        bool resting = false;  // the shifters cleared while the processed band isn't heard
        double frameIn[8] = {}, frameOut[8] = {};
        void clear() {
            for (auto& q : qmf) q.clear();
            for (int s = 0; s < 3; ++s) {
                std::fill(highDelay[s].begin(), highDelay[s].end(), 0.0);
                highPos[s] = 0;
            }
            lowLp.clear(); midHp.clear(); midLp.clear(); highHp.clear();
            pre.clear(); up.clear(); down.clear();
            for (auto& m : moved) m.clear();
            mid.clear();
            for (float* b : {inMid, inLow, inHigh, outBlock}) std::fill(b, b + kBlock, 0.f);
            std::fill(std::begin(frameIn), std::end(frameIn), 0.0);
            std::fill(std::begin(frameOut), std::end(frameOut), 0.0);
        }
    };

    static std::vector<DisplayInfo> displayInfos() {
        return {{"input", 1}, {"output", 1}, {"notes", kNotesSamples}};
    }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            const auto& onOff = offOnLabels();
            static const std::vector<std::string> kQuality = {"Low Latency", "High Quality"};
            std::vector<ParamInfo> list = {
                {"start", "Start", "Hz", 80.f, 1800.f, 240.f, true},
                {"end", "End", "Hz", 400.f, 8000.f, 2400.f, true},
                {"color", "Color", "%", 0.f, 200.f, 100.f},
                {"wet_gain", "Wet Gain", "dB", -6.f, 6.f, 0.f},
                {"pre_shift", "Pre-Shift", "cents", -50.f, 50.f, 0.f},
                {"post_shift", "Post-Shift", "cents", -50.f, 50.f, 0.f},
                {"mute_low", "Mute Low", "", 0.f, 1.f, 0.f, false, onOff},
                {"mute_high", "Mute High", "", 0.f, 1.f, 0.f, false, onOff},
                {"quality", "Quality", "", 0.f, 1.f, 0.f, false, kQuality},
                {"midi", "MIDI Notes", "", 0.f, 1.f, 1.f, false, onOff},
                {"auto", "Auto", "", 0.f, 1.f, 1.f, false, onOff},
            };
            static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            static const char* const kIds[12] = {"note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f",
                                                 "note_fs", "note_g", "note_gs", "note_a", "note_as", "note_b"};
            for (int c = 0; c < 12; ++c) list.push_back({kIds[c], kNames[c], "", 0.f, 1.f, 0.f, false, onOff});
            return list;
        }();
        return kInfos;
    }

    // Whether the notes are MIDI's: MIDI Notes on (as it is at first) and notes come (from MIDI From, or the
    // track's own), until a reset.
    bool fromMidi() const noexcept { return isOn(Midi) && midiNotes_ != 0; }
    // Whether they are the song key's: Auto on and a key known (and no MIDI notes).
    bool fromKey() const noexcept { return !fromMidi() && isOn(Auto) && keyTonic_ >= 0; }
    int enabledNotes() const noexcept {
        if (fromMidi()) return midiNotes_;
        if (fromKey()) {
            static constexpr int kMajor[7] = {0, 2, 4, 5, 7, 9, 11}, kMinor[7] = {0, 2, 3, 5, 7, 8, 10};
            int bits = 0;
            for (const int step : keyMinor_ ? kMinor : kMajor) bits |= 1 << ((keyTonic_ + step) % 12);
            return bits;
        }
        int bits = 0;
        for (int c = 0; c < 12; ++c)
            if (isOn(NoteC + c)) bits |= 1 << c;
        return bits;
    }
    DesignKey currentKey() const noexcept {
        DesignKey k;
        k.notes = enabledNotes();
        k.postCents = static_cast<int>(std::lround(param(PostShift) * 10));
        k.colorExponent = std::max(100, static_cast<int>(std::lround(param(Color))));
        k.start = static_cast<int>(std::lround(std::max(param(Start), kMinStart)));
        k.end = static_cast<int>(std::lround(std::min(param(End), kMaxEnd)));
        k.highQuality = choiceIndex(Quality) == 1;
        return k;
    }
    // How much of the processed band is heard (0..1): Color up to 100 %, and nothing with no notes or all twelve.
    float wetTarget() const noexcept {
        const int n = heard_.notes;
        if (n == 0 || n == 0xfff) return 0.f;
        return std::clamp(param(Color) / 100.f, 0.f, 1.f);
    }
    // A pitch class's shift: +1 up, -1 down (onto the lower neighbour when both are enabled), 0 none.
    static int moveOf(int bits, int c) noexcept {
        if (bits >> c & 1) return 0;
        if (bits >> ((c + 11) % 12) & 1) return -1;
        if (bits >> ((c + 1) % 12) & 1) return 1;
        return 0;
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
            heard_ = key_;
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
                hi[s][k] = d[c.highPos[s]];
                d[c.highPos[s]] = high;
                c.highPos[s] = (c.highPos[s] + 1) % d.size();
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

    // One core sample: what goes into this block, and what the block before it made out.
    double core(Channel& c, double x) noexcept {
        const double mid = c.midLp(c.midHp(x));
        c.inLow[pos_] = static_cast<float>(c.lowLp(x));
        c.inHigh[pos_] = static_cast<float>(c.highHp(x));
        c.inMid[pos_] = preRatio_ != 1.0 ? c.pre.process(static_cast<float>(mid), 1.0 - preRatio_) : static_cast<float>(mid);
        return c.outBlock[pos_];
    }

    // --- Convolution -----------------------------------------------------------------------------------

    void push(History& h, const float* block) noexcept {
        std::copy(h.previous, h.previous + kBlock, timeBuf_.begin());
        std::copy(block, block + kBlock, timeBuf_.begin() + kBlock);
        std::copy(block, block + kBlock, h.previous);
        fft_.forward(timeBuf_.data(), freqBuf_.data());
        h.pos = (h.pos + kParts - 1) % kParts;
        h.spectra.set(static_cast<size_t>(h.pos), freqBuf_.data());
    }

    // out (a block) = `filter` over the history, the block just pushed last.
    void convolve(const History& h, const Partitions& filter, float* out) noexcept {
        if (filter.used == 0) {
            std::fill(out, out + kBlock, 0.f);
            return;
        }
        std::fill(accumRe_.begin(), accumRe_.end(), 0.f);
        std::fill(accumIm_.begin(), accumIm_.end(), 0.f);
        for (int part = 0; part < filter.used; ++part) {
            const auto x = static_cast<size_t>((h.pos + part) % kParts);
            const auto f = static_cast<size_t>(part);
            multiplyAdd(h.spectra.real(x), h.spectra.imag(x), filter.spectra.real(f), filter.spectra.imag(f),
                        accumRe_.data(), accumIm_.data());
        }
        for (int k = 0; k < kBins; ++k)
            freqBuf_[static_cast<size_t>(k)] = Complex(accumRe_[static_cast<size_t>(k)], accumIm_[static_cast<size_t>(k)]);
        fft_.inverse(freqBuf_.data(), timeBuf_.data());
        const float scale = 1.f / (2 * kBlock);
        for (int k = 0; k < kBlock; ++k) out[k] = timeBuf_[static_cast<size_t>(kBlock + k)] * scale;
    }

    // --- A block ---------------------------------------------------------------------------------------

    // A filter's output this block with set `from`, and (with `to` a set, while they fade) with set `to` blended
    // in. `run(set, out)` fills a block for a set; the blend ramps from one to the other over the block.
    template <typename Run>
    void blended(int from, int to, float* out, float* scratch, Run run) noexcept {
        run(from, out);
        if (to < 0) return;
        run(to, scratch);
        for (int k = 0; k < kBlock; ++k) {
            const float t = (k + 1) / static_cast<float>(kBlock);
            out[k] = out[k] * (1 - t) + scratch[k] * t;
        }
    }

    // The notes moved, by the filters of set `from` (fading into those of set `to`, if any) and the shifters of a
    // mode: Low Latency's two (up and down), High Quality's one per pitch class.
    void moveNotes(Channel& c, bool highQuality, int from, int to, float* out) noexcept {
        float* band = out_[3].data();
        float* scratch = out_[4].data();
        auto filtered = [&](int slot) {
            blended(from, to, band, scratch, [&](int s, float* o) { convolve(c.mid, filters_[s][slot], o); });
        };
        std::fill(out, out + kBlock, 0.f);
        if (!highQuality) {
            const double upSlope = 1.0 - std::exp2(1.0 / 12), downSlope = 1.0 - std::exp2(-1.0 / 12);
            filtered(FromBelow);
            for (int k = 0; k < kBlock; ++k) out[k] += c.up.process(band[k], upSlope);
            filtered(FromAbove);
            for (int k = 0; k < kBlock; ++k) out[k] += c.down.process(band[k], downSlope);
            return;
        }
        for (int pc = 0; pc < 12; ++pc) {
            const int slot = Moved0 + pc;
            if (filters_[static_cast<size_t>(from)][slot].used == 0 &&
                (to < 0 || filters_[static_cast<size_t>(to)][slot].used == 0))
                continue;
            filtered(slot);
            Shifter& shifter = c.moved[static_cast<size_t>(pc)];
            for (int k = 0; k < kBlock; ++k) out[k] += shifter.process(band[k], movedSlope_[pc]);
        }
    }

    void processBlock(Channel& c) noexcept {
        const bool fading = fade_ > 0;
        const int incoming = 1 - active_;
        const bool muteLow = isOn(MuteLow), muteHigh = isOn(MuteHigh);
        const float wetGain = dbToGain(param(WetGain));
        const float* low = c.inLow;
        const float* high = c.inHigh;
        const float* mid = c.inMid;
        float* kept = out_[0].data();
        float* moved = out_[1].data();
        float* other = out_[2].data();
        float* scratch = out_[4].data();
        push(c.mid, c.inMid);

        // Nothing of the processed band heard (no notes, all twelve, Color 0): the band passes, and the notes'
        // filters and the shifters rest (the band's spectra are still kept, for when they are heard again).
        if (!fading && wetRamp_[0] == 0.f && wetRamp_[kBlock - 1] == 0.f) {
            if (!c.resting) {
                c.up.clear();
                c.down.clear();
                for (auto& m : c.moved) m.clear();
                c.resting = true;
            }
            for (int k = 0; k < kBlock; ++k) c.outBlock[k] = (muteLow ? 0.f : low[k]) + (muteHigh ? 0.f : high[k]) + mid[k];
            return;
        }
        c.resting = false;

        blended(active_, fading ? incoming : -1, kept, scratch,
                [&](int s, float* o) { convolve(c.mid, filters_[s][Kept], o); });
        // the notes moved: from one Quality to the other, each mode's own, crossfaded
        if (fading && highQualitySet_[active_] != highQualitySet_[incoming]) {
            moveNotes(c, highQualitySet_[active_], active_, -1, moved);
            moveNotes(c, highQualitySet_[incoming], incoming, -1, other);
            for (int k = 0; k < kBlock; ++k) {
                const float t = (k + 1) / static_cast<float>(kBlock);
                moved[k] = moved[k] * (1 - t) + other[k] * t;
            }
        } else {
            moveNotes(c, highQualitySet_[active_], active_, fading ? incoming : -1, moved);
        }
        for (int k = 0; k < kBlock; ++k) {
            const float a = wetRamp_[static_cast<size_t>(k)];
            const float midOut = (1 - a) * mid[k] + a * wetGain * (kept[k] + moved[k]);
            c.outBlock[k] = (muteLow ? 0.f : low[k]) + (muteHigh ? 0.f : high[k]) + midOut;
        }
    }

    // --- Filter design (in steps on the rendering thread; nothing allocated) -------------------------

    // A design, a step an audio callback: the masks' setup, then their bins kMaskChunk at a time, then one filter
    // a step (the slots this design uses; the others go silent).
    void runDesignStep() noexcept {
        if (designStep_ == 0) {
            prepareMasks();
            ++designStep_;
            return;
        }
        if (designStep_ <= kMaskSteps) {
            const int from = (designStep_ - 1) * kMaskChunk;
            maskBins(from, std::min(kDesignSize / 2 + 1, from + kMaskChunk));
            ++designStep_;
            return;
        }
        const int slot = designStep_ - 1 - kMaskSteps;
        Partitions& out = filters_[static_cast<size_t>(1 - active_)][static_cast<size_t>(slot)];
        if (!slotUsed_[slot]) {
            out.used = 0;
        } else {
            designMinimum(mag_[static_cast<size_t>(slot)], out);
        }
        if (++designStep_ > kMaskSteps + kSlots) finishDesign();
    }

    void finishDesign() noexcept {
        designing_ = false;
        pending_ = 1 - active_;
        // High Quality's shifters: one per pitch class, their periods kept in step with its notes
        for (int pc = 0; pc < 12; ++pc) {
            movedNext_[pc] = moveOf(key_.notes, pc);
            nextPeriod_[pc] = movedPeriod(pc, movedNext_[pc]);
        }
        // the next block crossfades from the old filters to these (and, from one Quality to the other, from the
        // one mode's shifters to the other's, started again)
        highQualitySet_[pending_] = key_.highQuality;
        if (highQualitySet_[pending_] != highQualitySet_[active_]) {
            for (auto& c : ch_) {
                if (key_.highQuality) {
                    for (auto& m : c.moved) m.clear();
                } else {
                    c.up.clear();
                    c.down.clear();
                }
            }
        }
        fade_ = 1;
        applyMoves();
    }

    // High Quality's shifters take their new periods (and moves) now; a shifter whose period changes starts again.
    void applyMoves() noexcept {
        for (int pc = 0; pc < 12; ++pc) {
            if (movedNext_[pc] != 0 && nextPeriod_[pc] != ch_[0].moved[static_cast<size_t>(pc)].period())
                for (auto& c : ch_) c.moved[static_cast<size_t>(pc)].setPeriod(nextPeriod_[pc]);
            const double ratio = std::exp2(movedNext_[pc] / 12.0);
            // the heads a whole number of the note's periods apart: the slope that makes them so
            const int period = ch_[0].moved[static_cast<size_t>(pc)].period();
            const double notePeriod = notePeriodSamples(pc);
            const double spacing = std::max(1.0, std::round(std::abs(1.0 - ratio) * period / notePeriod)) * notePeriod;
            // (a class no longer moved keeps its slope as it fades out)
            if (movedNext_[pc] != 0) movedSlope_[pc] = (ratio > 1 ? -1.0 : 1.0) * spacing / period;
        }
    }

    // The period, in core samples, of pitch class pc's lowest note in the processed band (an octave under Start
    // at the least): every higher octave's period divides it.
    double notePeriodSamples(int pc) const noexcept {
        double f = 440.0 * std::exp2((pc - 9) / 12.0);
        const double floorHz = 0.5 * key_.start;
        while (f >= 2 * floorHz) f *= 0.5;
        while (f < floorHz) f *= 2;
        return coreRate_ / f;
    }
    // A High Quality shifter's period: about the grain's, made so a whole number of note periods spans its sweep.
    int movedPeriod(int pc, int move) const noexcept {
        if (move == 0) return grain_;
        const double slope = std::abs(1.0 - std::exp2(move / 12.0));
        const double note = notePeriodSamples(pc);
        const double spacing = std::max(1.0, std::round(slope * grain_ / note)) * note;
        return std::min(3 * grain_, static_cast<int>(std::lround(spacing / slope)));
    }

    // The magnitude masks over the design grid's bins (core rate), for the slots the design uses.
    // The per-design values the masks' bins are made from (prepareMasks()).
    struct MaskSetup {
        int bits = 0;
        float v[12] = {};       // each enabled note's gain
        int move[12] = {};      // each pitch class's move
        float noteKernel[kKernelPoints] = {};
        float floorGain = 0.f;
        double narrow = 1.0, post = 0.0, combLow = 0.0, combHigh = 0.0, outsideDb = 0.0;
    };

    void prepareMasks() noexcept {
        MaskSetup& m = mask_;
        const int bits = key_.notes;
        const bool hq = key_.highQuality;
        m.bits = bits;
        float* v = m.v;
        std::fill(std::begin(m.v), std::end(m.v), 0.f);
        int count = 0;
        for (int c = 0; c < 12; ++c) {
            if (bits >> c & 1) {
                v[c] = std::pow(10.f, kNoteGainCentiDb[bits][c] / 2000.f);
                ++count;
            }
        }
        // Color past 100 % sharpens (measured at 125, 150, 175 and 200 %, HueData.h): the notes narrower and
        // louder, the floor between them lower, the more so the fewer notes are enabled. Between the measured
        // Colors, linear; the moved notes' bands narrow too (to 1.8 times).
        static constexpr float kNarrow[5] = {1.f, 1.1f, 1.4f, 1.6f, 1.8f};
        const float over = std::clamp(key_.colorExponent / 100.f - 1.f, 0.f, 1.f) * 4.f;
        const int oi = std::min(3, static_cast<int>(over));
        const float of = over - static_cast<float>(oi);
        auto lerp = [&](const float* t) { return t[oi] + (t[oi + 1] - t[oi]) * of; };
        m.narrow = lerp(kNarrow);
        // a step's value (dB) for this many notes: 0 at 100 %, then the table's (by count) at 125 ... 200 %
        auto byCount = [&](const float (*table)[kColorCountPoints], int step) noexcept {
            if (step < 0) return 0.f;
            const float* t = table[step];
            int i = 0;
            while (i < kColorCountPoints - 2 && count > kColorCounts[i + 1]) ++i;
            const float u = std::clamp(static_cast<float>(count - kColorCounts[i]) /
                                           static_cast<float>(kColorCounts[i + 1] - kColorCounts[i]), 0.f, 1.f);
            return t[i] + (t[i + 1] - t[i]) * u;
        };
        auto colorDb = [&](const float (*table)[kColorCountPoints]) noexcept {
            return byCount(table, oi - 1) + (byCount(table, oi) - byCount(table, oi - 1)) * of;
        };
        for (int i = 0; i < kKernelPoints; ++i) {
            const float from = oi == 0 ? kNoteKernel[i] : kColorKernel[oi - 1][i];
            m.noteKernel[i] = from + (kColorKernel[oi][i] - from) * of;
        }
        const float lift = std::pow(10.f, colorDb(kColorLiftDb) / 20.f);
        for (float& g : m.v) g *= lift;
        m.floorGain = std::max(0.f, -0.016f + 0.0507f * static_cast<float>(12 - count)) *
                      std::pow(10.f, colorDb(kColorFloorDb) / 20.f);
        m.post = key_.postCents / 1000.0;  // tenths of a cent -> semitones
        // The notes' comb reaches ten semitones past Start and End; beyond, the kept mask is flat (Low Latency, as
        // at a gain falling 1.89 dB per enabled note, +19.8 dB with one; the crossovers take most
        // of it away, but a minimum-phase filter's phase depends on its magnitude everywhere).
        m.combLow = 12.0 * std::log2(key_.start / 440.0) + 9.0 - 10.0;
        m.combHigh = 12.0 * std::log2(key_.end / 440.0) + 9.0 + 10.0;
        m.outsideDb = 19.8 - 1.89 * (count - 1);
        int* move = m.move;
        for (int c = 0; c < 12; ++c) move[c] = moveOf(bits, c);
        // which slots this design uses
        for (bool& used : slotUsed_) used = false;
        slotUsed_[Kept] = true;
        if (hq) {
            for (int c = 0; c < 12; ++c) slotUsed_[Moved0 + c] = move[c] != 0;
        } else {
            slotUsed_[FromBelow] = slotUsed_[FromAbove] = true;
        }
    }

    // The masks' bins `from` .. `to` (exclusive), from the setup prepareMasks() made.
    void maskBins(int from, int to) noexcept {
        const MaskSetup& m = mask_;
        const int bits = m.bits;
        const float* v = m.v;
        const int* move = m.move;
        const float* noteKernel = m.noteKernel;
        const double narrow = m.narrow, post = m.post, combLow = m.combLow, combHigh = m.combHigh;
        const double floorGain = m.floorGain, outsideDb = m.outsideDb;
        auto kernel = [&](double dist) noexcept {
            const double x = std::abs(dist) * 8.0;
            const int i = static_cast<int>(x);
            if (i >= kKernelPoints - 1) return 0.0;
            const double t = x - i;
            return noteKernel[i] * (1 - t) + noteKernel[i + 1] * t;
        };
        auto wrapped = [](double d) noexcept {
            d = std::fmod(d, 12.0);
            if (d < -6) d += 12;
            if (d >= 6) d -= 12;
            return d;
        };
        for (int k = from; k < to; ++k) {
            const double f = std::max(20.0, k * coreRate_ / kDesignSize);
            const double p = 12.0 * std::log2(f / 440.0) + 9.0 - post;  // semitones above C
            const double nearest = std::round(p);
            const double d = p - nearest;
            const int cls = static_cast<int>(((static_cast<int64_t>(nearest) % 12) + 12) % 12);
            double sum = 0, cover = 0;
            for (int c = 0; c < 12; ++c) {
                if (!(bits >> c & 1)) continue;
                const double dist = wrapped(p - c);
                const double kv = kernel(dist);
                sum += v[c] * kv;
                cover += kv;
            }
            const bool on = bits >> cls & 1;
            const double s = std::sin(std::numbers::pi * d);
            const double notch = on ? 1.0 : s * s;
            double kept = notch * (sum + floorGain * std::max(0.0, 1.0 - cover));
            // the notes moved: a band about each
            double fromBelow = 0, fromAbove = 0;
            double perClass[12] = {};
            for (int src = 0; src < 12; ++src) {
                if (move[src] == 0) continue;
                const double dist = wrapped(p - src) * narrow;
                if (std::abs(dist) >= 1.5) continue;
                const double band = kMoveGain * v[(src + move[src] + 12) % 12] * std::exp(-std::abs(dist) / kMoveWidth);
                (move[src] > 0 ? fromBelow : fromAbove) += band;
                perClass[src] = band;
            }
            // across a semitone at each end of the comb, from it to the flat gain (in dB)
            const double past = std::max(combLow - p, p - combHigh);  // semitones outside the comb
            if (past > -0.5) {
                const double t = std::min(1.0, past + 0.5);
                const double outside = std::pow(10.0, outsideDb / 20.0);
                kept = std::exp((1 - t) * std::log(std::max(kept, 1e-5)) + t * std::log(outside));
                fromBelow *= 1 - t;
                fromAbove *= 1 - t;
                for (double& b : perClass) b *= 1 - t;
            }
            for (int src = 0; src < 12; ++src)
                if (move[src] != 0) mag_[Moved0 + src][static_cast<size_t>(k)] = static_cast<float>(perClass[src]);
            mag_[Kept][static_cast<size_t>(k)] = static_cast<float>(kept);
            mag_[FromBelow][static_cast<size_t>(k)] = static_cast<float>(fromBelow);
            mag_[FromAbove][static_cast<size_t>(k)] = static_cast<float>(fromAbove);
        }
    }

    // The impulse response in work_ (first kTaps samples) into partitions.
    void partition(Partitions& out) noexcept {
        int last = 0;
        for (int n = 0; n < kTaps; ++n)
            if (std::abs(work_[static_cast<size_t>(n)]) > 1e-7f) last = n;
        out.used = std::min(kParts, last / kBlock + 1);
        for (int part = 0; part < out.used; ++part) {
            std::fill(timeBuf_.begin(), timeBuf_.end(), 0.f);
            std::copy(work_.begin() + part * kBlock, work_.begin() + (part + 1) * kBlock, timeBuf_.begin());
            fft_.forward(timeBuf_.data(), freqBuf_.data());
            out.spectra.set(static_cast<size_t>(part), freqBuf_.data());
        }
    }

    // A minimum-phase filter from a mask (real cepstrum).
    void designMinimum(const std::vector<float>& mag, Partitions& out) noexcept {
        const int half = kDesignSize / 2;
        float peak = 0;
        for (const float m : mag) peak = std::max(peak, m);
        if (peak <= kMaskFloor * 10) {
            out.used = 0;
            return;
        }
        // log magnitude -> cepstrum
        for (int k = 0; k <= half; ++k)
            spec_[static_cast<size_t>(k)] = Complex(std::log(std::max(mag[static_cast<size_t>(k)], kMaskFloor)), 0.f);
        designFft_.inverse(spec_.data(), work_.data());
        const float inv = 1.f / kDesignSize;
        // fold onto the causal half
        work_[0] *= inv;
        for (int n = 1; n < half; ++n) work_[static_cast<size_t>(n)] *= 2 * inv;
        work_[static_cast<size_t>(half)] *= inv;
        std::fill(work_.begin() + half + 1, work_.end(), 0.f);
        designFft_.forward(work_.data(), spec_.data());
        // exp -> the minimum-phase spectrum
        for (int k = 0; k <= half; ++k) spec_[static_cast<size_t>(k)] = std::exp(spec_[static_cast<size_t>(k)]);
        designFft_.inverse(spec_.data(), work_.data());
        // the impulse response's first kTaps samples, its tail faded
        const int fadeStart = kTaps * 7 / 8;
        for (int n = 0; n < kTaps; ++n) {
            float h = work_[static_cast<size_t>(n)] * inv;
            if (n >= fadeStart) {
                const float t = static_cast<float>(n - fadeStart) / (kTaps - fadeStart);
                h *= 0.5f + 0.5f * std::cos(std::numbers::pi_v<float> * t);
            }
            work_[static_cast<size_t>(n)] = h;
        }
        partition(out);
    }


    int stages_ = 1, factor_ = 2, latency_ = 0, grain_ = 2640;
    double coreRate_ = 24000;
    std::array<Channel, kChannels> ch_;
    int pos_ = 0;
    int held_[128] = {};
    int midiNotes_ = 0;  // the MIDI notes held last (bits): 0 until any come
    int keyTonic_ = -1;  // the song's key (ProcessContext's), as the last block had it
    bool keyMinor_ = false;
    double lastStart_ = -1, lastEnd_ = -1;

    RealDft fft_, designFft_;
    std::array<std::array<Partitions, kSlots>, 2> filters_;
    int active_ = 0, pending_ = 0, fade_ = 0;
    bool highQualitySet_[2] = {};  // each set of filters' Quality (how its notes are moved)
    DesignKey key_, heard_;
    bool designing_ = false;
    int designStep_ = 0;
    MaskSetup mask_;
    bool slotUsed_[kSlots] = {};
    int movedNext_[12] = {}, nextPeriod_[12] = {};
    double movedSlope_[12] = {};
    std::vector<std::vector<float>> mag_;
    std::vector<float> work_, timeBuf_;
    std::vector<Complex> spec_, freqBuf_;
    std::vector<float> accumRe_, accumIm_;
    std::array<std::vector<float>, 5> out_;
    SmoothedValue wetAmount_;
    std::array<float, kBlock> wetRamp_{};
    int notesCount_ = 0;  // host samples towards the next value of the notes' display
    int frameFill_ = 0;   // host samples gathered into the current frame
    double preRatio_ = 1.0;
};

}  // namespace

SUB_REGISTER_BUILTIN(HueProcessor, AudioEffect);

}  // namespace sub
