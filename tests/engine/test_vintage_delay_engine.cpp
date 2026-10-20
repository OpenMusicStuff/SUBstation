// The built-in Vintage Delay: the Delay's parameters with the Tape character (Valhalla Delay's Tape mode at its
// default preset): the echo's time with the tape's wander, the speed's EQ (darker on longer times), repeats that
// thin out at the bottom, the saturation, the splice, and renders that repeat exactly.

#include <algorithm>
#include <cmath>
#include <utility>

#include "Engine.h"
#include "builtin/DelayCharacter.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

constexpr int64_t kClick = 100;

using Values = std::vector<std::pair<std::string, float>>;

// A Vintage Delay, free times of `ms`, fully wet without feedback or filter unless `values` say otherwise.
uint32_t vintageDelay(sub::Engine& engine, uint32_t track, float ms, const Values& values = {}) {
    Values all{{"l_sync", 0.f}, {"l_time", ms}, {"r_sync", 0.f}, {"r_time", ms},
               {"feedback", 0.f}, {"filter", 0.f}, {"mix", 100.f}};
    for (const auto& [name, value] : values) {
        auto it = std::find_if(all.begin(), all.end(), [&](const auto& v) { return v.first == name; });
        if (it != all.end()) it->second = value;
        else all.emplace_back(name, value);
    }
    const uint32_t device = engine.addBuiltinProcessor(engine.trackChain(track), "vintage_delay", -1);
    for (const auto& [name, value] : all) setParam(engine, device, name, value);
    return device;
}

// A tone through a Vintage Delay of `ms`: its level (peak amplitude) over the last half second of `seconds`.
double toneThrough(double freq, double amplitude, float ms, const Values& values = {}, double seconds = 2.0) {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const std::string path = makeWav(stereo(sine(freq, seconds, amplitude)), 2);
    engine.loadSource(path);
    const uint32_t track = engine.addTrack();
    engine.setTrackClips(track, {clip(path, 0.0, seconds, 0.0, 1.f)});
    vintageDelay(engine, track, ms, values);
    const Samples left = channel(engine.renderOffline(0.0, static_cast<int64_t>(seconds * kSampleRate)), 0);
    const auto from = static_cast<int64_t>((seconds - 0.5) * kSampleRate);
    return rms(slice(left, from, from + kSampleRate / 2)) * std::sqrt(2.0);
}

}  // namespace

TEST_CASE("the vintage delay has the delay's parameters") {
    const sub::BuiltinInfo info = builtinInfo("vintage_delay");
    CHECK_EQ(info.name, std::string("Vintage Delay"));
    CHECK(paramIds(info.params) == paramIds(builtinInfo("delay").params));
}

TEST_CASE("the echo comes at the time, give or take the tape's wander") {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const uint32_t track = stereoClickTrack(engine, 1.f, 1.f, kClick, 1.0);
    vintageDelay(engine, track, 100.f);
    const Samples out = engine.renderOffline(0.0, kSampleRate);
    const Samples left = channel(out, 0), right = channel(out, 1);
    CHECK(maxAbs(slice(left, 0, kClick + kSampleRate / 10 - kSampleRate / 100)) < 1e-6);
    const auto peak = std::max_element(left.begin(), left.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
    // The drift wanders a few ms at most this early; the EQ adds a few samples.
    CHECK(std::abs((peak - left.begin()) - (kClick + kSampleRate / 10)) < kSampleRate / 200);
    CHECK(left == right);  // the wander is the same on both sides
}

TEST_CASE("the tape's EQ: a thin bottom and a soft top, darker on longer times") {
    // Valhalla's at 100 ms: -6 dB at 100 Hz, -2.1 dB at 1 kHz, -5.6 dB at 8 kHz.
    CHECK_APPROX_REL(toneThrough(100.0, 0.05, 100.f), 0.05 * std::pow(10.0, -6.0 / 20), 0.06);
    CHECK_APPROX_REL(toneThrough(1000.0, 0.05, 100.f), 0.05 * std::pow(10.0, -2.1 / 20), 0.05);
    CHECK_APPROX_REL(toneThrough(8000.0, 0.05, 100.f), 0.05 * std::pow(10.0, -5.6 / 20), 0.08);
    // At a second the tape runs at a third of the speed: -19 dB at 8 kHz, the bottom fuller (-2.3 dB at 300 Hz).
    CHECK_APPROX_REL(toneThrough(8000.0, 0.05, 1000.f, {}, 3.0), 0.05 * std::pow(10.0, -19.3 / 20), 0.12);
    CHECK_APPROX_REL(toneThrough(300.0, 0.05, 1000.f, {}, 3.0), 0.05 * std::pow(10.0, -2.25 / 20), 0.06);
}

TEST_CASE("the tape saturates loud echoes, the treble first") {
    const double low = toneThrough(300.0, 1.0, 100.f) / toneThrough(300.0, 0.05, 100.f) * 0.05;
    const double high = toneThrough(3000.0, 1.0, 100.f) / toneThrough(3000.0, 0.05, 100.f) * 0.05;
    CHECK(low < 0.85);
    CHECK(high < low);
}

TEST_CASE("repeats lose the bottom: the feedback goes through a 100 Hz high-pass") {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const uint32_t track = stereoClickTrack(engine, 0.1f, 0.1f, kClick, 1.0);
    vintageDelay(engine, track, 200.f, {{"feedback", 50.f}});
    const Samples left = channel(engine.renderOffline(0.0, kSampleRate), 0);
    const int64_t step = kSampleRate / 5;
    const Samples first = slice(left, kClick + step - 2000, kClick + step + 4000);
    const Samples second = slice(left, kClick + 2 * step - 2000, kClick + 2 * step + 4000);
    const double lowRatio = std::pow(10.0, (bandDb(second, 40, 80) - bandDb(first, 40, 80)) / 20);
    const double midRatio = std::pow(10.0, (bandDb(second, 800, 1600) - bandDb(first, 800, 1600)) / 20);
    CHECK_APPROX_REL(midRatio, 0.5 * std::pow(10.0, -2.0 / 20), 0.15);  // the feedback, and the tape's loss
    CHECK(lowRatio < 0.5 * midRatio);
}

TEST_CASE("the splice bumps the time once a pass of the loop") {
    sub::delay_character::TapeModulation modulation;
    modulation.prepare(kSampleRate);
    std::vector<float> bump;
    for (int i = 0; i < 25 * kSampleRate; ++i) bump.push_back(modulation.next(1.f).bump);
    // At full speed the 10 s loop's splice first comes 9.3 s in, then every 10 s; the bump peaks at 1.
    const auto firstAt = std::find_if(bump.begin(), bump.end(), [](float b) { return b > 0.f; }) - bump.begin();
    CHECK_NEAR(static_cast<double>(firstAt) / kSampleRate, 9.3, 0.01);
    CHECK_NEAR(maxAbs(slice(bump, firstAt, firstAt + kSampleRate)), 1.0, 1e-3);
    CHECK(maxAbs(slice(bump, firstAt + 3 * kSampleRate, firstAt + 9 * kSampleRate)) < 0.01);
    CHECK(bump[firstAt + 10 * kSampleRate + kSampleRate / 10] > 0.5f);
}

TEST_CASE("renders repeat exactly") {
    const auto render = [] {
        sub::Engine engine;
        engine.setClipFadeMs(0);
        const std::string path = makeWav(stereo(sine(440.0, 1.0, 0.5)), 2);
        engine.loadSource(path);
        const uint32_t track = engine.addTrack();
        engine.setTrackClips(track, {clip(path, 0.0, 1.0, 0.0, 1.f)});
        vintageDelay(engine, track, 300.f, {{"feedback", 70.f}});
        return engine.renderOffline(0.0, 2 * kSampleRate);
    };
    const Samples a = render(), b = render();
    CHECK(a == b);
    CHECK(maxAbs(a) > 0.05);
}

TEST_CASE("the vintage delay stays finite") {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const uint32_t track = stereoClickTrack(engine, 1.f, 1.f, kClick, 3.0);
    vintageDelay(engine, track, 1.f, {{"feedback", 95.f}, {"filter", 1.f}, {"width", 0.5f}, {"swing", 100.f}});
    const Samples out = engine.renderOffline(0.0, 3 * kSampleRate);
    CHECK(allFinite(out));
    CHECK(maxAbs(out) < 2.0);
}
