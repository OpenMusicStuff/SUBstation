// Built-in "Vocal Rider" device: rides a vocal's gain to a target level, as an engineer riding its fader would,
// so quiet words come up and loud ones go down without a compressor's sound (no attack, no ratio, no knee: a slow
// gain that follows the phrase).
//
// The vocal's level is its power over a window as long as kWindowShare of Speed, weighted as a listener hears it
// (Weighting: Vocal, BS.1770's K-weighting shelf, +4 dB from 1.68 kHz, and a 100 Hz high-pass for the room's
// rumble in place of its 38 Hz one; Flat: none). While the vocal is there (its level over kPresenceSeconds over
// Vocal Sens., until it falls kHysteresisDb under it) the gain heads for what takes it to Target, held between Range
// Min and Range Max, with Speed as its time constant; between phrases (breaths, bleed, silence, a phrase's tail) the
// gain holds where it was, so nothing quiet is brought up. A phrase's level starts from that short window's, so a
// phrase after a pause isn't taken as quieter than it is.
//
// Music Sens. (with something on the sidechain: the music the vocal sits in) moves the target towards the
// music's level: at 100 % the vocal rides to the music's level as it gets louder or quieter, at 0 % the music
// doesn't count. The music is heard while its level is over kMusicPresentDb. Then Output.
//
// Its editor draws four displays, every kDisplaySamples: "level", the vocal's level in dB (as the rider hears it);
// "gain", the ride in dB; "target", the target in dB (Music Sens. moving it); "music", the music's level in dB
// (kFloorDb with none).

#include <algorithm>
#include <array>
#include <cmath>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/DspBlocks.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

constexpr int kChannels = 2;
constexpr float kFloorDb = -96.f;
constexpr float kHysteresisDb = 6.f;
constexpr float kMusicPresentDb = -60.f;
constexpr double kWindowShare = 0.4;    // the level's window, a share of Speed
constexpr double kMinWindowSeconds = 0.01;
constexpr double kPresenceSeconds = 0.01;
constexpr double kMusicWindowSeconds = 0.4;

class VocalRiderProcessor final : public BuiltinProcessor {
public:
    enum Param { Target = 0, MinGain, MaxGain, Speed, Sensitivity, MusicSens, Weighting, Output, NumParams };
    enum Display { LevelDisplay = 0, GainDisplay, TargetDisplay, MusicDisplay };
    enum WeightingChoice { Vocal = 0, Flat };
    static constexpr int kDisplaySamples = 256;

    VocalRiderProcessor()
        : BuiltinProcessor(infos(), {{"level", kDisplaySamples},
                                     {"gain", kDisplaySamples},
                                     {"target", kDisplaySamples},
                                     {"music", kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:vocal_rider"; }
    std::string name() const override { return "Vocal Rider"; }
    bool hasSidechain() const override { return true; }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        rumble_ = dsp::BiquadCoefficients::highpass(100.0, 0.7071, sampleRate);
        shelf_ = dsp::BiquadCoefficients::highShelf(1681.97, 0.7071, 4.0, sampleRate);
        musicPole_ = static_cast<float>(onePoleCoefficient(kMusicWindowSeconds, sampleRate));
        presencePole_ = static_cast<float>(onePoleCoefficient(kPresenceSeconds, sampleRate));
        output_.reset(sampleRate, 0.02);
        reset();
    }

    void reset() override {
        for (auto& f : weight_) f.reset();
        for (auto& f : musicWeight_) f.reset();
        power_ = fastPower_ = musicPower_ = 0.f;
        present_ = false;
        gainDb_ = 0.f;
        displayCount_ = 0;
        output_.snapTo(param(Output));
    }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        const float target = param(Target);
        const float lowest = std::min(param(MinGain), 0.f);
        const float highest = std::max(param(MaxGain), 0.f);
        const double speed = std::max(1.0, double(param(Speed))) * 0.001;
        const float ridePole = static_cast<float>(onePoleCoefficient(speed, sampleRate_));
        const float levelPole =
            static_cast<float>(onePoleCoefficient(std::max(kMinWindowSeconds, kWindowShare * speed), sampleRate_));
        const float sensitivity = param(Sensitivity);
        const float musicSens = param(MusicSens) / 100.f;
        const bool weighted = choiceIndex(Weighting) == Vocal;
        output_.setTarget(param(Output));
        const bool keyed = sidechainConnected();
        const float* keyL = keyed ? sidechain(0) : nullptr;
        const float* keyR = keyed ? sidechain(1) : nullptr;

        for (int i = 0; i < numFrames; ++i) {
            const float sum = n == 2 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            const float heard = weighted ? weigh(weight_, sum) : sum;
            const float square = heard * heard;
            fastPower_ = square + presencePole_ * (fastPower_ - square);
            if (fastPower_ < 1e-30f) fastPower_ = 0.f;
            const float fastDb = 10.f * fastmath::log10(fastPower_ + 1e-12f);
            if (!present_ && fastDb > sensitivity) {
                present_ = true;
                power_ = fastPower_;  // (a new phrase: its level from the short window's)
            }
            if (present_ && fastDb < sensitivity - kHysteresisDb) present_ = false;
            power_ = square + levelPole * (power_ - square);
            if (power_ < 1e-30f) power_ = 0.f;
            const float levelDb = std::max(kFloorDb, 10.f * fastmath::log10(power_ + 1e-12f));

            float musicDb = kFloorDb;
            if (keyL != nullptr) {
                const float music = 0.5f * (keyL[i] + keyR[i]);
                const float m = weighted ? weigh(musicWeight_, music) : music;
                musicPower_ = m * m + musicPole_ * (musicPower_ - m * m);
                if (musicPower_ < 1e-30f) musicPower_ = 0.f;
                musicDb = std::max(kFloorDb, 10.f * fastmath::log10(musicPower_ + 1e-12f));
            }
            const float aim = musicDb > kMusicPresentDb ? target + musicSens * (musicDb - target) : target;

            if (present_) {
                const float wanted = std::clamp(aim - levelDb, lowest, highest);
                gainDb_ = wanted + ridePole * (gainDb_ - wanted);
            }
            gainDb_ = std::clamp(gainDb_, lowest, highest);  // (the range narrowed while holding)

            const float gain = expDbToGain(gainDb_ + output_.next());
            for (int c = 0; c < n; ++c) ch[c][i] *= gain;

            if (++displayCount_ == kDisplaySamples) {
                publish(LevelDisplay, levelDb);
                publish(GainDisplay, gainDb_);
                publish(TargetDisplay, aim);
                publish(MusicDisplay, musicDb);
                displayCount_ = 0;
            }
        }
    }

private:
    float weigh(std::array<dsp::Biquad, 2>& f, float x) noexcept {
        return f[1].process(shelf_, f[0].process(rumble_, x));
    }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            static const std::vector<std::string> kWeightings = {"Vocal", "Flat"};
            return std::vector<ParamInfo>{
                {"target", "Target", "dB", -50.f, 0.f, -18.f},
                {"min_gain", "Range Min", "dB", -24.f, 0.f, -12.f},
                {"max_gain", "Range Max", "dB", 0.f, 24.f, 12.f},
                {"speed", "Speed", "ms", 20.f, 2000.f, 250.f, true},
                {"sensitivity", "Vocal Sens.", "dB", -80.f, 0.f, -50.f},
                {"music_sens", "Music Sens.", "%", 0.f, 100.f, 0.f},
                {"weighting", "Weighting", "", 0.f, 1.f, 0.f, false, kWeightings},
                {"output", "Output", "dB", -24.f, 24.f, 0.f},
            };
        }();
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    dsp::BiquadCoefficients rumble_, shelf_;
    std::array<dsp::Biquad, 2> weight_, musicWeight_;
    float power_ = 0.f, fastPower_ = 0.f, musicPower_ = 0.f, musicPole_ = 0.f, presencePole_ = 0.f;
    bool present_ = false;
    float gainDb_ = 0.f;
    SmoothedValue output_;
    int displayCount_ = 0;
};

}  // namespace

SUB_REGISTER_BUILTIN(VocalRiderProcessor, AudioEffect);

}  // namespace sub
