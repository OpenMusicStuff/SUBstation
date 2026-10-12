// Built-in "Envelope" modulator: a drawn curve played once from each trigger,
// its value moving the parameters mapped to it (Modulation.h: the renderer runs
// it before its track's devices; it never touches the audio).
//
// Triggers: what it is given decides. Each note the device hears (its track's,
// or a MIDI track's it takes them from: MIDI From), and, once a sidechain is
// chosen, each hit in it. A hit is found as the Sidechain device finds one: the
// first sample whose key (the louder channel) is at the threshold, once the key
// has fallen 3 dB below it (falling over 10 ms) and 20 ms have passed.
//
// From a trigger the curve plays over its Length (ms, or with Sync a note value
// at the tempo), then holds its last point's value until the next (as it does
// before the first). A trigger while it plays starts it again. Its value is the
// curve's scaled by Depth, and with Velocity by the note's velocity (100 %: a
// note at velocity 64 plays it at half; a hit in the sidechain counts as full),
// held to 0..1; Smooth eases it (a one-pole).
//
// Displays (ModulatorDesign.h): "value", "phase" (-1 at rest), and "key" (the
// sidechain's peak over each stretch, linear; without a sidechain, 1 where a
// note came).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/ModulatorDesign.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

namespace md = modulator;

constexpr double kMinGapMs = 20.0;           // between two hits from the key
constexpr float kHysteresis = 0.70794578f;  // -3 dB: the key falls this far below the threshold to re-arm

class EnvelopeProcessor final : public BuiltinProcessor {
public:
    enum Param { Threshold = 0, Sync, Length, Synced, Depth, Velocity, Smooth, kFirstPoint };
    enum Display { ValueDisplay = 0, PhaseDisplay, KeyDisplay };

    EnvelopeProcessor()
        : BuiltinProcessor(infos(), {{"value", md::kDisplaySamples}, {"phase", md::kDisplaySamples},
                                     {"key", md::kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:envelope"; }
    std::string name() const override { return "Envelope"; }
    bool isModulator() const override { return true; }
    bool hasSidechain() const override { return true; }
    bool acceptsMidi() const override { return true; }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        reset();
    }

    void reset() override {
        position_ = -1.0;
        level_ = 1.f;
        armed_ = true;
        sinceKeyHit_ = 1 << 30;
        keyEnvelope_ = 0.f;
        value_ = -1.f;
        displayCount_ = 0;
        keyPeak_ = 0.f;
    }

protected:
    void render(const ProcessContext& ctx, float* const* ch, int numChannels, int numFrames) override {
        if (numChannels <= 0) return;
        curve_.load([this](int index) { return param(index); }, kFirstPoint);
        const double samplesPerBeat = ctx.samplesPerBeat();
        const double lengthSamples =
            std::max(1.0, isOn(Sync) && samplesPerBeat > 0.0
                              ? md::syncedBeats(choiceIndex(Synced)) * samplesPerBeat
                              : static_cast<double>(param(Length)) * 0.001 * sampleRate_);
        const bool keyed = sidechainConnected();
        const float* keyL = keyed ? sidechain(0) : nullptr;
        const float* keyR = keyed ? sidechain(1) : nullptr;
        const float threshold = dbToGain(param(Threshold));
        const float keyRelease = static_cast<float>(onePoleCoefficient(0.01, sampleRate_));
        const int minGap = static_cast<int>(kMinGapMs * 0.001 * sampleRate_);
        const float depth = std::clamp(param(Depth) / 100.f, 0.f, 1.f);
        const float byVelocity = std::clamp(param(Velocity) / 100.f, 0.f, 1.f);
        const float smoothing = md::smoothing(param(Smooth), sampleRate_);

        size_t event = 0;
        for (int i = 0; i < numFrames; ++i) {
            bool hit = false;
            float velocity = 0.f;  // the loudest note starting here (a hit in the key: full)
            for (; event < ctx.inEvents.count && ctx.inEvents.events[event].sampleOffset <= i; ++event) {
                const ProcessEvent& e = ctx.inEvents.events[event];
                if (!e.startsNote()) continue;
                hit = true;
                velocity = std::max(velocity, e.velocity() / 127.f);
                if (!keyed) keyPeak_ = 1.f;
            }
            // The key: a hit is its first sample at the threshold, once re-armed.
            if (keyed) {
                const float key = keyL ? std::max(std::abs(keyL[i]), std::abs(keyR[i])) : 0.f;
                keyPeak_ = std::max(keyPeak_, key);
                keyEnvelope_ = dsp::followPeak(keyEnvelope_, key, keyRelease);
                if (sinceKeyHit_ < (1 << 30)) ++sinceKeyHit_;
                if (armed_ && key >= threshold && sinceKeyHit_ >= minGap) {
                    velocity = 1.f;
                    hit = true;
                    armed_ = false;
                    sinceKeyHit_ = 0;
                } else if (!armed_ && keyEnvelope_ < threshold * kHysteresis) {
                    armed_ = true;
                }
            }
            if (hit) {
                position_ = 0.0;
                level_ = 1.f - byVelocity * (1.f - velocity);
            }
            float y;
            if (position_ >= 0.0) {
                y = curve_.at(position_ / lengthSamples);
                position_ += 1.0;
                if (position_ > lengthSamples) position_ = -1.0;
            } else {
                y = curve_.last();
            }
            const float target = std::clamp(y * depth * level_, 0.f, 1.f);
            value_ = value_ < 0.f ? target : target + smoothing * (value_ - target);
            for (int c = 0; c < std::min(numChannels, 2); ++c) ch[c][i] = value_;
            if (++displayCount_ >= md::kDisplaySamples) {
                displayCount_ = 0;
                publish(ValueDisplay, value_);
                publish(PhaseDisplay, position_ >= 0.0 ? static_cast<float>(position_ / lengthSamples) : -1.f);
                publish(KeyDisplay, keyPeak_);
                keyPeak_ = 0.f;
            }
        }
    }

private:
    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            const std::vector<std::string>& kOnOff = offOnLabels();
            const std::vector<std::string>& kSynced = md::syncedLabels();
            std::vector<ParamInfo> list = {
                {"threshold", "Threshold", "dB", -60.f, 0.f, -24.f},
                {"sync", "Sync", "", 0.f, 1.f, 0.f, false, kOnOff},
                {"length", "Length", "ms", 1.f, 20000.f, 500.f, true},
                {"synced", "Length (Synced)", "", 0.f, static_cast<float>(kSynced.size() - 1), 13.f, false, kSynced},
                {"depth", "Depth", "%", 0.f, 100.f, 100.f},
                {"velocity", "Velocity", "%", 0.f, 100.f, 0.f},
                {"smooth", "Smooth", "ms", 0.f, 250.f, 0.f},
            };
            md::appendPointParams(list, md::pluckPoints());
            return list;
        }();
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    md::Curve curve_;
    double position_ = -1.0;  // samples since the latest trigger; -1: its curve is over
    float level_ = 1.f;       // the latest trigger's (Velocity)
    bool armed_ = true;
    int sinceKeyHit_ = 1 << 30;
    float keyEnvelope_ = 0.f;
    float value_ = -1.f;  // its value, smoothed (-1: none yet)
    int displayCount_ = 0;
    float keyPeak_ = 0.f;
};

}  // namespace

SUB_REGISTER_BUILTIN(EnvelopeProcessor, Modulator);

}  // namespace sub
