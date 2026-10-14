// Built-in "LFO" modulator: a drawn curve played over and over, its value moving
// the parameters mapped to it (Modulation.h: the renderer runs it before its
// track's devices; it never touches the audio).
//
// A cycle lasts 1 / Rate seconds, or with Sync a note value at the tempo. While
// the song plays a synced LFO follows the song's position (a 1/4 LFO starts its
// cycle on every beat, wherever playback starts or loops); stopped, or with
// Retrigger, it runs on from where it is at the tempo. Free in Hz it always
// runs on. Retrigger starts the cycle again at each note the device hears (its
// track's, or a MIDI track's it takes them from).
//
// Phase shifts where in the cycle it is (0..360°); the curve's value then goes
// round the middle, scaled by Depth, and moves up or down by Offset (+100 %:
// half the range): value = 0.5 + (y - 0.5) * depth + offset / 2, held to 0..1.
// Smooth eases it (a one-pole: a stepped curve without clicks).
//
// Random plays a new random curve each cycle instead of the drawn one
// (ModulatorDesign.h's randomPoints), from where the last one ended. A cycle
// starts where the curve wraps round (after Phase), or where a note restarts it.
//
// Displays (ModulatorDesign.h): "value", "phase", "key" (1 where a note
// restarted it), "seed" and "start".

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/ModulatorDesign.h"

namespace sub {
namespace {

namespace md = modulator;

class LfoProcessor final : public BuiltinProcessor {
public:
    enum Param { Rate = 0, Sync, Synced, Depth, Offset, PhaseShift, Retrigger, Smooth, kFirstPoint };
    static constexpr int Random = kFirstPoint + md::kPoints * md::kPointParams;  // (after the points: added later)
    enum Display { ValueDisplay = 0, PhaseDisplay, KeyDisplay, SeedDisplay, StartDisplay };

    LfoProcessor()
        : BuiltinProcessor(infos(), {{"value", md::kDisplaySamples}, {"phase", md::kDisplaySamples},
                                     {"key", md::kDisplaySamples}, {"seed", md::kDisplaySamples},
                                     {"start", md::kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:lfo"; }
    std::string name() const override { return "LFO"; }
    bool isModulator() const override { return true; }
    bool acceptsMidi() const override { return true; }  // (Retrigger)

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        reset();
    }

    void reset() override {
        phase_ = 0.0;
        value_ = -1.f;
        displayCount_ = 0;
        noted_ = false;
        randoms_.reset();
        seed_ = -1;
        lastX_ = -1.0;
        y_ = 0.5f;
    }

protected:
    void render(const ProcessContext& ctx, float* const* ch, int numChannels, int numFrames) override {
        if (numChannels <= 0) return;
        const bool random = isOn(Random);
        if (!random) {
            curve_.load([this](int index) { return param(index); }, kFirstPoint);
            seed_ = -1;
        }
        const double samplesPerBeat = ctx.samplesPerBeat();
        const bool synced = isOn(Sync) && samplesPerBeat > 0.0;
        const double cycleBeats = md::syncedBeats(choiceIndex(Synced));
        const double step = synced ? 1.0 / (cycleBeats * samplesPerBeat)
                                   : std::clamp(static_cast<double>(param(Rate)), 0.001, 1000.0) / sampleRate_;
        const bool retrigger = isOn(Retrigger);
        const bool followsSong = synced && ctx.playing && !retrigger;
        const double shift = std::clamp(static_cast<double>(param(PhaseShift)), 0.0, 360.0) / 360.0;
        const float depth = std::clamp(param(Depth) / 100.f, 0.f, 1.f);
        const float offset = std::clamp(param(Offset) / 100.f, -1.f, 1.f) * 0.5f;
        const float smoothing = md::smoothing(param(Smooth), sampleRate_);

        size_t event = 0;
        for (int i = 0; i < numFrames; ++i) {
            bool restarted = false;
            for (; event < ctx.inEvents.count && ctx.inEvents.events[event].sampleOffset <= i; ++event) {
                if (retrigger && ctx.inEvents.events[event].startsNote()) {
                    phase_ = 0.0;
                    noted_ = true;
                    restarted = true;
                }
            }
            if (followsSong) {
                const double beat = ctx.beatPos + i / samplesPerBeat;
                phase_ = beat / cycleBeats - std::floor(beat / cycleBeats);
            }
            double x = phase_ + shift;
            x -= std::floor(x);
            // Random: a new curve from where the last left off, each time a cycle starts.
            if (random && (seed_ < 0 || restarted || x < lastX_)) {
                start_ = y_;
                seed_ = static_cast<int32_t>(randoms_.next(curve_, y_));
            }
            lastX_ = x;
            const float y = curve_.at(x);
            y_ = y;
            const float target = std::clamp(0.5f + (y - 0.5f) * depth + offset, 0.f, 1.f);
            value_ = value_ < 0.f ? target : target + smoothing * (value_ - target);
            for (int c = 0; c < std::min(numChannels, 2); ++c) ch[c][i] = value_;
            phase_ += step;
            phase_ -= std::floor(phase_);
            if (++displayCount_ >= md::kDisplaySamples) {
                displayCount_ = 0;
                publish(ValueDisplay, value_);
                publish(PhaseDisplay, static_cast<float>(x));
                publish(KeyDisplay, noted_ ? 1.f : 0.f);
                publish(SeedDisplay, static_cast<float>(seed_));
                publish(StartDisplay, start_);
                noted_ = false;
            }
        }
    }

private:
    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            const std::vector<std::string>& kOnOff = offOnLabels();
            const std::vector<std::string>& kSynced = md::syncedLabels();
            std::vector<ParamInfo> list = {
                {"rate", "Rate", "Hz", 0.01f, 40.f, 1.f, true},
                {"sync", "Sync", "", 0.f, 1.f, 1.f, false, kOnOff},
                {"synced", "Rate (Synced)", "", 0.f, static_cast<float>(kSynced.size() - 1), 9.f, false, kSynced},
                {"depth", "Depth", "%", 0.f, 100.f, 100.f},
                {"offset", "Offset", "%", -100.f, 100.f, 0.f},
                {"phase", "Phase", "°", 0.f, 360.f, 0.f},
                {"retrigger", "Retrigger", "", 0.f, 1.f, 0.f, false, kOnOff},
                {"smooth", "Smooth", "ms", 0.f, 250.f, 0.f},
            };
            md::appendPointParams(list, md::trianglePoints());
            list.push_back({"random", "Random", "", 0.f, 1.f, 0.f, false, kOnOff});
            return list;
        }();
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    md::Curve curve_;
    double phase_ = 0.0;  // where in the cycle it is (before Phase), 0..1
    float value_ = -1.f;  // its value, smoothed (-1: none yet)
    int displayCount_ = 0;
    bool noted_ = false;  // a note restarted it since the last display value
    md::RandomCurves randoms_;
    int32_t seed_ = -1;    // Random's curve now (-1: none)
    float start_ = 0.f;    // ... its first level
    double lastX_ = -1.0;  // where on the curve it was a sample ago
    float y_ = 0.5f;       // the curve's value a sample ago
};

}  // namespace

SUB_REGISTER_BUILTIN(LfoProcessor, Modulator);

}  // namespace sub
