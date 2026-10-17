// Built-in "Glitch" device: on a grid of note values, steps of the sound are
// cut up at random: stuttered, reversed, slowed to a stop or chopped.
//
// The grid follows the song while it plays (a step starts on each note value
// from the song's start), and its own clock at the tempo while it doesn't, so
// live input glitches too. At each step's start a roll decides, from the
// step's number and Variation (so the same place in the song always glitches
// the same way, and a render repeats exactly), whether it glitches (Chance)
// and how, the four kinds weighted by their knobs:
//
// - Stutter: the step's first 1/2, 1/3, 1/4, 1/6 or 1/8 played over and over
//   (no repeat shorter than 10 ms).
// - Reverse: the step before played backwards (not until a whole step has
//   come in since the start: the step before would be silence).
// - Tape: the step slowing from full speed to a stop, its pitch falling.
// - Gate: the step cut into 4, 6 or 8, every other one silent.
//
// A step entered other than at its start (playback starting or jumping into
// it, the grid changed) doesn't glitch. Wherever what is played jumps (a
// repeat's start, a step's start or end) the old and new are crossfaded over
// 2 ms, the old going on as it was; a gate opens and closes over the same.
//
// Its editor's sixteen cells light up as the steps go by: the display "step",
// the step's number modulo 16 plus 16 times what it is doing (0 for nothing,
// 1-4 as listed above; one value, so the two never come apart), and "level",
// the input's peak, one value each per 256 samples.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/DspBlocks.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

class GlitchProcessor final : public BuiltinProcessor {
public:
    enum Param { Grid = 0, Chance, Stutter, Reverse, Tape, Gate, Variation, Mix, NumParams };
    enum Display { StepDisplay = 0, LevelDisplay };
    enum Kind { None = 0, StutterKind, ReverseKind, TapeKind, GateKind };
    static constexpr int kDisplaySamples = 256;
    static constexpr double kHistorySeconds = 8.0;

    GlitchProcessor()
        : BuiltinProcessor(infos(), {{"step", kDisplaySamples}, {"level", kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:glitch"; }
    std::string name() const override { return "Glitch"; }

    // The grid's note value in beats (the last, a bar of `beatsPerBar`).
    static double gridBeats(int grid, double beatsPerBar) noexcept {
        static constexpr std::array<double, 5> kBeats = {0.125, 0.25, 0.5, 1.0, 2.0};
        return grid >= static_cast<int>(kBeats.size()) ? beatsPerBar : kBeats[static_cast<size_t>(std::max(0, grid))];
    }

    // 64 random bits from a step's number and a variation (splitmix64's finalizer).
    static uint64_t roll(int64_t step, int variation) noexcept {
        uint64_t z = static_cast<uint64_t>(step) * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(variation) * 0xD1B54A32D192ED03ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        for (auto& line : lines_) line.prepare(static_cast<int>(std::ceil(kHistorySeconds * sampleRate)));
        fadeSamples_ = std::max(1, static_cast<int>(std::lround(0.002 * sampleRate)));
        mix_.reset(sampleRate, 0.02);
        reset();
    }

    void reset() override {
        for (auto& line : lines_) line.reset();
        step_ = INT64_MIN;
        kind_ = None;
        recorded_ = 0;
        t_ = 0;
        freeBeat_ = 0.0;
        voice_ = {};
        old_ = {};
        fade_ = 0;
        gain_ = oldGain_ = 1.f;
        gateGain_ = 1.f;
        displayCount_ = 0;
        peak_ = 0.f;
        mix_.snapTo(param(Mix) / 100.f);
    }

    int tailSamples() const override {
        // A step's worth (at 60 BPM): a reverse plays the step before.
        return static_cast<int>(gridBeats(choiceIndex(Grid), 4.0) * sampleRate_);
    }

protected:
    void render(const ProcessContext& ctx, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        const double samplesPerBeat = ctx.samplesPerBeat() > 0.0 ? ctx.samplesPerBeat() : sampleRate_ * 0.5;
        const double stepBeats = gridBeats(choiceIndex(Grid), ctx.beatsPerBar());
        const double gridOffset = ctx.playing ? ctx.gridOffset(stepBeats) : 0.0;  // (steps on the bar's lines)
        mix_.setTarget(param(Mix) / 100.f);

        for (int i = 0; i < numFrames; ++i) {
            ++recorded_;
            for (int c = 0; c < n; ++c) {
                lines_[c].push(ch[c][i]);
                peak_ = std::max(peak_, std::abs(ch[c][i]));
            }
            const double beat = ctx.playing ? ctx.beatPos + i / samplesPerBeat : freeBeat_;
            freeBeat_ = beat + 1.0 / samplesPerBeat;
            const double steps = (beat - gridOffset) / stepBeats;
            const auto step = static_cast<int64_t>(std::floor(steps + 1e-9));
            if (step != step_) {
                const double into = (steps - static_cast<double>(step)) * stepBeats * samplesPerBeat;
                startStep(step, stepBeats * samplesPerBeat, into < 1.0);
            } else {
                ++t_;
            }

            const Voice voice = voiceNow();
            // A jump in what is played: the old voice goes on as it was, fading out.
            if (std::abs(voice.delay - (voice_.delay + voice_.rate)) > 0.5) {
                old_ = {voice_.delay + voice_.rate, voice_.rate};
                oldGain_ = gain_;
                fade_ = fadeSamples_;
            } else if (fade_ > 0) {
                old_.delay = std::max(0.0, old_.delay + old_.rate);
                --fade_;
            }
            voice_ = voice;
            const float gateTarget = kind_ == GateKind && (t_ / slice_) % 2 == 1 ? 0.f : 1.f;
            gateGain_ += std::clamp(gateTarget - gateGain_, -1.f / fadeSamples_, 1.f / fadeSamples_);
            const float tapeGain =
                kind_ == TapeKind ? dsp::sCurve(std::min(1.f, static_cast<float>(1.0 - voice.rate) / 0.08f)) : 1.f;
            const float gain = gateGain_ * tapeGain;
            gain_ = gain;
            const float in = fade_ > 0 ? dsp::sCurve(1.f - static_cast<float>(fade_) / fadeSamples_) : 1.f;
            const float mix = mix_.next();
            for (int c = 0; c < n; ++c) {
                const float dry = ch[c][i];
                float wet = read(c, voice.delay) * gain;
                if (fade_ > 0) wet = in * wet + (1.f - in) * read(c, old_.delay) * oldGain_;
                ch[c][i] = dry + mix * (wet - dry);
            }

            if (++displayCount_ >= kDisplaySamples) {
                publish(StepDisplay, static_cast<float>(((step_ % 16) + 16) % 16 + 16 * kind_));
                publish(LevelDisplay, peak_);
                displayCount_ = 0;
                peak_ = 0.f;
            }
        }
    }

private:
    static constexpr int kChannels = 2;

    // What is played: `delay` samples behind the input, moving `rate` further behind each sample.
    struct Voice {
        double delay = 0.0;
        double rate = 0.0;
    };

    float read(int c, double delay) const noexcept {
        const auto d = static_cast<float>(std::clamp(delay, 0.0, static_cast<double>(lines_[c].capacity() - 4)));
        return d == std::floor(d) ? lines_[c].tap(static_cast<int>(d)) : (d < 2.f ? lines_[c].linear(d) : lines_[c].hermite(d));
    }

    void startStep(int64_t step, double length, bool atStart) noexcept {
        step_ = step;
        t_ = 0;
        length_ = std::max(1.0, length);
        kind_ = None;
        // Entered other than at its start, or too long to reverse from the history: it plays as it is.
        if (!atStart || 2.0 * length_ + 2.0 * fadeSamples_ >= lines_[0].capacity() - 8) return;
        const uint64_t r = roll(step, choiceIndex(Variation));
        const double u1 = static_cast<double>(r & 0xFFFFF) / 0x100000;
        const double u2 = static_cast<double>((r >> 20) & 0xFFFFF) / 0x100000;
        const int u3 = static_cast<int>((r >> 40) & 0xFFFF);
        if (u1 * 100.0 >= param(Chance)) return;
        const std::array<double, 4> weights = {param(Stutter), param(Reverse), param(Tape), param(Gate)};
        double total = 0.0;
        for (const double w : weights) total += std::max(0.0, w);
        if (total <= 0.0) return;
        double pick = u2 * total;
        for (size_t k = 0; k < weights.size(); ++k) {
            if (weights[k] <= 0.0) continue;
            kind_ = static_cast<Kind>(k + 1);  // (the last weighted one, should rounding leave some over)
            if (pick < weights[k]) break;
            pick -= weights[k];
        }
        // A reverse plays the step before: not until a whole step has come since the start (silence, else).
        if (kind_ == ReverseKind && static_cast<double>(recorded_) < length_ + 1.0) kind_ = None;
        if (kind_ == None) return;
        static constexpr std::array<int, 5> kRepeats = {2, 3, 4, 6, 8};
        static constexpr std::array<int, 3> kChops = {4, 6, 8};
        int parts = kind_ == GateKind ? kChops[static_cast<size_t>(u3) % kChops.size()]
                                      : kRepeats[static_cast<size_t>(u3) % kRepeats.size()];
        if (kind_ == StutterKind)
            while (parts > 2 && length_ / parts < 0.01 * sampleRate_) --parts;
        slice_ = std::max<int64_t>(1, static_cast<int64_t>(std::llround(length_ / parts)));
    }

    // What the step plays `t_` samples in.
    Voice voiceNow() const noexcept {
        const auto t = static_cast<double>(t_);
        switch (kind_) {
        case StutterKind: return {static_cast<double>((t_ / slice_) * slice_), 0.0};
        case ReverseKind: return {2.0 * t + 1.0, 2.0};
        case TapeKind:  // (stopped past its end, should the tempo slow during it)
            return t < length_ ? Voice{t * t / (2.0 * length_), t / length_} : Voice{t - length_ / 2.0, 1.0};
        default: return {};
        }
    }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = {
            {"grid", "Grid", "", 0.f, 5.f, 2.f, false, {"1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar"}},
            {"chance", "Chance", "%", 0.f, 100.f, 40.f},
            {"stutter", "Stutter", "%", 0.f, 100.f, 100.f},
            {"reverse", "Reverse", "%", 0.f, 100.f, 60.f},
            {"tape", "Tape", "%", 0.f, 100.f, 40.f},
            {"gate", "Gate", "%", 0.f, 100.f, 40.f},
            {"variation", "Variation", "", 0.f, 99.f, 0.f, false, {}, 99},
            {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
        };
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    dsp::DelayLine lines_[kChannels];
    int64_t step_ = INT64_MIN;  // the step playing, on the grid
    Kind kind_ = None;          // what it does
    int64_t t_ = 0;             // samples into it
    int64_t recorded_ = 0;      // samples come in since reset() (a reverse needs a step of them)
    double length_ = 1.0;       // its length in samples
    int64_t slice_ = 1;         // a stutter's repeat, or a gate's part, in samples
    double freeBeat_ = 0.0;     // the grid's own clock while the song isn't playing
    Voice voice_, old_;         // what is played, and what was before a jump (fading out)
    int fade_ = 0;              // samples of the crossfade from `old_` left
    float gain_ = 1.f;          // the gain `voice_` is played at
    float oldGain_ = 1.f;       // and `old_`
    float gateGain_ = 1.f;
    int fadeSamples_ = 96;
    int displayCount_ = 0;
    float peak_ = 0.f;
    SmoothedValue mix_;
};

}  // namespace

SUB_REGISTER_BUILTIN(GlitchProcessor, AudioEffect);

}  // namespace sub
