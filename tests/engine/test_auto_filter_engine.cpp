// The built-in Auto Filter: its listing, each type and slope playing as its
// design draws it, Morph, the envelope and the LFO moving the cutoff (free,
// synced, Phase and Spin), the sidechain keying the envelope, the circuits'
// saturation staying bounded, Dry/Wet, changes without clicks, and silence.

#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include "Engine.h"
#include "builtin/AutoFilterDesign.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"

using namespace subtest;
namespace af = sub::autofilter;

namespace {

// The gain (dB) a steady sine at `freq` comes out with, after the filter has settled.
double gainDb(const ParamValues& values, double freq, double rate = kSampleRate) {
    Standalone device("auto_filter", rate, values);
    const Samples out = device.play(sine(freq, 1.0, 0.01, rate));
    const auto from = static_cast<int64_t>(0.5 * rate);
    const auto length = static_cast<int64_t>(0.4 * rate);
    return db(windowedAmplitude(out, freq, from, length, rate) / 0.01);
}

af::Weights weightsOf(int type, double morph = 0.0) { return af::weights(static_cast<af::Type>(type), morph); }

}  // namespace

TEST_CASE("the auto filter is listed as an audio effect with a sidechain") {
    const sub::BuiltinInfo info = builtinInfo("auto_filter");
    CHECK_EQ(info.name, std::string("Auto Filter"));
    CHECK(info.category == sub::BuiltinCategory::AudioEffect);
    const std::vector<std::string> ids{"type",       "circuit",    "slope",     "freq",        "resonance",
                                       "drive",      "morph",      "env_amount", "env_attack", "env_release",
                                       "lfo_amount", "lfo_wave",   "lfo_sync",  "lfo_freq",    "lfo_rate",
                                       "spin_on",    "phase",      "spin",      "lfo_offset",  "output",
                                       "mix"};
    CHECK(paramIds(info.params) == ids);
    Standalone device("auto_filter");
    CHECK(device.processor().hasSidechain());
}

TEST_CASE("each type and slope plays as its design draws it") {
    for (int type = 0; type < 4; ++type) {
        for (int slope = 0; slope < 2; ++slope) {
            for (const float resonance : {0.f, 60.f}) {
                for (const double freq : {200.0, 900.0, 1000.0, 1100.0, 5000.0}) {
                    INFO(std::string("type ") + std::to_string(type) + std::string(" slope ") + std::to_string(slope) + std::string(" resonance ") + std::to_string(resonance) + std::string(" at ") + std::to_string(freq));
                    const double want = af::responseDb(freq, 1000.0, resonance, slope == 1, weightsOf(type), kSampleRate);
                    if (want < -60.0) continue;  // (deep in a notch: the window's leakage sets the floor)
                    const double got = gainDb({{"type", float(type)},
                                               {"slope", float(slope)},
                                               {"freq", 1000.f},
                                               {"resonance", resonance}},
                                              freq);
                    CHECK_NEAR(got, want, 0.1);
                }
            }
        }
    }
}

TEST_CASE("the design's types and morph") {
    // At no resonance, 12 dB is Butterworth: -3 dB at the cutoff, either way.
    CHECK_NEAR(af::responseDb(1000.0, 1000.0, 0.0, false, weightsOf(0), 48000.0), -3.0103, 0.01);
    CHECK_NEAR(af::responseDb(1000.0, 1000.0, 0.0, true, weightsOf(1), 48000.0), -3.0103, 0.01);
    // A band-pass is unity at its centre; a notch is deep there.
    CHECK(std::abs(af::responseDb(1000.0, 1000.0, 50.0, false, weightsOf(2), 48000.0)) < 0.01);
    CHECK(af::responseDb(1000.0, 1000.0, 50.0, false, weightsOf(3), 48000.0) < -60.0);
    // Morph passes through each type, a quarter apart, and back to the low-pass.
    const auto same = [](const af::Weights& a, const af::Weights& b) {
        return std::abs(a.lp - b.lp) < 1e-12 && std::abs(a.bp - b.bp) < 1e-12 && std::abs(a.hp - b.hp) < 1e-12;
    };
    CHECK(same(af::morphWeights(0.0), weightsOf(0)));
    CHECK(same(af::morphWeights(25.0), weightsOf(2)));
    CHECK(same(af::morphWeights(50.0), weightsOf(1)));
    CHECK(same(af::morphWeights(75.0), weightsOf(3)));
    CHECK(same(af::morphWeights(100.0), weightsOf(0)));
    // Resonance raises Q, which grows with its square.
    CHECK_APPROX(af::resonanceQ(0.0), 0.70710678118654752);
    CHECK_APPROX(af::resonanceQ(125.0), 25.0);
    CHECK(af::resonanceQ(62.5) < 0.5 * (af::resonanceQ(0.0) + af::resonanceQ(125.0)));
}

TEST_CASE("morph plays its weights") {
    for (const float morph : {0.f, 12.5f, 37.5f, 60.f, 90.f}) {
        for (const double freq : {300.0, 1000.0, 4000.0}) {
            INFO(std::string("morph ") + std::to_string(morph) + std::string(" at ") + std::to_string(freq));
            const double want = af::responseDb(freq, 1000.0, 30.0, false, af::morphWeights(morph), kSampleRate);
            if (want < -50.0) continue;
            const double got = gainDb({{"type", 4.f}, {"freq", 1000.f}, {"resonance", 30.f}, {"morph", morph}}, freq);
            CHECK_NEAR(got, want, 0.15);
        }
    }
}

TEST_CASE("the envelope opens the filter as the sound gets louder") {
    Standalone device("auto_filter", kSampleRate, {{"freq", 300.f}, {"env_amount", 100.f}});
    Samples in = full(kSampleRate / 2, 0.f);
    const Samples loud = sine(200.0, 0.25, 0.9);
    std::copy(loud.begin(), loud.end(), in.begin() + kSampleRate / 4);
    device.play(in);
    const std::vector<float> cutoff = device.display("cutoff_l");
    const std::vector<float> env = device.display("env");
    REQUIRE(cutoff.size() == size_t(kSampleRate / 2 / 128));
    const size_t quiet = cutoff.size() / 8, playing = cutoff.size() * 3 / 4;
    CHECK_APPROX_REL(cutoff[quiet], 300.0, 1e-3);
    CHECK(env[quiet] == 0.f);
    CHECK(env[playing] > 0.9f);
    // At 0.9 the envelope is near 1: close to six octaves up.
    CHECK(cutoff[playing] > 300.0 * std::exp2(af::kEnvelopeOctaves * 0.95));
    // Turned the other way, it closes.
    Standalone down("auto_filter", kSampleRate, {{"freq", 3000.f}, {"env_amount", -50.f}});
    down.play(in);
    CHECK(down.display("cutoff_l")[playing] < 3000.0 / 4.0);
}

TEST_CASE("the sidechain keys the envelope") {
    Standalone device("auto_filter", kSampleRate, {{"freq", 300.f}, {"env_amount", 100.f}});
    const Samples key = sine(200.0, 0.5, 0.9);
    Samples in = sine(1000.0, 0.5, 0.01);
    device.processor().setSidechainConnected(true);
    device.run({&in}, {}, 256, [&](int64_t start, int) {
        device.processor().setSidechain(key.data() + start, key.data() + start);
    });
    const std::vector<float> cutoff = device.display("cutoff_l");
    CHECK(cutoff.back() > 300.0 * 32.0);
    // Connected but silent: the input's own level doesn't move it.
    Standalone silent("auto_filter", kSampleRate, {{"freq", 300.f}, {"env_amount", 100.f}});
    Samples loud = sine(200.0, 0.5, 0.9);
    silent.processor().setSidechainConnected(true);
    silent.run({&loud}, {}, 256, [&](int64_t, int) { silent.processor().setSidechain(nullptr, nullptr); });
    CHECK_APPROX_REL(silent.display("cutoff_l").back(), 300.0, 1e-3);
}

TEST_CASE("the LFO sweeps the cutoff at its rate, each side as Phase and Spin say") {
    const auto cutoffs = [](const ParamValues& values, bool playing = false) {
        Standalone device("auto_filter", kSampleRate, values);
        device.context().playing = playing;
        device.context().tempo = 120.0;
        Samples l = full(kSampleRate * 2, 0.f), r = l;
        device.play(l, r);
        return std::pair{device.display("cutoff_l"), device.display("cutoff_r")};
    };
    // 2 Hz, ±2 octaves round 1 kHz: 375 display values a second, so a cycle every 187.5.
    {
        const auto [l, r] = cutoffs({{"freq", 1000.f}, {"lfo_amount", 50.f}, {"lfo_freq", 2.f}});
        std::vector<double> octaves;
        for (const float hz : l) octaves.push_back(std::log2(hz / 1000.0));
        CHECK_APPROX_REL(maxOf(octaves), 2.0, 0.02);
        CHECK_APPROX_REL(minOf(octaves), -2.0, 0.02);
        CHECK_APPROX_REL(dominantFreq(octaves, 375.0), 2.0, 0.05);
        CHECK(allclose(l, r, 1e-6));  // Phase 0: both alike
    }
    // Phase 180: opposite.
    {
        const auto [l, r] = cutoffs({{"freq", 1000.f}, {"lfo_amount", 50.f}, {"lfo_freq", 2.f}, {"phase", 180.f}});
        for (size_t i = 100; i < l.size(); i += 37)
            CHECK_NEAR(std::log2(l[i] / 1000.0), -std::log2(r[i] / 1000.0), 0.06);
    }
    // Spin: the right side runs faster.
    {
        const auto [l, r] = cutoffs(
            {{"freq", 1000.f}, {"lfo_amount", 50.f}, {"lfo_freq", 2.f}, {"spin_on", 1.f}, {"spin", 50.f}});
        std::vector<double> octaves;
        for (const float hz : r) octaves.push_back(std::log2(hz / 1000.0));
        CHECK_APPROX_REL(dominantFreq(octaves, 375.0), 3.0, 0.05);
    }
    // Synced to the song at 120 BPM, 1/4 of a bar: 2 cycles a second, on the beat.
    {
        const auto [l, r] = cutoffs({{"freq", 1000.f}, {"lfo_amount", 50.f}, {"lfo_sync", 1.f}, {"lfo_rate", 9.f}},
                                    true);
        std::vector<double> octaves;
        for (const float hz : l) octaves.push_back(std::log2(hz / 1000.0));
        CHECK_APPROX_REL(dominantFreq(octaves, 375.0), 2.0, 0.05);
    }
}

TEST_CASE("the circuits saturate, bounded, and Clean doesn't") {
    for (int circuit = 0; circuit < 5; ++circuit) {
        INFO(std::string("circuit ") + std::to_string(circuit));
        Standalone device("auto_filter", kSampleRate,
                          {{"circuit", float(circuit)}, {"resonance", 125.f}, {"drive", 24.f}, {"freq", 500.f}});
        const Samples out = device.play(noise(kSampleRate, 3, 0.9f));
        CHECK(allFinite(out));
        CHECK(maxAbs(out) < (circuit == 0 ? 200.0 : 8.0));
    }
    // Clean ignores Drive.
    CHECK_NEAR(gainDb({{"freq", 2000.f}, {"drive", 24.f}}, 500.0), gainDb({{"freq", 2000.f}}, 500.0), 1e-4);
    // A driven circuit adds harmonics to a loud tone.
    Standalone driven("auto_filter", kSampleRate, {{"circuit", 2.f}, {"drive", 18.f}, {"freq", 10000.f}});
    const Samples out = driven.play(sine(500.0, 0.5, 0.5));
    const int64_t from = kSampleRate / 4, length = kSampleRate / 5;
    CHECK(windowedAmplitude(out, 1500.0, from, length) > 0.01 * windowedAmplitude(out, 500.0, from, length));
}

TEST_CASE("dry/wet and output") {
    const Samples in = noise(kSampleRate / 4, 5, 0.3f);
    Standalone dry("auto_filter", kSampleRate, {{"freq", 200.f}, {"resonance", 100.f}, {"mix", 0.f}});
    CHECK(dry.play(in) == in);
    // Wide open, a low-pass passes the lows at Output's level.
    CHECK_NEAR(gainDb({{"freq", 20000.f}, {"output", -6.f}}, 100.0), -6.0, 0.01);
}

TEST_CASE("changing type, slope and resonance doesn't click") {
    const Samples in = sine(300.0, 1.0, 0.5);
    for (const auto& [id, from, to] : std::vector<std::tuple<std::string, float, float>>{
             {"type", 0.f, 1.f}, {"type", 2.f, 3.f}, {"slope", 0.f, 1.f}, {"resonance", 0.f, 125.f},
             {"circuit", 0.f, 4.f}}) {
        INFO(id);
        Standalone device("auto_filter", kSampleRate, {{"freq", 600.f}, {id, from}});
        const Samples out = device.play(in, {{kSampleRate / 2, id, to}});
        const double around = largestStep(out, kSampleRate / 4, kSampleRate / 2 - 10);
        CHECK(largestStep(out, kSampleRate / 2 - 10, kSampleRate / 2 + 2000) < 2.5 * around + 1e-3);
    }
}

TEST_CASE("silence rings out to exact zeros, and plays the same in any blocks") {
    Standalone device("auto_filter", kSampleRate, {{"resonance", 125.f}, {"circuit", 4.f}, {"drive", 12.f}});
    Samples in = noise(kSampleRate / 10, 9, 0.5f);
    in.resize(kSampleRate * 4, 0.f);
    const Samples out = device.play(in);
    CHECK(allEqual(slice(out, kSampleRate * 3), 0.0));
    const ParamValues settings{{"lfo_amount", 40.f}, {"lfo_wave", 5.f}, {"env_amount", 30.f}, {"resonance", 90.f}};
    Standalone a("auto_filter", kSampleRate, settings), b("auto_filter", kSampleRate, settings);
    const Samples tone = noise(kSampleRate, 11, 0.4f);
    CHECK(a.play(tone, {}, 64) == b.play(tone, {}, 1024));
}
