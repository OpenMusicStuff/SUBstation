// Built-in "Compressor" device: a feed-forward compressor with a soft knee,
// keyed by its own input or by its sidechain (a ducker, with another track
// there). Linked stereo: both channels get the gain the louder one calls for.
//
// The key's level follows its peaks at once and falls at the release time (so
// it holds across a low note's cycles); the gain computer works in dB on that,
// and the gain reduction it asks for is smoothed with the attack as it grows.
// As Ableton's, it has three modes: Peak (that), RMS (the key's level averaged
// over 10 ms first: it reacts less to short peaks) and Expand (downward
// expansion: what is below the threshold is turned down by the ratio).
// Lookahead (0, 1 or 10 ms) delays the audio while the key isn't: the gain
// moves that much before a transient arrives. It is the device's latency.
// Makeup adds back, besides the Makeup gain, half of what it takes off a full
// scale signal (none in Expand: it takes nothing off loud sounds).
//
// Its editor draws three displays, one value per kMeterSamples: the key's
// level in dB, the gain reduction in dB (positive), and the output level in dB.

#include <algorithm>
#include <array>
#include <cmath>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

constexpr int kChannels = 2;
constexpr std::array<double, 3> kLookaheadMs = {0.0, 1.0, 10.0};
constexpr double kRmsSeconds = 0.01;
constexpr float kMostReductionDb = 90.f;  // (Expand on silence: as good as off)

int lookaheadSamples(int index, double sampleRate) noexcept {
    const int i = std::clamp(index, 0, static_cast<int>(kLookaheadMs.size()) - 1);
    return static_cast<int>(std::lround(kLookaheadMs[static_cast<size_t>(i)] * 0.001 * sampleRate));
}

class CompressorProcessor final : public BuiltinProcessor {
public:
    enum Param { Threshold = 0, Ratio, Attack, Release, Knee, Makeup, Mix, Mode, Lookahead, AutoMakeup, NumParams };
    enum Display { InputLevel = 0, Reduction, OutputLevel };
    enum ModeChoice { Peak = 0, Rms, Expand };

    CompressorProcessor()
        : BuiltinProcessor(infos(), {{"input", kMeterSamples}, {"reduction", kMeterSamples},
                                     {"output", kMeterSamples}}) {}

    std::string typeId() const override { return "builtin:compressor"; }
    std::string name() const override { return "Compressor"; }
    bool hasSidechain() const override { return true; }

    int latencySamples() const override { return lookaheadSamples(choiceIndex(Lookahead), sampleRate_); }
    // What is still in the lookahead's delay when the input stops.
    int tailSamples() const override { return latencySamples(); }

    // The lookahead changed: the engine must realign the tracks.
    bool idle() override {
        const int latency = latencySamples();
        if (latency == reportedLatency_) return false;
        reportedLatency_ = latency;
        return true;
    }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        const int most = lookaheadSamples(static_cast<int>(kLookaheadMs.size()) - 1, sampleRate);
        for (dsp::DelayLine& line : audio_) line.prepare(most + 1);
        rmsPole_ = static_cast<float>(onePoleCoefficient(kRmsSeconds, sampleRate));
        makeup_.reset(sampleRate, 0.02);
        mix_.reset(sampleRate, 0.02);
        makeup_.snapTo(makeupDb());
        mix_.snapTo(param(Mix) / 100.f);
        reportedLatency_ = latencySamples();
        reset();
    }

    void reset() override {
        envelope_ = reductionDb_ = meanSquare_ = 0.f;
        for (dsp::DelayLine& line : audio_) line.reset();
        delay_ = -1;
        meterCount_ = 0;
        peakIn_ = peakOut_ = maxReduction_ = 0.f;
    }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        const float threshold = param(Threshold);
        const float ratio = std::max(1.f, param(Ratio));
        const float slope = 1.f / ratio - 1.f;  // dB out per dB in above the knee, minus 1
        const float knee = param(Knee);
        const float attack = onePole(param(Attack));
        const float release = onePole(param(Release));
        const auto mode = static_cast<ModeChoice>(std::clamp(choiceIndex(Mode), 0, 2));
        makeup_.setTarget(makeupDb());
        mix_.setTarget(param(Mix) / 100.f);
        // A new lookahead starts the delay afresh (it isn't automatable: the engine realigns the tracks).
        const int delay = latencySamples();
        if (delay != delay_) {
            for (dsp::DelayLine& line : audio_) line.reset();
            delay_ = delay;
        }
        // With a sidechain the key is what it hears (silence while solo leaves its
        // source out); without one, the device's own input.
        const bool keyed = sidechainConnected();
        const float* keyL = keyed ? sidechain(0) : nullptr;
        const float* keyR = keyed ? sidechain(1) : nullptr;

        for (int i = 0; i < numFrames; ++i) {
            float key = 0.f;
            if (!keyed) {
                for (int c = 0; c < n; ++c) key = std::max(key, std::abs(ch[c][i]));
            } else if (keyL != nullptr) {
                key = std::max(std::abs(keyL[i]), std::abs(keyR[i]));
            }
            float detected = key;
            if (mode == Rms) {
                meanSquare_ = key * key + rmsPole_ * (meanSquare_ - key * key);
                detected = std::sqrt(meanSquare_);
            }
            envelope_ = dsp::followPeak(envelope_, detected, release);
            const float level = gainToDb(envelope_);
            const float target = mode == Expand ? expansion(level, threshold, ratio, knee)
                                                : level - curve(level, threshold, slope, knee);
            reductionDb_ = target > reductionDb_ ? target + attack * (reductionDb_ - target) : target;

            const float mix = mix_.next();
            const float gain = 1.f + mix * (expDbToGain(makeup_.next() - reductionDb_) - 1.f);
            float out = 0.f;
            for (int c = 0; c < n; ++c) {
                float x = ch[c][i];
                if (delay > 0) {
                    audio_[static_cast<size_t>(c)].push(x);
                    x = audio_[static_cast<size_t>(c)].tap(delay);
                }
                ch[c][i] = x * gain;
                out = std::max(out, std::abs(ch[c][i]));
            }

            peakIn_ = std::max(peakIn_, key);
            peakOut_ = std::max(peakOut_, out);
            maxReduction_ = std::max(maxReduction_, reductionDb_);
            if (++meterCount_ == kMeterSamples) {
                publish(InputLevel, toDb(peakIn_));
                publish(Reduction, maxReduction_);
                publish(OutputLevel, toDb(peakOut_));
                meterCount_ = 0;
                peakIn_ = peakOut_ = maxReduction_ = 0.f;
            }
        }
    }

private:
    static constexpr int kMeterSamples = 256;  // audio per meter value
    static constexpr float kFloorDb = -90.f;

    // The level out for `level` in (dB): unchanged below the knee, the ratio's
    // slope above it, and a quadratic blend across it.
    static float curve(float level, float threshold, float slope, float knee) noexcept {
        const float over = level - threshold;
        if (2.f * over <= -knee) return level;
        if (2.f * over < knee) {
            const float x = over + 0.5f * knee;
            return level + slope * x * x / (2.f * knee);
        }
        return level + slope * over;
    }

    // Expand: how far `level` is turned down (dB, positive): nothing above the
    // knee, the ratio's slope below it (each dB under the threshold, ratio dB
    // under it out), a quadratic blend across it.
    static float expansion(float level, float threshold, float ratio, float knee) noexcept {
        const float under = threshold - level;
        float reduction = 0.f;
        if (2.f * under <= -knee) {
            reduction = 0.f;
        } else if (2.f * under < knee) {
            const float x = under + 0.5f * knee;
            reduction = (ratio - 1.f) * x * x / (2.f * knee);
        } else {
            reduction = (ratio - 1.f) * under;
        }
        return std::min(reduction, kMostReductionDb);
    }

    // The Makeup gain, and with Makeup on, half of what a full scale signal is turned down.
    float makeupDb() const noexcept {
        float db = param(Makeup);
        if (isOn(AutoMakeup) && choiceIndex(Mode) != Expand)
            db += 0.5f * -std::min(0.f, param(Threshold)) * (1.f - 1.f / std::max(1.f, param(Ratio)));
        return db;
    }

    static float toDb(float peak) noexcept { return std::max(kFloorDb, gainToDb(peak)); }

    float onePole(float ms) const noexcept {
        return static_cast<float>(onePoleCoefficient(std::max(0.01f, ms) * 0.001, sampleRate_));
    }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            const std::vector<std::string>& kOnOff = offOnLabels();
            static const std::vector<std::string> kModes = {"Peak", "RMS", "Expand"};
            static const std::vector<std::string> kLookaheads = {"0 ms", "1 ms", "10 ms"};
            std::vector<ParamInfo> list = {
                {"threshold", "Threshold", "dB", -60.f, 0.f, -18.f},
                {"ratio", "Ratio", ":1", 1.f, 20.f, 4.f, true},
                {"attack", "Attack", "ms", 0.1f, 200.f, 10.f, true},
                {"release", "Release", "ms", 5.f, 2000.f, 150.f, true},
                {"knee", "Knee", "dB", 0.f, 24.f, 6.f},
                {"makeup", "Makeup", "dB", -12.f, 24.f, 0.f},
                {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
                {"mode", "Mode", "", 0.f, 2.f, 0.f, false, kModes},
                {"lookahead", "Lookahead", "", 0.f, 2.f, 0.f, false, kLookaheads},
                {"auto_makeup", "Auto Makeup", "", 0.f, 1.f, 0.f, false, kOnOff},
            };
            list[Lookahead].automatable = false;  // (it is the device's latency)
            return list;
        }();
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    int reportedLatency_ = 0;  // (main thread: idle())
    float envelope_ = 0.f;     // the key's level (linear)
    float meanSquare_ = 0.f;   // RMS: the key's square, averaged
    float rmsPole_ = 0.f;
    float reductionDb_ = 0.f;
    std::array<dsp::DelayLine, kChannels> audio_;  // the lookahead
    int delay_ = -1;                               // its length now (samples; -1: not started)
    SmoothedValue makeup_, mix_;
    int meterCount_ = 0;
    float peakIn_ = 0.f, peakOut_ = 0.f, maxReduction_ = 0.f;
};

}  // namespace

SUB_REGISTER_BUILTIN(CompressorProcessor, AudioEffect);

}  // namespace sub
