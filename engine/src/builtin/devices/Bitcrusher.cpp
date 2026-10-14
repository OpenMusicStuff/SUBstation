// Built-in "Bitcrusher" device: fewer bits and a lower sample rate, the sound
// of early samplers and game consoles.
//
// Per channel: Drive into the crusher (a gain, clipped at full scale), then the sample rate: a sample is held until the
// next tick of a clock at Rate (fractional, so any rate between ticks of the
// device's own; the aliasing is the point), then the bits: rounded to 2^(bits-1)
// steps either side of zero (fractional bits move evenly between the steps of
// the whole numbers around them), with Dither's noise of up to a step added
// first (TPDF: a hiss in place of the steps' distortion on quiet sounds). Tone
// smooths what comes out (a one-pole low-pass, open at 100%), then Output and
// Dry/Wet.

#include <algorithm>
#include <cmath>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/DspBlocks.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

class BitcrusherProcessor final : public BuiltinProcessor {
public:
    enum Param { Bits = 0, Rate, Drive, Dither, Tone, Output, Mix, NumParams };
    BitcrusherProcessor() : BuiltinProcessor(infos()) {}

    std::string typeId() const override { return "builtin:bitcrusher"; }
    std::string name() const override { return "Bitcrusher"; }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        drive_.reset(sampleRate, 0.02);
        output_.reset(sampleRate, 0.02);
        mix_.reset(sampleRate, 0.02);
        reset();
    }

    void reset() override {
        phase_ = 1.0;  // a new sample at once
        for (int c = 0; c < kChannels; ++c) {
            held_[c] = 0.f;
            tone_[c].reset();
        }
        noise_.seed(0x2545F491U);
        drive_.snapTo(dbToGain(param(Drive)));
        output_.snapTo(dbToGain(param(Output)));
        mix_.snapTo(param(Mix) / 100.f);
    }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        // The clock: how far along to the next held sample each sample goes.
        const double step = std::min(1.0, static_cast<double>(param(Rate)) / sampleRate_);
        const float steps = std::exp2(std::clamp(param(Bits), 1.f, 16.f) - 1.f);  // either side of zero
        const float dither = param(Dither) / 100.f;
        const float tone = param(Tone) / 100.f;
        // Tone: from 1 kHz (at 0) up to fully open (at 100%), evenly in log.
        const float toneCoeff =
            tone >= 1.f ? 0.f : dsp::onePoleCutoff(1000.0 * std::pow(20.0, static_cast<double>(tone)), sampleRate_);
        drive_.setTarget(dbToGain(param(Drive)));
        output_.setTarget(dbToGain(param(Output)));
        mix_.setTarget(param(Mix) / 100.f);

        for (int i = 0; i < numFrames; ++i) {
            const float drive = drive_.next();
            const float output = output_.next();
            const float mix = mix_.next();
            phase_ += step;
            const bool tick = phase_ >= 1.0;
            if (tick) phase_ -= std::floor(phase_);
            for (int c = 0; c < n; ++c) {
                const float dry = ch[c][i];
                if (tick) {
                    float x = std::clamp(drive * dry, -1.f, 1.f);
                    if (dither > 0.f) x += dither * (noise_.next() + noise_.next()) * 0.5f / steps;
                    held_[c] = std::clamp(std::round(x * steps) / steps, -1.f, 1.f);
                }
                float y = held_[c];
                y = tone_[c].lowpass(y, toneCoeff);  // (open, it passes y and follows it: no jump when it closes)
                y *= output;
                ch[c][i] = dry + mix * (y - dry);
            }
        }
    }

private:
    static constexpr int kChannels = 2;

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = {
            {"bits", "Bits", "", 1.f, 16.f, 8.f},
            {"rate", "Rate", "Hz", 200.f, 48000.f, 11025.f, true},
            {"drive", "Drive", "dB", 0.f, 24.f, 0.f},
            {"dither", "Dither", "%", 0.f, 100.f, 0.f},
            {"tone", "Tone", "%", 0.f, 100.f, 100.f},
            {"output", "Output", "dB", -24.f, 12.f, 0.f},
            {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
        };
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    double phase_ = 1.0;  // the rate's clock: a new sample is held each time it passes 1
    float held_[kChannels] = {};
    dsp::OnePole tone_[kChannels];
    dsp::Noise noise_;
    SmoothedValue drive_, output_, mix_;
};

}  // namespace

SUB_REGISTER_BUILTIN(BitcrusherProcessor, AudioEffect);

}  // namespace sub
