// Built-in "Tape Stop" device: the sound slows to a halt, as a tape machine's
// or a turntable's does when its motor is cut, and spins back up when it is
// let go.
//
// Stop (a switch, made to be automated or pressed) starts it. The input is
// written into a ring, and a read head plays it back at a speed that falls
// from 1 to 0 over Stop Time, along Curve (0: an even slope; above, the speed
// holds and then drops; below, it drops at once and crawls to the end); the
// pitch falls with the speed. Stopped, it is silent. Let go, it spins up again
// over Start Time along the same curve, played from where the head stops if it
// had stopped (from the input as it is then) or from where it had got to if it
// hadn't; back at full speed, it is behind the input by what it lost, and it
// crossfades to the input as it is over 20 ms. At full speed and caught up it
// passes the input as it is, bit for bit.
//
// Tone darkens it as it slows (a one-pole low-pass following the speed down,
// to 80 Hz when stopped at 100%); the level fades out over the last tenth of
// the speed, so the stop ends without a thump.
//
// Its editor's reels turn at the speed: the display "speed", one value per 256
// samples.

#include <algorithm>
#include <cmath>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/DspBlocks.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

class TapeStopProcessor final : public BuiltinProcessor {
public:
    enum Param { Stop = 0, StopTime, StartTime, Curve, Tone, Mix, NumParams };
    enum Display { SpeedDisplay = 0 };
    static constexpr int kDisplaySamples = 256;
    static constexpr float kMaxTimeMs = 4000.f;

    TapeStopProcessor() : BuiltinProcessor(infos(), {{"speed", kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:tape_stop"; }
    std::string name() const override { return "Tape Stop"; }

    // The speed at `progress` through a stop (0: full speed, 1: stopped), along `curve` (-100..100).
    static double speedAt(double progress, double curve) noexcept {
        const double p = std::clamp(progress, 0.0, 1.0);
        const double k = std::exp2(std::abs(curve) / 40.0);
        return curve >= 0.0 ? 1.0 - std::pow(p, k) : std::pow(1.0 - p, k);
    }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        // The most it can fall behind: a whole stop, then a whole start.
        const int most = static_cast<int>(std::ceil(2.0 * kMaxTimeMs / 1000.0 * sampleRate)) + 16;
        for (auto& line : lines_) line.prepare(most);
        catchUpSamples_ = std::max(1, static_cast<int>(std::lround(0.02 * sampleRate)));
        mix_.reset(sampleRate, 0.02);
        reset();
    }

    void reset() override {
        for (int c = 0; c < kChannels; ++c) {
            lines_[c].reset();
            tone_[c].reset();
        }
        progress_ = isOn(Stop) ? 1.0 : 0.0;
        lag_ = 0.0;
        catchUp_ = -1;
        displayCount_ = 0;
        mix_.snapTo(param(Mix) / 100.f);
    }

    int tailSamples() const override { return static_cast<int>(param(StopTime) / 1000.f * sampleRate_); }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        const bool stop = isOn(Stop);
        const double stopStep = 1.0 / std::max(1.0, param(StopTime) / 1000.0 * sampleRate_);
        const double startStep = 1.0 / std::max(1.0, param(StartTime) / 1000.0 * sampleRate_);
        const double curve = param(Curve);
        const float tone = param(Tone) / 100.f;
        const double most = lines_[0].capacity() - 8.0;
        mix_.setTarget(param(Mix) / 100.f);

        for (int i = 0; i < numFrames; ++i) {
            for (int c = 0; c < n; ++c) lines_[c].push(ch[c][i]);
            // Where the stop has got to (a stop waits for the crossfade back to the input to end).
            if (stop && catchUp_ < 0) {
                progress_ = std::min(1.0, progress_ + stopStep);
            } else if (!stop && progress_ > 0.0) {
                if (progress_ >= 1.0) lag_ = 0.0;  // stopped (silent): the head starts again from the input
                progress_ = std::max(0.0, progress_ - startStep);
            }
            const double speed = speedAt(progress_, curve);
            lag_ = std::min(most, lag_ + (1.0 - speed));
            if (progress_ <= 0.0 && lag_ > 0.0 && catchUp_ < 0) catchUp_ = 0;  // back at speed: catch up

            const float gain = static_cast<float>(dsp::sCurve(std::min(1.0, speed / 0.1)));
            const float toneCoeff =
                speed >= 1.0 || tone <= 0.f
                    ? 0.f
                    : tone * dsp::onePoleCutoff(80.0 + 18000.0 * speed * speed, sampleRate_);
            const float caughtUp =
                catchUp_ < 0 ? 0.f : dsp::sCurve(static_cast<float>(catchUp_) / static_cast<float>(catchUpSamples_));
            const float mix = mix_.next();
            const auto lag = static_cast<float>(lag_);
            for (int c = 0; c < n; ++c) {
                const float dry = ch[c][i];
                float wet = lag < 2.f ? lines_[c].linear(lag) : lines_[c].hermite(lag);
                wet = tone_[c].lowpass(wet, toneCoeff) * gain;
                if (catchUp_ >= 0) wet += caughtUp * (dry - wet);
                ch[c][i] = dry + mix * (wet - dry);
            }
            if (catchUp_ >= 0 && ++catchUp_ > catchUpSamples_) {
                catchUp_ = -1;
                lag_ = 0.0;
            }
            if (++displayCount_ >= kDisplaySamples) {
                publish(SpeedDisplay, static_cast<float>(speed));
                displayCount_ = 0;
            }
        }
    }

private:
    static constexpr int kChannels = 2;

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = {
            {"stop", "Stop", "", 0.f, 1.f, 0.f, false, offOnLabels()},
            {"stop_time", "Stop Time", "ms", 20.f, kMaxTimeMs, 600.f, true},
            {"start_time", "Start Time", "ms", 1.f, kMaxTimeMs, 250.f, true},
            {"curve", "Curve", "%", -100.f, 100.f, 0.f},
            {"tone", "Tone", "%", 0.f, 100.f, 50.f},
            {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
        };
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    dsp::DelayLine lines_[kChannels];
    dsp::OnePole tone_[kChannels];
    double progress_ = 0.0;  // through a stop: 0 at full speed, 1 stopped
    double lag_ = 0.0;       // how far the head is behind the input, in samples
    int catchUp_ = -1;       // samples into the crossfade back to the input (-1: none)
    int catchUpSamples_ = 960;
    int displayCount_ = 0;
    SmoothedValue mix_;
};

}  // namespace

SUB_REGISTER_BUILTIN(TapeStopProcessor, AudioEffect);

}  // namespace sub
