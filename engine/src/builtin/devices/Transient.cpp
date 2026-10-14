// Built-in "Transient Shaper" device: turns a sound's onsets (Attack) and its
// tails (Sustain) up or down, whatever its level, by following its envelope
// (TransientDesign.h has the detector and the gain law, which its editor shares).
//
// - Linked stereo: the louder channel's envelope shapes both.
// - Attack Time is how long an onset counts as one (how slowly the slow
//   follower rises); Release how long a tail is held up against (how slowly
//   the held one falls). Below the threshold (by the held level) the shaping
//   fades out, so a noise floor isn't lifted.
// - Output scales what comes out, after Dry/Wet; Clip then bends what goes
//   over -6 dBFS smoothly towards full scale.
// - Lookahead (0, 1 or 5 ms) delays the audio while the detector hears it as it
//   comes, so the gain moves before the onset reaches the output; it is the
//   device's latency (idle() tells the engine when it changes), and a change
//   crossfades from the old delay to the new over 10 ms.
// - Nothing jumps: Attack, Sustain, Threshold, Output and Dry/Wet ramp over
//   20 ms, Clip fades in and out over 10 ms; the times only change how fast
//   the followers move. At its defaults (Attack and Sustain 0 %) it passes its
//   input bit for bit.
//
// Its editor draws five displays, one value per transient::kDisplaySamples,
// pushed together: the input's level as it reaches the gain and the output's
// (dB, the envelope's most: smooth on a low tone, where sample peaks beat
// against its cycles), the parts of the gain Attack and Sustain made (dB, the one furthest
// from 0 of each stretch) and the transient detected (dB, after the
// threshold's fade): what the editor's sparks go off on.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"
#include "builtin/TransientDesign.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

constexpr double kRampSeconds = 0.02;    // Attack, Sustain, Threshold, Output, Dry/Wet
constexpr double kSwitchSeconds = 0.01;  // Clip's fade; the lookahead's crossfade
constexpr int kChannels = 2;

class TransientProcessor final : public BuiltinProcessor {
public:
    enum Param { Attack = 0, Sustain, AttackTime, Release, Threshold, Output, Mix, Clip, Lookahead, NumParams };
    enum Display { InputLevel = 0, OutputLevel, AttackGain, SustainGain, Detected };

    TransientProcessor()
        : BuiltinProcessor(infos(), {{"input", transient::kDisplaySamples},
                                     {"output", transient::kDisplaySamples},
                                     {"attack", transient::kDisplaySamples},
                                     {"sustain", transient::kDisplaySamples},
                                     {"detect", transient::kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:transient"; }
    std::string name() const override { return "Transient Shaper"; }

    int latencySamples() const override {
        return transient::lookaheadSamples(choiceIndex(Lookahead), sampleRate_);
    }
    // What is still in the lookahead's delay when the input stops.
    int tailSamples() const override { return latencySamples(); }

    // The lookahead changed: the engine must realign the tracks.
    bool idle() override { return latencyReport_.changed(latencySamples()); }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        const int most = transient::lookaheadSamples(transient::kLookaheads - 1, sampleRate);
        for (dsp::DelayLine& delay : audio_) delay.prepare(most + 8);
        envelopes_.prepare(most + 8);
        fadeLength_ = std::max(1, static_cast<int>(std::lround(kSwitchSeconds * sampleRate)));
        for (SmoothedValue* ramp : {&attack_, &sustain_, &threshold_, &output_, &mix_})
            ramp->reset(sampleRate, kRampSeconds);
        clip_.reset(sampleRate, kSwitchSeconds);
        latencyReport_.reported(latencySamples());
        reset();
    }

    // Silent, the followers at rest; the next render() starts every ramp and the
    // lookahead at the parameters as they are then.
    void reset() override {
        for (int c = 0; c < kChannels; ++c) {
            audio_[c].reset();
            hilbert_[c].reset();
        }
        envelopes_.reset();
        detector_.reset();
        fadeLeft_ = 0;
        fresh_ = true;
        meterCount_ = 0;
        clearMeters();
    }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        if (n != channels_) {  // (back to two: the right starts afresh, not from what it held before)
            for (int c = channels_; c < n; ++c) {
                audio_[c].reset();
                hilbert_[c].reset();
            }
            channels_ = n;
        }

        detector_.setTimes(param(AttackTime), param(Release), sampleRate_);
        const int lookahead = transient::lookaheadSamples(choiceIndex(Lookahead), sampleRate_);
        const float outputGain = dbToGain(param(Output));
        if (fresh_) {
            attack_.snapTo(param(Attack));
            sustain_.snapTo(param(Sustain));
            threshold_.snapTo(param(Threshold));
            output_.snapTo(outputGain);
            mix_.snapTo(param(Mix) / 100.f);
            clip_.snapTo(isOn(Clip) ? 1.f : 0.f);
            lookahead_ = lookahead;
            fadeLeft_ = 0;
            fresh_ = false;
        }
        attack_.setTarget(param(Attack));
        sustain_.setTarget(param(Sustain));
        threshold_.setTarget(param(Threshold));
        output_.setTarget(outputGain);
        mix_.setTarget(param(Mix) / 100.f);
        clip_.setTarget(isOn(Clip) ? 1.f : 0.f);

        for (int i = 0; i < numFrames; ++i) {
            // A new lookahead crossfades in from the old one (one that comes during a crossfade
            // starts when it is done).
            if (fadeLeft_ == 0 && lookahead != lookahead_) {
                fadeFrom_ = lookahead_;
                lookahead_ = lookahead;
                fadeLeft_ = fadeLength_;
            }
            const float fadeIn =
                fadeLeft_ > 0 ? dsp::sCurve(1.f - static_cast<float>(fadeLeft_) / static_cast<float>(fadeLength_))
                              : 1.f;

            // The envelope as the input comes (ahead of the delayed audio).
            float envelope = 0.f;
            for (int c = 0; c < n; ++c) envelope = std::max(envelope, hilbert_[c].magnitude(ch[c][i]));
            const transient::Levels levels = detector_.step(envelope);
            const float weight = transient::weight(levels.heldDb, threshold_.next());
            const transient::Gain g = transient::gain(attack_.next(), sustain_.next(), levels, weight);
            const float gain = expDbToGain(g.totalDb());
            const float mix = mix_.next();
            const float out = output_.next();
            const float clip = clip_.next();

            const float scale = (1.f + mix * (gain - 1.f)) * out;  // (what the dry sound is multiplied by)
            for (int c = 0; c < n; ++c) {
                audio_[c].push(ch[c][i]);
                float dry = audio_[c].tap(lookahead_);
                if (fadeLeft_ > 0) {
                    const float old = audio_[c].tap(fadeFrom_);
                    dry = old + fadeIn * (dry - old);
                }
                float y = (dry + mix * (dry * gain - dry)) * out;
                if (clip > 0.f) y += clip * (transient::softClip(y) - y);
                ch[c][i] = y;
            }
            if (fadeLeft_ > 0) --fadeLeft_;

            // The levels shown are envelopes, not sample peaks (which beat against a low tone's cycles): the
            // input's as it reaches the gain, and the output's, that times the gain (Clip bending it too).
            envelopes_.push(envelope);
            const float envelopeIn = envelopes_.tap(lookahead_);
            float envelopeOut = envelopeIn * std::abs(scale);
            if (clip > 0.f) envelopeOut += clip * (transient::softClip(envelopeOut) - envelopeOut);
            peakIn_ = std::max(peakIn_, envelopeIn);
            peakOut_ = std::max(peakOut_, envelopeOut);
            if (std::abs(g.attackDb) > std::abs(attackDb_)) attackDb_ = g.attackDb;
            if (std::abs(g.sustainDb) > std::abs(sustainDb_)) sustainDb_ = g.sustainDb;
            detectedDb_ = std::max(detectedDb_, levels.transientDb * weight);
            if (++meterCount_ == transient::kDisplaySamples) {
                publish(InputLevel, toDb(peakIn_));
                publish(OutputLevel, toDb(peakOut_));
                publish(AttackGain, attackDb_);
                publish(SustainGain, sustainDb_);
                publish(Detected, detectedDb_);
                meterCount_ = 0;
                clearMeters();
            }
        }
    }

private:
    static float toDb(float peak) noexcept { return std::max(transient::kDisplayFloorDb, gainToDb(peak)); }

    void clearMeters() noexcept { peakIn_ = peakOut_ = attackDb_ = sustainDb_ = detectedDb_ = 0.f; }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            const std::vector<std::string>& kOnOff = offOnLabels();
            static const std::vector<std::string> kLookaheads = {"0 ms", "1 ms", "5 ms"};
            using namespace transient;
            std::vector<ParamInfo> list = {
                {"attack", "Attack", "%", kAmountMin, kAmountMax, 0.f},
                {"sustain", "Sustain", "%", kAmountMin, kAmountMax, 0.f},
                {"attack_time", "Attack Time", "ms", kAttackTimeMinMs, kAttackTimeMaxMs, 20.f, true},
                {"release", "Release", "ms", kReleaseMinMs, kReleaseMaxMs, 300.f, true},
                {"threshold", "Threshold", "dB", kThresholdMinDb, kThresholdMaxDb, -60.f},
                {"output", "Output", "dB", kOutputMinDb, kOutputMaxDb, 0.f},
                {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
                {"clip", "Clip", "", 0.f, 1.f, 0.f, false, kOnOff},
                {"lookahead", "Lookahead", "", 0.f, 2.f, 0.f, false, kLookaheads},
            };
            list[Lookahead].automatable = false;  // (it is the device's latency)
            return list;
        }();
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    dsp::LatencyReport latencyReport_;
    bool fresh_ = true;        // reset() since the last render(): snap, don't ramp
    int channels_ = kChannels;

    transient::Hilbert hilbert_[kChannels];
    transient::Detector detector_;
    SmoothedValue attack_, sustain_, threshold_, output_, mix_, clip_;

    // The audio through the lookahead, and a change's crossfade from the old delay; the envelope
    // through it too (for the displays).
    dsp::DelayLine audio_[kChannels];
    dsp::DelayLine envelopes_;
    int lookahead_ = 0;
    int fadeFrom_ = 0, fadeLeft_ = 0;
    int fadeLength_ = 480;

    // The displays' accumulators.
    int meterCount_ = 0;
    float peakIn_ = 0.f, peakOut_ = 0.f, attackDb_ = 0.f, sustainDb_ = 0.f, detectedDb_ = 0.f;
};

}  // namespace

SUB_REGISTER_BUILTIN(TransientProcessor, AudioEffect);

}  // namespace sub
