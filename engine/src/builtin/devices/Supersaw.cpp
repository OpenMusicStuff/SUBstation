// Built-in "Supersaw": an instrument for stacks of detuned saws, as thick and
// as clean as they can be made. Up to three stacks a note, each of 1 to 64 saws
// (Saws, Detune, Curve, Blend, Width, Transpose, Fine, Level; SupersawDesign.h
// places them), their start phases and drift, a sub sine, a filter with an
// envelope of its own, an amplitude envelope, Poly, Mono or Legato with glide;
// 16 notes. Each voice follows its note's bend (MIDI 2.0's per-note pitch
// bend); MIDI pitch bend, the sustain pedal and all notes / sound off reach
// every note.
//
// How it stays clean:
// - A saw's phase is a 32-bit integer stepped by a 32-bit increment: exact,
//   wrapping by itself, and its value is the phase read as a signed number (a
//   rise from -1 to 1). A voice's saws are summed a control period at a time,
//   in loops the compiler vectorises.
// - Each jump gets a band-limited step's correction (SupersawDesign.h's
//   StepTable), placed to the fraction of a sample where it falls. It reaches
//   kStepHalf samples back, so a voice's oscillators (and its amplitude
//   envelope) run that far ahead of what it puts out: its accumulator, whose
//   first samples are final once the oscillators are past them.
// - Below 80 kHz the oscillators run at twice the rate, and a half-band
//   decimator (DspBlocks.h) takes the mix down: flat to 20 kHz, 120 dB down
//   where it would fold. Aliases end up more than 130 dB under each saw.
// - Controls (pitch, detune, the saws' gains and drift, the cutoff, the
//   envelopes' settings) are worked out every kControl oscillator samples on
//   a grid of the device's own time, not the blocks', so the output is the
//   same however the blocks are cut. Gains and the cutoff ramp sample by
//   sample between updates; pitches step, in phase.
// Its latency (the step's reach and the decimator's delay) is reported.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "NoteBend.h"
#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"
#include "builtin/SupersawDesign.h"
#include "rt/FastMath.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

using supersaw::kMaxSaws;
using supersaw::kStacks;
using supersaw::kStepHalf;
using supersaw::kStepTaps;

constexpr int kNotes = 16;     // notes sounding at once: a 17th steals one
constexpr int kVoices = 24;    // the notes, and room for those fading out after a steal
constexpr int kControl = 32;   // oscillator samples between control updates
constexpr int kHeldKeys = 32;  // the keys Mono and Legato remember, to go back to
constexpr float kVoiceGain = 0.25f;           // a note's stack (unit power) at full velocity
constexpr double kOversampleBelow = 80000.0;  // rates below this run the oscillators at twice the rate
constexpr double kPassbandHz = 20000.0;       // the decimator: flat to here (to 0.4535 of the rate at most),
constexpr double kDecimatorDb = 120.0;        // and this far down where it would fold back
constexpr double kGainSeconds = 0.005;        // the saws' gains, the sub's level and the volume glide this long
constexpr double kDetuneSeconds = 0.012;      // the saws' places glide this long when Detune or Curve moves
constexpr double kCutoffSeconds = 0.005;      // the filter's cutoff and resonance
constexpr double kVelocitySeconds = 0.002;    // a note's velocity changing on a voice that goes on (Mono, Legato)
constexpr double kKillSeconds = 0.004;        // a stolen voice fades out this long
constexpr double kDriftCents = 4.0;           // Drift 100 %: each saw wanders this far (its standard deviation)
constexpr double kDriftSeconds = 0.8;         // and this slowly (each of its two one-poles' time constant)
constexpr double kSleepSeconds = 0.25;        // silent this long, it skips the work until a note comes
constexpr double kMinCutoff = 16.0;           // Hz
constexpr double kMaxFraction = 0.45;         // of the oscillators' rate: the highest cutoff and saw
constexpr double kEnvOctaves = 10.0;          // Filter Env ±100 %: the cutoff moves this far at the envelope's peak
constexpr double kMaxQ = 12.0;                // Resonance 100 %
constexpr double kButterworthK = 1.4142135623730951;                           // a section's damping at Q 0.707
constexpr double kQuadK1 = 1.8477590650225735, kQuadK2 = 0.7653668647301796;  // the 4-pole Butterworth's two
constexpr float kSubLevel = 0.81649658f;      // Sub 100 %: a sine as loud (RMS) as a stack, sqrt(2/3)
constexpr int kDisplaySamples = 256;          // samples per value of the display `voices`
constexpr uint32_t kSeed = 0x53a9f1u;

enum StackParam { On = 0, Saws, Transpose, Fine, Detune, Curve, Blend, Width, Level, kStackParams };
enum Param {
    Phase = kStacks * kStackParams,
    Random,
    Drift,
    Sub,
    SubOctave,
    FilterType,
    Cutoff,
    Resonance,
    KeyTrack,
    FilterEnv,
    FilterAttack,
    FilterDecay,
    FilterSustain,
    FilterRelease,
    Attack,
    Decay,
    Sustain,
    Release,
    Velocity,
    Mode,
    Glide,
    BendRange,
    Volume,
    NumParams
};
constexpr int stackParam(int stack, StackParam p) { return stack * kStackParams + p; }

enum class FilterKind : uint8_t { Off, Low12, Low24, High12, High24, Band };
enum class VoiceMode : uint8_t { Poly, Mono, Legato };

const std::vector<ParamInfo>& infos() {
    static const std::vector<ParamInfo> kInfos = [] {
        std::vector<ParamInfo> list;
        struct StackDefaults {
            float on, saws, transpose, detune, width, level;
        };
        constexpr StackDefaults kDefaults[kStacks] = {
            {1.f, 7.f, 0.f, 45.f, 100.f, 0.f}, {0.f, 7.f, -12.f, 35.f, 70.f, -3.f}, {0.f, 5.f, 12.f, 30.f, 100.f, -6.f}};
        for (int s = 0; s < kStacks; ++s) {
            const std::string id = "s" + std::to_string(s + 1) + "_";
            const std::string name = "Stack " + std::to_string(s + 1);
            const StackDefaults& d = kDefaults[s];
            list.push_back({id + "on", name, "", 0.f, 1.f, d.on});
            list.push_back({id + "saws", name + " Saws", "#", 1.f, static_cast<float>(kMaxSaws), d.saws});
            list.push_back({id + "transpose", name + " Transpose", "st", -48.f, 48.f, d.transpose});
            list.push_back({id + "fine", name + " Fine", "ct", -100.f, 100.f, 0.f});
            list.push_back({id + "detune", name + " Detune", "%", 0.f, 100.f, d.detune});
            list.push_back({id + "curve", name + " Curve", "%", -100.f, 100.f, 35.f});
            list.push_back({id + "blend", name + " Blend", "%", 0.f, 100.f, 75.f});
            list.push_back({id + "width", name + " Width", "%", 0.f, 100.f, d.width});
            list.push_back({id + "level", name + " Level", "dB", -60.f, 6.f, d.level});
            list[static_cast<size_t>(stackParam(s, On))].valueLabels = BuiltinProcessor::offOnLabels();
            list[static_cast<size_t>(stackParam(s, Saws))].steps = kMaxSaws - 1;
            list[static_cast<size_t>(stackParam(s, Transpose))].steps = 96;
        }
        const std::vector<ParamInfo> rest = {
            {"phase", "Phase", "°", 0.f, 360.f, 0.f},
            {"random", "Random Phase", "%", 0.f, 100.f, 100.f},
            {"drift", "Drift", "%", 0.f, 100.f, 15.f},
            {"sub", "Sub", "%", 0.f, 100.f, 0.f},
            {"sub_octave", "Sub Octave", "", 0.f, 2.f, 1.f},
            {"filter_type", "Filter", "", 0.f, 5.f, 2.f},
            {"cutoff", "Cutoff", "Hz", 20.f, 20000.f, 20000.f, true},
            {"resonance", "Resonance", "%", 0.f, 100.f, 0.f},
            {"key_track", "Key Track", "%", 0.f, 100.f, 0.f},
            {"filter_env", "Filter Env", "%", -100.f, 100.f, 0.f},
            {"filter_attack", "Filter Attack", "ms", 1.f, 5000.f, 2.f, true},
            {"filter_decay", "Filter Decay", "ms", 1.f, 10000.f, 500.f, true},
            {"filter_sustain", "Filter Sustain", "%", 0.f, 100.f, 50.f},
            {"filter_release", "Filter Release", "ms", 1.f, 10000.f, 300.f, true},
            {"attack", "Attack", "ms", 1.f, 5000.f, 2.f, true},
            {"decay", "Decay", "ms", 1.f, 10000.f, 600.f, true},
            {"sustain", "Sustain", "%", 0.f, 100.f, 100.f},
            {"release", "Release", "ms", 1.f, 10000.f, 300.f, true},
            {"velocity", "Velocity", "%", 0.f, 100.f, 50.f},
            {"mode", "Mode", "", 0.f, 2.f, 0.f},
            {"glide", "Glide", "ms", 0.f, 2000.f, 0.f},
            {"bend_range", "Bend Range", "st", 0.f, 24.f, 2.f},
            {"volume", "Volume", "dB", -60.f, 6.f, 0.f},
        };
        list.insert(list.end(), rest.begin(), rest.end());
        list[SubOctave].valueLabels = {"0", "-1", "-2"};  // octaves under the note
        list[FilterType].valueLabels = {"Off", "Low 12", "Low 24", "High 12", "High 24", "Band"};
        list[Mode].valueLabels = {"Poly", "Mono", "Legato"};
        list[BendRange].steps = 24;
        return list;
    }();
    return kInfos;
}

// sin(2 pi phase / 2^32), the phase a 32-bit integer: within 6e-8.
inline double phaseSine(uint32_t phase) noexcept {
    double t = static_cast<double>(static_cast<int32_t>(phase)) * (1.0 / 2147483648.0);  // half cycles, -1..1
    if (t > 0.5) t = 1.0 - t;
    if (t < -0.5) t = -1.0 - t;
    const double x = dsp::kPi * t, x2 = x * x;
    return x * (1.0 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040 +
                                                                 x2 * (1.0 / 362880 + x2 * (-1.0 / 39916800))))));
}

// A gain gliding by one-pole steps, a control period at a time, and ramping
// linearly between: it is `value + q * step` at the period's sample q.
struct GainGlide {
    float value = 0.f;  // at the period's start
    float step = 0.f;   // per sample, through the period
    float next = 0.f;   // at its end

    void snap(float target) noexcept {
        value = next = target;
        step = 0.f;
    }
    // At a period's start: towards `target`, `coeff` of the way left after it.
    void update(float target, float coeff) noexcept {
        value = next;
        float to = target + (value - target) * coeff;
        if (std::abs(to - target) <= 1e-6f * std::max(1.f, std::abs(target))) to = target;
        next = to;
        step = (next - value) / static_cast<float>(kControl);
    }
    bool settled() const noexcept { return step == 0.f; }
    bool silent() const noexcept { return value == 0.f && next == 0.f; }
};

class SupersawProcessor final : public BuiltinProcessor {
public:
    SupersawProcessor() : BuiltinProcessor(infos(), {{"output", 1}, {"voices", kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:supersaw"; }
    std::string name() const override { return "Supersaw"; }
    bool acceptsMidi() const override { return true; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    int latencySamples() const override { return latency_; }
    int tailSamples() const override {
        return static_cast<int>(param(Release) * 0.001 * sampleRate_) + latency_ + 1;
    }

protected:
    // Writes (does not add) the synth's output: it is the first device on a MIDI track.
    void render(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) override;

private:
    enum class Stage : uint8_t { Attack, Decay, Release };  // Decay ends at, and holds, the sustain level

    // A voice's saws of one stack: their phases and increments (what the inner loops read), and drift.
    struct StackVoice {
        std::array<uint32_t, kMaxSaws> phase{};
        std::array<uint32_t, kMaxSaws> increment{};
        std::array<float, kMaxSaws> drift{};  // two one-poles on noise: the second, of unit variance at rest,
        std::array<float, kMaxSaws> driftIn{};  // and the first
        int count = 0;  // saws with a phase: those playing and those fading out
    };

    struct Voice {
        bool active = false;
        bool held = false;       // no note-off yet
        bool sustained = false;  // its note-off came while the pedal was down
        bool killed = false;     // stolen: fading out
        uint8_t key = 0;
        int32_t noteId = -1;
        uint64_t started = 0;
        float velocity = 1.f, velocityTarget = 1.f;
        // Pitch, in semitones: its key and the glide to it, its note's bend.
        double pitch = 60.0, glideTo = 60.0, glideStep = 0.0;
        int glideLeft = 0;  // oscillator samples
        double bend = 0.0, bendTarget = 0.0;
        // The envelopes: the amplitude's a sample at a time (as far ahead as the oscillators), the filter's a period.
        Stage amp = Stage::Attack, filt = Stage::Attack;
        float level = 0.f, filterLevel = 0.f;
        float kill = 1.f;
        uint32_t subPhase = 0, subIncrement = 0;
        std::array<StackVoice, kStacks> stacks;
        // The filter: g (the cutoff's) ramping through the period; each channel's two sections.
        float g = 0.f, gStep = 0.f, gNext = 0.f;
        std::array<std::array<dsp::Svf, 2>, 2> svf{};
        // What the oscillators made, from the period's start (final up to where they are, less kStepHalf; a jump
        // at the sample after them reaches a sample further), and the amplitude envelope (velocity and a steal's
        // fade in it) for each of those samples.
        std::array<std::array<float, kControl + kStepTaps + 1>, 2> acc{};
        std::array<float, kControl + kStepHalf> env{};
    };

    // What every voice's stack shares: its saws' gains (left and right, with the stack's level and switch in
    // them), their places (cents, and as frequency ratios), how many play, its pitch.
    struct Stack {
        std::array<GainGlide, kMaxSaws> left, right;
        std::array<float, kMaxSaws> leftTarget{}, rightTarget{};
        std::array<double, kMaxSaws> cents{}, centsTarget{}, ratio{};
        int playing = 0;     // saws that play (the shape's count; 0 while the stack is off)
        int count = 0;       // saws with state: those playing, and those still fading out
        bool ramping = false;  // some gain moves this period
        double pitch = 0.0;    // Transpose + Fine, semitones
        // What the targets were made from.
        supersaw::Shape shape;
        bool on = false;
        float levelDb = 0.f;
        bool made = false;
    };

    struct HeldKey {
        uint8_t key;
        int32_t noteId;
        float velocity;
    };

    // Notes and MIDI.
    void handle(const ProcessEvent& event);
    void noteOn(const ProcessEvent& event);
    void noteOff(const ProcessEvent& event);
    void noteBend(const ProcessEvent& event);
    void midi(const ProcessEvent& event);
    Voice* voiceOf(const ProcessEvent& event, bool held);
    Voice& allocate();
    void startVoice(Voice& voice, uint8_t key, int32_t noteId, float velocity, double fromPitch);
    void startSaws(Voice& voice, int stack, int from, int to);
    void glideTo(Voice& voice, double pitch, bool glide) const;
    void releaseVoice(Voice& voice) const;
    void releaseAll(bool kill);
    void followMode();
    float velocityGain(uint8_t velocity) const;

    // Control, a period at a time.
    void readSettings();
    void settle();
    void controlUpdate();
    void updateStacks();
    void advanceVoice(Voice& voice);
    void tuneVoice(Voice& voice);
    // Rendering.
    void run(int from, int to);
    void renderOscillators(Voice& voice, int q0, int n);
    void renderVoice(Voice& voice, int from, int n);
    void endPeriod();

    std::array<Voice, kVoices> voices_{};
    std::array<Stack, kStacks> stacks_{};
    std::array<HeldKey, kHeldKeys> heldKeys_{};
    int heldCount_ = 0;
    uint64_t noteCounter_ = 0;
    double lastPitch_ = -1.0;  // the last note's, for glides (-1: none yet)
    bool pedal_ = false;
    double wheel_ = 0.0, wheelTarget_ = 0.0;  // MIDI pitch bend, -1..1
    VoiceMode mode_ = VoiceMode::Poly;
    FilterKind filterKind_ = FilterKind::Off;

    double sampleRate_ = 48000.0;
    double oscRate_ = 96000.0;
    int factor_ = 2;
    int latency_ = 0;
    const supersaw::StepTable* steps_ = nullptr;
    std::array<dsp::HalfbandDecimator, 2> decimators_;
    std::array<std::vector<float>, 2> mix_;
    std::vector<float> spare_;  // the right channel's output when there is only one
    int controlPos_ = 0;        // where the device is in its control period
    bool periodStarted_ = false;  // its update made (at the period's start) and the oscillators run through it
    int64_t quiet_ = 0;         // oscillator samples rendered with no voice since the last one ended
    bool asleep_ = true;        // silent long enough: writes silence, skips the work
    int displayCount_ = 0;
    dsp::Noise noise_;

    // The settings as each period reads them, and what they were made from.
    struct Settings {
        float attackStep, decayCoeff, sustain, releaseCoeff;      // per oscillator sample
        float fAttackStep, fDecayCoeff, fSustain, fReleaseCoeff;  // per period
        FilterKind filter;
        double cutoffLog2, keyTrack, envOctaves, resonance;
        double drift;         // cents per unit of a saw's drift
        double glideSamples;  // oscillator samples
        double bendRange;
        int subOctave;
        float sub;
    } set_{};
    std::array<float, NumParams> read_{};  // the parameters set_ was made from
    bool readAny_ = false;
    double gainCoeff_ = 0.0, detuneCoeff_ = 0.0, cutoffCoeff_ = 0.0, bendCoeff_ = 0.0;
    double driftCoeff_ = 0.0, driftNoise_ = 0.0;
    float killStep_ = 0.f, velocityCoeff_ = 0.f;
    double cutoffLog2_ = 0.0, resonance_ = 0.0;  // gliding
    float k1_ = 1.f, k2_ = 1.f;                  // the filter's sections' damping (k2: the 24 dB's second)
    GainGlide sub_;
    SmoothedValue volume_;
};

void SupersawProcessor::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    factor_ = sampleRate < kOversampleBelow ? 2 : 1;
    oscRate_ = sampleRate * factor_;
    steps_ = &supersaw::stepTable();
    const int maxBlock = std::max(1, maxBlockSize);
    for (size_t c = 0; c < 2; ++c) {
        mix_[c].assign(static_cast<size_t>(maxBlock * factor_), 0.f);
        decimators_[c].design(std::min(kPassbandHz / sampleRate, 0.4535), kDecimatorDb, maxBlock);
    }
    spare_.assign(static_cast<size_t>(maxBlock), 0.f);
    latency_ = factor_ == 2 ? (kStepHalf + decimators_[0].delay()) / 2 : kStepHalf;

    const double period = kControl;
    gainCoeff_ = onePoleCoefficient(kGainSeconds, oscRate_, period);
    detuneCoeff_ = onePoleCoefficient(kDetuneSeconds, oscRate_, period);
    cutoffCoeff_ = onePoleCoefficient(kCutoffSeconds, oscRate_, period);
    bendCoeff_ = onePoleCoefficient(dsp::kBendGlideSeconds, oscRate_, period);
    velocityCoeff_ = static_cast<float>(1.0 - onePoleCoefficient(kVelocitySeconds, oscRate_));
    killStep_ = static_cast<float>(1.0 / (kKillSeconds * oscRate_));
    // Drift: two one-poles moving `a` of the way a period, on uniform noise scaled so the second has unit
    // variance at rest (two such one-poles in a row pass a^4 (1 + r^2) / (1 - r^2)^3 of a white input's).
    const double a = 1.0 - onePoleCoefficient(kDriftSeconds, oscRate_, period);
    const double r2 = (1.0 - a) * (1.0 - a);
    const double passed = a * a * a * a * (1.0 + r2) / ((1.0 - r2) * (1.0 - r2) * (1.0 - r2));
    driftCoeff_ = a;
    driftNoise_ = std::sqrt(3.0 / passed);  // uniform -1..1 has variance 1/3
    volume_.reset(sampleRate, 0.02);
    reset();
}

void SupersawProcessor::reset() {
    for (Voice& voice : voices_) voice.active = false;
    heldCount_ = 0;
    pedal_ = false;
    wheel_ = wheelTarget_ = 0.0;
    lastPitch_ = -1.0;
    noteCounter_ = 0;
    noise_.seed(kSeed);
    for (dsp::HalfbandDecimator& decimator : decimators_) decimator.reset();
    controlPos_ = 0;
    periodStarted_ = false;
    quiet_ = 0;
    asleep_ = true;
    displayCount_ = 0;
    settle();
}

// Every glide at its target, as if they had come to rest: a note after silence starts at the settings as they are.
void SupersawProcessor::settle() {
    readAny_ = false;
    readSettings();
    followMode();  // (nothing plays here to fade: a reset, or waking from sleep)
    for (Stack& stack : stacks_) {
        stack.count = stack.playing;
        for (size_t i = 0; i < static_cast<size_t>(kMaxSaws); ++i) {
            stack.left[i].snap(stack.leftTarget[i]);
            stack.right[i].snap(stack.rightTarget[i]);
            stack.cents[i] = stack.centsTarget[i];
            stack.ratio[i] = fastmath::exp2(stack.cents[i] / 1200.0);
        }
        stack.ramping = false;
    }
    cutoffLog2_ = set_.cutoffLog2;
    resonance_ = set_.resonance;
    filterKind_ = set_.filter;
    sub_.snap(set_.sub);
    wheel_ = wheelTarget_;
    volume_.snapTo(dbToGain(param(Volume)));
}

float SupersawProcessor::velocityGain(uint8_t velocity) const {
    const float sense = std::clamp(param(Velocity) / 100.f, 0.f, 1.f);
    return 1.f - sense + sense * static_cast<float>(velocity) / 127.f;
}

// --- Settings ----------------------------------------------------------------------------------

void SupersawProcessor::readSettings() {
    // Only what changed is worked out again (most periods, nothing).
    bool changed = !readAny_;
    for (int p = 0; p < NumParams; ++p) {
        const float value = param(p);
        if (value != read_[static_cast<size_t>(p)]) {
            read_[static_cast<size_t>(p)] = value;
            changed = true;
        }
    }
    if (!changed) return;
    readAny_ = true;

    const double samplesPerMs = oscRate_ * 0.001;
    const auto period = static_cast<float>(kControl);
    Settings& s = set_;
    s.attackStep = dsp::riseStep(param(Attack), samplesPerMs);
    s.decayCoeff = dsp::fallCoefficient(param(Decay), samplesPerMs);
    s.sustain = std::clamp(param(Sustain) / 100.f, 0.f, 1.f);
    s.releaseCoeff = dsp::fallCoefficient(param(Release), samplesPerMs);
    // The filter's envelope moves a period at a time: its step and factors for kControl samples.
    s.fAttackStep = period * dsp::riseStep(param(FilterAttack), samplesPerMs);
    s.fDecayCoeff = std::pow(dsp::fallCoefficient(param(FilterDecay), samplesPerMs), period);
    s.fSustain = std::clamp(param(FilterSustain) / 100.f, 0.f, 1.f);
    s.fReleaseCoeff = std::pow(dsp::fallCoefficient(param(FilterRelease), samplesPerMs), period);
    s.filter = static_cast<FilterKind>(std::clamp(choiceIndex(FilterType), 0, 5));
    s.cutoffLog2 = std::log2(std::clamp(static_cast<double>(param(Cutoff)), 20.0, 20000.0));
    s.keyTrack = std::clamp(param(KeyTrack) / 100.0, 0.0, 1.0);
    s.envOctaves = std::clamp(param(FilterEnv) / 100.0, -1.0, 1.0) * kEnvOctaves;
    s.resonance = std::clamp(param(Resonance) / 100.0, 0.0, 1.0);
    s.drift = std::clamp(param(Drift) / 100.0, 0.0, 1.0) * kDriftCents;
    s.glideSamples = std::max(0.0, static_cast<double>(param(Glide))) * samplesPerMs;
    s.bendRange = std::clamp(static_cast<double>(param(BendRange)), 0.0, 24.0);
    s.subOctave = std::clamp(choiceIndex(SubOctave), 0, 2);
    s.sub = std::clamp(param(Sub) / 100.f, 0.f, 1.f) * kSubLevel;

    // The stacks: each saw's place and gains (the stack's level and switch in them).
    for (int st = 0; st < kStacks; ++st) {
        Stack& stack = stacks_[static_cast<size_t>(st)];
        supersaw::Shape shape;
        shape.saws = choiceIndex(stackParam(st, Saws));
        shape.detune = param(stackParam(st, Detune));
        shape.curve = param(stackParam(st, Curve));
        shape.blend = param(stackParam(st, Blend));
        shape.width = param(stackParam(st, Width));
        const bool on = isOn(stackParam(st, On));
        const float levelDb = param(stackParam(st, Level));
        stack.pitch = choiceIndex(stackParam(st, Transpose)) + param(stackParam(st, Fine)) / 100.0;
        if (stack.made && on == stack.on && levelDb == stack.levelDb && shape.saws == stack.shape.saws &&
            shape.detune == stack.shape.detune && shape.curve == stack.shape.curve &&
            shape.blend == stack.shape.blend && shape.width == stack.shape.width)
            continue;
        stack.made = true;
        stack.shape = shape;
        stack.on = on;
        stack.levelDb = levelDb;
        std::array<supersaw::Saw, kMaxSaws> saws;
        const int n = supersaw::layout(shape, saws.data());
        const double level = on ? dbToGain(levelDb) : 0.0;
        for (int j = 0; j < kMaxSaws; ++j) {
            const auto i = static_cast<size_t>(j);
            double left = 0.0, right = 0.0;
            if (j < n) {
                supersaw::panGains(saws[i].pan, left, right);
                stack.centsTarget[i] = saws[i].cents;  // (a saw that stops keeps its place as it fades)
            }
            stack.leftTarget[i] = static_cast<float>(j < n ? left * saws[i].gain * level : 0.0);
            stack.rightTarget[i] = static_cast<float>(j < n ? right * saws[i].gain * level : 0.0);
        }
        stack.playing = on ? n : 0;
    }
}

// --- Notes ---------------------------------------------------------------------------------------

void SupersawProcessor::handle(const ProcessEvent& event) {
    if (event.startsNote()) {
        noteOn(event);
    } else if (event.endsNote()) {
        noteOff(event);
    } else if (event.type == ProcessEvent::Type::NoteBend) {
        noteBend(event);
    } else if (event.type == ProcessEvent::Type::Midi) {
        midi(event);
    }
}

SupersawProcessor::Voice& SupersawProcessor::allocate() {
    // A note beyond kNotes steals one (the quietest releasing, else the oldest), which fades out meanwhile.
    int sounding = 0;
    for (const Voice& voice : voices_) sounding += voice.active && !voice.killed ? 1 : 0;
    if (sounding >= kNotes) {
        Voice* victim = nullptr;
        for (Voice& voice : voices_) {
            if (!voice.active || voice.killed || voice.amp != Stage::Release) continue;
            if (!victim || voice.level < victim->level) victim = &voice;
        }
        if (!victim) {
            for (Voice& voice : voices_) {
                if (voice.active && !voice.killed && (!victim || voice.started < victim->started)) victim = &voice;
            }
        }
        if (victim) victim->killed = true;
    }
    for (Voice& voice : voices_) {
        if (!voice.active) return voice;
    }
    // No room even so (many fading at once): the faintest goes now.
    Voice* faintest = &voices_[0];
    for (Voice& voice : voices_) {
        if (voice.kill * voice.level < faintest->kill * faintest->level) faintest = &voice;
    }
    return *faintest;
}

void SupersawProcessor::startSaws(Voice& voice, int stack, int from, int to) {
    // Each saw starts at Phase, plus as much of a random one as Random Phase says, and somewhere along its drift.
    StackVoice& sv = voice.stacks[static_cast<size_t>(stack)];
    const double start = std::clamp(param(Phase) / 360.0, 0.0, 1.0);
    const double random = std::clamp(param(Random) / 100.0, 0.0, 1.0);
    constexpr float kRootThree = 1.7320508f;
    for (int j = from; j < to; ++j) {
        const auto i = static_cast<size_t>(j);
        const double cycles = start + random * 0.5 * (noise_.next() + 1.0);
        sv.phase[i] = static_cast<uint32_t>(static_cast<uint64_t>((cycles - std::floor(cycles)) * 4294967296.0));
        sv.drift[i] = sv.driftIn[i] = kRootThree * noise_.next();
        sv.increment[i] = 0;
    }
}

void SupersawProcessor::startVoice(Voice& voice, uint8_t key, int32_t noteId, float velocity, double fromPitch) {
    voice.active = true;
    voice.held = true;
    voice.sustained = false;
    voice.killed = false;
    voice.kill = 1.f;
    voice.key = key;
    voice.noteId = noteId;
    voice.started = ++noteCounter_;
    voice.velocity = voice.velocityTarget = velocity;
    voice.bend = voice.bendTarget = 0.0;
    voice.pitch = fromPitch >= 0.0 ? fromPitch : key;
    glideTo(voice, key, true);
    voice.amp = voice.filt = Stage::Attack;
    voice.level = voice.filterLevel = 0.f;
    voice.subPhase = 0;
    for (auto& channel : voice.svf)
        for (dsp::Svf& section : channel) section.reset();
    for (auto& channel : voice.acc) channel.fill(0.f);
    voice.env.fill(0.f);
    for (int s = 0; s < kStacks; ++s) {
        const int count = stacks_[static_cast<size_t>(s)].count;
        voice.stacks[static_cast<size_t>(s)].count = count;
        startSaws(voice, s, 0, count);
    }
    // Its increments and cutoff for the rest of this period (the filter holding still through it), and its
    // oscillators through it; at a period's start, its update does that.
    tuneVoice(voice);
    voice.g = voice.gNext;
    voice.gStep = 0.f;
    if (periodStarted_) renderOscillators(voice, controlPos_, kControl - controlPos_);
}

void SupersawProcessor::glideTo(Voice& voice, double pitch, bool glide) const {
    voice.glideTo = pitch;
    if (!glide || set_.glideSamples < 1.0 || pitch == voice.pitch) {
        voice.pitch = pitch;
        voice.glideLeft = 0;
        return;
    }
    voice.glideLeft = static_cast<int>(std::lround(set_.glideSamples));
    voice.glideStep = (pitch - voice.pitch) / voice.glideLeft;
}

void SupersawProcessor::noteOn(const ProcessEvent& event) {
    followMode();  // (changed since the period's update: the note plays in the new one)
    const float velocity = velocityGain(event.velocity());
    if (mode_ != VoiceMode::Poly) {
        // Remember the key (the newest last), to go back to.
        if (heldCount_ == kHeldKeys) {
            std::copy(heldKeys_.begin() + 1, heldKeys_.end(), heldKeys_.begin());
            --heldCount_;
        }
        const bool overlapping = heldCount_ > 0;
        heldKeys_[static_cast<size_t>(heldCount_++)] = {event.key(), event.noteId, velocity};
        Voice* mono = nullptr;
        for (Voice& voice : voices_) {
            if (voice.active && !voice.killed && (!mono || voice.started > mono->started)) mono = &voice;
        }
        if (mono) {
            // The voice moves to the new key, its saws going on: Mono strikes its envelopes again (from where they
            // are) and glides; Legato does only if no key was held, and glides only from one that was.
            const bool strike = mode_ == VoiceMode::Mono || !overlapping;
            mono->key = event.key();
            mono->noteId = event.noteId;
            mono->held = true;
            mono->sustained = false;
            mono->bendTarget = 0.0;
            glideTo(*mono, event.key(), mode_ == VoiceMode::Mono || overlapping);
            if (strike) {
                mono->amp = mono->filt = Stage::Attack;
                mono->velocityTarget = velocity;
            }
            lastPitch_ = event.key();
            return;
        }
    }
    Voice& voice = allocate();
    startVoice(voice, event.key(), event.noteId, velocity, lastPitch_);
    lastPitch_ = event.key();
}

SupersawProcessor::Voice* SupersawProcessor::voiceOf(const ProcessEvent& event, bool held) {
    if (event.noteId >= 0) {
        for (Voice& voice : voices_) {
            if (voice.active && !voice.killed && voice.noteId == event.noteId && (!held || voice.held)) return &voice;
        }
    }
    // With the same key held twice, the older note ends first; a bend goes to the newest.
    Voice* target = nullptr;
    for (Voice& voice : voices_) {
        if (!voice.active || voice.killed || voice.key != event.key() || (held && !voice.held)) continue;
        if (event.noteId >= 0 && voice.noteId >= 0) continue;  // another note of that key
        if (!target || (held ? voice.started < target->started : voice.started > target->started)) target = &voice;
    }
    return target;
}

void SupersawProcessor::releaseVoice(Voice& voice) const {
    voice.held = false;
    voice.sustained = pedal_;
    if (!pedal_) voice.amp = voice.filt = Stage::Release;
}

void SupersawProcessor::noteOff(const ProcessEvent& event) {
    followMode();
    if (mode_ == VoiceMode::Poly) {
        if (Voice* voice = voiceOf(event, true)) releaseVoice(*voice);
        return;
    }
    // Forget the key; if it was the one playing and others are held, go back to the newest of them.
    int found = -1;
    for (int i = heldCount_ - 1; i >= 0 && found < 0; --i) {
        const HeldKey& held = heldKeys_[static_cast<size_t>(i)];
        if (held.key == event.key() && (event.noteId < 0 || held.noteId < 0 || held.noteId == event.noteId)) found = i;
    }
    if (found < 0) return;
    std::copy(heldKeys_.begin() + found + 1, heldKeys_.begin() + heldCount_, heldKeys_.begin() + found);
    --heldCount_;
    Voice* mono = voiceOf(event, true);
    if (!mono) return;
    if (heldCount_ > 0) {
        const HeldKey& back = heldKeys_[static_cast<size_t>(heldCount_ - 1)];
        mono->key = back.key;
        mono->noteId = back.noteId;
        mono->bendTarget = 0.0;
        glideTo(*mono, back.key, true);
        lastPitch_ = back.key;
        return;
    }
    releaseVoice(*mono);
}

void SupersawProcessor::noteBend(const ProcessEvent& event) {
    if (Voice* voice = voiceOf(event, false)) {
        voice->bendTarget = std::clamp(static_cast<double>(event.bend), -kMaxBendSemitones, kMaxBendSemitones);
    }
}

void SupersawProcessor::releaseAll(bool kill) {
    heldCount_ = 0;
    for (Voice& voice : voices_) {
        if (!voice.active) continue;
        if (kill) {
            voice.killed = true;
        } else {
            voice.held = voice.sustained = false;
            voice.amp = voice.filt = Stage::Release;
        }
    }
}

// The Mode as it is now: another fades out what plays, and forgets the keys held. Taken at each period's update
// and at each note (a change since the update), so a note never starts in a mode that then fades it out.
void SupersawProcessor::followMode() {
    const auto mode = static_cast<VoiceMode>(std::clamp(choiceIndex(Mode), 0, 2));
    if (mode == mode_) return;
    mode_ = mode;
    releaseAll(true);
}

void SupersawProcessor::midi(const ProcessEvent& event) {
    const uint8_t status = event.data[0] & 0xF0;
    if (status == 0xE0) {  // pitch bend: 14 bits, 8192 in the middle
        const int value = (event.data[2] & 0x7F) << 7 | (event.data[1] & 0x7F);
        wheelTarget_ = std::clamp((value - 8192) / 8192.0, -1.0, 1.0);
    } else if (status == 0xB0) {
        const uint8_t controller = event.data[1], value = event.data[2];
        if (controller == 64) {  // sustain
            pedal_ = value >= 64;
            if (!pedal_) {
                for (Voice& voice : voices_) {
                    if (voice.active && voice.sustained) {
                        voice.sustained = false;
                        voice.amp = voice.filt = Stage::Release;
                    }
                }
            }
        } else if (controller == 120) {  // all sound off
            releaseAll(true);
        } else if (controller == 123) {  // all notes off
            pedal_ = false;
            releaseAll(false);
        }
    }
}

// --- Control ---------------------------------------------------------------------------------

void SupersawProcessor::updateStacks() {
    for (int st = 0; st < kStacks; ++st) {
        Stack& stack = stacks_[static_cast<size_t>(st)];
        // Saws that start playing take a phase in every voice; saws that stop fade out first.
        if (stack.playing > stack.count) {
            for (Voice& voice : voices_) {
                StackVoice& sv = voice.stacks[static_cast<size_t>(st)];
                if (!voice.active || sv.count >= stack.playing) continue;
                startSaws(voice, st, sv.count, stack.playing);
                sv.count = stack.playing;
            }
            stack.count = stack.playing;
        }
        bool ramping = false;
        int last = 0;  // past the last saw still to render
        for (int j = 0; j < stack.count; ++j) {
            const auto i = static_cast<size_t>(j);
            stack.left[i].update(stack.leftTarget[i], static_cast<float>(gainCoeff_));
            stack.right[i].update(stack.rightTarget[i], static_cast<float>(gainCoeff_));
            ramping = ramping || !stack.left[i].settled() || !stack.right[i].settled();
            if (j < stack.playing || !stack.left[i].silent() || !stack.right[i].silent()) last = j + 1;
            if (j < stack.playing && stack.cents[i] != stack.centsTarget[i]) {
                double to = stack.centsTarget[i] + (stack.cents[i] - stack.centsTarget[i]) * detuneCoeff_;
                if (std::abs(to - stack.centsTarget[i]) < 1e-4) to = stack.centsTarget[i];
                stack.cents[i] = to;
                stack.ratio[i] = fastmath::exp2(to / 1200.0);
            }
        }
        stack.ramping = ramping;
        if (last < stack.count) {  // those faded out to silence stop
            stack.count = last;
            for (Voice& voice : voices_) {
                StackVoice& sv = voice.stacks[static_cast<size_t>(st)];
                sv.count = std::min(sv.count, last);
            }
        }
    }
}

// A period on: the voice's glide, its note's bend, its filter's envelope.
void SupersawProcessor::advanceVoice(Voice& voice) {
    if (voice.glideLeft > 0) {
        const int n = std::min(voice.glideLeft, kControl);
        voice.glideLeft -= n;
        voice.pitch = voice.glideLeft == 0 ? voice.glideTo : voice.pitch + voice.glideStep * n;
    }
    if (voice.bend != voice.bendTarget) {
        voice.bend = voice.bendTarget + (voice.bend - voice.bendTarget) * bendCoeff_;
        if (std::abs(voice.bend - voice.bendTarget) < 1e-6) voice.bend = voice.bendTarget;
    }
    switch (voice.filt) {
        case Stage::Attack:
            voice.filterLevel += set_.fAttackStep;
            if (voice.filterLevel >= 1.f) {
                voice.filterLevel = 1.f;
                voice.filt = Stage::Decay;
            }
            break;
        case Stage::Decay:
            voice.filterLevel = set_.fSustain + (voice.filterLevel - set_.fSustain) * set_.fDecayCoeff;
            break;
        case Stage::Release: voice.filterLevel *= set_.fReleaseCoeff; break;
    }
    // Each saw's own slow wander: two one-poles on noise.
    if (set_.drift > 0.0) {
        const auto a = static_cast<float>(driftCoeff_), scale = static_cast<float>(driftNoise_);
        for (StackVoice& sv : voice.stacks) {
            for (size_t i = 0; i < static_cast<size_t>(sv.count); ++i) {
                sv.driftIn[i] += a * (scale * noise_.next() - sv.driftIn[i]);
                sv.drift[i] += a * (sv.driftIn[i] - sv.drift[i]);
            }
        }
    }
}

// The voice's saws' increments, its sub's, and its cutoff (g) at the period's end.
void SupersawProcessor::tuneVoice(Voice& voice) {
    const double pitch = voice.pitch + voice.bend + wheel_ * set_.bendRange;
    constexpr double kMaxIncrement = kMaxFraction * 4294967296.0;
    const double perHz = 4294967296.0 / oscRate_;
    const double drift = set_.drift * (0.69314718055994531 / 1200.0);  // a unit of drift, in nepers of frequency
    for (int s = 0; s < kStacks; ++s) {
        const Stack& stack = stacks_[static_cast<size_t>(s)];
        StackVoice& sv = voice.stacks[static_cast<size_t>(s)];
        if (sv.count == 0) continue;
        const double base = 440.0 * fastmath::exp2((pitch + stack.pitch - 69.0) / 12.0) * perHz;
        for (size_t i = 0; i < static_cast<size_t>(sv.count); ++i) {
            const double x = sv.drift[i] * drift;  // the drift's ratio, e^x to second order (within 1e-7)
            const double ratio = stack.ratio[i] * (1.0 + x * (1.0 + 0.5 * x));
            sv.increment[i] = static_cast<uint32_t>(std::min(base * ratio, kMaxIncrement) + 0.5);  // the nearest
        }
    }
    const double subHz = 440.0 * fastmath::exp2((pitch - 12.0 * set_.subOctave - 69.0) / 12.0);
    voice.subIncrement = static_cast<uint32_t>(std::min(subHz * perHz, kMaxIncrement) + 0.5);

    const double octaves = set_.keyTrack * (pitch - 60.0) / 12.0 + set_.envOctaves * voice.filterLevel;
    const double hz = std::clamp(fastmath::exp2(cutoffLog2_ + octaves), kMinCutoff, kMaxFraction * oscRate_);
    double sine = 0.0, cosine = 1.0;
    fastmath::sincos(dsp::kPi * hz / oscRate_, sine, cosine);
    voice.g = voice.gNext;
    voice.gNext = static_cast<float>(sine / cosine);
    voice.gStep = (voice.gNext - voice.g) / static_cast<float>(kControl);
}

void SupersawProcessor::controlUpdate() {
    readSettings();
    followMode();
    if (set_.filter != filterKind_) {  // another filter: the sections start again from rest
        filterKind_ = set_.filter;
        for (Voice& voice : voices_)
            for (auto& channel : voice.svf)
                for (dsp::Svf& section : channel) section.reset();
    }
    // The shared glides: the cutoff and resonance, the sub's level, the wheel.
    cutoffLog2_ = set_.cutoffLog2 + (cutoffLog2_ - set_.cutoffLog2) * cutoffCoeff_;
    if (std::abs(cutoffLog2_ - set_.cutoffLog2) < 1e-6) cutoffLog2_ = set_.cutoffLog2;
    if (resonance_ != set_.resonance) {
        resonance_ = set_.resonance + (resonance_ - set_.resonance) * cutoffCoeff_;
        if (std::abs(resonance_ - set_.resonance) < 1e-6) resonance_ = set_.resonance;
    }
    // Resonance raises Q from Butterworth's up to kMaxQ (in the 24 dB's second section).
    const bool quad = set_.filter == FilterKind::Low24 || set_.filter == FilterKind::High24;
    const double baseK = quad ? kQuadK2 : kButterworthK;
    const auto resonantK = static_cast<float>(baseK * fastmath::exp2(-resonance_ * std::log2(kMaxQ * baseK)));
    k1_ = quad ? static_cast<float>(kQuadK1) : resonantK;
    k2_ = resonantK;
    sub_.update(set_.sub, static_cast<float>(gainCoeff_));
    if (wheel_ != wheelTarget_) {
        wheel_ = wheelTarget_ + (wheel_ - wheelTarget_) * bendCoeff_;
        if (std::abs(wheel_ - wheelTarget_) < 1e-7) wheel_ = wheelTarget_;
    }
    updateStacks();
    for (Voice& voice : voices_) {
        if (!voice.active) continue;
        advanceVoice(voice);
        tuneVoice(voice);
        renderOscillators(voice, controlPos_, kControl - controlPos_);
    }
    periodStarted_ = true;
}

// --- Rendering ---------------------------------------------------------------------------------

// The voice's oscillators over the period's samples q0 .. q0 + n (to its end, from its start or where the voice
// started), into its accumulator: everything they do in a period is known at its start, so they run it at once,
// and the accumulator's sums come out the same however the period's samples are cut into blocks.
void SupersawProcessor::renderOscillators(Voice& voice, int q0, int n) {
    float* accL = voice.acc[0].data();
    float* accR = voice.acc[1].data();
    constexpr float kScale = 1.f / 2147483648.f;  // a phase read as a signed number, to -1..1

    // The saws: their plain rise into the accumulator kStepHalf on, each jump's correction around it.
    float* left = accL + q0 + kStepHalf;
    float* right = accR + q0 + kStepHalf;
    for (int s = 0; s < kStacks; ++s) {
        const Stack& stack = stacks_[static_cast<size_t>(s)];
        StackVoice& sv = voice.stacks[static_cast<size_t>(s)];
        for (size_t i = 0; i < static_cast<size_t>(sv.count); ++i) {
            const uint32_t phase = sv.phase[i], increment = sv.increment[i];
            const uint32_t rise = phase + 0x80000000u;  // read as signed: -1 just after the jump
            const GainGlide& gl = stack.left[i];
            const GainGlide& gr = stack.right[i];
            if (stack.ramping) {
                // The gain at the period's sample q0 + k, worked out from the period's start (so the same however
                // the period is cut).
                const float la = gl.value * kScale, ra = gr.value * kScale, ls = gl.step * kScale, rs = gr.step * kScale;
                for (int k = 0; k < n; ++k) {
                    const auto v = static_cast<float>(static_cast<int32_t>(rise + static_cast<uint32_t>(k) * increment));
                    const auto t = static_cast<float>(q0 + k);
                    left[k] += (la + t * ls) * v;
                    right[k] += (ra + t * rs) * v;
                }
            } else {
                const float la = gl.value * kScale, ra = gr.value * kScale;
                for (int k = 0; k < n; ++k) {
                    const auto v = static_cast<float>(static_cast<int32_t>(rise + static_cast<uint32_t>(k) * increment));
                    left[k] += la * v;
                    right[k] += ra * v;
                }
            }
            const uint64_t end = static_cast<uint64_t>(phase) + static_cast<uint64_t>(n) * increment;
            sv.phase[i] = static_cast<uint32_t>(end);
            // Each jump (a drop of 2): sample m is the first after it (m = n: the next part's first), `fraction`
            // of a sample after it.
            for (uint64_t wrap = 1; wrap <= end >> 32; ++wrap) {
                const uint64_t m = ((wrap << 32) - phase + increment - 1) / increment;
                const uint64_t after = static_cast<uint64_t>(phase) + m * increment - (wrap << 32);
                const int at = q0 + static_cast<int>(m);
                float correction[kStepTaps];
                steps_->correction(static_cast<float>(static_cast<double>(after) / increment), correction);
                const auto atF = static_cast<float>(at);
                const float dropL = -2.f * (gl.value + atF * gl.step), dropR = -2.f * (gr.value + atF * gr.step);
                float* l = accL + at;
                float* r = accR + at;
                for (int k = 0; k < kStepTaps; ++k) {
                    l[k] += dropL * correction[k];
                    r[k] += dropR * correction[k];
                }
            }
        }
    }

    // The sub: a sine (no jumps), in the middle.
    if (!sub_.silent()) {
        uint32_t phase = voice.subPhase;
        for (int k = 0; k < n; ++k, phase += voice.subIncrement) {
            const float gain = sub_.value + static_cast<float>(q0 + k) * sub_.step;
            const float v = static_cast<float>(phaseSine(phase)) * gain;
            left[k] += v;
            right[k] += v;
        }
        voice.subPhase = phase;
    } else {
        voice.subPhase += static_cast<uint32_t>(n) * voice.subIncrement;
    }
}

// A part of the period, from the period's sample controlPos_ for n samples (the mix's from on): the amplitude
// envelope, as far ahead as the oscillators; and what is final, through the filter and the envelope, into the mix.
void SupersawProcessor::renderVoice(Voice& voice, int from, int n) {
    const int q0 = controlPos_;
    const float* accL = voice.acc[0].data();
    const float* accR = voice.acc[1].data();
    float* env = voice.env.data() + q0 + kStepHalf;
    for (int k = 0; k < n; ++k) {
        switch (voice.amp) {
            case Stage::Attack:
                voice.level += set_.attackStep;
                if (voice.level >= 1.f) {
                    voice.level = 1.f;
                    voice.amp = Stage::Decay;
                }
                break;
            case Stage::Decay: voice.level = set_.sustain + (voice.level - set_.sustain) * set_.decayCoeff; break;
            case Stage::Release: voice.level *= set_.releaseCoeff; break;
        }
        voice.velocity += (voice.velocityTarget - voice.velocity) * velocityCoeff_;
        if (voice.killed) voice.kill = std::max(0.f, voice.kill - killStep_);
        env[k] = voice.level * voice.velocity * voice.kill * kVoiceGain;
    }

    // What is final (kStepHalf behind the oscillators), through the filter and the envelope, into the mix.
    float* mixL = mix_[0].data() + from;
    float* mixR = mix_[1].data() + from;
    const float* gain = voice.env.data() + q0;
    const float* inL = accL + q0;
    const float* inR = accR + q0;
    const FilterKind filter = filterKind_;
    if (filter == FilterKind::Off) {
        for (int k = 0; k < n; ++k) {
            mixL[k] += inL[k] * gain[k];
            mixR[k] += inR[k] * gain[k];
        }
        return;
    }
    const float k1 = k1_, k2 = k2_;
    auto& svfL = voice.svf[0];
    auto& svfR = voice.svf[1];
    for (int k = 0; k < n; ++k) {
        const float g = voice.g + static_cast<float>(q0 + k) * voice.gStep;
        const dsp::SvfCoefficients c1(g, k1);
        const float xl = inL[k], xr = inR[k];
        const dsp::Svf::Outputs al = svfL[0].tick(c1, xl), ar = svfR[0].tick(c1, xr);
        float yl = 0.f, yr = 0.f;
        switch (filter) {
            case FilterKind::Low12: yl = al.low, yr = ar.low; break;
            case FilterKind::High12: yl = xl - k1 * al.band - al.low, yr = xr - k1 * ar.band - ar.low; break;
            case FilterKind::Band: yl = k1 * al.band, yr = k1 * ar.band; break;  // unity at its centre
            case FilterKind::Low24: {
                const dsp::SvfCoefficients c2(g, k2);
                yl = svfL[1].tick(c2, al.low).low;
                yr = svfR[1].tick(c2, ar.low).low;
                break;
            }
            case FilterKind::High24: {
                const dsp::SvfCoefficients c2(g, k2);
                const float hl = xl - k1 * al.band - al.low, hr = xr - k1 * ar.band - ar.low;
                const dsp::Svf::Outputs bl = svfL[1].tick(c2, hl), br = svfR[1].tick(c2, hr);
                yl = hl - k2 * bl.band - bl.low;
                yr = hr - k2 * br.band - br.low;
                break;
            }
            case FilterKind::Off: break;
        }
        mixL[k] += yl * gain[k];
        mixR[k] += yr * gain[k];
    }
}

void SupersawProcessor::endPeriod() {
    for (Voice& voice : voices_) {
        if (!voice.active) continue;
        // The next period starts with what reaches past this one.
        for (auto& channel : voice.acc) {
            std::copy(channel.begin() + kControl, channel.end(), channel.begin());
            std::fill(channel.end() - kControl, channel.end(), 0.f);
        }
        std::copy(voice.env.begin() + kControl, voice.env.end(), voice.env.begin());
        std::fill(voice.env.end() - kControl, voice.env.end(), 0.f);
        for (auto& channel : voice.svf)
            for (dsp::Svf& section : channel) section.flush();
        const bool fading = voice.amp == Stage::Release || (voice.amp == Stage::Decay && set_.sustain < dsp::kSilent);
        if ((fading && voice.level < dsp::kSilent) || (voice.killed && voice.kill <= 0.f)) voice.active = false;
    }
}

void SupersawProcessor::run(int from, int to) {
    while (from < to) {
        if (!periodStarted_) controlUpdate();
        const int n = std::min(to - from, kControl - controlPos_);
        for (Voice& voice : voices_) {
            if (voice.active) renderVoice(voice, from, n);
        }
        from += n;
        controlPos_ += n;
        if (controlPos_ == kControl) {
            endPeriod();
            controlPos_ = 0;
            periodStarted_ = false;
        }
    }
}

void SupersawProcessor::render(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) {
    const int frames = std::min(numFrames, static_cast<int>(spare_.size()));  // prepare() said no more
    if (asleep_ && ctx.inEvents.count == 0) {
        // Silent a while, no note: the work skipped (the grid goes on). MIDI messages still count.
        controlPos_ = static_cast<int>((controlPos_ + static_cast<int64_t>(numFrames) * factor_) % kControl);
        for (int c = 0; c < std::min(numChannels, 2); ++c) std::fill_n(channels[c], numFrames, 0.f);
        return;
    }
    if (asleep_) {
        // As everything would have come to rest meanwhile: settled, the period's update as good as made.
        settle();
        periodStarted_ = controlPos_ != 0;
        asleep_ = false;
    }

    const int oscFrames = frames * factor_;
    std::fill_n(mix_[0].data(), oscFrames, 0.f);
    std::fill_n(mix_[1].data(), oscFrames, 0.f);
    int position = 0;
    for (size_t e = 0; e < ctx.inEvents.count; ++e) {
        const ProcessEvent& event = ctx.inEvents.events[e];
        const int at = std::clamp(static_cast<int>(event.sampleOffset), position, frames);
        run(position * factor_, at * factor_);
        position = at;
        handle(event);
    }
    run(position * factor_, oscFrames);

    // Down to the rate, then the volume.
    float* left = channels[0];
    float* right = numChannels > 1 ? channels[1] : nullptr;
    for (size_t c = 0; c < 2; ++c) {
        float* to = c == 0 ? left : (right ? right : spare_.data());
        if (factor_ == 2) {
            decimators_[c].process(mix_[c].data(), frames, to);
        } else {
            std::copy_n(mix_[c].data(), frames, to);
        }
    }
    volume_.setTarget(dbToGain(param(Volume)));
    int sounding = 0;
    for (const Voice& voice : voices_) sounding += voice.active && !voice.killed ? 1 : 0;
    for (int i = 0; i < frames; ++i) {
        const float volume = volume_.next();
        left[i] *= volume;
        if (right) right[i] *= volume;
        publish(0, right ? 0.5f * (left[i] + right[i]) : left[i]);
        if (++displayCount_ == kDisplaySamples) {
            displayCount_ = 0;
            publish(1, static_cast<float>(sounding));
        }
    }
    for (int c = 0; c < std::min(numChannels, 2); ++c) std::fill(channels[c] + frames, channels[c] + numFrames, 0.f);

    // Asleep once no voice has played for a while (the decimator's tail long gone by then).
    const bool any = std::any_of(voices_.begin(), voices_.end(), [](const Voice& voice) { return voice.active; });
    quiet_ = any ? 0 : quiet_ + oscFrames;
    if (quiet_ >= static_cast<int64_t>(kSleepSeconds * oscRate_)) asleep_ = true;
}

}  // namespace

SUB_REGISTER_BUILTIN(SupersawProcessor, Instrument);

}  // namespace sub
