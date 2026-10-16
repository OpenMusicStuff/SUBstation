// Built-in "Auto Filter" device, after Ableton's: a resonant filter (Lowpass,
// Highpass, Bandpass, Notch or Morph; 12 or 24 dB/octave; five circuits, Clean
// and four that saturate) whose cutoff an envelope follower and an LFO move.
//
// The filter's maths (its sections, Q, the types' weights, the circuits'
// curves, how far the modulation moves the cutoff) is AutoFilterDesign.h's,
// shared with the editor. Here:
//
// - Drive (not on Clean) raises the level into the circuit's curve; what comes
//   out is lowered by half as much, so more Drive is more grit and a little
//   louder. The circuit's curve also squashes the band-pass state, so a ringing
//   resonance saturates as an analog filter's does.
// - The modulation runs in chunks of kChunk samples: the envelope follower runs
//   every sample on the input (or on the sidechain, when one is connected), the
//   LFO is moved to each chunk's end (free, or following the song's position
//   while it plays when synced), and each channel's cutoff glides (kCutoffGlide)
//   to where they put it, its tan() interpolated across the chunk.
// - The LFO's right channel is either Phase degrees ahead of the left, or (Spin)
//   runs Spin % faster. Offset turns both.
// - Type, Morph, Slope and Resonance glide (kGlideSeconds): a type's change
//   walks the sections' output weights, 24 dB fades the gentle first section in
//   (its input from the dry signal to its output) while the resonant one's Q
//   rises, so nothing clicks.
// - Then Output and Dry/Wet (ramped over 20 ms).
//
// Displays for its editor: `sweep_l` and `sweep_r` (how far the modulation
// moves each side's cutoff from Frequency, in octaves: the editor draws them
// from Frequency as it is now, so an edit shows at once), `env` (the
// envelope, 0..1) and `lfo` (the left LFO's value, -1..1), one value each per
// kDisplaySamples; and `input`, the input summed to mono, a value a sample (the
// editor's spectrum).

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "builtin/AutoFilterDesign.h"
#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

using autofilter::Circuit;
using autofilter::Type;
using autofilter::Weights;

class AutoFilterProcessor final : public BuiltinProcessor {
public:
    enum Param {
        FilterType = 0,
        FilterCircuit,
        Slope,
        Frequency,
        Resonance,
        Drive,
        Morph,
        EnvAmount,
        EnvAttack,
        EnvRelease,
        LfoAmount,
        LfoWave,
        LfoSync,
        LfoFreq,
        LfoRate,
        SpinOn,
        Phase,
        Spin,
        LfoOffset,
        Output,
        Mix,
        LfoMorph,
        LfoQuantize,
        LfoSteps,
        SoftClip,
        NumParams
    };
    enum Display { SweepL = 0, SweepR, EnvDisplay, LfoDisplay, InputDisplay };
    static constexpr int kDisplaySamples = 128;
    static constexpr int kChunk = 16;
    static constexpr double kGlideSeconds = 0.01;   // type, morph, slope and resonance
    static constexpr double kCutoffGlide = 0.0015;  // the modulated cutoff (S&H's steps rounded off)

    AutoFilterProcessor()
        : BuiltinProcessor(infos(), {{"sweep_l", kDisplaySamples},
                                     {"sweep_r", kDisplaySamples},
                                     {"env", kDisplaySamples},
                                     {"lfo", kDisplaySamples},
                                     {"input", 1}}) {}

    std::string typeId() const override { return "builtin:auto_filter"; }
    std::string name() const override { return "Auto Filter"; }
    bool hasSidechain() const override { return true; }
    int tailSamples() const override { return static_cast<int>(0.25 * sampleRate_); }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        drive_.reset(sampleRate, 0.02);
        output_.reset(sampleRate, 0.02);
        mix_.reset(sampleRate, 0.02);
        glide_ = static_cast<float>(1.0 - std::exp(-kChunk / (kGlideSeconds * sampleRate)));
        cutoffGlide_ = static_cast<float>(1.0 - std::exp(-kChunk / (kCutoffGlide * sampleRate)));
        reset();
    }

    void reset() override {
        for (auto& channel : channels_) channel = {};
        envelope_.reset();
        lfo_ = {};
        displayCount_ = 0;
        chunkLeft_ = 0;
        envValue_ = lfoValue_ = 0.f;
        const Targets t = targets();
        weights_ = t.weights;
        slopeMix_ = t.slope24 ? 1.f : 0.f;
        q_ = t.q;
        kB_ = static_cast<float>(1.0 / (q_ * (t.slope24 ? autofilter::kSecondSectionScale : 1.0)));
        drive_.snapTo(t.drive);
        output_.snapTo(t.output);
        mix_.snapTo(t.mix);
        // The cutoff where the modulation has it at rest.
        for (auto& channel : channels_) {
            channel.octaves = 0.f;
            channel.cutoff = autofilter::cutoffHz(t.frequency, 0.0, sampleRate_);
            channel.g = gOf(channel.cutoff);
            channel.dg = 0.f;
        }
    }

protected:
    void render(const ProcessContext& ctx, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        const Targets t = targets();
        envelope_.setTimes(param(EnvAttack), param(EnvRelease), sampleRate_);
        drive_.setTarget(t.drive);
        output_.setTarget(t.output);
        mix_.setTarget(t.mix);
        const bool keyed = sidechainConnected();
        const float* key[kChannels] = {keyed ? sidechain(0) : ch[0],
                                       keyed ? sidechain(n > 1 ? 1 : 0) : ch[n > 1 ? 1 : 0]};
        const float limit = autofilter::stateLimit(t.circuit);
        const bool driven = autofilter::hasDrive(t.circuit);

        for (int i = 0; i < numFrames;) {
            if (chunkLeft_ == 0) startChunk(ctx, t, i, n);
            const int len = std::min(chunkLeft_, numFrames - i);
            const auto w = weights_;
            const float kA = static_cast<float>(1.0 / autofilter::kFirstSectionQ);
            const float kB = kB_;
            const float slope = slopeMix_;
            for (const int end = i + len; i < end; ++i) {
                float level = 0.f;
                for (int c = 0; c < kChannels; ++c) {
                    const float v = key[c] ? key[c][i] : 0.f;
                    level = std::max(level, std::isfinite(v) ? std::abs(v) : 0.f);
                }
                envelope_.peak(level);
                const float drive = drive_.next();
                const float output = output_.next();
                const float mix = mix_.next();
                float mono = 0.f;
                for (int c = 0; c < n; ++c) {
                    Channel& s = channels_[static_cast<size_t>(c)];
                    float dry = ch[c][i];
                    if (!std::isfinite(dry)) dry = 0.f;
                    mono += dry;
                    s.g += s.dg;
                    float x = dry;
                    if (driven) x = autofilter::saturate(t.circuit, x * drive);
                    // 24 dB's gentle section, faded in.
                    if (slope > 0.f) {
                        const float a = tick(s.a, s.g, kA, w, x, t.circuit, limit);
                        x += slope * (a - x);
                    } else {
                        s.a.reset();
                    }
                    float y = tick(s.b, s.g, kB, w, x, t.circuit, limit);
                    if (driven) y /= std::sqrt(drive);
                    y *= output;
                    if (t.softClip) y = dsp::fastTanh(y);
                    ch[c][i] = dry + mix * (y - dry);
                }
                publish(InputDisplay, n > 1 ? 0.5f * mono : mono);
                if (++displayCount_ >= kDisplaySamples) {
                    displayCount_ = 0;
                    publish(SweepL, channels_[0].octaves);
                    publish(SweepR, channels_[n > 1 ? 1 : 0].octaves);
                    publish(EnvDisplay, envValue_);
                    publish(LfoDisplay, lfoValue_);
                }
            }
            chunkLeft_ -= len;
        }
        for (int c = 0; c < n; ++c) {
            channels_[static_cast<size_t>(c)].a.flush();
            channels_[static_cast<size_t>(c)].b.flush();
        }
    }

private:
    static constexpr int kChannels = 2;

    struct Channel {
        dsp::Svf a, b;  // 24 dB's gentle section, the resonant one
        float g = 0.f;   // tan(pi cutoff / sr), stepping across the chunk
        float dg = 0.f;  // its step a sample
        float octaves = 0.f;
        double cutoff = 1000.0;
    };

    struct LfoRun {
        dsp::Lfo lfo;
        double position = 0.0;  // synced: the song's position in cycles at the last chunk's end
        double offset = 0.0;    // the right side's lead, in cycles
        bool synced = false;
        int division = -1;
    };

    struct Targets {
        Circuit circuit = Circuit::Clean;
        Weights weights;
        bool slope24 = false;
        double frequency = 1000.0;
        double q = 0.7071;
        float drive = 1.f, output = 1.f, mix = 1.f;
        double envAmount = 0.0, lfoAmount = 0.0;
        bool lfoOn = false;
        dsp::LfoShape wave = dsp::LfoShape::Sine;
        double lfoMorph = 0.0;  // -1..1
        int lfoSteps = 0;       // 0: not quantized
        bool softClip = false;
    };

    Targets targets() const noexcept {
        Targets t;
        t.circuit = choice<Circuit>(FilterCircuit);
        t.weights = autofilter::weights(choice<Type>(FilterType), param(Morph));
        t.slope24 = choiceIndex(Slope) == 1;
        t.frequency = param(Frequency);
        t.q = autofilter::resonanceQ(param(Resonance));
        t.drive = autofilter::hasDrive(t.circuit) ? dbToGain(param(Drive)) : 1.f;
        t.output = dbToGain(param(Output));
        t.mix = param(Mix) / 100.f;
        t.envAmount = param(EnvAmount);
        t.lfoAmount = param(LfoAmount);
        t.lfoOn = t.lfoAmount > 0.0;
        t.wave = static_cast<dsp::LfoShape>(std::clamp(choiceIndex(LfoWave), 0, 6));
        t.lfoMorph = param(LfoMorph) / 100.0;
        t.lfoSteps = choiceIndex(LfoQuantize) == 1 ? choiceIndex(LfoSteps) : 0;
        t.softClip = isOn(SoftClip);
        return t;
    }

    // The LFO's value `offset` cycles on from where it is: its phase bent by Morph, its value quantized to
    // Steps levels when Quantize is on.
    float lfoValue(const Targets& t, double offset) const noexcept {
        const double at = lfo_.lfo.phase() + offset;
        const double whole = std::floor(at);
        const double phase = autofilter::morphPhase(at - whole, t.lfoMorph);
        const uint32_t cycle = lfo_.lfo.cycle() + static_cast<uint32_t>(static_cast<int64_t>(whole));
        return static_cast<float>(autofilter::quantizeLfo(dsp::Lfo::shape(t.wave, phase, cycle), t.lfoSteps));
    }

    float gOf(double cutoff) const noexcept {
        return static_cast<float>(std::tan(autofilter::kPi * cutoff / sampleRate_));
    }

    // A new chunk from frame `at` of the stretch: the envelope as it is, the LFO moved on to the chunk's
    // end, each channel's cutoff gliding there (its tan() stepping across the chunk), and the glides of
    // type, slope and resonance a step on.
    void startChunk(const ProcessContext& ctx, const Targets& t, int at, int n) noexcept {
        chunkLeft_ = kChunk;
        const double env = autofilter::envelopeValue(envelope_.value());
        stepLfo(ctx, at + kChunk, kChunk);
        const double offset = param(LfoOffset) / 360.0;
        const float lfoL = lfoValue(t, offset);
        const float lfoR = lfoValue(t, offset + lfo_.offset);
        for (int c = 0; c < n; ++c) {
            Channel& s = channels_[static_cast<size_t>(c)];
            const double octaves = autofilter::sweepOctaves(t.envAmount, env, t.lfoAmount, c == 0 ? lfoL : lfoR);
            s.octaves += cutoffGlide_ * (static_cast<float>(octaves) - s.octaves);
            s.cutoff = autofilter::cutoffHz(t.frequency, s.octaves, sampleRate_);
            s.dg = (gOf(s.cutoff) - s.g) / static_cast<float>(kChunk);
        }
        weights_.lp += glide_ * (t.weights.lp - weights_.lp);
        weights_.bp += glide_ * (t.weights.bp - weights_.bp);
        weights_.hp += glide_ * (t.weights.hp - weights_.hp);
        slopeMix_ += glide_ * ((t.slope24 ? 1.f : 0.f) - slopeMix_);
        q_ += glide_ * (t.q - q_);
        kB_ = static_cast<float>(
            1.0 / (q_ * (1.0 + (autofilter::kSecondSectionScale - 1.0) * static_cast<double>(slopeMix_))));
        envValue_ = static_cast<float>(env);
        lfoValue_ = t.lfoOn ? lfoL : 0.f;
    }

    // A sample through a section at g and damping k, its output weighted; the band-pass state squashed by
    // the circuit's curve over `limit` (none at 0).
    static float tick(dsp::Svf& svf, float g, float k, const Weights& w, float x, Circuit circuit,
                      float limit) noexcept {
        const dsp::SvfCoefficients coefficients(g, k);
        const dsp::Svf::Outputs out = svf.tick(coefficients, x);
        if (limit > 0.f) svf.ic1 = limit * autofilter::saturate(circuit, svf.ic1 / limit);
        const float band = k * out.band;
        const float high = x - band - out.low;
        return static_cast<float>(w.lp) * out.low + static_cast<float>(w.bp) * band + static_cast<float>(w.hp) * high;
    }

    // Moves the LFO on to `endFrame` (relative to the stretch), `frames` since the last step: synced while
    // the song plays, to the song's position; else at its rate. And the right side's lead: Spin's faster
    // cycles, or gliding to Phase the short way round.
    void stepLfo(const ProcessContext& ctx, int endFrame, int frames) noexcept {
        const bool sync = isOn(LfoSync);
        const int division = choiceIndex(LfoRate);
        const bool spinOn = isOn(SpinOn);
        const double spin = param(Spin) / 100.0;
        const bool synced = sync && ctx.playing && ctx.tempo > 0.0;
        double cycles = 0.0;
        if (synced) {
            const double samplesPerBeat = ctx.samplesPerBeat();
            const double beats = dsp::syncedCycleBeats(division);
            const double position = std::max(0.0, (ctx.beatPos + endFrame / samplesPerBeat) / beats);
            cycles = frames / samplesPerBeat / beats;
            lfo_.lfo.reset();
            lfo_.lfo.advance(position);
            lfo_.position = position;
            lfo_.division = division;
        } else {
            const double rateHz =
                sync ? (ctx.tempo > 0.0 ? ctx.tempo / 60.0 / dsp::syncedCycleBeats(division) : 1.0) : param(LfoFreq);
            cycles = rateHz * frames / sampleRate_;
            lfo_.lfo.advance(cycles);
        }
        lfo_.synced = synced;
        if (spinOn) {
            lfo_.offset += spin * cycles;
            lfo_.offset -= std::floor(lfo_.offset);
        } else {
            double d = param(Phase) / 360.0 - lfo_.offset;
            d -= std::floor(d + 0.5);  // the short way round
            lfo_.offset += std::abs(d) < 1e-9 ? d : glide_ * d;
        }
    }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            const std::vector<std::string>& divisions = dsp::syncedDivisionLabels();
            const auto lastDivision = static_cast<float>(divisions.size() - 1);
            const auto& offOn = offOnLabels();
            return std::vector<ParamInfo>{
                {"type", "Filter Type", "", 0.f, 4.f, 0.f, false, {"Lowpass", "Highpass", "Bandpass", "Notch", "Morph"}},
                {"circuit", "Circuit", "", 0.f, 4.f, 0.f, false, {"Clean", "OSR", "MS2", "SMP", "PRD"}},
                {"slope", "Slope", "", 0.f, 1.f, 0.f, false, {"12 dB", "24 dB"}},
                {"freq", "Frequency", "Hz", static_cast<float>(autofilter::kMinFrequency),
                 static_cast<float>(autofilter::kMaxFrequency), 2000.f, true},
                {"resonance", "Resonance", "%", 0.f, static_cast<float>(autofilter::kMaxResonance), 20.f},
                {"drive", "Drive", "dB", 0.f, 24.f, 0.f},
                {"morph", "Morph", "%", 0.f, 100.f, 0.f},
                {"env_amount", "Env Amount", "%", -100.f, 100.f, 0.f},
                {"env_attack", "Env Attack", "ms", 0.1f, 30.f, 6.f, true},
                {"env_release", "Env Release", "ms", 1.f, 3000.f, 200.f, true},
                {"lfo_amount", "LFO Amount", "%", 0.f, 100.f, 0.f},
                {"lfo_wave", "LFO Waveform", "", 0.f, 6.f, 0.f, false,
                 {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth"}},
                {"lfo_sync", "LFO Sync", "", 0.f, 1.f, 0.f, false, offOn},
                {"lfo_freq", "LFO Freq", "Hz", 0.01f, 40.f, 1.f, true},
                {"lfo_rate", "LFO Rate", "", 0.f, lastDivision, 9.f, false, divisions},  // 1/4
                {"spin_on", "Spin On", "", 0.f, 1.f, 0.f, false, offOn},
                {"phase", "Phase", "°", 0.f, 360.f, 0.f},
                {"spin", "Spin", "%", 0.f, 50.f, 0.f},
                {"lfo_offset", "LFO Offset", "°", 0.f, 360.f, 0.f},
                {"output", "Output", "dB", -24.f, 12.f, 0.f},
                {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
                {"lfo_morph", "LFO Morph", "%", -100.f, 100.f, 0.f},
                {"lfo_quantize", "LFO Quantize", "", 0.f, 1.f, 0.f, false, {"None", "Steps"}},
                {"lfo_steps", "LFO Steps", "", 2.f, 64.f, 8.f, false, {}, 62},
                {"soft_clip", "Soft Clip", "", 0.f, 1.f, 0.f, false, offOn},
            };
        }();
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    float glide_ = 0.1f, cutoffGlide_ = 0.5f;
    std::array<Channel, kChannels> channels_{};
    dsp::EnvelopeFollower envelope_;
    LfoRun lfo_;
    Weights weights_;
    float slopeMix_ = 0.f;
    double q_ = 0.7071;
    float kB_ = 1.4142f;  // the resonant section's damping
    int chunkLeft_ = 0;   // samples left in the chunk (0: a new one starts)
    float envValue_ = 0.f, lfoValue_ = 0.f;
    int displayCount_ = 0;
    SmoothedValue drive_, output_, mix_;
};

}  // namespace

SUB_REGISTER_BUILTIN(AutoFilterProcessor, AudioEffect);

}  // namespace sub
