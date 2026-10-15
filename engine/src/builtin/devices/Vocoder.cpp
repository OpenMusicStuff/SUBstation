// Built-in "Vocoder" device, as Ableton's: the input (the modulator, a voice) is split into bands, and each band's
// level shapes the same band of another sound (the carrier), so the carrier speaks.
//
// Carriers (Carrier):
// - Noise: white noise of its own, held at Rate (a sample-and-hold clock; full rate at the top) and thinned by
//   Crackle (100 %: noise; less: ever sparser clicks of the same power).
// - External: the sidechain (a synth on another track, as Ableton's routing); silence with none.
// - Modulator: the input itself (its own spectrum, flattened by Enhance: a filter bank's colour).
// - Pitch Tracking: a monophonic oscillator (a saw, or pulses of 50 %, 25 % and 12.5 %; band-limited by PolyBLEP)
//   following the input's pitch between Low and High (VocoderDesign.h's tracker), moved by Pitch (semitones).
//
// The bands (VocoderDesign.h): Bands of them from Low to High, BW wide, Precise or Retro, the carrier's moved
// by Formant. Each modulator band's level is its RMS, followed with Attack and Release; Depth scales each band's
// distance (in dB) from the bands' mean level (0 %: every band at the mean, the carrier's own spectrum as loud as the
// modulator; 100 %: the modulator's levels; 200 %: its peaks stand out further); a band whose level is under Gate is
// silent. Each carrier band is first brought to a steady level: without Enhance, by the carrier's
// level over all its bands (its spectrum and its quicker dynamics kept); with it, by its own level, band by band
// (a flat, steady carrier: every sound speaks as clearly). So whatever the carrier's level the output is about
// as loud as the modulator. (The carrier's level over its bands is weighted by the modulator's: a carrier dark
// where the voice is dark isn't taken as louder than it is there.) A band past kTopFraction of the rate, or moved
// past it by Formant, is left out.
//
// Unvoiced: noise added for what has no pitch (s, f, t), as loud as the modulator's highs, when the share of
// its power above kUnvoicedSplit Hz says it is unvoiced (Sens.: from how small a share; Fast or Slow: how quickly
// that is heard). Channels: Mono (modulator and carrier summed), Stereo (the modulator summed, the carrier in
// stereo), L/R (each side its own). Then Level and Dry/Wet.
//
// Its editor draws three displays, every kDisplaySamples: "bands", kDisplayBands values a time (the modulator's
// band levels in dB, from the lowest band, the rest kFloorDb); "pitch", the pitch tracked (Hz; 0 when not
// tracking or unvoiced); "output", the output's peak in dB.

#include <algorithm>
#include <array>
#include <cmath>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "builtin/DspBlocks.h"
#include "builtin/VocoderDesign.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

constexpr int kChannels = 2;
constexpr int kMaxBands = vocoder::kMaxBands;
// A carrier's level (each band's with Enhance, its bands' mean, weighted, without): its mean square over kEnhanceSeconds or
// kCarrierSeconds, but never under kOnsetShare of its peaks (held as long), so a carrier starting from silence isn't
// raised more than 10 dB while its mean catches up.
constexpr float kCarrierSeconds = 0.1f;
constexpr float kCarrierHoldSeconds = 0.3f;
constexpr float kEnhanceSeconds = 0.02f;
constexpr float kOnsetShare = 0.1f;
constexpr float kMostEnhanceDb = 60.f;     // Enhance raises a quiet carrier band this much over the carrier at most
constexpr float kFloorDb = -96.f;
constexpr double kUnvoicedSplit = 4000.0;
constexpr double kUnvoicedNoiseCut = 3000.0;
constexpr float kGateOff = -95.9f;         // Gate at its least: none
constexpr int kControlSamples = 8;         // the bands' gains are worked out this often, ramped between

class VocoderProcessor final : public BuiltinProcessor {
public:
    enum Param {
        Carrier = 0, NoiseRate, Crackle, Pitch, Wave, TrackLow, TrackHigh, Enhance, Unvoiced, Sensitivity, Speed,
        Bands, Low, High, Bandwidth, Filter, Formant, Gate, Depth, Attack, Release, Channels, Level, Mix, NumParams
    };
    enum CarrierChoice { NoiseCarrier = 0, External, ModulatorCarrier, PitchTracking };
    enum ChannelsChoice { Mono = 0, Stereo, LeftRight };
    enum Display { BandsDisplay = 0, PitchDisplay, OutputDisplay };
    static constexpr int kDisplaySamples = 1024;
    static constexpr int kDisplayBands = 64;  // a power of two: frames stay whole in the display's ring

    VocoderProcessor()
        : BuiltinProcessor(infos(), {{"bands", kDisplaySamples / kDisplayBands},
                                     {"pitch", kDisplaySamples},
                                     {"output", kDisplaySamples}}) {}

    std::string typeId() const override { return "builtin:vocoder"; }
    std::string name() const override { return "Vocoder"; }
    bool hasSidechain() const override { return true; }
    int tailSamples() const override { return static_cast<int>(param(Release) * 0.001f * 5.f * sampleRate_); }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        tracker_.prepare(sampleRate);
        level_.reset(sampleRate, 0.02);
        mix_.reset(sampleRate, 0.02);
        layoutKey_ = {};
        const auto hp = dsp::BiquadCoefficients::highpass(kUnvoicedSplit, 0.7071, sampleRate);
        splitHigh_ = hp;
        noiseHigh_ = dsp::BiquadCoefficients::highpass(kUnvoicedNoiseCut, 0.7071, sampleRate);
        carrierPole_ = static_cast<float>(onePoleCoefficient(kCarrierSeconds, sampleRate));
        carrierHold_ = static_cast<float>(onePoleCoefficient(kCarrierHoldSeconds, sampleRate));
        enhancePole_ = static_cast<float>(onePoleCoefficient(kEnhanceSeconds, sampleRate));
        gatePole_ = static_cast<float>(onePoleCoefficient(0.002, sampleRate, kControlSamples));
        frequencyPole_ = static_cast<float>(onePoleCoefficient(0.005, sampleRate));
        reset();
    }

    void reset() override {
        for (auto& channel : mod_)
            for (auto& band : channel) band = {};
        for (auto& channel : car_)
            for (auto& band : channel) band = {};
        carrierLevel_.fill({});
        for (int c = 0; c < kChannels; ++c) {
            noise_[c].seed(0x51ED270BU + 0x9E3779B9U * static_cast<uint32_t>(c));
            noiseClock_[c] = 1.0;
            held_[c] = 0.f;
            for (auto& s : noiseFilter_[c]) s.reset();
        }
        for (auto& s : split_) s.reset();
        highPower_ = totalPower_ = unvoiced_ = 0.f;
        tracker_.reset();
        oscPhase_ = 0.0;
        frequency_ = 0.0;
        displayCount_ = 0;
        controlCount_ = 0;
        peakOut_ = 0.f;
        level_.snapTo(dbToGain(param(Level)));
        mix_.snapTo(param(Mix) / 100.f);
    }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        const int n = std::min(numChannels, kChannels);
        if (n <= 0) return;
        updateLayout();
        const auto carrier = static_cast<CarrierChoice>(std::clamp(choiceIndex(Carrier), 0, 3));
        const auto channels = static_cast<ChannelsChoice>(std::clamp(choiceIndex(Channels), 0, 2));
        const int modCount = channels == LeftRight && n == 2 ? 2 : 1;
        const int carCount = channels == Mono || n == 1 ? 1 : 2;
        const int bands = layout_.count;

        const float attack = follower(param(Attack));
        const float release = follower(param(Release));
        const float depth = param(Depth) / 100.f;
        const float gateDb = param(Gate);
        const bool gated = gateDb > kGateOff;
        const float gateLevel = dbToGain(gateDb);
        const bool enhance = isOn(Enhance);
        const float unvoicedAmount = param(Unvoiced) / 100.f;
        const float sensitivity = param(Sensitivity) / 100.f;
        // The share of the power above the split from which a sound is taken as unvoiced (Sens. 100 %: any).
        const float unvoicedFrom = 0.6f * (1.f - sensitivity);
        const float unvoicedPole =
            static_cast<float>(onePoleCoefficient(choiceIndex(Speed) == 0 ? 0.003 : 0.03, sampleRate_));
        // Noise: the hold clock's step, and the chance a held sample is a click (Crackle).
        const double noiseStep = std::min(1.0, double(param(NoiseRate)) / sampleRate_);
        const float crackle = param(Crackle) / 100.f;
        const float clickChance = std::max(1e-3f, crackle * crackle);
        const float clickLevel = 1.f / std::sqrt(clickChance);
        const double pitchRatio = std::exp2(double(param(Pitch)) / 12.0);
        const int wave = std::clamp(choiceIndex(Wave), 0, 3);
        tracker_.setRange(param(TrackLow), param(TrackHigh));
        level_.setTarget(dbToGain(param(Level)));
        mix_.setTarget(param(Mix) / 100.f);
        const bool keyed = sidechainConnected();
        const float* keyL = keyed ? sidechain(0) : nullptr;
        const float* keyR = keyed ? sidechain(1) : nullptr;

        for (int i = 0; i < numFrames; ++i) {
            // The modulators.
            float modIn[kChannels];
            const float sum = n == 2 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            if (modCount == 2) {
                modIn[0] = ch[0][i];
                modIn[1] = ch[1][i];
            } else {
                modIn[0] = sum;
            }

            // Unvoiced: how much of the (summed) modulator is above the split, followed.
            const float high = split_[1].process(splitHigh_, split_[0].process(splitHigh_, sum));
            highPower_ = dsp::flushTiny(high * high + unvoicedPole * (highPower_ - high * high));
            totalPower_ = dsp::flushTiny(sum * sum + unvoicedPole * (totalPower_ - sum * sum));
            unvoiced_ = dsp::flushTiny(unvoiced_);
            const float share = totalPower_ > 1e-12f ? highPower_ / totalPower_ : 0.f;
            const float unvoiced = std::clamp((share - unvoicedFrom) * 4.f, 0.f, 1.f);
            unvoiced_ = unvoiced + unvoicedPole * (unvoiced_ - unvoiced);
            const float unvoicedGain = unvoicedAmount * unvoiced_ * std::sqrt(highPower_);

            // The carriers.
            float carIn[kChannels] = {0.f, 0.f};
            switch (carrier) {
            case NoiseCarrier:
                for (int c = 0; c < carCount; ++c) carIn[c] = noiseSample(c, noiseStep, crackle, clickChance, clickLevel);
                break;
            case External:
                if (keyL != nullptr) {
                    if (carCount == 2) {
                        carIn[0] = keyL[i];
                        carIn[1] = keyR[i];
                    } else {
                        carIn[0] = 0.5f * (keyL[i] + keyR[i]);
                    }
                }
                break;
            case ModulatorCarrier:
                if (carCount == 2) {
                    carIn[0] = ch[0][i];
                    carIn[1] = ch[1][i];
                } else {
                    carIn[0] = sum;
                }
                break;
            case PitchTracking:
                if (tracker_.push(sum) && tracker_.voiced()) target_ = tracker_.pitch();
                carIn[0] = carIn[1] = oscillator(pitchRatio, wave);
                break;
            }

            // Each modulator's band levels; every kControlSamples, the gains they make (ramped to between).
            const bool control = controlCount_ == 0;
            controlCount_ = controlCount_ + 1 == kControlSamples ? 0 : controlCount_ + 1;
            for (int m = 0; m < modCount; ++m) {
                auto& modBands = mod_[static_cast<size_t>(m)];
                float total = 0.f;
                for (int k = 0; k < bands; ++k) {
                    ModBand& b = modBands[static_cast<size_t>(k)];
                    const float y = b.filter[1].process(modCoeffs_[static_cast<size_t>(k)][1],
                                                        b.filter[0].process(modCoeffs_[static_cast<size_t>(k)][0],
                                                                            modIn[m]));
                    const float square = y * y;
                    b.power = square + (square > b.power ? attack : release) * (b.power - square);
                    if (b.power < 1e-20f) b.power = 0.f;  // (-200 dB: silence)
                    total += b.power;
                    b.gain += b.step;
                }
                if (control) updateGains(modBands, bands, total, depth, gated, gateLevel);
            }

            // Each carrier's bands, brought to a steady level and shaped.
            float wet[kChannels] = {0.f, 0.f};
            for (int c = 0; c < carCount; ++c) {
                const auto& gains = mod_[static_cast<size_t>(std::min(c, modCount - 1))];
                const float overall = std::sqrt(carrierLevel_[static_cast<size_t>(c)].power());
                const float floor = std::max(1e-6f, overall * dbToGain(-kMostEnhanceDb));
                const float steady = 1.f / std::max(overall, 1e-6f);
                float out = 0.f, power = 0.f, weight = 0.f;
                for (int k = 0; k < bands; ++k) {
                    CarBand& b = car_[static_cast<size_t>(c)][static_cast<size_t>(k)];
                    const float y = b.filter[1].process(carCoeffs_[static_cast<size_t>(k)][1],
                                                        b.filter[0].process(carCoeffs_[static_cast<size_t>(k)][0],
                                                                            carIn[c]));
                    const float square = y * y;
                    const float g = gains[static_cast<size_t>(k)].gain;
                    power += g * g * square;
                    weight += g * g;
                    float scaled;
                    if (enhance) {
                        b.level.push(square, enhancePole_, enhancePole_);
                        if (control) b.inverse = 1.f / std::max(std::sqrt(b.level.power()), floor);
                        scaled = y * b.inverse;
                    } else {
                        scaled = y * steady;
                    }
                    out += (k & 1 ? -scaled : scaled) * gains[static_cast<size_t>(k)].gain;  // (see updateLayout())
                }
                // (While the modulator is silent nothing comes out: the level holds.)
                if (weight > 1e-30f) carrierLevel_[static_cast<size_t>(c)].push(power / weight, carrierPole_, carrierHold_);
                // Unvoiced noise, as loud as the modulator's highs.
                if (unvoicedAmount > 0.f) {
                    const float white = noise_[c].next() * 1.7320508f;
                    const float hiss = noiseFilter_[c][1].process(noiseHigh_, noiseFilter_[c][0].process(noiseHigh_, white));
                    out += hiss * unvoicedGain;
                }
                wet[c] = out;
            }
            if (carCount == 1) wet[1] = wet[0];

            const float levelGain = level_.next();
            const float mix = mix_.next();
            float peak = 0.f;
            for (int c = 0; c < n; ++c) {
                const float dry = ch[c][i];
                ch[c][i] = dry * (1.f - mix) + wet[c] * levelGain * mix;
                peak = std::max(peak, std::abs(ch[c][i]));
            }

            peakOut_ = std::max(peakOut_, peak);
            if (++displayCount_ == kDisplaySamples) publishDisplays(carrier == PitchTracking);
        }
    }

private:
    struct ModBand {
        std::array<dsp::Biquad, vocoder::kSections> filter{};
        float power = 0.f;  // its mean square, followed
        float gate = 1.f;   // Gate's gain, smoothed (every kControlSamples)
        float gain = 0.f;   // what it lets of the carrier's band through
        float step = 0.f;   // added to it each sample, to reach its goal
        float goal = 0.f;   // where it is when the gains are next worked out (exactly: 0 is silence)
        float target = 0.f; // (updateGains()'s scratch)
    };
    struct CarrierLevel {
        float mean = 0.f, held = 0.f;  // mean square, and its peaks held
        void push(float square, float meanPole, float holdPole) noexcept {
            mean = square + meanPole * (mean - square);
            held = std::max(square, holdPole * held);
            if (held < 1e-30f) mean = held = 0.f;
        }
        float power() const noexcept { return std::max(mean, kOnsetShare * held); }
    };
    struct CarBand {
        std::array<dsp::Biquad, vocoder::kSections> filter{};
        CarrierLevel level;  // Enhance: its level
        float inverse = 0.f; // and what brings it to 1 (every kControlSamples)
    };
    struct LayoutKey {
        int bands = 0;
        float low = 0.f, high = 0.f, bandwidth = 0.f, formant = 0.f;
        bool retro = false;
        double rate = 0.0;
        bool operator==(const LayoutKey&) const = default;
    };

    // The gains a modulator's band levels make, reached over the next kControlSamples. Depth moves each band from
    // the bands' mean (in dB), then all are scaled back to the modulator's power.
    void updateGains(std::array<ModBand, kMaxBands>& modBands, int bands, float total, float depth, bool gated,
                     float gateLevel) noexcept {
        const float meanDb = depth != 1.f ? 10.f * fastmath::log10(total / static_cast<float>(bands) + 1e-30f) : 0.f;
        float shaped = 0.f;
        for (int k = 0; k < bands; ++k) {
            ModBand& b = modBands[static_cast<size_t>(k)];
            float target = std::sqrt(b.power);
            if (depth != 1.f && total > 0.f) {
                const float db = std::max(10.f * fastmath::log10(b.power + 1e-30f), meanDb - 120.f);
                target = expDbToGain(meanDb + depth * (db - meanDb));
            }
            b.target = target;
            shaped += target * target;
        }
        const float scale = compensation_ * (depth != 1.f && shaped > 0.f ? std::sqrt(total / shaped) : 1.f);
        for (int k = 0; k < bands; ++k) {
            ModBand& b = modBands[static_cast<size_t>(k)];
            float target = b.target * scale * bandLevel_[static_cast<size_t>(k)];
            if (gated) {
                const float open = std::sqrt(b.power) >= gateLevel ? 1.f : 0.f;
                b.gate = dsp::flushTiny(open + gatePole_ * (b.gate - open));
                target *= b.gate;
            }
            b.gain = b.goal;  // (where the ramp was headed, without its rounding)
            b.goal = target;
            b.step = (target - b.gain) / static_cast<float>(kControlSamples);
        }
    }

    // The bands' filters, made again when their settings change.
    void updateLayout() noexcept {
        const LayoutKey key{vocoder::bandCount(choiceIndex(Bands)), param(Low), param(High), param(Bandwidth),
                            param(Formant), choiceIndex(Filter) == 1, sampleRate_};
        if (key == layoutKey_) return;
        const bool rebuilt = key.bands != layoutKey_.bands || key.retro != layoutKey_.retro;
        LayoutKey bank = key, before = layoutKey_;
        bank.formant = before.formant = 0.f;
        const bool reshaped = !(bank == before);
        layoutKey_ = key;
        layout_ = vocoder::layout(key.bands, key.low, key.high, key.bandwidth / 100.0, key.retro);
        // A band past the top (or moved there by Formant) is left out: clamped there, they would all play one band.
        const double top = vocoder::kTopFraction * sampleRate_;
        const double shift = std::exp2(double(key.formant) / 12.0);
        for (int k = 0; k < layout_.count; ++k) {
            const vocoder::Band& band = layout_.bands[static_cast<size_t>(k)];
            modCoeffs_[static_cast<size_t>(k)] = vocoder::sections(layout_, k, 0.0, sampleRate_);
            carCoeffs_[static_cast<size_t>(k)] = vocoder::sections(layout_, k, key.formant, sampleRate_);
            const bool fits = band.centre <= top && band.centre * shift <= top;
            bandLevel_[static_cast<size_t>(k)] = fits ? dbToGain(static_cast<float>(band.gainDb)) : 0.f;
        }
        // Neighbouring bands overlap (at -3 dB, as BW 100 % has them), so the modulator's bands hold more power than
        // it has, and on the carrier's side they would partly cancel where they meet (two sections apart in phase
        // by about 130 degrees there): every other band's polarity is turned (about 50 degrees: they add), and the
        // gains are divided by the bank's mean power gain across its range (once for each side), so the output is
        // as loud as the input.
        // (Real-time, as automation moves the bands: kPoints frequencies, the bands within kNear of each.)
        if (reshaped) {
            constexpr int kPoints = 16, kNear = 4;
            double sum = 0.0;
            const int last = layout_.count - 1;
            const double low = layout_.bands[0].centre, high = layout_.bands[static_cast<size_t>(last)].centre;
            for (int i = 0; i < kPoints; ++i) {
                const double at = (i + 0.5) / kPoints;
                const double f = std::min(low * std::pow(high / low, at), top);
                const int nearest = static_cast<int>(std::lround(at * last));
                for (int k = std::max(0, nearest - kNear); k <= std::min(last, nearest + kNear); ++k)
                    sum += std::pow(10.0, vocoder::bandResponseDb(layout_, k, 0.0, f, sampleRate_) / 10.0);
            }
            compensation_ = static_cast<float>(1.0 / std::max(sum / kPoints, 1e-3));  // (twice: both sides overlap)
        }
        if (rebuilt) {  // other bands: their states start afresh
            for (auto& channel : mod_)
                for (auto& band : channel) band = {};
            for (auto& channel : car_)
                for (auto& band : channel) band = {};
        }
    }

    float noiseSample(int c, double step, float crackle, float chance, float level) noexcept {
        noiseClock_[c] += step;
        if (noiseClock_[c] >= 1.0) {
            noiseClock_[c] -= std::floor(noiseClock_[c]);
            const float white = noise_[c].next();
            if (crackle >= 1.f) {
                held_[c] = white * 1.7320508f;  // (unit RMS)
            } else {
                const float u = 0.5f * (noise_[c].next() + 1.f);
                held_[c] = u < chance ? (white < 0.f ? -level : level) : 0.f;
            }
        }
        return held_[c];
    }

    static double polyBlep(double t, double dt) noexcept {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }

    float oscillator(double ratio, int wave) noexcept {
        if (target_ <= 0.0) target_ = 110.0;  // nothing heard yet
        const double wanted = std::min(target_ * ratio, vocoder::kTopFraction * sampleRate_);
        frequency_ = frequency_ <= 0.0 ? wanted : wanted + frequencyPole_ * (frequency_ - wanted);
        const double dt = frequency_ / sampleRate_;
        double value;
        if (wave == 0) {
            value = 2.0 * oscPhase_ - 1.0 - polyBlep(oscPhase_, dt);
        } else {
            const double width = 0.5 / double(1 << (wave - 1));  // 50 %, 25 %, 12.5 %
            value = oscPhase_ < width ? 1.0 : -1.0;
            value += polyBlep(oscPhase_, dt);
            double fall = oscPhase_ + 1.0 - width;
            fall -= std::floor(fall);
            value -= polyBlep(fall, dt);
            value -= 2.0 * width - 1.0;  // (no DC)
        }
        oscPhase_ += dt;
        oscPhase_ -= std::floor(oscPhase_);
        return static_cast<float>(value);
    }

    void publishDisplays(bool tracking) noexcept {
        for (int k = 0; k < kDisplayBands; ++k) {
            float db = kFloorDb;
            if (k < layout_.count)
                db = std::max(kFloorDb, 10.f * std::log10(mod_[0][static_cast<size_t>(k)].power + 1e-12f));
            publish(BandsDisplay, db);
        }
        publish(PitchDisplay, tracking && tracker_.voiced() ? static_cast<float>(frequency_) : 0.f);
        publish(OutputDisplay, std::max(kFloorDb, gainToDb(peakOut_)));
        displayCount_ = 0;
        peakOut_ = 0.f;
    }

    // A power follower's coefficient for a time in ms.
    float follower(float ms) const noexcept {
        return static_cast<float>(onePoleCoefficient(std::max(0.01f, ms) * 0.001, sampleRate_));
    }

    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = [] {
            static const std::vector<std::string> kCarriers = {"Noise", "External", "Modulator", "Pitch Tracking"};
            static const std::vector<std::string> kWaves = {"Saw", "Pulse 50%", "Pulse 25%", "Pulse 12.5%"};
            static const std::vector<std::string> kSpeeds = {"Fast", "Slow"};
            static const std::vector<std::string> kBands = [] {
                std::vector<std::string> labels;
                for (int count = 4; count <= vocoder::kMaxBands; count += 4) labels.push_back(std::to_string(count));
                return labels;
            }();
            static const std::vector<std::string> kFilters = {"Precise", "Retro"};
            static const std::vector<std::string> kChannelModes = {"Mono", "Stereo", "L/R"};
            return std::vector<ParamInfo>{
                {"carrier", "Carrier", "", 0.f, 3.f, 0.f, false, kCarriers},
                {"noise_rate", "Rate", "Hz", 100.f, 20000.f, 20000.f, true},
                {"crackle", "Crackle", "%", 0.f, 100.f, 100.f},
                {"pitch", "Pitch", "st", -24.f, 24.f, 0.f},
                {"wave", "Wave", "", 0.f, 3.f, 0.f, false, kWaves},
                {"track_low", "Low", "Hz", float(vocoder::kLowestPitch), 1000.f, 80.f, true},
                {"track_high", "High", "Hz", 100.f, float(vocoder::kHighestPitch), 1000.f, true},
                {"enhance", "Enhance", "", 0.f, 1.f, 0.f, false, offOnLabels()},
                {"unvoiced", "Unvoiced", "%", 0.f, 100.f, 0.f},
                {"sensitivity", "Sens.", "%", 0.f, 100.f, 50.f},
                {"speed", "Speed", "", 0.f, 1.f, 0.f, false, kSpeeds},
                {"bands", "Bands", "", 0.f, float(kBands.size() - 1), 4.f, false, kBands},
                {"low", "Low", "Hz", 20.f, 18000.f, 80.f, true},
                {"high", "High", "Hz", 20.f, 18000.f, 12000.f, true},
                {"bandwidth", "BW", "%", 10.f, 200.f, 100.f},
                {"filter", "Filter", "", 0.f, 1.f, 0.f, false, kFilters},
                {"formant", "Formant", "st", -12.f, 12.f, 0.f},
                {"gate", "Gate", "dB", -96.f, 0.f, -96.f},
                {"depth", "Depth", "%", 0.f, 200.f, 100.f},
                {"attack", "Attack", "ms", 0.1f, 100.f, 5.f, true},
                {"release", "Release", "ms", 1.f, 3000.f, 150.f, true},
                {"channels", "Channels", "", 0.f, 2.f, 1.f, false, kChannelModes},
                {"level", "Level", "dB", -24.f, 24.f, 0.f},
                {"mix", "Dry/Wet", "%", 0.f, 100.f, 100.f},
            };
        }();
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    LayoutKey layoutKey_;
    vocoder::Layout layout_;
    std::array<std::array<dsp::BiquadCoefficients, vocoder::kSections>, kMaxBands> modCoeffs_{}, carCoeffs_{};
    std::array<float, kMaxBands> bandLevel_{};
    float compensation_ = 1.f;  // the bank's overlap, made up for
    std::array<std::array<ModBand, kMaxBands>, kChannels> mod_{};
    std::array<std::array<CarBand, kMaxBands>, kChannels> car_{};
    std::array<CarrierLevel, kChannels> carrierLevel_{};  // each carrier's band power, weighted by the modulator's
    float carrierPole_ = 0.f, carrierHold_ = 0.f, enhancePole_ = 0.f, gatePole_ = 0.f, frequencyPole_ = 0.f;

    dsp::Noise noise_[kChannels];
    double noiseClock_[kChannels] = {1.0, 1.0};
    float held_[kChannels] = {0.f, 0.f};
    std::array<dsp::Biquad, 2> noiseFilter_[kChannels];
    dsp::BiquadCoefficients noiseHigh_, splitHigh_;
    std::array<dsp::Biquad, 2> split_;
    float highPower_ = 0.f, totalPower_ = 0.f, unvoiced_ = 0.f;

    vocoder::PitchTracker tracker_;
    double target_ = 0.0, frequency_ = 0.0, oscPhase_ = 0.0;

    SmoothedValue level_, mix_;
    int displayCount_ = 0;
    int controlCount_ = 0;  // samples since the gains were last worked out
    float peakOut_ = 0.f;
};

}  // namespace

SUB_REGISTER_BUILTIN(VocoderProcessor, AudioEffect);

}  // namespace sub
