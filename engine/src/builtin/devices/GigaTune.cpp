// Built-in "GigaTune" device: pitch correction for a single voice. It hears the pitch (refined to a fraction of a
// cent), pulls it to the nearest note of a key's scale and moves it there with TD-PSOLA, the formants staying where
// they are (builtin/GigaTuneDesign.h has how).
//
// - Quality: High Quality (at first) hears the pitch with SwiftF0, a small neural network (builtin/SwiftF0.h): sure
//   of the voice where pYIN takes a sub-harmonic, and through noise and reverb. Low Latency hears it with pYIN
//   (fastpitch's, as librosa has it, each frame decided as it comes), as soon after as MAutoPitch does. Not
//   automatable: it sets the latency, and a change starts the device again.
//
// - Key and Scale: the notes it corrects to. Song (at first, in both) follows the song's key
//   (ProcessContext::keyTonic: the project's key or the one its notes are in): its tonic, and major or natural
//   minor. Without a song key known, Song corrects to all twelve notes.
// - Retune Speed: how fast the pitch is pulled onto the note (the correction's time constant, from nothing at
//   each note's start); 0 holds it on the note exactly. Strength: how much of the correction is made.
// - Pitch (whole semitones) and Formant: after the correction, the pitch moved with the formants kept, and the
//   formants moved with the pitch kept.
// - Its latency is fixed for the Quality, at any rate: 21.3 ms with Low Latency (MAutoPitch's: 1024 samples at
//   48 kHz; the tracker decides a frame 16 ms after it, and the marks past that are placed from the last pitch
//   heard), about 277 ms with High Quality (SwiftF0 decides a frame 193 ms after it, and the grains need their
//   periods either side).
//
// Its editor draws three displays, one value per kDisplaySamples of the output, as it is heard: the pitch heard
// (MIDI; 0 unvoiced), where the correction takes it (before Pitch) and the note it is corrected to (-1: none).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/GigaTuneDesign.h"

namespace sub {
namespace {

class GigaTuneProcessor final : public BuiltinProcessor {
public:
    enum Param { Key = 0, ScaleParam, Speed, Strength, Pitch, Formant, QualityParam, NumParams };
    enum Display { HeardPitch = 0, Corrected, Note };
    static constexpr int kDisplaySamples = 256;

    GigaTuneProcessor()
        : BuiltinProcessor(infos(), {{"pitch", kDisplaySamples}, {"corrected", kDisplaySamples},
                                     {"note", kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:gigatune"; }
    std::string name() const override { return "GigaTune"; }
    // The Quality's (a change: the engine realigns the tracks, idle()).
    int latencySamples() const override { return corrector_.latency(quality()); }
    int tailSamples() const override { return latencySamples(); }

    bool idle() override { return latencyReport_.changed(latencySamples()); }

    // Formant, to a hundredth of a semitone (the "st" unit shows whole ones).
    std::string paramText(int index, float value) const override {
        if (index != Formant) return {};
        const double rounded = std::round(value * 100.0) / 100.0;
        if (rounded == 0.0) return "0 st";
        char text[32];
        std::snprintf(text, sizeof text, "%+.2f st", rounded);
        return text;
    }

    void prepare(double sampleRate, int) override {
        corrector_.prepare(sampleRate);
        latencyReport_.reported(latencySamples());
    }

    void reset() override {
        corrector_.reset();
        displayPhase_ = 0;
    }

protected:
    void render(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) override {
        gigatune::Controls controls;
        controls.notes = gigatune::scaleNotes(choiceIndex(Key), choice<gigatune::Scale>(ScaleParam), ctx.keyTonic,
                                              ctx.keyMinor);
        controls.speedMs = std::max(0.f, param(Speed));
        controls.strength = std::clamp(param(Strength) / 100.0, 0.0, 1.0);
        controls.pitch = choiceIndex(Pitch);
        controls.formant = param(Formant);
        controls.quality = quality();
        corrector_.setControls(controls);
        corrector_.process(channels, std::min(numChannels, 2), numFrames);
        // The displays: a value at each kDisplaySamples-th sample out, of what it plays.
        displayPhase_ += numFrames;
        while (displayPhase_ >= kDisplaySamples) {
            displayPhase_ -= kDisplaySamples;
            const gigatune::Heard heard = corrector_.heard(displayPhase_);
            publish(HeardPitch, heard.voiced ? static_cast<float>(heard.pitch) : 0.f);
            publish(Corrected, heard.voiced ? static_cast<float>(heard.corrected) : 0.f);
            publish(Note, static_cast<float>(heard.voiced ? heard.note : -1));
        }
    }

private:
    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            std::vector<ParamInfo> list = {
                {"key", "Key", "", 0.f, 12.f, 0.f, false, gigatune::keyLabels()},
                {"scale", "Scale", "", 0.f, static_cast<float>(gigatune::scaleLabels().size() - 1), 0.f, false,
                 gigatune::scaleLabels()},
                {"speed", "Retune Speed", "ms", 0.f, static_cast<float>(gigatune::kSpeedMaxMs), 20.f},
                {"strength", "Strength", "%", 0.f, 100.f, 100.f},
                {"pitch", "Pitch", "st", static_cast<float>(-gigatune::kPitchMax), static_cast<float>(gigatune::kPitchMax), 0.f},
                {"formant", "Formant", "st", static_cast<float>(-gigatune::kMaxFormant),
                 static_cast<float>(gigatune::kMaxFormant), 0.f},
            };
            list.push_back({"quality", "Quality", "", 0.f, 1.f, 1.f, false, gigatune::qualityLabels()});
            list[Pitch].steps = static_cast<int>(2 * gigatune::kPitchMax);
            list[Formant].ownText = true;
            list[QualityParam].automatable = false;  // (it is the device's latency)
            return list;
        }();
        return kInfos;
    }

    gigatune::Quality quality() const noexcept {
        return choiceIndex(QualityParam) == 0 ? gigatune::Quality::LowLatency : gigatune::Quality::HighQuality;
    }

    gigatune::Corrector corrector_;
    dsp::LatencyReport latencyReport_;
    int displayPhase_ = 0;  // samples out since the last display value
};

}  // namespace

SUB_REGISTER_BUILTIN(GigaTuneProcessor, AudioEffect);

}  // namespace sub
