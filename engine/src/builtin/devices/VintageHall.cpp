// Built-in "Vintage Hall" device: a 1970s digital concert hall, after a classic
// one, measured from renders of it (VintageHallDesign.h has the numbers).
//
// Signal flow:
// - At the host's rate: High Cut (a one-pole low-pass) and Low Cut (a DC
//   blocker), then down to the reverb's rate (half the host's at 44.1/48 kHz)
//   through a steep elliptic low-pass: the reverb is band-limited to 12 kHz and
//   rings a little there, as the old converters did.
// - At the reverb's rate, each side: Predelay and a fixed delay into the ring,
//   the left at its start, the right half way round.
// - The ring: two halves, each from where its side comes in: the high shelf
//   (High Shelf at High Freq), two all-passes (Early Diffusion), two nested
//   all-passes (Late Diffusion) with plain delays between, then the loss: the
//   decay's gain for the half's length and Bass Mult below Bass Xover (a one-pole
//   crossover). The innermost delay of the second nested all-pass in each half
//   wanders (Mod Rate, Mod Depth) towards random targets at a steady speed, read
//   linearly between samples (which takes a little off the top as it moves, as the
//   original's does).
// - Each side's output is four taps of the ring: three of its own half and one of
//   the other's; Attack moves the weight from the first tap to the third.
// - Back up to the host's rate through the same low-pass, then Dry/Wet (equal
//   power: the dry by cos, the wet by sin of a quarter turn times the mix).
//
// Controls glide: the damping, the diffusion, the modulation and the taps move
// over about 30 ms (a step a block, the taps a straight line across it), Size
// and Predelay glide every delay (read between samples while they move).
//
// Silence: once the input has been below -160 dB for longer than it takes to
// reach the ring, and the ring and the wet have stayed below -120 dB for a
// whole lap, it sleeps: everything it holds is cleared, the wet is exactly 0
// and the reverb is skipped (the dry still goes through Dry/Wet) until any
// sound comes in, which it hears from silence as a reverb just reset would.

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"
#include "builtin/VintageHallDesign.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

namespace vh = vintage_hall;

constexpr double kControlSeconds = 0.03;  // the controls' glide
constexpr double kLengthSeconds = 0.08;   // Size's and Predelay's glide
constexpr double kMaxPredelayMs = 500.0;
constexpr float kLandedSamples = 1e-4f;   // a gliding length lands on its target within this
constexpr float kAwakeLevel = 1e-8f;      // -160 dB: any input louder wakes it
constexpr float kSleepLevel = 1e-6f;      // -120 dB: a tail this quiet lets it sleep

// A delay line at the reverb's rate, read before each write. Its length glides
// to its target (and is read between samples, linearly, while it moves).
struct Line {
    std::vector<float> buffer = std::vector<float>(8, 0.f);
    size_t mask = 7;
    size_t write = 0;
    float length = 1.f, target = 1.f;
    int whole = 1;
    float frac = 0.f;

    void prepare(int maxSamples) {
        size_t size = 8;
        while (size < static_cast<size_t>(maxSamples) + 16) size <<= 1;
        buffer.assign(size, 0.f);
        mask = size - 1;
        write = 0;
    }
    void clear() noexcept {
        std::fill(buffer.begin(), buffer.end(), 0.f);
        write = 0;
    }
    void setTarget(float samples, bool snap) noexcept {
        target = std::clamp(samples, 1.f, static_cast<float>(mask - 8));
        if (snap) land();
    }
    void land() noexcept {
        length = target;
        whole = static_cast<int>(length);
        frac = length - static_cast<float>(whole);
    }
    bool moving() const noexcept { return length != target; }
    void glide(float coefficient) noexcept {
        length = target + coefficient * (length - target);
        if (std::abs(length - target) < kLandedSamples) {
            land();
            return;
        }
        whole = static_cast<int>(length);
        frac = length - static_cast<float>(whole);
    }
    float read() const noexcept {
        const float a = buffer[(write - static_cast<size_t>(whole)) & mask];
        if (frac == 0.f) return a;
        return a + frac * (buffer[(write - static_cast<size_t>(whole) - 1) & mask] - a);
    }
    // `delay` samples ago (at least 1), read linearly between samples.
    float readAt(float delay) const noexcept {
        const int w = static_cast<int>(delay);
        const float t = delay - static_cast<float>(w);
        const float a = buffer[(write - static_cast<size_t>(w)) & mask];
        return a + t * (buffer[(write - static_cast<size_t>(w) - 1) & mask] - a);
    }
    void push(float x) noexcept {
        buffer[write] = x;
        write = (write + 1) & mask;
    }
};

// A Schroeder all-pass: v = x + g r, y = -g v + r, r being v `length` samples ago.
struct Diffuser {
    Line line;
    float process(float x, float g) noexcept {
        const float r = line.read();
        const float v = x + g * r;
        line.push(v);
        return r - g * v;
    }
};

// Nested all-passes: the delay of each level holds a plain delay and the next
// level; the innermost's is a delay alone (moving, for the modulated ones: read
// linearly between samples, as the original does in its 1970s mode, which
// dulls the highs a little as it moves).
struct NestedAllpass {
    int levels = 2;
    bool modulated = false;
    Line line[3];  // level k < levels - 1: its plain delay; the innermost: its delay

    void clear() noexcept {
        for (Line& l : line) l.clear();
    }
    void glide(float coefficient) noexcept {
        for (int k = 0; k < levels; ++k)
            if (line[k].moving()) line[k].glide(coefficient);
    }
    // `gains` outermost first; `offset`: how much longer the innermost delay is now (samples).
    float process(float x, const float* gains, float offset) noexcept {
        float u[3];
        u[0] = x;
        for (int k = 1; k < levels; ++k) u[k] = line[k - 1].read();
        const int n = levels - 1;
        Line& inner = line[n];
        float r;
        if (modulated) r = inner.readAt(inner.length + offset);
        else r = inner.read();
        float v = u[n] + gains[n] * r;
        float out = r - gains[n] * v;
        inner.push(v);
        for (int k = n - 1; k >= 0; --k) {
            v = u[k] + gains[k] * out;
            out = out - gains[k] * v;
            line[k].push(v);
        }
        return out;
    }
};

// A first-order section's state (VintageHallDesign.h's FirstOrder).
struct FirstOrderState {
    double x1 = 0.0, y1 = 0.0;
    void reset() noexcept { x1 = y1 = 0.0; }
    double process(const vh::FirstOrder& c, double x) noexcept {
        const double y = c.b0 * x + c.b1 * x1 - c.a1 * y1;
        x1 = x;
        y1 = dsp::flushTiny(y);
        return y;
    }
};

// The elliptic low-pass of one halving (second-order sections, transposed direct form II).
struct Elliptic {
    double s1[6] = {}, s2[6] = {};
    void reset() noexcept {
        std::fill(std::begin(s1), std::end(s1), 0.0);
        std::fill(std::begin(s2), std::end(s2), 0.0);
    }
    double process(double x) noexcept {
        for (int k = 0; k < 6; ++k) {
            const double* c = vh::kResampler[k];
            const double y = c[0] * x + s1[k];
            s1[k] = dsp::flushTiny(c[1] * x - c[3] * y + s2[k]);
            s2[k] = dsp::flushTiny(c[2] * x - c[4] * y);
            x = y;
        }
        return x;
    }
};

// Where the loop's halves lose their energy: the decay's gain, the bass crossover and the high shelf.
struct Damping {
    double low = 0.0;  // the crossover's low band
    FirstOrderState shelf;
    void reset() noexcept {
        low = 0.0;
        shelf.reset();
    }
    // The decay's gain and the bass crossover (the input comes in after them),
    float lose(float x, float gain, double bassPole, double bassExtra) noexcept {
        low = dsp::flushTiny((1.0 - bassPole) * x + bassPole * low);
        return static_cast<float>(gain * (x + bassExtra * low));
    }
    // then the high shelf.
    float shelve(float x, const vh::FirstOrder& shelfCoeffs) noexcept { return static_cast<float>(shelf.process(shelfCoeffs, x)); }
};

// The modulation of one delay: a position in 0..1 moving at a steady speed towards a target picked at random.
struct Wander {
    dsp::Noise noise;
    double position = 0.0, target = 0.0;
    void reset(uint32_t seed) noexcept {
        noise = dsp::Noise{};
        noise.seed(seed);
        position = pick();
        target = pick();
    }
    double pick() noexcept { return 0.5 * (1.0 + noise.next()); }
    double next(double step) noexcept {
        if (position < target) position = std::min(target, position + step);
        else position = std::max(target, position - step);
        if (position == target) target = pick();
        return position;
    }
};

class VintageHallProcessor final : public BuiltinProcessor {
public:
    enum Param {
        Mix = 0, Predelay, Decay, Size, Attack, BassMult, BassXover, HighShelf, HighFreq, EarlyDiffusion,
        LateDiffusion, ModRate, ModDepth, HighCut, LowCut, NumParams
    };

    // Its editor's displays (vh::kDisplaySamples host samples a value).
    enum Display { InputLevel = 0, OutputLevel, Tap0, ModB = Tap0 + 8, ModD, NumDisplays };

    VintageHallProcessor() : BuiltinProcessor(infos(), displayInfos()) {}

    std::string typeId() const override { return "builtin:vintage_hall"; }
    std::string name() const override { return "Vintage Hall"; }

    // Until the tail is 60 dB down where it decays slowest (the bass, Bass Mult times longer), and a bit.
    int tailSamples() const override {
        const double seconds = param(Decay) * 0.001 * std::max(1.f, param(BassMult)) * 1.2 + param(Predelay) * 0.001 + 1.0;
        return static_cast<int>(std::min(300.0, seconds) * hostRate_);
    }

    void prepare(double sampleRate, int) override {
        hostRate_ = sampleRate;
        divider_ = vh::rateDivider(sampleRate);
        stages_ = 0;
        while ((1 << stages_) < divider_) ++stages_;
        rate_ = sampleRate / divider_;
        const double perReference = rate_ / vh::kReferenceRate;
        const auto most = [&](int samples, double scale) { return static_cast<int>(std::ceil(samples * perReference * scale)) + 4; };
        const double ring = 1.0, input = 1.2, plainMost = 1.2;  // the largest scales Size gives (plainScale: 1.17)
        for (int side = 0; side < 2; ++side) {
            fixed_[side].prepare(most(vh::kFixedDelay[side], input) + static_cast<int>(std::ceil(kMaxPredelayMs * 0.001 * rate_)));
            for (int k = 0; k < 2; ++k) diffusers_[side][k].line.prepare(most(vh::kDiffuser[side][k], input));
        }
        const vh::Allpass* designs[4] = {&vh::kA, &vh::kB, &vh::kC, &vh::kD};
        for (int a = 0; a < 4; ++a) {
            NestedAllpass& ap = allpasses_[a];
            ap.levels = designs[a]->levels;
            ap.modulated = designs[a]->levels == 3;
            for (int k = 0; k < ap.levels; ++k) {
                const int inner = k + 1 < ap.levels ? designs[a]->length[k + 1] : 0;
                const int extra = ap.modulated && k + 1 == ap.levels
                                      ? static_cast<int>(std::ceil(vh::kModRangeSeconds * rate_)) + 4 : 0;
                ap.line[k].prepare(most(designs[a]->length[k] - inner, ring) + extra);
            }
        }
        const int plain[8] = {vh::kAfterA, vh::kCross, vh::kAfterB, vh::kBeforeF1,
                              vh::kAfterC, vh::kCross, vh::kAfterD, vh::kBeforeF2};
        for (int k = 0; k < 8; ++k) plain_[k].prepare(most(plain[k], plainMost));
        mix_.reset(sampleRate, 0.02);
        lengthGlide_ = static_cast<float>(onePoleCoefficient(kLengthSeconds, rate_));
        reset();
    }

    // Silent; the next render() starts every control where it is then.
    void reset() override {
        for (int side = 0; side < 2; ++side) {
            fixed_[side].clear();
            for (Diffuser& d : diffusers_[side]) d.line.clear();
            highCutState_[side].reset();
            lowCutState_[side].reset();
            for (int s = 0; s < 3; ++s) {
                down_[s][side].reset();
                up_[s][side].reset();
            }
        }
        for (NestedAllpass& ap : allpasses_) ap.clear();
        for (Line& l : plain_) l.clear();
        for (Damping& d : damping_) d.reset();
        wander_[0].reset(0x2545f491u);
        wander_[1].reset(0x9e3779b9u);
        for (int& p : downPhase_) p = 0;
        hostCount_ = 0;
        outL_ = outR_ = 0.f;
        lastWet_[0] = lastWet_[1] = 0.0;
        mix_.snapTo(param(Mix) / 100.f);  // (silent now: no need to ramp)
        displayCount_ = 0;
        peakIn_ = peakOut_ = 0.f;
        tapPeaks_.fill(0.f);
        sleeping_ = false;
        inputQuiet_ = quietSamples_ = 0;
        fresh_ = true;
    }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, 2);
        if (n <= 0) return;
        // How long the input has been silent; any sound wakes it before it is heard.
        int lastLoud = -1;
        for (int c = 0; c < n; ++c) {
            for (int i = numFrames - 1; i > lastLoud; --i) {
                if (std::abs(ch[c][i]) > kAwakeLevel) {
                    lastLoud = i;
                    break;
                }
            }
        }
        if (lastLoud >= 0) {
            inputQuiet_ = numFrames - 1 - lastLoud;
            if (sleeping_) {
                sleeping_ = false;
                fresh_ = true;  // (from silence: every control starts where it is, as after a reset)
            }
        } else {
            inputQuiet_ = std::min<int64_t>(inputQuiet_ + numFrames, int64_t{1} << 40);
        }
        if (sleeping_) {
            renderAsleep(ch, n, numFrames);
            return;
        }
        updateControls(numFrames);
        ringPeak_ = 0.f;
        float wetPeak = 0.f;
        const vh::FirstOrder highCut = vh::highCut(std::min<double>(param(HighCut), 0.49 * hostRate_), hostRate_);
        const vh::FirstOrder lowCut = vh::lowCut(param(LowCut), hostRate_);
        mix_.setTarget(param(Mix) / 100.f);
        const bool mixMoving = mix_.isSmoothing();
        float dryGain = std::cos(0.5f * std::numbers::pi_v<float> * mix_.target());
        float wetGain = std::sin(0.5f * std::numbers::pi_v<float> * mix_.target());

        // The taps' weights glide in a straight line across the block's reverb samples.
        const int reverbSamples = std::max(1, (numFrames + divider_ - 1) / divider_);
        std::array<float, 4> tapStep;
        for (int k = 0; k < 4; ++k) tapStep[k] = (taps_[k] - tapsFrom_[k]) / static_cast<float>(reverbSamples);
        std::array<float, 4> tap = tapsFrom_;

        for (int i = 0; i < numFrames; ++i) {
            const float dryL = ch[0][i], dryR = n > 1 ? ch[1][i] : dryL;
            double x[2] = {lowCutState_[0].process(lowCut, highCutState_[0].process(highCut, dryL)),
                           lowCutState_[1].process(lowCut, highCutState_[1].process(highCut, dryR))};
            // Down: each halving filters every sample and keeps every other.
            bool ready = true;
            for (int s = 0; s < stages_; ++s) {
                x[0] = down_[s][0].process(x[0]);
                x[1] = down_[s][1].process(x[1]);
                const bool keep = downPhase_[s] == 0;
                downPhase_[s] ^= 1;
                if (!keep) {
                    ready = false;
                    break;
                }
            }
            if (ready) {
                for (int k = 0; k < 4; ++k) tap[k] += tapStep[k];
                tick(static_cast<float>(x[0]), static_cast<float>(x[1]), tap);
            }
            // Up: halving s runs on every 2^s-th host sample, given the deeper one's output (doubled)
            // where it has one and silence between.
            const int count = hostCount_;
            hostCount_ = (hostCount_ + 1) % divider_;
            double wet[2] = {outL_, outR_};
            for (int s = stages_ - 1; s >= 0; --s) {
                if (count % (1 << s) != 0) continue;
                const bool fed = count % (1 << (s + 1)) == 0;
                for (int c = 0; c < 2; ++c) wet[c] = up_[s][c].process(fed ? 2.0 * wet[c] : 0.0);
            }
            if (stages_ > 0) {  // a host sample later, as the original's resampling comes out
                for (int c = 0; c < 2; ++c) std::swap(wet[c], lastWet_[c]);
            }
            const float wetNow = std::max(std::abs(static_cast<float>(wet[0])), std::abs(static_cast<float>(wet[1])));
            wetPeak = std::max(wetPeak, wetNow);
            peakIn_ = std::max({peakIn_, std::abs(static_cast<float>(x[0])), std::abs(static_cast<float>(x[1]))});
            peakOut_ = std::max(peakOut_, wetNow);
            if (++displayCount_ == vh::kDisplaySamples) publishDisplays();
            if (mixMoving) {  // (settled, the gains are the block's: no trigonometry a sample)
                const float mix = mix_.next();
                dryGain = std::cos(0.5f * std::numbers::pi_v<float> * mix);
                wetGain = std::sin(0.5f * std::numbers::pi_v<float> * mix);
            }
            if (n == 1) {
                ch[0][i] = dryGain * dryL + wetGain * 0.5f * static_cast<float>(wet[0] + wet[1]);
            } else {
                ch[0][i] = dryGain * dryL + wetGain * static_cast<float>(wet[0]);
                ch[1][i] = dryGain * dryR + wetGain * static_cast<float>(wet[1]);
            }
        }
        tapsFrom_ = taps_;

        // Quiet for a whole lap, with nothing on its way in: asleep.
        const bool quiet = inputQuiet_ > inputReach() && ringPeak_ < kSleepLevel && wetPeak < kSleepLevel;
        quietSamples_ = quiet ? quietSamples_ + numFrames : 0;
        if (quietSamples_ > lapReach()) fallAsleep();
    }

private:
    // Host samples from the input to the ring (High and Low Cut, the resampler, the fixed delays and
    // Predelay), and for a lap of the ring, with some to spare.
    int64_t inputReach() const noexcept {
        const double fixed = std::max(fixed_[0].length, fixed_[1].length);
        return static_cast<int64_t>(fixed * divider_) + static_cast<int64_t>(0.01 * hostRate_);
    }
    int64_t lapReach() const noexcept {
        return static_cast<int64_t>((halfSamples_[0] + halfSamples_[1]) * divider_) + static_cast<int64_t>(0.01 * hostRate_);
    }

    // Everything it holds (all below -120 dB) is let go: it wakes as from a reset, the controls as they are.
    void fallAsleep() noexcept {
        for (int side = 0; side < 2; ++side) {
            fixed_[side].clear();
            for (Diffuser& d : diffusers_[side]) d.line.clear();
            highCutState_[side].reset();
            lowCutState_[side].reset();
            for (int s = 0; s < 3; ++s) {
                down_[s][side].reset();
                up_[s][side].reset();
            }
        }
        for (NestedAllpass& ap : allpasses_) ap.clear();
        for (Line& l : plain_) l.clear();
        for (Damping& d : damping_) d.reset();
        for (int& p : downPhase_) p = 0;
        hostCount_ = 0;
        outL_ = outR_ = 0.f;
        lastWet_[0] = lastWet_[1] = 0.0;
        sleeping_ = true;
        quietSamples_ = 0;
    }

    // Asleep: the wet is exactly 0, the dry goes through Dry/Wet. The other controls aren't followed
    // (nothing to hear them): waking, they start where they are.
    void renderAsleep(float* const* ch, int n, int numFrames) noexcept {
        mix_.setTarget(param(Mix) / 100.f);
        if (mix_.isSmoothing()) {
            for (int i = 0; i < numFrames; ++i) {
                const float dryGain = std::cos(0.5f * std::numbers::pi_v<float> * mix_.next());
                for (int c = 0; c < n; ++c) ch[c][i] *= dryGain;
            }
        } else {
            const float dryGain = std::cos(0.5f * std::numbers::pi_v<float> * mix_.target());
            if (dryGain != 1.f)
                for (int c = 0; c < n; ++c)
                    for (int i = 0; i < numFrames; ++i) ch[c][i] *= dryGain;
        }
        displayCount_ += numFrames;
        while (displayCount_ >= vh::kDisplaySamples) {
            const int over = displayCount_ - vh::kDisplaySamples;
            publishDisplays();  // (the floor: nothing heard)
            displayCount_ = over;
        }
    }

    // One sample at the reverb's rate: the input in, the outputs to outL_ and outR_.
    void tick(float inL, float inR, const std::array<float, 4>& tap) noexcept {
        if (lengthsMoving_) glideLengths();
        // The plain delays' outputs (what was written before this sample).
        const float afterA = plain_[0].read(), cross1 = plain_[1].read(), afterB = plain_[2].read(),
                    beforeF1 = plain_[3].read(), afterC = plain_[4].read(), cross2 = plain_[5].read(),
                    afterD = plain_[6].read(), beforeF2 = plain_[7].read();

        float in[2] = {inL, inR};
        for (int side = 0; side < 2; ++side) {  // (Predelay is part of the fixed delay)
            const float delayed = fixed_[side].read();
            fixed_[side].push(in[side] * static_cast<float>(vh::kInputGain));
            in[side] = delayed;
        }

        const float offset0 = static_cast<float>(wander_[0].next(wanderStep_) * modRange_);
        const float offset1 = static_cast<float>(wander_[1].next(wanderStep_) * modRange_);

        // Half 1, from the left input: it comes in after the second half's loss, before its shelf and diffusers.
        float l1 = damping_[1].shelve(damping_[1].lose(beforeF2, halfGain_[1], bassPole_, bassExtra_[1]) + in[0], shelf_);
        l1 = diffusers_[0][1].process(diffusers_[0][0].process(l1, earlyGain_[0]), earlyGain_[1]);
        plain_[0].push(allpasses_[0].process(l1, apGains_, 0.f));
        const float l2 = afterA;
        plain_[1].push(l2);
        const float r1 = cross1;
        plain_[2].push(allpasses_[1].process(r1, apGains_, offset0));
        const float l3 = afterB;
        plain_[3].push(l3);
        // Half 2, from the right input.
        float r2 = damping_[0].shelve(damping_[0].lose(beforeF1, halfGain_[0], bassPole_, bassExtra_[0]) + in[1], shelf_);
        r2 = diffusers_[1][1].process(diffusers_[1][0].process(r2, earlyGain_[0]), earlyGain_[1]);
        plain_[4].push(allpasses_[2].process(r2, apGains_, 0.f));
        const float l4 = afterC;
        plain_[5].push(l4);
        const float r3 = cross2;
        plain_[6].push(allpasses_[3].process(r3, apGains_, offset1));
        const float r4 = afterD;
        plain_[7].push(r4);

        const float points[8] = {l1, l2, l3, l4, r1, r2, r3, r4};
        for (int k = 0; k < 8; ++k) {
            const float a = std::abs(points[k]);
            tapPeaks_[k] = std::max(tapPeaks_[k], a);
            ringPeak_ = std::max(ringPeak_, a);
        }
        outL_ = tap[0] * l1 + tap[1] * l2 + tap[2] * l3 + tap[3] * l4;
        outR_ = tap[0] * r2 + tap[1] * r3 + tap[2] * r4 + tap[3] * r1;
    }

    void publishDisplays() noexcept {
        const auto db = [](float peak) {
            return peak > 0.f ? std::max(vh::kDisplayFloorDb, 20.f * std::log10(peak)) : vh::kDisplayFloorDb;
        };
        publish(InputLevel, db(peakIn_));
        publish(OutputLevel, db(peakOut_));
        for (int k = 0; k < 8; ++k) publish(Tap0 + k, db(tapPeaks_[k]));
        publish(ModB, static_cast<float>(wander_[0].position));
        publish(ModD, static_cast<float>(wander_[1].position));
        displayCount_ = 0;
        peakIn_ = peakOut_ = 0.f;
        tapPeaks_.fill(0.f);
    }

    static std::vector<DisplayInfo> displayInfos() {
        std::vector<DisplayInfo> d = {{"input", vh::kDisplaySamples}, {"output", vh::kDisplaySamples}};
        for (const char* tap : {"l1", "l2", "l3", "l4", "r1", "r2", "r3", "r4"}) d.push_back({tap, vh::kDisplaySamples});
        d.push_back({"mod_b", vh::kDisplaySamples});
        d.push_back({"mod_d", vh::kDisplaySamples});
        return d;
    }

    void glideLengths() noexcept {
        bool moving = false;
        for (Line* l : lengthLines()) {
            if (l->moving()) l->glide(lengthGlide_);
            moving = moving || l->moving();
        }
        for (NestedAllpass& ap : allpasses_) {
            ap.glide(lengthGlide_);
            for (int k = 0; k < ap.levels; ++k) moving = moving || ap.line[k].moving();
        }
        lengthsMoving_ = moving;
    }

    std::array<Line*, 14> lengthLines() noexcept {
        return {&fixed_[0], &fixed_[1], &diffusers_[0][0].line, &diffusers_[0][1].line,
                &diffusers_[1][0].line, &diffusers_[1][1].line, &plain_[0], &plain_[1], &plain_[2], &plain_[3],
                &plain_[4], &plain_[5], &plain_[6], &plain_[7]};
    }

    // The block's controls: glided towards the parameters (a step a block), and what they set.
    void updateControls(int numFrames) noexcept {
        const float target[NumParams] = {0.f,
                                         param(Predelay),
                                         std::log(param(Decay)),
                                         param(Size) / 100.f,
                                         param(Attack) / 100.f,
                                         std::log(param(BassMult)),
                                         std::log(param(BassXover)),
                                         param(HighShelf),
                                         std::log(param(HighFreq)),
                                         param(EarlyDiffusion) / 100.f,
                                         param(LateDiffusion) / 100.f,
                                         param(ModRate),
                                         param(ModDepth) / 100.f,
                                         0.f,
                                         0.f};
        const float keep = fresh_ ? 0.f
                                  : static_cast<float>(std::exp(-static_cast<double>(numFrames) / (kControlSeconds * hostRate_)));
        for (int k = 0; k < NumParams; ++k) control_[k] = target[k] + keep * (control_[k] - target[k]);

        // Size and Predelay set lengths, which glide on their own.
        const float sizeNow = param(Size) / 100.f;
        if (fresh_ || sizeNow != lengthsSize_ || param(Predelay) != lengthsPredelay_) {
            setLengths(sizeNow, param(Predelay), fresh_);
            lengthsSize_ = sizeNow;
            lengthsPredelay_ = param(Predelay);
        }

        const double decay = std::exp(control_[Decay]) * 0.001;
        const double mult = std::exp(control_[BassMult]);
        for (int h = 0; h < 2; ++h) {
            const double mid = vh::halfGainDb(halfSamples_[h] / rate_, decay), low = mid / mult;
            halfGain_[h] = static_cast<float>(std::pow(10.0, mid / 20.0));
            bassExtra_[h] = std::pow(10.0, (low - mid) / 20.0) - 1.0;
        }
        bassPole_ = vh::bassPole(std::exp(control_[BassXover]), rate_);
        shelf_ = vh::highShelf(std::exp(control_[HighFreq]), control_[HighShelf], rate_);
        for (int k = 0; k < 2; ++k) earlyGain_[k] = static_cast<float>(vh::kDiffuserGain[k] * control_[EarlyDiffusion]);
        const float late = control_[LateDiffusion];
        apGains_[0] = static_cast<float>(vh::kOuterGain * late);
        apGains_[1] = apGains_[2] = static_cast<float>(vh::kInnerGain * late);
        modRange_ = control_[ModDepth] * vh::kModRangeSeconds * rate_;
        wanderStep_ = control_[ModRate] / rate_;
        taps_ = vh::tapWeights(control_[Attack]);
        if (fresh_) tapsFrom_ = taps_;
        fresh_ = false;
    }

    void setLengths(double size, double predelayMs, bool snap) noexcept {
        const double perReference = rate_ / vh::kReferenceRate;
        const double ring = vh::ringScale(size) * perReference, input = vh::inputScale(size) * perReference;
        const auto samples = [](double value) { return static_cast<float>(std::max(1.0, std::round(value))); };
        for (int side = 0; side < 2; ++side) {
            fixed_[side].setTarget(samples(vh::fixedDelay(side, size) * perReference +
                                           std::clamp(predelayMs, 0.0, kMaxPredelayMs) * 0.001 * rate_),
                                   snap);
            for (int k = 0; k < 2; ++k) diffusers_[side][k].line.setTarget(samples(vh::kDiffuser[side][k] * input), snap);
        }
        const vh::Allpass* designs[4] = {&vh::kA, &vh::kB, &vh::kC, &vh::kD};
        for (int a = 0; a < 4; ++a) {
            NestedAllpass& ap = allpasses_[a];
            for (int k = 0; k < ap.levels; ++k) {
                const int inner = k + 1 < ap.levels ? designs[a]->length[k + 1] : 0;
                ap.line[k].setTarget(samples((designs[a]->length[k] - inner) * ring), snap);
            }
        }
        const int plain[8] = {vh::kAfterA, vh::kCross, vh::kAfterB, vh::kBeforeF1,
                              vh::kAfterC, vh::kCross, vh::kAfterD, vh::kBeforeF2};
        for (int k = 0; k < 8; ++k) plain_[k].setTarget(samples(plain[k] * vh::plainScale(k, size) * perReference), snap);
        // Each half's length along the loop, which its loss is worked out from.
        for (int h = 0; h < 2; ++h) {
            halfSamples_[h] = diffusers_[h][0].line.target + diffusers_[h][1].line.target;
            for (int a = 2 * h; a < 2 * h + 2; ++a)
                for (int k = 0; k < allpasses_[a].levels; ++k) halfSamples_[h] += allpasses_[a].line[k].target;
            for (int k = 4 * h; k < 4 * h + 4; ++k) halfSamples_[h] += plain_[k].target;
        }
        lengthsMoving_ = !snap;
    }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = {
            {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
            {"predelay", "Predelay", "ms", 0.f, static_cast<float>(kMaxPredelayMs), 20.f},
            {"decay", "Decay", "ms", 200.f, 70000.f, 4000.f, true},
            {"size", "Size", "%", 0.f, 100.f, 100.f},
            {"attack", "Attack", "%", 0.f, 100.f, 50.f},
            {"bass_mult", "Bass Mult", "x", 0.25f, 4.f, 1.5f, true},
            {"bass_xover", "Bass Xover", "Hz", 100.f, 10000.f, 300.f, true},
            {"high_shelf", "High Shelf", "dB", -24.f, 0.f, -24.f},
            {"high_freq", "High Freq", "Hz", 100.f, 20000.f, 6000.f, true},
            {"early_diffusion", "Early Diffusion", "%", 0.f, 100.f, 100.f},
            {"late_diffusion", "Late Diffusion", "%", 0.f, 100.f, 100.f},
            {"mod_rate", "Mod Rate", "Hz", 0.1f, 10.f, 2.53f, true},
            {"mod_depth", "Mod Depth", "%", 0.f, 100.f, 38.f},
            {"high_cut", "High Cut", "Hz", 100.f, 20000.f, 8000.f, true},
            {"low_cut", "Low Cut", "Hz", 10.f, 1500.f, 10.f, true},
        };
        return kInfos;
    }

    double hostRate_ = 48000.0, rate_ = 24000.0;
    int divider_ = 2, stages_ = 1;
    int downPhase_[3] = {};
    int hostCount_ = 0;
    Elliptic down_[3][2], up_[3][2];
    FirstOrderState highCutState_[2], lowCutState_[2];

    Line fixed_[2];  // with Predelay
    Diffuser diffusers_[2][2];    // in the ring, after where each side comes in
    NestedAllpass allpasses_[4];  // A, B, C, D
    Line plain_[8];               // after A, L2 to R1, after B, before F1, after C, L4 to R3, after D, before F2
    Damping damping_[2];          // F1 (end of half 1), F2 (end of half 2)
    Wander wander_[2];            // B's and D's moving delays

    float control_[NumParams] = {};
    float halfGain_[2] = {1.f, 1.f};
    double halfSamples_[2] = {1.0, 1.0};
    double bassExtra_[2] = {0.0, 0.0};
    double bassPole_ = 0.0;
    vh::FirstOrder shelf_;
    float earlyGain_[2] = {};
    float apGains_[3] = {};
    double modRange_ = 0.0, wanderStep_ = 0.0;
    std::array<float, 4> taps_{}, tapsFrom_{};
    float outL_ = 0.f, outR_ = 0.f;
    double lastWet_[2] = {};
    float lengthGlide_ = 0.f;
    bool lengthsMoving_ = false;
    float lengthsSize_ = -1.f, lengthsPredelay_ = -1.f;
    bool fresh_ = true;
    SmoothedValue mix_;
    int displayCount_ = 0;
    float peakIn_ = 0.f, peakOut_ = 0.f;
    float ringPeak_ = 0.f;          // the ring's peak at its taps over the block
    bool sleeping_ = false;
    int64_t inputQuiet_ = 0;        // host samples the input has been silent
    int64_t quietSamples_ = 0;      // host samples it has all been quiet, nothing coming in
    std::array<float, 8> tapPeaks_{};
};

}  // namespace

SUB_REGISTER_BUILTIN(VintageHallProcessor, AudioEffect);

}  // namespace sub
