// The built-in Vocoder: its listing; its bands (where they are, Precise and Retro); each carrier (Noise, External,
// Modulator, Pitch Tracking) speaking with the modulator's spectrum at about its level, Enhance or not; Formant,
// Depth and Gate; Unvoiced; the channel modes; Dry/Wet; silence; and its displays.

#include <cmath>
#include <string>
#include <vector>

#include "builtin/VocoderDesign.h"
#include "harness/Standalone.h"

using namespace subtest;

namespace {

constexpr int64_t kLength = 32768;  // (a power of two: the spectra's)

// A Vocoder on its own (harness/Standalone.h), with a sidechain if given.
struct Vocoder : Standalone {
    explicit Vocoder(const ParamValues& values = {}, double rate = kSampleRate) : Standalone("vocoder", rate, values) {}

    std::pair<Samples, Samples> playKeyed(Samples left, Samples right, const Samples& keyLeft, const Samples& keyRight) {
        processor().setSidechainConnected(true);
        run({&left, &right}, {}, 256, [&](int64_t start, int) {
            processor().setSidechain(keyLeft.data() + start, keyRight.data() + start);
        });
        processor().setSidechain(nullptr, nullptr);
        return {std::move(left), std::move(right)};
    }
};

// A sawtooth (naive: rich in harmonics) at `freq`.
Samples saw(double freq, int64_t length, double amplitude = 0.5) {
    Samples x(static_cast<size_t>(length));
    for (int64_t i = 0; i < length; ++i) x[size_t(i)] = float(amplitude * (2.0 * frac(freq * double(i) / kSampleRate) - 1.0));
    return x;
}

Samples tone(double freq, double amplitude = 0.25) { return slice(sine(freq, double(kLength) / kSampleRate, amplitude), 0, kLength); }

// The second half (the bands settled).
Samples settled(const Samples& x) { return slice(x, int64_t(x.size()) / 2); }

}  // namespace

TEST_CASE("vocoder: listed as an audio effect with a sidechain") {
    const sub::BuiltinInfo info = builtinInfo("vocoder");
    CHECK_EQ(info.name, std::string("Vocoder"));
    CHECK(info.category == sub::BuiltinCategory::AudioEffect);
    const std::vector<std::string> ids{"carrier", "noise_rate", "crackle", "pitch",  "wave",     "track_low",
                                       "track_high", "enhance", "unvoiced", "sensitivity", "speed", "bands",
                                       "low",     "high",       "bandwidth", "filter", "formant", "gate",
                                       "depth",   "attack",     "release", "channels", "level",  "mix"};
    CHECK(paramIds(info.params) == ids);
    Vocoder vocoder;
    CHECK(vocoder.processor().hasSidechain());
}

TEST_CASE("vocoder: bands from Low to High, evenly in log frequency") {
    for (int choice = 0; choice < 10; ++choice) CHECK_EQ(sub::vocoder::bandCount(choice), 4 * (choice + 1));
    const auto l = sub::vocoder::layout(20, 80.0, 12000.0, 1.0, false);
    REQUIRE(l.count == 20);
    CHECK_NEAR(l.bands[0].centre, 80.0, 1e-9);
    CHECK_NEAR(l.bands[19].centre, 12000.0, 1e-6);
    const double ratio = l.bands[1].centre / l.bands[0].centre;
    for (int k = 1; k < 20; ++k) CHECK_NEAR(l.bands[size_t(k)].centre / l.bands[size_t(k - 1)].centre, ratio, 1e-9);
    // Precise: each band at 0 dB at its centre, the same width all the way up, -3 dB half a spacing out at BW 100 %.
    for (int k = 0; k < 20; ++k) {
        const double centre = l.bands[size_t(k)].centre;
        CHECK_NEAR(sub::vocoder::bandResponseDb(l, k, 0.0, centre, 48000.0), 0.0, 0.05);
        const double edge = centre * std::sqrt(ratio);
        CHECK_NEAR(sub::vocoder::bandResponseDb(l, k, 0.0, edge, 48000.0), -3.0, 0.6);
    }
    // Retro: the higher bands narrower and louder.
    const auto retro = sub::vocoder::layout(20, 80.0, 12000.0, 1.0, true);
    CHECK(retro.bands[19].octaves < retro.bands[0].octaves);
    CHECK(retro.bands[19].gainDb > retro.bands[0].gainDb + 6.0);
    // Formant moves the band: a semitone shift of 12 is an octave.
    CHECK_NEAR(sub::vocoder::bandResponseDb(l, 7, 12.0, 2.0 * l.bands[7].centre, 48000.0), 0.0, 0.05);
}

TEST_CASE("vocoder: the noise carrier speaks with the modulator's spectrum, at about its level") {
    for (const float enhance : {0.f, 1.f}) {
        INFO("enhance " + std::to_string(enhance));
        Vocoder vocoder({{"enhance", enhance}});
        const Samples in = tone(1000.0);
        auto [left, right] = vocoder.play(in, in);
        CHECK(allFinite(left));
        const Samples out = settled(left);
        // The energy sits about 1 kHz: far more there than an octave and more away.
        CHECK(bandDb(out, 700.0, 1400.0) > bandDb(out, 3000.0, 20000.0) + 25.0);
        CHECK(bandDb(out, 700.0, 1400.0) > bandDb(out, 20.0, 350.0) + 25.0);
        // About as loud as the modulator.
        CHECK(std::abs(rmsDb(out) - rmsDb(settled(in))) < 4.0);
        CHECK(rmsDb(slice(right, kLength / 2)) > rmsDb(settled(in)) - 4.0);
    }
}

TEST_CASE("vocoder: silence in, silence out") {
    Vocoder vocoder;
    auto [left, right] = vocoder.play(Samples(kLength, 0.f), Samples(kLength, 0.f));
    CHECK_EQ(maxAbs(left), 0.0);
    CHECK_EQ(maxAbs(right), 0.0);
}

TEST_CASE("vocoder: a sound stopping rings out to exact zeros") {
    // Whatever the carrier: once the modulator's bands have fallen (a short Release) or closed (Gate), nothing at
    // all comes out.
    for (const auto& [carrier, ending] :
         {std::pair{0.f, ParamValues{{"release", 5.f}}}, std::pair{0.f, ParamValues{{"gate", -20.f}}},
          std::pair{2.f, ParamValues{{"release", 5.f}}}, std::pair{3.f, ParamValues{{"release", 5.f}}},
          std::pair{3.f, ParamValues{{"gate", -20.f}}}}) {
        INFO("carrier " + std::to_string(carrier) + ", " + describe(ending));
        Vocoder vocoder(ParamValues{{"carrier", carrier}, {"enhance", 1.f}, {"unvoiced", 100.f}, {"depth", 150.f}} + ending);
        Samples in = tone(440.0);
        std::fill(in.begin() + kLength / 4, in.end(), 0.f);
        auto [left, right] = vocoder.play(in, in);
        CHECK(maxAbs(slice(left, 0, kLength / 4)) > 0.01);
        CHECK_EQ(maxAbs(slice(left, kLength - 4800)), 0.0);
        CHECK_EQ(maxAbs(slice(right, kLength - 4800)), 0.0);
    }
}

TEST_CASE("vocoder: a modulator starting after silence comes in without a spike") {
    // The carrier's level is only heard once the modulator opens a band: its first samples must not be scaled
    // by a level from before (silence), with Enhance or not, the carrier on all along or starting late.
    Samples late = saw(220.0, kLength);
    std::fill(late.begin(), late.begin() + kLength / 4 + 3, 0.f);  // (between two of the gains' updates)
    for (const float carrier : {0.f, 1.f, 3.f}) {
        for (const float enhance : {0.f, 1.f}) {
            INFO("carrier " + std::to_string(carrier) + ", enhance " + std::to_string(enhance));
            Samples in = tone(440.0);
            std::fill(in.begin(), in.begin() + kLength / 4, 0.f);
            Vocoder vocoder({{"carrier", carrier}, {"enhance", enhance}});
            const auto [left, right] = vocoder.playKeyed(in, in, late, late);
            CHECK(allFinite(left));
            CHECK(maxAbs(left) < 1.0);
            CHECK(maxAbs(slice(left, 0, kLength / 4)) == 0.0);
        }
    }
}

TEST_CASE("vocoder: Dry/Wet 0 % passes the input") {
    Vocoder vocoder({{"mix", 0.f}});
    const Samples in = tone(440.0);
    auto [left, right] = vocoder.play(in, in);
    for (size_t i = 0; i < in.size(); ++i) REQUIRE(left[i] == in[i]);
}

TEST_CASE("vocoder: an external carrier, from the sidechain") {
    const Samples voice = noise(size_t(kLength), 7, 0.3f);
    const Samples synth = saw(220.0, kLength);
    for (const float enhance : {0.f, 1.f}) {
        INFO("enhance " + std::to_string(enhance));
        Vocoder vocoder({{"carrier", 1.f}, {"enhance", enhance}});
        auto [left, right] = vocoder.playKeyed(voice, voice, synth, synth);
        const Samples out = settled(left);
        // The carrier's harmonics come through: the strongest at a multiple of 220 Hz.
        const double strongest = dominantFreq(out);
        CHECK(std::abs(strongest / 220.0 - std::round(strongest / 220.0)) < 0.02);
        // Enhanced, as loud as the modulator; as it is, the saw's dark spectrum keeps it quieter under bright noise.
        if (enhance > 0.f)
            CHECK(std::abs(rmsDb(out) - rmsDb(settled(voice))) < 4.0);
        else
            CHECK(rmsDb(out) > rmsDb(settled(voice)) - 15.0);
    }
    // With no sidechain, no carrier: silence.
    Vocoder alone({{"carrier", 1.f}});
    auto [quiet, quietRight] = alone.play(voice, voice);
    CHECK_EQ(maxAbs(quiet), 0.0);
}

TEST_CASE("vocoder: the modulator as its own carrier") {
    Vocoder vocoder({{"carrier", 2.f}});
    const Samples in = saw(150.0, kLength, 0.3);
    auto [left, right] = vocoder.play(in, in);
    const Samples out = settled(left);
    const double strongest = dominantFreq(out);
    CHECK(std::abs(strongest / 150.0 - std::round(strongest / 150.0)) < 0.02);
    CHECK(std::abs(rmsDb(out) - rmsDb(settled(in))) < 6.0);
}

TEST_CASE("vocoder: pitch tracking follows the modulator's pitch, moved by Pitch") {
    for (const auto& [pitch, ratio] : {std::pair{0.f, 1.0}, std::pair{12.f, 2.0}, std::pair{-12.f, 0.5}}) {
        INFO("pitch " + std::to_string(pitch));
        Vocoder vocoder({{"carrier", 3.f}, {"pitch", pitch}});
        const Samples in = saw(196.0, kLength, 0.3);
        auto [left, right] = vocoder.play(in, in);
        const std::vector<float> tracked = vocoder.display("pitch");
        REQUIRE(tracked.size() > 10);
        CHECK_NEAR(tracked.back(), 196.0 * ratio, 196.0 * ratio * 0.01);
        const double strongest = dominantFreq(settled(left));
        const double fundamental = 196.0 * ratio;
        CHECK(std::abs(strongest / fundamental - std::round(strongest / fundamental)) < 0.03);
    }
    // The tracker alone, on a sine, between its limits.
    sub::vocoder::PitchTracker tracker;
    tracker.prepare(44100.0);
    tracker.setRange(50.0, 1000.0);
    const Samples s = sine(330.0, 0.5, 0.5, 44100.0);
    for (const float x : s) tracker.push(x);
    CHECK(tracker.voiced());
    CHECK_NEAR(tracker.pitch(), 330.0, 1.0);
}

TEST_CASE("vocoder: Formant moves the carrier's bands") {
    Vocoder up({{"formant", 12.f}});
    const Samples in = tone(1000.0);
    auto [left, right] = up.play(in, in);
    const Samples out = settled(left);
    CHECK(bandDb(out, 1500.0, 2800.0) > bandDb(out, 600.0, 1300.0) + 10.0);
}

TEST_CASE("vocoder: Depth and Gate") {
    // Depth 0: the modulator's spectrum doesn't count: the (enhanced, so even) noise comes out over all the bands,
    // as loud as the modulator. 200 %: the band it is in stands out further than at 100 %.
    const Samples in = tone(1000.0);
    Vocoder flat({{"depth", 0.f}, {"enhance", 1.f}});
    const Samples out = settled(flat.play(in, in).first);
    CHECK(std::abs(bandDb(out, 4000.0, 8000.0) - bandDb(out, 500.0, 1000.0)) < 6.0);
    CHECK(std::abs(rmsDb(out) - rmsDb(settled(in))) < 4.0);
    const Samples normal = settled(Vocoder({{"enhance", 1.f}}).play(in, in).first);
    const Samples deep = settled(Vocoder({{"depth", 200.f}, {"enhance", 1.f}}).play(in, in).first);
    const auto contrast = [](const Samples& x) { return bandDb(x, 700.0, 1400.0) - bandDb(x, 125.0, 250.0); };
    CHECK(contrast(deep) > contrast(normal) + 10.0);
    // A modulator under Gate: silence.
    Vocoder gated({{"gate", -20.f}});
    const Samples quiet = tone(1000.0, 0.01);
    auto [g, gr] = gated.play(quiet, quiet);
    CHECK(rmsDb(settled(g)) < -100.0);
    Vocoder open({{"gate", -60.f}});
    auto [o, orr] = open.play(quiet, quiet);
    CHECK(rmsDb(settled(o)) > -60.0);
}

TEST_CASE("vocoder: Unvoiced adds noise for the modulator's hiss") {
    // A sibilant: noise high-passed (a difference of noise).
    Samples hiss = noise(size_t(kLength), 3, 0.3f);
    for (size_t i = hiss.size() - 1; i > 0; --i) hiss[i] -= hiss[i - 1];
    Vocoder without({{"carrier", 3.f}});
    Vocoder with({{"carrier", 3.f}, {"unvoiced", 100.f}, {"sensitivity", 80.f}});
    const Samples a = settled(without.play(hiss, hiss).first);
    const Samples b = settled(with.play(hiss, hiss).first);
    CHECK(bandDb(b, 5000.0, 20000.0) > bandDb(a, 5000.0, 20000.0) + 3.0);
    // On a pitched modulator it stays out.
    const Samples voiced = saw(150.0, kLength, 0.3);
    const Samples c = settled(Vocoder({{"carrier", 3.f}}).play(voiced, voiced).first);
    const Samples d = settled(Vocoder({{"carrier", 3.f}, {"unvoiced", 100.f}}).play(voiced, voiced).first);
    CHECK(std::abs(rmsDb(c) - rmsDb(d)) < 1.0);
}

TEST_CASE("vocoder: channels") {
    const Samples in = tone(1000.0);
    const Samples none(size_t(kLength), 0.f);
    // L/R: each side its own modulator: the silent right stays silent.
    Vocoder lr({{"channels", 2.f}});
    auto [l, r] = lr.play(in, none);
    CHECK(rmsDb(settled(l)) > -30.0);
    CHECK_EQ(maxAbs(r), 0.0);
    // Mono: one carrier for both, the same both sides.
    Vocoder mono({{"channels", 0.f}});
    auto [ml, mr] = mono.play(in, none);
    CHECK(ml == mr);
    // Stereo: a noise carrier of its own each side.
    Vocoder stereo({{"channels", 1.f}});
    auto [sl, sr] = stereo.play(in, in);
    CHECK(sl != sr);
    CHECK(std::abs(rmsDb(settled(sl)) - rmsDb(settled(sr))) < 2.0);
    // One channel.
    Vocoder one;
    const Samples single = one.play(in);
    CHECK(rmsDb(settled(single)) > -30.0);
}

TEST_CASE("vocoder: every band count, filter and rate stays finite and speaks") {
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        for (const float bands : {0.f, 9.f}) {
            for (const float filter : {0.f, 1.f}) {
                INFO(std::to_string(rate) + " " + std::to_string(bands) + " " + std::to_string(filter));
                Vocoder vocoder({{"bands", bands}, {"filter", filter}, {"high", 18000.f}, {"formant", 12.f}}, rate);
                const Samples in = noise(size_t(kLength), 11, 0.5f);
                auto [left, right] = vocoder.play(in, in);
                CHECK(allFinite(left));
                CHECK(rmsDb(settled(left)) > -40.0);
                CHECK(rmsDb(settled(left)) < 6.0);
            }
        }
    }
}

TEST_CASE("vocoder: displays") {
    Vocoder vocoder({{"bands", 4.f}});  // 20 bands
    const Samples in = tone(1000.0);
    vocoder.play(in, in);
    const std::vector<float> bands = vocoder.display("bands");
    REQUIRE(bands.size() >= 64);
    REQUIRE(bands.size() % 64 == 0);
    const std::vector<float> last(bands.end() - 64, bands.end());
    // The loudest band is the one nearest 1 kHz; the slots past the 20 bands are at the floor.
    const auto l = sub::vocoder::layout(20, 80.0, 12000.0, 1.0, false);
    int nearest = 0;
    for (int k = 0; k < 20; ++k)
        if (std::abs(std::log(l.bands[size_t(k)].centre / 1000.0)) < std::abs(std::log(l.bands[size_t(nearest)].centre / 1000.0)))
            nearest = k;
    CHECK_EQ(int(std::max_element(last.begin(), last.begin() + 20) - last.begin()), nearest);
    for (size_t k = 20; k < 64; ++k) CHECK_EQ(last[k], -96.f);
    const std::vector<float> output = vocoder.display("output");
    REQUIRE(!output.empty());
    CHECK(output.back() > -30.f);
    const std::vector<float> pitch = vocoder.display("pitch");
    REQUIRE(!pitch.empty());
    CHECK_EQ(pitch.back(), 0.f);  // (not tracking)
}
