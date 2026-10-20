// Built-in "Changeling" device: a pitch and formant shifter for a single voice, Little AlterBoy's controls with
// Polyverse Manipulator's on top. It hears the pitch and moves it with TD-PSOLA, the formants where they were or
// moved apart from it (builtin/ChangelingDesign.h has how, and what each control does, measured from both).
//
// - Mode: Transpose (Pitch moves the voice), Quantize (each note pulled onto the nearest of Key and Scale's notes,
//   then moved by Pitch), Robot (one note: C4 plus Pitch, or the last MIDI note played). MIDI notes in Transpose
//   and Quantize transpose by their distance from C4, as Little AlterBoy's do.
// - Pitch and Formant (semitones, continuous), Link (the formants move with the pitch), Glide (how long a move to a
//   new note or transposition takes), Key and Scale (Quantize's notes: Chromatic at first, as LAB's; Song follows
//   the song's key, as GigaTune's).
// - Drive (LAB's, 0 to 10), Mix (dry, as late as the wet, to wet) and Output.
// - Harmonics, Twin and Interval, Detune, Width and Smear: Manipulator's harmonic shifter, its alternator as a
//   second voice, its doubler, stereo and smear.
// - Quality: Live (21.3 ms latency), Studio (48 ms, Little AlterBoy's: notes are heard as voiced from their start),
//   High Quality (277 ms, SwiftF0). Not automatable: it sets the latency, and a change starts the device again.
//
// Its editor draws two displays, one value per kDisplaySamples of the output as it is heard: the pitch heard (MIDI;
// 0 unvoiced) and the pitch out, and a third, the note Quantize or Robot holds the voice on (-1: none).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/ChangelingDesign.h"
#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"
#include "builtin/GigaTuneDesign.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

namespace cl = changeling;

class ChangelingProcessor final : public BuiltinProcessor {
public:
    enum Param {
        ModeParam = 0, Pitch, Formant, Link, Glide, Key, ScaleParam, Drive, Mix, Output,
        Harmonics, Twin, Interval, Detune, Width, Smear, QualityParam, NumParams
    };
    enum Display { HeardPitch = 0, OutPitch, Note };
    static constexpr int kDisplaySamples = 256;
    static constexpr int kMaxBlock = 4096;  // processed in pieces this long at the most (the drive's buffers)

    ChangelingProcessor()
        : BuiltinProcessor(infos(), {{"pitch", kDisplaySamples}, {"out", kDisplaySamples}, {"note", kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:changeling"; }
    std::string name() const override { return "Changeling"; }
    // The Quality's, and the drive's oversampling (a change: the engine realigns the tracks, idle()).
    int latencySamples() const override { return shifter_.latency(quality()) + drive_.latency(); }
    int tailSamples() const override { return latencySamples(); }
    bool idle() override { return latencyReport_.changed(latencySamples()); }
    bool acceptsMidi() const override { return true; }

    // Pitch, Formant and Harmonics to a hundredth (the "st" unit shows whole semitones).
    std::string paramText(int index, float value) const override {
        if (index != Pitch && index != Formant && index != Harmonics) return {};
        const double rounded = std::round(value * 100.0) / 100.0;
        const char* unit = index == Harmonics ? "" : " st";
        char text[32];
        if (rounded == 0.0) std::snprintf(text, sizeof text, "0%s", unit);
        else std::snprintf(text, sizeof text, "%+.2f%s", rounded, unit);
        return text;
    }

    void prepare(double sampleRate, int) override {
        shifter_.prepare(sampleRate);
        drive_.prepare(sampleRate, kMaxBlock);
        // the dry signal, as late as the wet at the longest latency
        const int longest = std::max({shifter_.latency(cl::Quality::Live), shifter_.latency(cl::Quality::Studio),
                                      shifter_.latency(cl::Quality::HighQuality)}) + drive_.latency();
        for (auto& d : dry_) d.prepare(longest + kMaxBlock + 4);
        smoothing_ = static_cast<float>(std::exp(-1.0 / (0.01 * sampleRate)));  // 10 ms
        latencyReport_.reported(latencySamples());
        reset();
    }

    void reset() override {
        shifter_.reset();
        drive_.reset();
        for (auto& d : dry_) d.reset();
        mix_.reset(mixTarget());
        gain_.reset(gainTarget());
        displayPhase_ = 0;
    }

protected:
    void render(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) override {
        const int chs = std::min(numChannels, 2);
        if (chs < 1) return;
        cl::Controls c;
        c.mode = choice<cl::Mode>(ModeParam);
        c.pitch = param(Pitch);
        c.formant = param(Formant);
        c.link = isOn(Link);
        c.glideMs = param(Glide) <= 1.f ? 0.0 : param(Glide);
        c.notes = gigatune::scaleNotes(choiceIndex(Key), choice<gigatune::Scale>(ScaleParam), ctx.keyTonic, ctx.keyMinor);
        c.harmonics = param(Harmonics);
        c.twin = std::clamp(param(Twin) / 100.0, 0.0, 1.0);
        c.interval = param(Interval);
        c.detune = param(Detune);
        c.width = std::clamp(param(Width) / 100.0, 0.0, 1.0);
        c.smear = std::clamp(param(Smear) / 100.0, 0.0, 1.0);
        c.quality = quality();
        shifter_.setControls(c);
        drive_.setDrive(param(Drive));

        for (int from = 0; from < numFrames;) {
            const int n = std::min(kMaxBlock, numFrames - from);
            float* io[2] = {channels[0] + from, chs > 1 ? channels[1] + from : nullptr};
            // the notes played in this piece
            int count = 0;
            for (size_t e = 0; e < ctx.inEvents.count && count < kMaxNotes; ++e) {
                const ProcessEvent& event = ctx.inEvents.events[e];
                if (event.startsNote() && event.sampleOffset >= from && event.sampleOffset < from + n)
                    notes_[static_cast<size_t>(count++)] = {event.sampleOffset - from, event.key()};
            }
            for (int ch = 0; ch < chs; ++ch)
                for (int i = 0; i < n; ++i) dry_[static_cast<size_t>(ch)].push(io[ch][i]);
            shifter_.process(io, chs, n, notes_.data(), count);
            drive_.process(io, chs, n);
            const int late = latencySamples();
            const float mixTo = mixTarget(), gainTo = gainTarget();
            for (int i = 0; i < n; ++i) {
                const float m = mix_.next(mixTo, smoothing_), g = gain_.next(gainTo, smoothing_);
                for (int ch = 0; ch < chs; ++ch) {
                    const float dry = dry_[static_cast<size_t>(ch)].tap(n - 1 - i + late);
                    io[ch][i] = g * (dry + m * (io[ch][i] - dry));
                }
            }
            from += n;
        }
        // The displays: a value at each kDisplaySamples-th sample out, of what it plays.
        displayPhase_ += numFrames;
        while (displayPhase_ >= kDisplaySamples) {
            displayPhase_ -= kDisplaySamples;
            const cl::Heard heard = shifter_.heard(displayPhase_ + drive_.latency());
            publish(HeardPitch, heard.voiced ? static_cast<float>(heard.pitch) : 0.f);
            publish(OutPitch, heard.voiced ? static_cast<float>(heard.out) : 0.f);
            publish(Note, static_cast<float>(heard.voiced ? heard.note : -1));
        }
    }

private:
    static constexpr int kMaxNotes = 64;

    // A value smoothed towards its target (Mix and Output, so moving them doesn't click).
    struct Smoothed {
        float value = 0.f;
        void reset(float v) noexcept { value = v; }
        float next(float target, float coeff) noexcept {
            value = target + coeff * (value - target);
            return value;
        }
    };

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            std::vector<ParamInfo> list = {
                {"mode", "Mode", "", 0.f, 2.f, 0.f, false, cl::modeLabels()},
                {"pitch", "Pitch", "st", static_cast<float>(-cl::kPitchMax), static_cast<float>(cl::kPitchMax), 0.f},
                {"formant", "Formant", "st", static_cast<float>(-cl::kFormantMax), static_cast<float>(cl::kFormantMax), 0.f},
                {"link", "Link", "", 0.f, 1.f, 0.f, false, offOnLabels()},
                {"glide", "Glide", "ms", 1.f, static_cast<float>(cl::kGlideMaxMs), 20.f, true},
                {"key", "Key", "", 0.f, 12.f, 0.f, false, gigatune::keyLabels()},
                {"scale", "Scale", "", 0.f, static_cast<float>(gigatune::scaleLabels().size() - 1),
                 static_cast<float>(gigatune::Scale::Chromatic), false, gigatune::scaleLabels()},
                {"drive", "Drive", "dial", 0.f, static_cast<float>(cl::kDriveMax), 0.f},
                {"mix", "Mix", "%", 0.f, 100.f, 100.f},
                {"output", "Output", "dB", -24.f, 24.f, 0.f},
                {"harmonics", "Harmonics", "", static_cast<float>(-cl::kHarmonicsMax), static_cast<float>(cl::kHarmonicsMax), 0.f},
                {"twin", "Twin", "%", 0.f, 100.f, 0.f},
                {"interval", "Interval", "st", static_cast<float>(-cl::kIntervalMax), static_cast<float>(cl::kIntervalMax), -12.f},
                {"detune", "Detune", "ct", 0.f, static_cast<float>(cl::kDetuneMax), 0.f},
                {"width", "Width", "%", 0.f, 100.f, 0.f},
                {"smear", "Smear", "%", 0.f, 100.f, 0.f},
                {"quality", "Quality", "", 0.f, 2.f, 1.f, false, cl::qualityLabels()},
            };
            list[Pitch].ownText = true;
            list[Formant].ownText = true;
            list[Harmonics].ownText = true;
            list[Interval].steps = static_cast<int>(2 * cl::kIntervalMax);
            list[QualityParam].automatable = false;  // (it is the device's latency)
            return list;
        }();
        return kInfos;
    }

    cl::Quality quality() const noexcept { return choice<cl::Quality>(QualityParam); }
    float mixTarget() const noexcept { return std::clamp(param(Mix) / 100.f, 0.f, 1.f); }
    float gainTarget() const noexcept { return dbToGain(param(Output)); }

    cl::Shifter shifter_;
    cl::Drive drive_;
    std::array<dsp::DelayLine, 2> dry_;
    std::array<cl::NoteEvent, kMaxNotes> notes_{};
    Smoothed mix_, gain_;
    float smoothing_ = 0.999f;
    dsp::LatencyReport latencyReport_;
    int displayPhase_ = 0;
};

}  // namespace

SUB_REGISTER_BUILTIN(ChangelingProcessor, AudioEffect);

}  // namespace sub
