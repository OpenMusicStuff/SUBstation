// Built-in "Overdrive" device, after Ableton's: a band-pass (Frequency, Width)
// into a pedal's soft clipping (Drive), Tone's low-pass after it, Dynamics
// putting back some of the level changes the clipping takes out, and Dry/Wet.
// The maths is OverdriveDesign.h's, shared with the editor.
//
// The filters run in chunks of kChunk samples: their cutoffs glide in log
// (kGlideSeconds) to where the parameters put them, so turning Frequency,
// Width or Tone doesn't zipper; Drive's gain and Dry/Wet are ramped over 20 ms.
// Dynamics' gain is worked out a chunk at a time from followers of the band's
// level and the clipped signal's (2 ms attack, 60 ms release) and ramped across
// the chunk. A DC blocker after the clipper takes out what its lean adds.
//
// Displays for its editor: `drive` (the driven band's peak, linear: how far into
// the curve it goes) and `output` (the output's peak), one value each per
// kDisplaySamples; and `input`, the input summed to mono, a value a sample.

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"
#include "builtin/OverdriveDesign.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

class OverdriveProcessor final : public BuiltinProcessor {
public:
    enum Param { Frequency = 0, Width, Drive, Tone, Dynamics, Mix, NumParams };
    enum Display { DriveDisplay = 0, OutputDisplay, InputDisplay };
    static constexpr int kDisplaySamples = 128;
    static constexpr int kChunk = 16;
    static constexpr double kGlideSeconds = 0.015;
    static constexpr float kButterworth = static_cast<float>(std::numbers::sqrt2);  // the sections' damping

    OverdriveProcessor()
        : BuiltinProcessor(infos(), {{"drive", kDisplaySamples}, {"output", kDisplaySamples}, {"input", 1}}) {}

    std::string typeId() const override { return "builtin:overdrive"; }
    std::string name() const override { return "Overdrive"; }
    int tailSamples() const override { return static_cast<int>(0.1 * sampleRate_); }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        drive_.reset(sampleRate, 0.02);
        mix_.reset(sampleRate, 0.02);
        glide_ = 1.0 - std::exp(-kChunk / (kGlideSeconds * sampleRate));
        for (auto& c : channels_) c.dc.prepare(sampleRate);
        reset();
    }

    void reset() override {
        for (auto& c : channels_) {
            c.high.reset();
            c.low.reset();
            c.tone.reset();
            c.adaa.reset();
            c.dc.reset();
        }
        bandFollower_.reset();
        clipFollower_.reset();
        dynamicsGain_ = 1.f;
        dynamicsStep_ = 0.f;
        chunkLeft_ = 0;
        const auto [low, high] = overdrive::bandEdges(param(Frequency), param(Width), sampleRate_);
        logLow_ = std::log(low);
        logHigh_ = std::log(high);
        logTone_ = std::log(overdrive::toneHz(param(Tone), sampleRate_));
        drive_.snapTo(static_cast<float>(overdrive::driveGain(param(Drive))));
        mix_.snapTo(param(Mix) / 100.f);
        displayCount_ = 0;
        drivePeak_ = outPeak_ = 0.f;
    }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        const auto [lowTarget, highTarget] = overdrive::bandEdges(param(Frequency), param(Width), sampleRate_);
        const double toneTarget = overdrive::toneHz(param(Tone), sampleRate_);
        const double dynamics = param(Dynamics);
        drive_.setTarget(static_cast<float>(overdrive::driveGain(param(Drive))));
        mix_.setTarget(param(Mix) / 100.f);
        bandFollower_.setTimes(2.f, 60.f, sampleRate_);
        clipFollower_.setTimes(2.f, 60.f, sampleRate_);

        for (int i = 0; i < numFrames;) {
            if (chunkLeft_ == 0) {
                // A new chunk: the cutoffs a glide's step on, Dynamics' gain ramping to where the levels put it.
                chunkLeft_ = kChunk;
                logLow_ += glide_ * (std::log(lowTarget) - logLow_);
                logHigh_ += glide_ * (std::log(highTarget) - logHigh_);
                logTone_ += glide_ * (std::log(toneTarget) - logTone_);
                high_ = dsp::SvfCoefficients(gOf(std::exp(logLow_)), kButterworth);
                low_ = dsp::SvfCoefficients(gOf(std::exp(logHigh_)), kButterworth);
                tone_ = dsp::SvfCoefficients(gOf(std::exp(logTone_)), kButterworth);
                const auto target = static_cast<float>(
                    overdrive::dynamicsGain(bandFollower_.value(), clipFollower_.value(), dynamics));
                dynamicsStep_ = (target - dynamicsGain_) / static_cast<float>(kChunk);
            }
            const int len = std::min(chunkLeft_, numFrames - i);
            for (const int end = i + len; i < end; ++i) {
                const float drive = drive_.next();
                const float mix = mix_.next();
                dynamicsGain_ += dynamicsStep_;
                float bandPeak = 0.f, clipPeak = 0.f, mono = 0.f;
                for (int c = 0; c < n; ++c) {
                    Channel& s = channels_[static_cast<size_t>(c)];
                    float dry = ch[c][i];
                    if (!std::isfinite(dry)) dry = 0.f;
                    mono += dry;
                    // The band: high-pass, then low-pass.
                    const dsp::Svf::Outputs h = s.high.tick(high_, dry);
                    const float hp = dry - high_.k * h.band - h.low;
                    const float band = s.low.tick(low_, hp).low;
                    const float driven = drive * band;
                    const float clipped = s.dc.process(static_cast<float>(s.adaa.process(driven)));
                    bandPeak = std::max(bandPeak, std::abs(band));
                    clipPeak = std::max(clipPeak, std::abs(clipped));
                    drivePeak_ = std::max(drivePeak_, std::abs(driven));
                    float y = clipped * dynamicsGain_;
                    y = s.tone.tick(tone_, y).low;
                    ch[c][i] = dry + mix * (y - dry);
                    outPeak_ = std::max(outPeak_, std::abs(ch[c][i]));
                }
                bandFollower_.peak(bandPeak);
                clipFollower_.peak(clipPeak);
                publish(InputDisplay, n > 1 ? 0.5f * mono : mono);
                if (++displayCount_ >= kDisplaySamples) {
                    displayCount_ = 0;
                    publish(DriveDisplay, drivePeak_);
                    publish(OutputDisplay, outPeak_);
                    drivePeak_ = outPeak_ = 0.f;
                }
            }
            chunkLeft_ -= len;
        }
        for (int c = 0; c < n; ++c) {
            Channel& s = channels_[static_cast<size_t>(c)];
            s.high.flush();
            s.low.flush();
            s.tone.flush();
        }
    }

private:
    static constexpr int kChannels = 2;

    struct Channel {
        dsp::Svf high, low, tone;
        overdrive::Adaa adaa;
        dsp::DcBlocker dc;
    };

    float gOf(double hz) const noexcept { return static_cast<float>(std::tan(overdrive::kPi * hz / sampleRate_)); }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = {
            {"freq", "Frequency", "Hz", static_cast<float>(overdrive::kMinFrequency),
             static_cast<float>(overdrive::kMaxFrequency), 1500.f, true},
            {"width", "Width", "oct", static_cast<float>(overdrive::kMinWidth), static_cast<float>(overdrive::kMaxWidth),
             6.5f},
            {"drive", "Drive", "%", 0.f, 100.f, 50.f},
            {"tone", "Tone", "%", 0.f, 100.f, 50.f},
            {"dynamics", "Dynamics", "%", 0.f, 100.f, 50.f},
            {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
        };
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    double glide_ = 0.1;
    double logLow_ = 0.0, logHigh_ = 0.0, logTone_ = 0.0;  // the filters' cutoffs (ln Hz), gliding
    std::array<Channel, kChannels> channels_{};
    dsp::EnvelopeFollower bandFollower_, clipFollower_;
    float dynamicsGain_ = 1.f, dynamicsStep_ = 0.f;
    int chunkLeft_ = 0;  // samples left in the chunk (0: a new one starts)
    dsp::SvfCoefficients high_, low_, tone_;
    int displayCount_ = 0;
    float drivePeak_ = 0.f, outPeak_ = 0.f;
    SmoothedValue drive_, mix_;
};

}  // namespace

SUB_REGISTER_BUILTIN(OverdriveProcessor, AudioEffect);

}  // namespace sub
