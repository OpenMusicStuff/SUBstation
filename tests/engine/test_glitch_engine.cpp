// The built-in Glitch: its steps on the song's grid, each kind (stutter, reverse, tape, gate) doing what it says
// sample for sample, the chance and the variation (the same render twice, another with another variation), a step
// entered partway left alone, the mix and its displays.

#include <cmath>
#include <map>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

constexpr int64_t kEighth = kBeat / 2;  // a step of the default grid (1/8) at the engine's 120 BPM
constexpr int64_t kEdge = 200;          // samples clear of a step's or a part's crossfades (2 ms: 96)

// A track playing `input` (mono, in both channels) through a Glitch with `values`, or none: the left channel.
Samples glitched(const std::map<std::string, float>& values, const Samples& input, bool withDevice = true,
                 double startBeat = 0.0, double seconds = 2.0) {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const std::string path = makeWav(stereo(input), 2);
    engine.loadSource(path);
    const uint32_t track = engine.addTrack();
    engine.setTrackClips(track, {clip(path, 0.0, static_cast<double>(input.size()) / kSampleRate, 0.0, 1.f)});
    if (withDevice) {
        const uint32_t device = engine.addBuiltinProcessor(engine.trackChain(track), "glitch", -1);
        for (const auto& [name, value] : values) setParam(engine, device, name, value);
    }
    return channel(engine.renderOffline(startBeat, static_cast<int64_t>(seconds * kSampleRate)), 0);
}

const Samples& noiseInput() {
    static const Samples kNoise = noise(3 * kSampleRate, 7, 0.5f);
    return kNoise;
}

// Only `kind` (the others' weights 0), every step.
std::map<std::string, float> only(const std::string& kind) {
    std::map<std::string, float> values{{"chance", 100.f}, {"stutter", 0.f}, {"reverse", 0.f}, {"tape", 0.f}, {"gate", 0.f}};
    values[kind] = 100.f;
    return values;
}

float sampleAt(const Samples& x, int64_t i) { return x[static_cast<size_t>(i)]; }

}  // namespace

TEST_CASE("the glitch is listed as an audio effect") {
    const sub::BuiltinInfo info = builtinInfo("glitch");
    CHECK_EQ(info.name, std::string("Glitch"));
    CHECK(info.category == sub::BuiltinCategory::AudioEffect);
    const std::vector<std::string> ids{"grid", "chance", "stutter", "reverse", "tape", "gate", "variation", "mix"};
    CHECK(paramIds(info.params) == ids);
}

TEST_CASE("at no chance the glitch passes the input as it is") {
    const Samples dry = glitched({}, noiseInput(), false);
    const Samples out = glitched({{"chance", 0.f}}, noiseInput());
    REQUIRE(out.size() == dry.size());
    for (size_t i = 0; i < out.size(); ++i) REQUIRE(out[i] == dry[i]);
}

TEST_CASE("a stutter repeats the step's start") {
    const Samples dry = glitched({}, noiseInput(), false);
    const Samples out = glitched(only("stutter"), noiseInput());
    std::map<int, int> parts;
    for (int64_t step = 0; step < 8; ++step) {
        const int64_t start = step * kEighth;
        int found = 0;
        for (const int n : {2, 3, 4, 6, 8}) {
            const int64_t repeat = (kEighth + n / 2) / n;
            bool repeats = true;
            for (int64_t r = 0; r < n && repeats; ++r)
                for (int64_t t = kEdge; t < repeat - kEdge && repeats; ++t)
                    repeats = sampleAt(out, start + r * repeat + t) == sampleAt(dry, start + t);
            if (repeats) found = n;
        }
        CHECK(found > 0);
        ++parts[found];
    }
    CHECK(parts.size() > 1);  // repeats of more than one length
}

TEST_CASE("a reverse plays the step before backwards") {
    const Samples dry = glitched({}, noiseInput(), false);
    const Samples out = glitched(only("reverse"), noiseInput());
    for (int64_t step = 1; step < 8; ++step) {
        const int64_t start = step * kEighth;
        for (int64_t t = kEdge; t < kEighth - kEdge; ++t)
            REQUIRE(sampleAt(out, start + t) == sampleAt(dry, start - 1 - t));
    }
}

TEST_CASE("a gate silences every other part of the step") {
    const Samples dry = glitched({}, noiseInput(), false);
    const Samples out = glitched(only("gate"), noiseInput());
    for (int64_t step = 0; step < 8; ++step) {
        const int64_t start = step * kEighth;
        int found = 0;
        for (const int n : {4, 6, 8}) {
            const int64_t part = (kEighth + n / 2) / n;
            bool gated = true;
            for (int64_t p = 0; p < n && gated; ++p)
                for (int64_t t = kEdge; t < part - kEdge && gated; ++t) {
                    const int64_t i = start + p * part + t;
                    gated = sampleAt(out, i) == (p % 2 == 0 ? sampleAt(dry, i) : 0.f);
                }
            if (gated) found = n;
        }
        CHECK(found > 0);
    }
}

TEST_CASE("a tape step slows to a stop, its pitch falling") {
    // Steps of 1/2 (two beats, a second): half way through, half the speed.
    auto values = only("tape");
    values["grid"] = 4.f;
    const Samples out = glitched(values, sine(440.0, 3.0, 0.5), true, 0.0, 2.0);
    CHECK(allFinite(out));
    for (const int64_t start : {int64_t{0}, int64_t{kSampleRate}}) {
        CHECK_NEAR(dominantFreq(slice(out, start + kSampleRate / 2 - 1024, start + kSampleRate / 2 + 1024)), 220.0, 20.0);
        CHECK_NEAR(dominantFreq(slice(out, start + kSampleRate / 4 - 1024, start + kSampleRate / 4 + 1024)), 330.0, 20.0);
        CHECK(rms(slice(out, start + kSampleRate * 99 / 100, start + kSampleRate - kEdge)) < 0.03);
    }
}

TEST_CASE("the chance glitches some steps; a variation others") {
    const Samples dry = glitched({}, noiseInput(), false);
    const auto changedSteps = [&](const Samples& out) {
        std::vector<int64_t> steps;
        for (int64_t step = 0; step < 16; ++step) {
            for (int64_t t = 0; t < kEighth; ++t)
                if (sampleAt(out, step * kEighth + t) != sampleAt(dry, step * kEighth + t)) {
                    steps.push_back(step);
                    break;
                }
        }
        return steps;
    };
    const Samples first = glitched({{"chance", 50.f}}, noiseInput());
    const Samples again = glitched({{"chance", 50.f}}, noiseInput());
    CHECK(first == again);  // the same place, the same glitches
    const auto steps = changedSteps(first);
    CHECK(steps.size() >= 3);
    CHECK(steps.size() <= 13);
    const Samples other = glitched({{"chance", 50.f}, {"variation", 1.f}}, noiseInput());
    CHECK(other != first);
    CHECK(changedSteps(glitched({{"chance", 100.f}}, noiseInput())).size() == 16);
}

TEST_CASE("a step entered partway plays as it is") {
    // From half way through the first step (beat 0.25): that half as it is, the next step glitched.
    const Samples dry = glitched({}, noiseInput(), false, 0.25);
    const Samples out = glitched(only("reverse"), noiseInput(), true, 0.25);
    for (int64_t i = 0; i < kEighth / 2; ++i) REQUIRE(sampleAt(out, i) == sampleAt(dry, i));
    CHECK(sampleAt(out, kEighth / 2 + 1000) == sampleAt(dry, kEighth / 2 - 1001));
}

TEST_CASE("dry, the glitch passes the input") {
    const Samples dry = glitched({}, noiseInput(), false);
    auto values = only("stutter");
    values["mix"] = 0.f;
    const Samples out = glitched(values, noiseInput());
    for (size_t i = 0; i < out.size(); ++i) REQUIRE(std::abs(out[i] - dry[i]) < 1e-6f);
}

TEST_CASE("the displays: the step and what it does, the input's level") {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const std::string path = makeWav(stereo(sine(440.0, 2.0, 0.5)), 2);
    engine.loadSource(path);
    const uint32_t track = engine.addTrack();
    engine.setTrackClips(track, {clip(path, 0.0, 2.0, 0.0, 1.f)});
    const uint32_t device = engine.addBuiltinProcessor(engine.trackChain(track), "glitch", -1);
    for (const auto& [name, value] : only("gate")) setParam(engine, device, name, value);
    setParam(engine, device, "grid", 0.f);  // 1/32: steps of 3000 samples
    const auto displays = engine.processorDisplays(device);
    REQUIRE(displays.size() == 2);
    CHECK_EQ(displays[0].id, std::string("step"));
    CHECK_EQ(displays[1].id, std::string("level"));
    engine.renderOffline(0.0, 2 * kSampleRate);
    std::vector<float> steps, levels;
    engine.readProcessorDisplay(device, 0, 0, steps);
    engine.readProcessorDisplay(device, 1, 0, levels);
    REQUIRE(steps.size() > 350);
    const auto valueAt = [](const std::vector<float>& values, int64_t sample) {
        return values[static_cast<size_t>(sample / 256)];
    };
    constexpr int64_t kStep = kEighth / 4;
    CHECK_EQ(valueAt(steps, 3 * kStep + 1000), 3.f + 16.f * 4.f);   // step 3, gating
    CHECK_EQ(valueAt(steps, 18 * kStep + 1000), 2.f + 16.f * 4.f);  // step 18: 2 of the next sixteen
    CHECK_NEAR(valueAt(levels, 3 * kStep + 1000), 0.5, 0.01);
}
