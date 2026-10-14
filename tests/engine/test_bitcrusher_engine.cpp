// The built-in Bitcrusher: its bits, its rate, the mix, and nothing lost when it is wide open.

#include <cmath>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

// A track playing a 440 Hz sine (0.5) for a second, through a Bitcrusher with `values`: the left channel.
Samples crushed(const std::vector<std::pair<std::string, float>>& values, const Samples& input = sine(440.0, 1.0, 0.5)) {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const std::string path = makeWav(stereo(input), 2);
    engine.loadSource(path);
    const uint32_t track = engine.addTrack();
    engine.setTrackClips(track, {clip(path, 0.0, 1.0, 0.0, 1.f)});
    const uint32_t device = engine.addBuiltinProcessor(engine.trackChain(track), "bitcrusher", -1);
    for (const auto& [name, value] : values) setParam(engine, device, name, value);
    return channel(engine.renderOffline(0.0, kSampleRate / 2), 0);
}

}  // namespace

TEST_CASE("the bitcrusher is listed as an audio effect") {
    const sub::BuiltinInfo info = builtinInfo("bitcrusher");
    CHECK_EQ(info.name, std::string("Bitcrusher"));
    CHECK(info.category == sub::BuiltinCategory::AudioEffect);
    const std::vector<std::string> ids{"bits", "rate", "drive", "dither", "tone", "output", "mix"};
    CHECK(paramIds(info.params) == ids);
}

TEST_CASE("bits round the signal to their levels") {
    const Samples out = crushed({{"bits", 3.f}, {"rate", 48000.f}});
    // 3 bits: 4 levels either side of zero, a quarter apart.
    for (const float x : out) REQUIRE(std::abs(x * 4.f - std::round(x * 4.f)) < 1e-5f);
    CHECK_NEAR(maxAbs(out), 0.5, 1e-6);
    const Samples fine = crushed({{"bits", 16.f}, {"rate", 48000.f}});
    const Samples input = slice(sine(440.0, 1.0, 0.5), 0, kSampleRate / 2);
    CHECK(maxAbs(slice(fine, 0, 1000)) > 0.4);
    for (size_t i = 0; i < fine.size(); ++i) REQUIRE(std::abs(fine[i] - input[i]) <= 1.f / 32768.f + 1e-6f);
}

TEST_CASE("the rate holds each sample") {
    const Samples out = crushed({{"bits", 16.f}, {"rate", 4800.f}});
    // A tenth of the rate: each value held for 10 samples.
    int changes = 0;
    for (size_t i = 1; i < out.size(); ++i) changes += out[i] != out[i - 1] ? 1 : 0;
    CHECK_NEAR(changes, static_cast<int>(out.size() / 10), 2);
    CHECK(dominantFreq(out) > 430.0);  // the tone is still there under the steps
}

TEST_CASE("dry passes the input as it is") {
    const Samples out = crushed({{"bits", 1.f}, {"rate", 200.f}, {"mix", 0.f}});
    const Samples input = slice(sine(440.0, 1.0, 0.5), 0, kSampleRate / 2);
    for (size_t i = 0; i < out.size(); ++i) REQUIRE(std::abs(out[i] - input[i]) < 1e-4f);  // (the file's 16 bits)
}

TEST_CASE("drive, dither and tone stay in range") {
    const Samples out = crushed({{"bits", 4.f}, {"rate", 8000.f}, {"drive", 24.f}, {"dither", 100.f}, {"tone", 30.f}});
    CHECK(allFinite(out));
    CHECK(maxAbs(out) <= 1.0 + 1e-6);
    CHECK(rms(out) > 0.3);  // driven into the clip: nearly a square
}
