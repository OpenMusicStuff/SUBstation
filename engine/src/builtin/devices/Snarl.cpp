// Built-in "Snarl" instrument: a bass synth for dubstep, brostep, tearout and
// riddim. One voice (a bass plays one note at a time), its notes gliding into
// each other while held; its sound made as bass music makes it:
//
//   unison oscillators ─► growl ─► filter ─► amp ─► dirt ─► split ─► squash ─► clip ─┐
//   (Wave, Unison,       (FM, Sync,  (Low, Band,       (Tube, Hard, (a high-  (OTT)            ├─► Volume
//    Detune, Width,       Fold,       Vowel, Comb)      Rectify,     pass)                     │
//    Octave)              Ring)                         Crush)                                 │
//   sub (a clean sine at the note, under the split) ─────────────────────────────────┘
//
// and a wobble (an LFO on the song's note values) moving the filter, the growl
// and the pitch. All of it but Volume runs oversampled (4x up to 50 kHz, 2x up to
// 100 kHz, none above), the oscillators made at the high rate and brought down
// by the Oversampler's half-band filters: FM, folding and the dirt alias little.
// Its latency is the filters' (18 samples at 4x), and the engine lines the other
// tracks up with it.
//
// - The voice: a note held while another starts glides there (Glide; its
//   envelope and wobble carry on); letting go of the newest glides back to the
//   one still held. A note with none held starts its envelope again (from where
//   a release had got to), its wobble with Retrig, and, from silence, its
//   oscillators' phases, so each hit is the same. Velocity doesn't change the
//   level: a bass hits the same every time. Each note follows its bend.
// - Unison: up to 7 oscillators, Detune spreading them (cents either way) and
//   Width across the stereo field (equal power), at the level of one.
// - The growl's four modes, Wave's shapes and the vowels: SnarlDesign.h.
// - The filter: Low, two state-variable sections (24 dB), the second resonant
//   (its passband made up half the resonant peak's rise); Band, one; Vowel,
//   three band-passes at the vowel's formants, alternately signed (so they add
//   between their peaks rather than cancel), the frequency choosing the vowel;
//   Comb, a feedback comb (damped) at the frequency, its level held. The wobble
//   moves the frequency up to 6 octaves.
// - Dirt: a 20 kHz low-pass first (nothing ultrasonic into it), then Drive from
//   x1 to x16 (+24 dB), with 6 dB of headroom, into Tube (tanh, biased: even
//   harmonics), Hard (a clip), Rectify (1 - sech: an octave up, rounded at 0) or
//   Crush (fewer levels and samples held), each but Crush anti-aliased
//   (SnarlDesign.h); then a DC blocker (Rectify's DC, which Split's high-pass
//   takes the rest of).
// - Split: a 24 dB high-pass on everything but the sub, so the sub owns the lows.
// - Squash: Over The Top's (OttDesign.h), its depth, and made up as much as it
//   brings a driven bass down (21 dB at 100 %): denser as it turns, not quieter.
//   A soft clip after it holds the body's peaks (what gets past the squash's
//   quick detectors) under -2.5 dBFS; the sub stays clean.
// - A change of the growl's mode, the filter or the dirt crossfades over 3 ms.
//
// The parameters glide (10 ms) to where they are set, so turning them doesn't
// step. Silent for 0.5 s after its release, it sleeps (exact zeros, nothing run).
//
// Its displays, for its editor: "scope", the sound's latest cycle (at the note's
// pitch, from the sub's phase), 64 values every 512 samples; and every 256
// samples "lfo" (the wobble, 0..1), "phase" (its phase), "level" (the output's
// peak), "growl" (the growl as the wobble has it, 0..1) and "filter" (the
// filter's frequency as the wobble has it, 0..1 along its range).

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
#include "builtin/OttDesign.h"
#include "builtin/SnarlDesign.h"
#include "rt/FastMath.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

using snarl::DirtType;
using snarl::FilterType;
using snarl::GrowlMode;
using snarl::WobbleShape;

class SnarlProcessor final : public BuiltinProcessor {
public:
    enum Param {
        Wave = 0, Unison, Detune, Width, Octave, Sub, Split, GrowlModeParam, Growl, Ratio, FilterTypeParam, Cutoff,
        Resonance, DirtTypeParam, Drive, WobbleRate, WobbleShapeParam, WobbleFilter, WobbleGrowl, WobblePitch,
        Retrig, Squash, Glide, Attack, Release, Volume, NumParams
    };
    enum Display { ScopeDisplay = 0, LfoDisplay, PhaseDisplay, LevelDisplay, GrowlDisplay, FilterDisplay };
    static constexpr int kMaxUnison = 7;
    static constexpr int kMaxNotes = 16;
    static constexpr int kScopePoints = 64;
    static constexpr int kScopeEvery = 512;
    static constexpr int kDisplaySamples = 256;
    static constexpr double kSleepSeconds = 0.5;
    static constexpr double kSmoothSeconds = 0.01;
    static constexpr double kFadeSeconds = 0.003;
    static constexpr float kBodyGain = 0.25f;
    static constexpr float kSubGain = 0.45f;
    // The body's ceiling, before kBodyGain: its soft clip's (see bodyClip()).
    static constexpr float kBodyCeiling = 3.f;
    // Over The Top brings a hot sound down towards its thresholds (some 22 dB at full depth on a driven
    // bass): Squash makes that much back up, in proportion, so turning it up makes the sound denser, not quieter.
    static constexpr float kSquashMakeupDb = 21.f;

    SnarlProcessor()
        : BuiltinProcessor(infos(), {{"scope", kScopeEvery / kScopePoints},
                                     {"lfo", kDisplaySamples},
                                     {"phase", kDisplaySamples},
                                     {"level", kDisplaySamples},
                                     {"growl", kDisplaySamples},
                                     {"filter", kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:snarl"; }
    std::string name() const override { return "Snarl"; }
    bool acceptsMidi() const override { return true; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    int latencySamples() const override { return latency_; }
    int tailSamples() const override {
        return static_cast<int>((param(Release) * 0.001 + 0.3) * sampleRate_);
    }

protected:
    // Writes (does not add) its output: it is the first device on a MIDI track.
    void render(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) override;

private:
    enum class Stage : uint8_t { Idle, Attack, Sustain, Release };

    struct Held {
        uint8_t key = 0;
        int32_t noteId = -1;
        float bend = 0.f;
    };

    // A parameter gliding to where it is set, a step each sample.
    struct Smooth {
        float value = 0.f;
        float next(float target, float coefficient) noexcept {
            value += (target - value) * coefficient;
            if (std::abs(target - value) < 1e-6f) value = target;
            return value;
        }
    };

    // One channel's filters, dirt and split.
    struct Channel {
        std::array<dsp::Svf, 2> low;                      // Low: two sections
        dsp::Svf band;                                    // Band
        std::array<dsp::Svf, snarl::kFormants> formants;  // Vowel
        dsp::DelayLine comb;                              // Comb
        float combDamp = 0.f;
        float held = 0.f;  // Crush: the sample held
        int holdLeft = 0;
        double dirtIn = 0.0;  // the dirt's last input (u), what its anti-aliasing averages from
        std::array<dsp::Svf, 2> band20k;  // the 20 kHz low-pass before the dirt
        dsp::DcBlocker dc;
        std::array<dsp::Svf, 2> split;
        void reset() noexcept {
            for (dsp::Svf& s : low) s.reset();
            band.reset();
            for (dsp::Svf& s : formants) s.reset();
            comb.reset();
            combDamp = held = 0.f;
            holdLeft = 0;
            dirtIn = 0.0;
            for (dsp::Svf& s : band20k) s.reset();
            dc.reset();
            for (dsp::Svf& s : split) s.reset();
        }
        void resetFilter(FilterType type) noexcept {
            switch (type) {
            case FilterType::Low: for (dsp::Svf& s : low) s.reset(); break;
            case FilterType::Band: band.reset(); break;
            case FilterType::Vowel: for (dsp::Svf& s : formants) s.reset(); break;
            case FilterType::Comb: comb.reset(); combDamp = 0.f; break;
            }
        }
    };

    // The filter's settings for a sample (at the oversampled rate).
    struct FilterSettings {
        std::array<dsp::SvfCoefficients, 2> low;
        dsp::SvfCoefficients band;
        float bandGain = 1.f;
        float lowGain = 1.f;
        std::array<dsp::SvfCoefficients, snarl::kFormants> formants;
        std::array<float, snarl::kFormants> formantGains{};
        float combDelay = 2.f;
        float combFeedback = 0.f;
    };

    // What a change of a list crossfades from: the old value, and how much of it is left (1 → 0).
    template <typename E>
    struct Crossfade {
        E current{};
        E old{};
        int left = 0;
        // A new value: crossfades from the one playing over `length` samples (a change during a
        // crossfade starts again from the newer of the two). True if it changed.
        bool set(E value, int length) noexcept {
            if (value == current) return false;
            old = current;
            current = value;
            left = length;
            return true;
        }
        void snap(E value) noexcept {
            current = old = value;
            left = 0;
        }
        // The old value's share now (1 → 0, an S-curve), stepping on a sample.
        float oldShare(int length) noexcept {
            if (left <= 0) return 0.f;
            const float share = dsp::sCurve(static_cast<float>(left) / static_cast<float>(length));
            --left;
            return share;
        }
    };

    void noteOn(const ProcessEvent& event);
    void noteOff(const ProcessEvent& event);
    void noteBend(const ProcessEvent& event);
    // The note at the top of the held ones starts sounding: from silence or a
    // release (`legato` false), or gliding from the one before.
    void start(const Held& note, bool legato);
    void renderFrames(const ProcessContext& ctx, int from, int to);
    float filter(Channel& ch, FilterType type, const FilterSettings& s, float x) noexcept;
    // The dirt of `type` on `u` (the sound times half Drive's gain), anti-aliased from the channel's last u.
    float dirt(Channel& ch, DirtType type, double u, float drive) noexcept;
    // The body's soft clip: x / (1 + (x / ceiling)^4)^(1/4), within 0.15 dB of x up to half the ceiling,
    // never past it. (What gets past the squash's quick detectors, made up, would peak far over.)
    static float bodyClip(float x) noexcept {
        const float r = x / kBodyCeiling;
        const float r2 = r * r;
        return x / std::sqrt(std::sqrt(1.f + r2 * r2));
    }
    FilterSettings filterSettings(FilterType type, double hz, float resonance) const noexcept;
    void captureScope();
    void sleep();

    static const std::vector<ParamInfo>& infos();

    double sampleRate_ = 48000.0;
    int factorLog2_ = 2;
    int factor_ = 4;
    double osRate_ = 192000.0;
    int latency_ = 18;

    // The voice.
    std::array<Held, kMaxNotes> held_{};
    int heldCount_ = 0;
    Stage stage_ = Stage::Idle;
    float level_ = 0.f;
    double keyPitch_ = 60.0;   // semitones, gliding to keyTarget_
    double keyTarget_ = 60.0;
    double bend_ = 0.0;        // the note's bend, gliding to bendTarget_
    double bendTarget_ = 0.0;
    std::array<double, kMaxUnison> phases_{};
    std::array<double, kMaxUnison> modPhases_{};
    std::array<double, kMaxUnison> foldIns_{};  // Fold's last input, each oscillator's
    bool primeFold_ = true;  // Fold's inputs to be set where the oscillators are (a new mode, phases restarted)
    double subPhase_ = 0.0;
    bool sleeping_ = true;
    int64_t quietSamples_ = 0;

    // The wobble.
    double wobbleTotal_ = 0.0;  // cycles since it started (free running)
    float wobble_ = 0.f;        // its value, smoothed

    // The parameters as they glide.
    Smooth wave_, detune_, width_, sub_, splitHz_, growl_, cutoffOct_, resonance_, drive_, squash_;
    SmoothedValue volume_;
    float smooth_ = 0.f;  // a parameter's glide per sample
    float wobbleSmooth_ = 0.f;
    double bendGlide_ = 0.0;
    int fadeLength_ = 1;
    Crossfade<GrowlMode> mode_;
    Crossfade<FilterType> filterType_;
    Crossfade<DirtType> dirtType_;

    std::array<Channel, 2> channels_{};
    ott::Squash squasher_;
    std::array<dsp::Oversampler, 2> downsamplers_;
    std::array<dsp::SvfCoefficients, 2> band20k_;  // a 4-pole Butterworth low-pass at 20 kHz (oversampled rate)
    std::array<std::vector<float>, 2> oversampled_;  // a block at the oversampled rate, each channel
    std::vector<float> monoRight_;                   // the right brought down, with one channel out

    // What the displays need, per base sample of the block.
    std::vector<float> wobbleTrack_, phaseTrack_, growlTrack_, filterTrack_;

    // The scope: the output (mid), a ring of the latest, and where the sub's phase last wrapped (in samples
    // of the output, its latency counted).
    std::vector<float> scopeRing_;
    size_t scopeMask_ = 0;
    int64_t written_ = 0;  // output samples written into the ring
    std::array<double, 4> wraps_{};
    uint32_t wrapNext_ = 0;  // where the next wrap goes in wraps_
    int wrapsKept_ = 0;      // how many of wraps_ hold one (of this run of the oscillators)
    int64_t lastCapture_ = 0;
    int displayCount_ = 0;
    float peak_ = 0.f;
};

const std::vector<ParamInfo>& SnarlProcessor::infos() {
    static const std::vector<ParamInfo> kInfos = [] {
        std::vector<ParamInfo> list = {
            {"wave", "Wave", "%", 0.f, 100.f, 50.f},
            {"unison", "Unison", "", 1.f, 7.f, 3.f},
            {"detune", "Detune", "ct", 0.f, 100.f, 15.f},
            {"width", "Width", "%", 0.f, 100.f, 50.f},
            {"octave", "Octave", "", -1.f, 2.f, 0.f},
            {"sub", "Sub", "%", 0.f, 100.f, 60.f},
            {"split", "Split", "Hz", 20.f, 500.f, 100.f, true},
            {"growl_mode", "Growl Mode", "", 0.f, 3.f, 0.f},
            {"growl", "Growl", "%", 0.f, 100.f, 30.f},
            {"ratio", "FM Ratio", "", 0.f, 7.f, 3.f},
            {"filter_type", "Filter Type", "", 0.f, 3.f, 0.f},
            {"cutoff", "Filter Freq", "Hz", static_cast<float>(snarl::kMinFreq), static_cast<float>(snarl::kMaxFreq),
             700.f, true},
            {"resonance", "Resonance", "%", 0.f, 100.f, 30.f},
            {"dirt_type", "Dirt Type", "", 0.f, 3.f, 0.f},
            {"drive", "Drive", "%", 0.f, 100.f, 35.f},
            {"wobble_rate", "Wobble Rate", "", 0.f, 11.f, 7.f},
            {"wobble_shape", "Wobble Shape", "", 0.f, 5.f, 0.f},
            {"wobble_filter", "Wobble > Filter", "%", 0.f, 100.f, 50.f},
            {"wobble_growl", "Wobble > Growl", "%", 0.f, 100.f, 0.f},
            {"wobble_pitch", "Wobble > Pitch", "st", -24.f, 24.f, 0.f},
            {"wobble_retrig", "Wobble Retrig", "", 0.f, 1.f, 1.f},
            {"squash", "Squash", "%", 0.f, 100.f, 40.f},
            {"glide", "Glide", "ms", 0.f, 1000.f, 30.f},
            {"attack", "Attack", "ms", 0.5f, 1000.f, 2.f, true},
            {"release", "Release", "ms", 5.f, 4000.f, 80.f, true},
            {"volume", "Volume", "dB", -60.f, 6.f, 0.f},
        };
        list[Unison].steps = 6;
        list[Octave].steps = 3;
        list[GrowlModeParam].valueLabels = snarl::growlModeLabels();
        list[Ratio].valueLabels = snarl::ratioLabels();
        list[FilterTypeParam].valueLabels = snarl::filterTypeLabels();
        list[DirtTypeParam].valueLabels = snarl::dirtTypeLabels();
        list[WobbleRate].valueLabels = snarl::rateLabels();
        list[WobbleShapeParam].valueLabels = snarl::wobbleShapeLabels();
        list[Retrig].valueLabels = BuiltinProcessor::offOnLabels();
        return list;
    }();
    return kInfos;
}

void SnarlProcessor::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    factorLog2_ = sampleRate <= 50000.0 ? 2 : (sampleRate <= 100000.0 ? 1 : 0);
    factor_ = 1 << factorLog2_;
    osRate_ = sampleRate * factor_;
    // The half-band filters' delay, down only: half of what up and down together delay.
    latency_ = static_cast<int>(std::lround(dsp::Oversampler::latencyFor(factorLog2_) / 2.0));
    for (dsp::Oversampler& down : downsamplers_) {
        down.prepare(maxBlockSize, 2);
        down.setFactorLog2(factorLog2_);
    }
    for (std::vector<float>& buffer : oversampled_) buffer.assign(static_cast<size_t>(maxBlockSize * factor_), 0.f);
    for (std::vector<float>* track : {&wobbleTrack_, &phaseTrack_, &growlTrack_, &filterTrack_, &monoRight_})
        track->assign(static_cast<size_t>(maxBlockSize), 0.f);
    for (Channel& ch : channels_) {
        ch.comb.prepare(static_cast<int>(std::ceil(osRate_ / snarl::kMinFreq)) + 4);
        ch.dc.prepare(osRate_, 10.0);
    }
    squasher_.prepare(osRate_);
    {
        const auto g = static_cast<float>(std::tan(dsp::kPi * std::min(20000.0, 0.24 * osRate_) / osRate_));
        band20k_ = {dsp::SvfCoefficients(g, 1.848f), dsp::SvfCoefficients(g, 0.765f)};
    }
    smooth_ = static_cast<float>(1.0 - onePoleCoefficient(kSmoothSeconds, sampleRate));
    wobbleSmooth_ = static_cast<float>(1.0 - onePoleCoefficient(0.0015, sampleRate));
    bendGlide_ = 1.0 - onePoleCoefficient(dsp::kBendGlideSeconds, sampleRate);
    fadeLength_ = std::max(1, static_cast<int>(kFadeSeconds * osRate_));
    volume_.reset(sampleRate, 0.02);
    // The scope's ring: two cycles of a 10 Hz note.
    size_t size = 1024;
    while (static_cast<double>(size) < 2.0 * sampleRate / 10.0) size <<= 1;
    scopeRing_.assign(size, 0.f);
    scopeMask_ = size - 1;
    reset();
}

void SnarlProcessor::reset() {
    heldCount_ = 0;
    stage_ = Stage::Idle;
    level_ = 0.f;
    bend_ = bendTarget_ = 0.0;
    phases_.fill(0.0);
    modPhases_.fill(0.0);
    foldIns_.fill(0.0);
    primeFold_ = true;
    subPhase_ = 0.0;
    wobbleTotal_ = 0.0;
    wobble_ = 0.f;
    sleep();
    std::fill(scopeRing_.begin(), scopeRing_.end(), 0.f);
    written_ = lastCapture_ = 0;
    wrapsKept_ = 0;
    displayCount_ = 0;
    peak_ = 0.f;
}

// Everything that rings out silenced, and the parameters where they are set: what a note wakes it to.
void SnarlProcessor::sleep() {
    sleeping_ = true;
    quietSamples_ = 0;
    for (Channel& ch : channels_) ch.reset();
    squasher_.reset();
    for (dsp::Oversampler& down : downsamplers_) down.reset();
    wave_.value = param(Wave);
    detune_.value = param(Detune);
    width_.value = param(Width);
    sub_.value = param(Sub);
    splitHz_.value = param(Split);
    growl_.value = param(Growl);
    cutoffOct_.value = static_cast<float>(std::log2(param(Cutoff)));
    resonance_.value = param(Resonance);
    drive_.value = param(Drive);
    squash_.value = param(Squash);
    volume_.snapTo(dbToGain(param(Volume)));
    mode_.snap(choice<GrowlMode>(GrowlModeParam));
    filterType_.snap(choice<FilterType>(FilterTypeParam));
    dirtType_.snap(choice<DirtType>(DirtTypeParam));
}

// --- Notes -------------------------------------------------------------------------------

void SnarlProcessor::start(const Held& note, bool legato) {
    keyTarget_ = note.key;
    bendTarget_ = std::clamp(static_cast<double>(note.bend), -kMaxBendSemitones, kMaxBendSemitones);
    if (legato) {
        if (param(Glide) <= 0.f) keyPitch_ = keyTarget_;
        return;
    }
    keyPitch_ = keyTarget_;
    bend_ = bendTarget_;
    if (level_ < dsp::kSilent) {  // from silence: the same hit every time
        for (int u = 0; u < kMaxUnison; ++u) {
            phases_[static_cast<size_t>(u)] = u == 0 ? 0.0 : snarl::wrap(u * 0.6180339887);
            primeFold_ = true;
            modPhases_[static_cast<size_t>(u)] = 0.0;
        }
        subPhase_ = 0.0;
        wrapsKept_ = 0;  // (the scope's cycles start again)
    }
    stage_ = Stage::Attack;
    if (isOn(Retrig)) {
        wobbleTotal_ = 0.0;
        wobble_ = static_cast<float>(snarl::wobble(choice<WobbleShape>(WobbleShapeParam), 0.0, 0));
    }
}

void SnarlProcessor::noteOn(const ProcessEvent& event) {
    if (sleeping_) {
        sleep();  // (the parameters where they are now)
        sleeping_ = false;
    }
    const bool legato = heldCount_ > 0;
    if (heldCount_ == kMaxNotes) {  // full: the oldest goes
        std::move(held_.begin() + 1, held_.end(), held_.begin());
        --heldCount_;
    }
    held_[static_cast<size_t>(heldCount_++)] = {event.key(), event.noteId, 0.f};
    start(held_[static_cast<size_t>(heldCount_ - 1)], legato);
}

void SnarlProcessor::noteOff(const ProcessEvent& event) {
    // The note it ends: by its id, else the oldest held on its key (as the Synth's).
    int found = -1;
    for (int i = 0; i < heldCount_; ++i) {
        const Held& h = held_[static_cast<size_t>(i)];
        if (event.noteId >= 0 ? h.noteId == event.noteId : (h.key == event.key() && h.noteId < 0)) {
            found = i;
            break;
        }
    }
    if (found < 0) return;
    const bool top = found == heldCount_ - 1;
    std::move(held_.begin() + found + 1, held_.begin() + heldCount_, held_.begin() + found);
    --heldCount_;
    if (!top) return;
    if (heldCount_ > 0)
        start(held_[static_cast<size_t>(heldCount_ - 1)], true);  // back to the one still held
    else if (stage_ != Stage::Idle)
        stage_ = Stage::Release;
}

void SnarlProcessor::noteBend(const ProcessEvent& event) {
    for (int i = heldCount_ - 1; i >= 0; --i) {
        Held& h = held_[static_cast<size_t>(i)];
        if (event.noteId >= 0 ? h.noteId == event.noteId : h.key == event.key()) {
            h.bend = event.bend;
            if (i == heldCount_ - 1)
                bendTarget_ = std::clamp(static_cast<double>(event.bend), -kMaxBendSemitones, kMaxBendSemitones);
            return;
        }
    }
}

// --- Filter and dirt ---------------------------------------------------------------------

SnarlProcessor::FilterSettings SnarlProcessor::filterSettings(FilterType type, double hz, float resonance) const noexcept {
    FilterSettings s;
    const double top = 0.24 * osRate_;  // (under the base rate's Nyquist at 4x; a whole octave under it at 1x)
    const auto g = [&](double f) { return static_cast<float>(std::tan(dsp::kPi * std::clamp(f, 10.0, top) / osRate_)); };
    switch (type) {
    case FilterType::Low:
        s.low[0] = dsp::SvfCoefficients(g(hz), 1.848f);
        s.low[1] = dsp::SvfCoefficients(g(hz), 0.765f - 0.71f * resonance);
        s.lowGain = std::sqrt(s.low[1].k / 0.765f);  // (the resonant peak's rise half made up for in the passband)
        break;
    case FilterType::Band: {
        const float k = 1.4f - 1.32f * resonance;
        s.band = dsp::SvfCoefficients(g(hz), k);
        s.bandGain = k * (1.f + resonance);  // (unity at its peak, made up a little as it narrows)
        break;
    }
    case FilterType::Vowel: {
        const auto formants = snarl::formantsAt(snarl::vowelPosition(hz));
        const float q = 3.f + 17.f * resonance;
        for (int f = 0; f < snarl::kFormants; ++f) {
            s.formants[static_cast<size_t>(f)] = dsp::SvfCoefficients(g(formants[static_cast<size_t>(f)].hz), 1.f / q);
            // Alternate signs: summed, two band-passes cancel between their peaks (each a quarter cycle the other way
            // there), which notches the sound and, as the vowel moves, phases it; opposite signs add there instead.
            const double sign = f % 2 == 0 ? 1.0 : -1.0;
            s.formantGains[static_cast<size_t>(f)] = static_cast<float>(sign * 2.0 * formants[static_cast<size_t>(f)].gain / q);
        }
        break;
    }
    case FilterType::Comb:
        s.combDelay = static_cast<float>(osRate_ / std::clamp(hz, snarl::kMinFreq, osRate_ / 3.0));
        s.combFeedback = 0.3f + 0.68f * resonance;
        break;
    }
    return s;
}

float SnarlProcessor::filter(Channel& ch, FilterType type, const FilterSettings& s, float x) noexcept {
    switch (type) {
    case FilterType::Low: return ch.low[1].tick(s.low[1], ch.low[0].tick(s.low[0], x).low).low * s.lowGain;
    case FilterType::Band: return ch.band.tick(s.band, x).band * s.bandGain;
    case FilterType::Vowel: {
        float sum = 0.f;
        for (size_t f = 0; f < snarl::kFormants; ++f)
            sum += ch.formants[f].tick(s.formants[f], x).band * s.formantGains[f];
        return sum;
    }
    case FilterType::Comb: {
        // y[n] = x[n] + feedback * (y[n - delay], damped); its level held under 1 at the teeth.
        const float delayed = ch.comb.hermite(s.combDelay - 1.f);
        ch.combDamp = dsp::flushTiny(delayed + 0.35f * (ch.combDamp - delayed));
        const float y = x + s.combFeedback * ch.combDamp;
        ch.comb.push(y);
        return y * (1.f - s.combFeedback);
    }
    }
    return x;
}

float SnarlProcessor::dirt(Channel& ch, DirtType type, double u, float drive) noexcept {
    switch (type) {
    case DirtType::Tube: {
        const double bias = 0.3 * drive;  // (lopsided: even harmonics)
        return static_cast<float>(2.0 * (snarl::dirtAveraged(type, u + bias, ch.dirtIn + bias) - std::tanh(bias)));
    }
    case DirtType::Hard: return static_cast<float>(2.0 * snarl::dirtAveraged(type, u, ch.dirtIn));
    case DirtType::Rectify: return static_cast<float>(3.0 * snarl::dirtAveraged(type, u, ch.dirtIn));  // (its DC: the blocker's)
    case DirtType::Crush: {
        if (ch.holdLeft <= 0) {
            const float levels = fastmath::exp2(10.f - 8.f * drive);  // 1024 .. 4 levels a side
            ch.held = 2.f * std::round(std::clamp(static_cast<float>(u) * (1.f + drive), -1.f, 1.f) * levels) / levels;
            ch.holdLeft = 1 + static_cast<int>(drive * drive * 12.f * static_cast<float>(factor_));
        }
        --ch.holdLeft;
        return ch.held;
    }
    }
    return static_cast<float>(u);
}

// --- Rendering ---------------------------------------------------------------------------

void SnarlProcessor::renderFrames(const ProcessContext& ctx, int from, int to) {
    if (from >= to) return;
    const double samplesPerMs = sampleRate_ * 0.001;
    const float attackStep = dsp::riseStep(param(Attack), samplesPerMs);
    const float releaseCoef = dsp::fallCoefficient(param(Release), samplesPerMs);
    const double glideMs = param(Glide);
    const double keyGlide = glideMs <= 0.0 ? 1.0 : 1.0 - std::exp(-3.0 / (glideMs * samplesPerMs));  // there in Glide
    const int unison = std::clamp(choiceIndex(Unison), 1, kMaxUnison);
    const int octave = std::clamp(choiceIndex(Octave), -1, 2);
    const double ratio = snarl::ratio(choiceIndex(Ratio));
    const auto shape = choice<WobbleShape>(WobbleShapeParam);
    const double cycleBeats = snarl::rateBeats(choiceIndex(WobbleRate));
    const double tempo = ctx.tempo > 0.0 ? ctx.tempo : 120.0;
    const double samplesPerBeat = sampleRate_ * 60.0 / tempo;
    const bool follow = ctx.playing && !isOn(Retrig);
    const float wobbleFilter = param(WobbleFilter) / 100.f;
    const float wobbleGrowl = param(WobbleGrowl) / 100.f;
    const float wobblePitch = param(WobblePitch);
    const float c = smooth_;
    const float waveTo = param(Wave), detuneTo = param(Detune), widthTo = param(Width), subTo = param(Sub);
    const float splitTo = param(Split), growlTo = param(Growl), resonanceTo = param(Resonance);
    const float driveTo = param(Drive), squashTo = param(Squash);
    const auto cutoffTo = static_cast<float>(std::log2(param(Cutoff)));
    if (mode_.set(choice<GrowlMode>(GrowlModeParam), fadeLength_)) primeFold_ = true;
    if (filterType_.set(choice<FilterType>(FilterTypeParam), fadeLength_)) {
        for (Channel& ch : channels_) ch.resetFilter(filterType_.current);
    }
    dirtType_.set(choice<DirtType>(DirtTypeParam), fadeLength_);

    float* outL = oversampled_[0].data();
    float* outR = oversampled_[1].data();
    for (int i = from; i < to; ++i) {
        // At the base rate: the envelope, the pitch, the wobble, the parameters' glides.
        switch (stage_) {
        case Stage::Attack:
            level_ += attackStep;
            if (level_ >= 1.f) {
                level_ = 1.f;
                stage_ = Stage::Sustain;
            }
            break;
        case Stage::Sustain: break;
        case Stage::Release:
            level_ *= releaseCoef;
            if (level_ < dsp::kSilent) {
                level_ = 0.f;
                stage_ = Stage::Idle;
            }
            break;
        case Stage::Idle: break;
        }
        keyPitch_ += (keyTarget_ - keyPitch_) * keyGlide;
        if (std::abs(keyTarget_ - keyPitch_) < 1e-6) keyPitch_ = keyTarget_;
        bend_ += (bendTarget_ - bend_) * bendGlide_;
        if (std::abs(bendTarget_ - bend_) < 1e-6) bend_ = bendTarget_;

        double total;
        if (follow) {
            total = (ctx.beatPos + i / samplesPerBeat) / cycleBeats;
            wobbleTotal_ = total;
        } else {
            wobbleTotal_ += 1.0 / (samplesPerBeat * cycleBeats);
            total = wobbleTotal_;
        }
        const double whole = std::floor(total);
        const double wobblePhase = total - whole;
        const auto cycle = static_cast<uint32_t>(static_cast<int64_t>(whole));
        wobble_ += (static_cast<float>(snarl::wobble(shape, wobblePhase, cycle)) - wobble_) * wobbleSmooth_;

        const float wave = wave_.next(waveTo, c);
        const float detune = detune_.next(detuneTo, c);
        const float width = width_.next(widthTo, c) / 100.f;
        const float subLevel = sub_.next(subTo, c) / 100.f;
        const float splitHz = splitHz_.next(splitTo, c);
        const float growlKnob = growl_.next(growlTo, c) / 100.f;
        const float cutoffOct = cutoffOct_.next(cutoffTo, c);
        const float resonance = resonance_.next(resonanceTo, c) / 100.f;
        const float drive = drive_.next(driveTo, c) / 100.f;
        const double driveGain = snarl::driveGain(drive);
        const float squashDepth = squash_.next(squashTo, c) / 100.f;
        const float squashMakeup = dbToGain(kSquashMakeupDb * squashDepth);

        const double pitch = keyPitch_ + bend_ + wobble_ * wobblePitch;
        const double subHz = 440.0 * fastmath::exp2((pitch - 69.0) / 12.0);
        const double bodyHz = subHz * fastmath::exp2(static_cast<double>(octave));
        const double growl = std::clamp(growlKnob + wobble_ * wobbleGrowl, 0.f, 1.f);
        const double filterHz = fastmath::exp2(cutoffOct + wobble_ * wobbleFilter * snarl::kWobbleOctaves);
        const double position = snarl::morphPosition(wave);
        wobbleTrack_[static_cast<size_t>(i)] = wobble_;
        phaseTrack_[static_cast<size_t>(i)] = static_cast<float>(wobblePhase);
        growlTrack_[static_cast<size_t>(i)] = static_cast<float>(growl);
        filterTrack_[static_cast<size_t>(i)] = static_cast<float>(std::clamp(snarl::freqPosition(filterHz), 0.0, 1.0));

        // Each unison voice's step, and its place left and right (equal power, at the level of one).
        std::array<double, kMaxUnison> steps{};
        std::array<float, kMaxUnison> gainsL{}, gainsR{};
        const float norm = std::sqrt(2.f / static_cast<float>(unison));
        for (int u = 0; u < unison; ++u) {
            const double spread = unison > 1 ? 2.0 * u / (unison - 1) - 1.0 : 0.0;
            steps[static_cast<size_t>(u)] = bodyHz * fastmath::exp2(detune * spread / 1200.0) / osRate_;
            const double angle = 0.25 * dsp::kPi * (1.0 + width * spread);
            gainsL[static_cast<size_t>(u)] = static_cast<float>(std::cos(angle)) * norm;
            gainsR[static_cast<size_t>(u)] = static_cast<float>(std::sin(angle)) * norm;
        }
        const double subStep = subHz / osRate_;
        if (primeFold_) {  // Fold averages from where each oscillator is now, not from a stale input
            for (int u = 0; u < kMaxUnison; ++u)
                foldIns_[static_cast<size_t>(u)] = snarl::morph(position, phases_[static_cast<size_t>(u)],
                                                                std::max(steps[static_cast<size_t>(u)], 1e-9));
            primeFold_ = false;
        }
        const FilterSettings now = filterSettings(filterType_.current, filterHz, resonance);
        const FilterSettings before =
            filterType_.left > 0 ? filterSettings(filterType_.old, filterHz, resonance) : now;
        const float splitG = static_cast<float>(std::tan(dsp::kPi * std::min<double>(splitHz, 0.24 * osRate_) / osRate_));
        const dsp::SvfCoefficients split0(splitG, 1.848f), split1(splitG, 0.765f);
        const float env = level_;

        for (int k = 0; k < factor_; ++k) {
            const size_t at = static_cast<size_t>(i * factor_ + k);
            // The oscillators through the growl (crossfading from the mode before, if it just changed).
            const float modeOld = mode_.oldShare(fadeLength_);
            float body[2] = {0.f, 0.f};
            for (int u = 0; u < unison; ++u) {
                const size_t v = static_cast<size_t>(u);
                const double dt = steps[v];
                double y = snarl::growled(mode_.current, growl, position, phases_[v], dt, modPhases_[v], ratio,
                                          foldIns_[v]);
                if (modeOld > 0.f) {
                    y += modeOld * (snarl::growled(mode_.old, growl, position, phases_[v], dt, modPhases_[v], ratio,
                                                   foldIns_[v]) - y);
                }
                body[0] += static_cast<float>(y) * gainsL[v];
                body[1] += static_cast<float>(y) * gainsR[v];
                phases_[v] = snarl::wrap(phases_[v] + dt);
                modPhases_[v] = snarl::wrap(modPhases_[v] + dt * ratio);
            }
            // The filter, the amp, the dirt, the split (each crossfading from the type before).
            const float filterOld = filterType_.oldShare(fadeLength_);
            const float dirtOld = dirtType_.oldShare(fadeLength_);
            for (int ch = 0; ch < 2; ++ch) {
                Channel& s = channels_[static_cast<size_t>(ch)];
                float y = filter(s, filterType_.current, now, body[ch]);
                if (filterOld > 0.f) y += filterOld * (filter(s, filterType_.old, before, body[ch]) - y);
                // Nothing above the audible band into the dirt: the oscillators' ultrasonic harmonics (a Comb
                // passes them all) would come out of it as products past the oversampled Nyquist, folding back.
                y = s.band20k[1].tick(band20k_[1], s.band20k[0].tick(band20k_[0], y).low).low;
                y *= env;
                const double u = 0.5 * driveGain * y;
                float d = dirt(s, dirtType_.current, u, drive);
                if (dirtOld > 0.f) d += dirtOld * (dirt(s, dirtType_.old, u, drive) - d);
                s.dirtIn = u;
                d = s.dc.process(d);
                const dsp::Svf::Outputs a = s.split[0].tick(split0, d);
                const float high = d - split0.k * a.band - a.low;
                const dsp::Svf::Outputs b = s.split[1].tick(split1, high);
                body[ch] = high - split1.k * b.band - b.low;
            }
            squasher_.tick(body, 2, squashDepth);
            body[0] = bodyClip(body[0] * squashMakeup);
            body[1] = bodyClip(body[1] * squashMakeup);

            // The sub, clean, under it all.
            const float subOut = static_cast<float>(fastmath::sin(dsp::kTwoPi * subPhase_)) * subLevel * kSubGain * env;
            outL[at] = body[0] * kBodyGain + subOut;
            outR[at] = body[1] * kBodyGain + subOut;
            const double next = subPhase_ + subStep;
            if (next >= 1.0) {  // a cycle of the note: where it shows in the output, for the scope
                const double fraction = (1.0 - subPhase_) / subStep;
                wraps_[wrapNext_++ & 3] = static_cast<double>(written_ + i) + (k + fraction) / factor_ + latency_;
                wrapsKept_ = std::min(wrapsKept_ + 1, 4);
            }
            subPhase_ = next - std::floor(next);
        }
    }
}

void SnarlProcessor::render(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) {
    float* left = channels[0];
    float* right = numChannels > 1 ? channels[1] : nullptr;
    if (sleeping_ && ctx.inEvents.count == 0) {
        std::fill_n(left, numFrames, 0.f);
        if (right) std::fill_n(right, numFrames, 0.f);
        volume_.snapTo(dbToGain(param(Volume)));
        return;
    }
    const int frames = std::min(numFrames, static_cast<int>(wobbleTrack_.size()));  // prepare() said no more
    dsp::renderBetweenNotes(
        ctx.inEvents, frames, [&](int from, int to) { renderFrames(ctx, from, to); },
        [&](const ProcessEvent& event) { noteOn(event); }, [&](const ProcessEvent& event) { noteOff(event); },
        [&](const ProcessEvent& event) { noteBend(event); });
    if (sleeping_) {  // (events, but none started a note)
        std::fill_n(left, numFrames, 0.f);
        if (right) std::fill_n(right, numFrames, 0.f);
        return;
    }
    downsamplers_[0].down(oversampled_[0].data(), frames, left);
    float* rightOut = right ? right : monoRight_.data();  // (one channel: the right brought down all the same)
    downsamplers_[1].down(oversampled_[1].data(), frames, rightOut);

    volume_.setTarget(dbToGain(param(Volume)));
    for (int i = 0; i < numFrames; ++i) {
        const float gain = volume_.next();
        const float l = i < frames ? left[i] * gain : 0.f;
        const float r = i < frames ? rightOut[i] * gain : 0.f;
        left[i] = l;
        if (right) right[i] = r;
        scopeRing_[static_cast<size_t>(written_) & scopeMask_] = 0.5f * (l + r);
        ++written_;
        peak_ = std::max({peak_, std::abs(l), std::abs(r)});
        if (++displayCount_ >= kDisplaySamples) {
            const size_t at = static_cast<size_t>(std::min(i, frames - 1));
            publish(LfoDisplay, wobbleTrack_[at]);
            publish(PhaseDisplay, phaseTrack_[at]);
            publish(LevelDisplay, peak_);
            publish(GrowlDisplay, growlTrack_[at]);
            publish(FilterDisplay, filterTrack_[at]);
            displayCount_ = 0;
            peak_ = 0.f;
        }
        if (written_ - lastCapture_ >= kScopeEvery) captureScope();
    }

    // Asleep once its release has rung out for a while.
    if (stage_ == Stage::Idle) {
        quietSamples_ += numFrames;
        if (quietSamples_ >= static_cast<int64_t>(kSleepSeconds * sampleRate_)) sleep();
    } else {
        quietSamples_ = 0;
    }
}

// The latest whole cycle of the note in the output, as 64 values, if one is there.
void SnarlProcessor::captureScope() {
    lastCapture_ = written_;
    if (stage_ == Stage::Idle) return;
    const double newest = static_cast<double>(written_ - 1);
    double last = -1.0, before = -1.0;  // the latest wrap in the output, and the one before it
    for (int w = 0; w < wrapsKept_; ++w) {
        const double at = wraps_[(wrapNext_ - 1 - static_cast<uint32_t>(w)) & 3];
        if (at > newest) continue;
        if (at > last) {
            before = last;
            last = at;
        } else if (at > before) {
            before = at;
        }
    }
    if (before < 0.0) return;
    const double period = last - before;
    const double oldest = newest - static_cast<double>(scopeRing_.size()) + 2.0;
    if (period < 4.0 || before < oldest) return;
    for (int p = 0; p < kScopePoints; ++p) {
        const double t = before + period * p / kScopePoints;
        const auto whole = static_cast<int64_t>(std::floor(t));
        const auto fraction = static_cast<float>(t - static_cast<double>(whole));
        const float a = scopeRing_[static_cast<size_t>(whole) & scopeMask_];
        const float b = scopeRing_[static_cast<size_t>(whole + 1) & scopeMask_];
        publish(ScopeDisplay, a + fraction * (b - a));
    }
}

}  // namespace

SUB_REGISTER_BUILTIN(SnarlProcessor, Instrument);

}  // namespace sub
