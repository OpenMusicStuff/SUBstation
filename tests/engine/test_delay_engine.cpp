// The built-in Delay: synced and free times, offset, link, feedback, ping pong, freeze, the filter, the modes,
// swing, drive and duck; and its Digital character (Valhalla Delay's Digital mode at its default preset): the
// band-limit, the compressor, the modulation, equal-power dry/wet.

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "Engine.h"
#include "builtin/DelayCharacter.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

constexpr int64_t kClick = 100;                         // where the click is in the clip
constexpr int64_t kSixteenth = kSampleRate * 60 / 120 / 4;  // at the engine's 120 BPM

struct DelayEngine {
    sub::Engine engine;
    DelayEngine() { engine.setClipFadeMs(0); }
};

using Values = std::vector<std::pair<std::string, float>>;

uint32_t clickTrack(sub::Engine& engine, float left = 1.f, float right = 1.f, double seconds = 3.0, int64_t at = kClick) {
    return stereoClickTrack(engine, left, right, at, seconds);
}

// A Delay, fully wet without feedback or filter unless `values` say otherwise
// (set in that order, as a Python dict merges them).
uint32_t delay(sub::Engine& engine, uint32_t track, const Values& values = {}) {
    Values all{{"feedback", 0.f}, {"filter", 0.f}, {"mix", 100.f}};
    for (const auto& [name, value] : values) {
        auto it = std::find_if(all.begin(), all.end(), [&](const auto& v) { return v.first == name; });
        if (it != all.end()) it->second = value;
        else all.emplace_back(name, value);
    }
    const uint32_t device = engine.addBuiltinProcessor(engine.trackChain(track), "delay", -1);
    for (const auto& [name, value] : all) setParam(engine, device, name, value);
    return device;
}

// The band-limit's taps at the engine's rate, and where its peak is (an echo of a click peaks that late).
const std::vector<float>& bandLimit() {
    static const std::vector<float> taps = sub::delay_character::DigitalDesign::bandLimit(kSampleRate);
    return taps;
}
int64_t bandLimitPeak() {
    const auto& taps = bandLimit();
    return std::max_element(taps.begin(), taps.end()) - taps.begin();
}
double bandLimitHeight() { return *std::max_element(bandLimit().begin(), bandLimit().end()); }

// Where a click's echo peaks when the line is read `delay` samples back: the modulation moves the read as the
// device runs it from the render's start, and the band-limit adds its own lateness.
int64_t echoPeak(int64_t click, double delay) {
    sub::delay_character::DigitalModulation modulation;
    modulation.prepare(kSampleRate);
    const int64_t peak = bandLimitPeak();
    for (int64_t t = 0;; ++t) {
        const double reads = static_cast<double>(t) - delay - modulation.next();  // the line's sample read at t
        if (reads >= static_cast<double>(click + peak) - 0.5) return t;
    }
}

// The channel's loudest sample within `reach` of `around`.
int64_t peakNear(const Samples& channel, int64_t around, int64_t reach = 400) {
    const int64_t from = std::max<int64_t>(0, around - reach), to = std::min<int64_t>(channel.size(), around + reach);
    int64_t best = from;
    for (int64_t i = from; i < to; ++i)
        if (std::abs(channel[i]) > std::abs(channel[best])) best = i;
    return best;
}

double energyNear(const Samples& channel, int64_t at, int64_t reach = 300) {
    double sum = 0;
    for (int64_t i = std::max<int64_t>(0, at - reach); i < std::min<int64_t>(channel.size(), at + reach); ++i)
        sum += static_cast<double>(channel[i]) * channel[i];
    return sum;
}

}  // namespace

TEST_CASE("the delay is listed with its parameters") {
    const sub::BuiltinInfo info = builtinInfo("delay");
    CHECK_EQ(info.name, std::string("Delay"));
    const std::vector<std::string> ids = paramIds(info.params);
    REQUIRE(ids.size() >= 4);
    CHECK(std::vector<std::string>(ids.begin(), ids.begin() + 4) ==
          (std::vector<std::string>{"l_sync", "l_division", "l_time", "l_offset"}));
    const std::set<std::string> all(ids.begin(), ids.end());
    for (const char* id : {"link", "feedback", "freeze", "filter", "freq", "width", "mode", "ping_pong", "mix"}) {
        INFO(id);
        CHECK(all.count(id) == 1);
    }
}

TEST_CASE("synced times follow the tempo") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine);
    delay(engine, track, {{"l_division", 2.f}, {"r_division", 3.f}});  // 3 and 4 sixteenths
    const Samples out = engine.renderOffline(0.0, kSampleRate);
    // Fully wet: the click itself is gone, its echo comes 3 (left) and 4 (right) sixteenths later.
    const Samples left = channel(out, 0), right = channel(out, 1);
    CHECK(maxAbs(slice(left, 0, kClick + kSixteenth)) < 1e-6);
    const int64_t l = echoPeak(kClick, 3 * kSixteenth), r = echoPeak(kClick, 4 * kSixteenth);
    CHECK_NEAR(peakNear(left, l), l, 2);
    CHECK_NEAR(peakNear(right, r), r, 2);
    // A click comes back band-limited: about as tall as the band-limit's peak (less, read between samples).
    CHECK_APPROX_REL(maxAbs(left), bandLimitHeight(), 0.07);
}

TEST_CASE("offset, free time and link") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine);
    const uint32_t device =
        delay(engine, track, {{"l_division", 3.f}, {"l_offset", 25.f}, {"r_sync", 0.f}, {"r_time", 100.f}});
    Samples out = engine.renderOffline(0.0, kSampleRate);
    const int64_t l = echoPeak(kClick, 1.25 * 4 * kSixteenth), r = echoPeak(kClick, kSampleRate / 10);
    CHECK_NEAR(peakNear(channel(out, 0), l), l, 2);
    CHECK_NEAR(peakNear(channel(out, 1), r), r, 2);

    setParam(engine, device, "link", 1.f);
    setParam(engine, device, "mode", 2.f);  // Jump: no glide
    out = engine.renderOffline(0.0, kSampleRate);
    CHECK_EQ(peakNear(channel(out, 1), l), peakNear(channel(out, 0), l));  // the right side follows the left
}

TEST_CASE("feedback repeats") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine, 0.1f, 0.1f);  // quiet: the compressor leaves it be
    delay(engine, track, {{"l_sync", 0.f}, {"l_time", 100.f}, {"r_sync", 0.f}, {"r_time", 100.f}, {"feedback", 50.f}});
    const Samples left = channel(engine.renderOffline(0.0, kSampleRate), 0);
    const int64_t step = kSampleRate / 10;
    // Each repeat half as loud as the last (a quarter of the energy), a little less as the band-limit
    // (with its droop) goes round again.
    int64_t at = echoPeak(kClick, step);
    double last = energyNear(left, peakNear(left, at), 150);
    CHECK(last > 0.0);
    for (int n = 2; n < 5; ++n) {
        INFO("repeat " + std::to_string(n));
        at = peakNear(left, echoPeak(at, step), 150);
        const double now = energyNear(left, at, 150);
        INFO("energy ratio " + std::to_string(now / last));
        CHECK(now / last < 0.25);
        CHECK(now / last > 0.25 * 0.6);
        last = now;
    }
}

TEST_CASE("ping pong bounces") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine, 1.f, 0.f);
    delay(engine, track,
          {{"l_sync", 0.f}, {"l_time", 100.f}, {"r_sync", 0.f}, {"r_time", 50.f}, {"feedback", 50.f}, {"ping_pong", 1.f}});
    const Samples out = engine.renderOffline(0.0, kSampleRate / 2);
    const Samples left = channel(out, 0), right = channel(out, 1);
    const int64_t stepL = kSampleRate / 10, stepR = kSampleRate / 20;
    // The input, summed to mono, goes left first, then bounces right, then left again (each pass the band-limit's
    // lateness later).
    const int64_t first = echoPeak(kClick, stepL);
    CHECK_NEAR(peakNear(left, first), first, 2);
    CHECK(maxAbs(slice(right, 0, first + stepR - 200)) < 1e-6);
    const int64_t second = echoPeak(first, stepR), third = echoPeak(second, stepL);
    CHECK_NEAR(peakNear(right, second, 150), second, 3);
    CHECK_NEAR(peakNear(left, third, 150), third, 3);
    CHECK_APPROX_REL(maxAbs(left), 0.5 * bandLimitHeight(), 0.03);  // half of the click, on each side
}

TEST_CASE("freeze holds the loop and ignores new input") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine);
    const uint32_t device = delay(engine, track, {{"l_sync", 0.f}, {"l_time", 100.f}, {"r_sync", 0.f}, {"r_time", 100.f}});
    setParam(engine, device, "freeze", 1.f);
    const Samples out = engine.renderOffline(0.0, kSampleRate);
    CHECK(maxAbs(out) <= 0.01);  // frozen before the click came: it never went in

    setParam(engine, device, "freeze", 0.f);
    setParam(engine, device, "feedback", 10.f);
    const Samples first = engine.renderOffline(0.0, kSampleRate / 4);  // the click goes in, and its first echo comes
    CHECK_APPROX_REL(maxAbs(channel(first, 0)), bandLimitHeight(), 0.03);
}

TEST_CASE("the filter takes out what is outside its band") {
    DelayEngine e;
    auto& engine = e.engine;
    const Samples low = sine(60.0, 2.0, 0.1);
    const std::string path = makeWav(stereo(low), 2);
    engine.loadSource(path);
    const uint32_t track = engine.addTrack();
    engine.setTrackClips(track, {clip(path, 0.0, 2.0, 0.0, 1.f)});
    const uint32_t device = delay(engine, track, {{"l_sync", 0.f}, {"l_time", 10.f}, {"r_sync", 0.f}, {"r_time", 10.f}});
    const double unfiltered = maxAbs(channel(frames(engine.renderOffline(0.0, kSampleRate), kSampleRate / 2), 0));
    for (const auto& [name, value] : Values{{"filter", 1.f}, {"freq", 4000.f}, {"width", 2.f}})  // 2 to 8 kHz
        setParam(engine, device, name, value);
    const double filtered = maxAbs(channel(frames(engine.renderOffline(0.0, kSampleRate), kSampleRate / 2), 0));
    CHECK_APPROX_REL(unfiltered, 0.1, 0.05);  // (Digital's high-passes take 0.3 dB at 60 Hz)
    CHECK(filtered < 0.002);
}

TEST_CASE("time changes glide only in Repitch") {
    // The time jumps from 10 to 200 ms at beat 0.5; a click comes 0.375 s later. Fade (done fading) and Jump
    // echo it at 200 ms; Repitch is still gliding there.
    for (const auto& [mode, exact] : std::vector<std::pair<float, bool>>{{0.f, false}, {1.f, true}, {2.f, true}}) {
        INFO("mode " + std::to_string(mode));  // Repitch, Fade, Jump
        DelayEngine e;
        auto& engine = e.engine;
        const int64_t click = 30000;
        const uint32_t track = clickTrack(engine, 1.f, 1.f, 2.0, click);
        const uint32_t device =
            delay(engine, track, {{"l_sync", 0.f}, {"l_time", 10.f}, {"r_sync", 0.f}, {"r_time", 10.f}, {"mode", mode}});
        const sub::ParamInfo info = paramInfo(engine, device, "l_time");
        const float before = info.toNormalized(10.f), after = info.toNormalized(200.f);
        engine.setTrackAutomation(track, {{device, "l_time", {{0.0, before, 0.f}, {0.5, before, 0.f}, {0.5, after, 0.f}}}});
        const Samples out = engine.renderOffline(0.0, kSampleRate);
        const int64_t expected = echoPeak(click, kSampleRate / 5);
        const int64_t at = peakNear(channel(out, 0), expected, 2000);
        CHECK_EQ(std::abs(at - expected) <= 2, exact);
    }
}

TEST_CASE("mono and silence stay finite") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine);
    delay(engine, track, {{"feedback", 95.f}, {"filter", 1.f}, {"width", 0.5f}});
    const Samples out = engine.renderOffline(0.0, 2 * kSampleRate);
    CHECK(allFinite(out));
    CHECK(maxAbs(out) < 2.0);
}

TEST_CASE("the input display is the input in mono") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine, 1.f, 0.5f);
    const uint32_t device = delay(engine, track);
    const auto displays = engine.processorDisplays(device);
    REQUIRE(displays.size() == 1);
    CHECK_EQ(displays[0].id, std::string("input"));
    CHECK_EQ(displays[0].samplesPerValue, 1);
    engine.renderOffline(0.0, 4096);
    std::vector<float> values;
    const uint64_t position = engine.readProcessorDisplay(device, 0, 0, values);
    CHECK_EQ(position, uint64_t{4096});
    REQUIRE(values.size() > static_cast<size_t>(kClick));
    CHECK_NEAR(values[kClick], 0.75, 1e-3);
    CHECK_EQ(countNonzero(values), size_t{1});
}

TEST_CASE("swing puts every other echo late") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine);
    delay(engine, track,
          {{"l_sync", 0.f}, {"l_time", 100.f}, {"r_sync", 0.f}, {"r_time", 100.f}, {"feedback", 50.f}, {"swing", 50.f}});
    const Samples left = channel(engine.renderOffline(0.0, kSampleRate), 0);
    const int64_t step = kSampleRate / 10;
    // Half swing: the odd echoes a quarter of the time late, the even ones on time (each echo a pass of the loop:
    // its band-limit's lateness and the modulation's move).
    const int64_t first = echoPeak(kClick, step + step / 4);
    CHECK_NEAR(peakNear(left, first), first, 2);
    const int64_t second = echoPeak(first, step - step / 4);
    CHECK_NEAR(peakNear(left, second, 150), second, 3);
}

TEST_CASE("swing in ping pong") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine, 1.f, 0.f);
    delay(engine, track,
          {{"l_sync", 0.f}, {"l_time", 100.f}, {"r_sync", 0.f}, {"r_time", 100.f}, {"feedback", 50.f},
           {"ping_pong", 1.f}, {"swing", 100.f}});
    const Samples out = engine.renderOffline(0.0, kSampleRate / 2);
    const int64_t step = kSampleRate / 10;
    // Full swing: left half the time late, then right (half the time after it).
    const int64_t first = echoPeak(kClick, step + step / 2);
    CHECK_NEAR(peakNear(channel(out, 0), first), first, 2);
    const int64_t second = echoPeak(first, step / 2);
    CHECK_NEAR(peakNear(channel(out, 1), second, 150), second, 3);
}

TEST_CASE("drive saturates the echoes") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine);
    const uint32_t device =
        delay(engine, track, {{"l_sync", 0.f}, {"l_time", 100.f}, {"r_sync", 0.f}, {"r_time", 100.f}});
    setParam(engine, device, "drive", 100.f);
    const Samples left = channel(engine.renderOffline(0.0, kSampleRate / 2), 0);
    // The band-limited click into a tanh 8 times louder, and down as much after.
    CHECK_APPROX_REL(maxAbs(left), std::tanh(8.0 * bandLimitHeight()) / 8.0, 0.03);
    const int64_t expected = echoPeak(kClick, kSampleRate / 10);
    CHECK_NEAR(peakNear(left, expected), expected, 2);
}

TEST_CASE("duck dips the echoes while the input plays") {
    const auto wetLevel = [](float duck) {
        DelayEngine e;
        auto& engine = e.engine;
        const std::string path = makeWav(stereo(sine(440.0, 1.0, 0.5)), 2);
        engine.loadSource(path);
        const uint32_t track = engine.addTrack();
        engine.setTrackClips(track, {clip(path, 0.0, 1.0, 0.0, 1.f)});
        delay(engine, track,
              {{"l_sync", 0.f}, {"l_time", 50.f}, {"r_sync", 0.f}, {"r_time", 50.f}, {"feedback", 80.f}, {"duck", duck}});
        const Samples left = channel(engine.renderOffline(0.0, 2 * kSampleRate), 0);
        // While the sine plays, and the echoes ringing on well after it.
        return std::pair{rms(slice(left, kSampleRate / 2, kSampleRate * 9 / 10)),
                         rms(slice(left, kSampleRate * 16 / 10, kSampleRate * 19 / 10))};
    };
    const auto [playing, open] = wetLevel(0.f);
    const auto [ducked, after] = wetLevel(100.f);
    CHECK(ducked < 0.25 * playing);
    CHECK(after > 0.4 * open);  // swells back once the input stops
}

// --- Digital: Valhalla Delay's Digital mode -------------------------------------------------------

namespace {

// A tone's level through the delay, fully wet: its amplitude over the last half second of `seconds`.
double toneThrough(double freq, double amplitude, const Values& values = {}, double seconds = 1.5) {
    DelayEngine e;
    auto& engine = e.engine;
    const std::string path = makeWav(stereo(sine(freq, seconds, amplitude)), 2);
    engine.loadSource(path);
    const uint32_t track = engine.addTrack();
    engine.setTrackClips(track, {clip(path, 0.0, seconds, 0.0, 1.f)});
    Values all{{"l_sync", 0.f}, {"l_time", 20.f}, {"r_sync", 0.f}, {"r_time", 20.f}};
    all.insert(all.end(), values.begin(), values.end());
    delay(engine, track, all);
    const Samples left = channel(engine.renderOffline(0.0, static_cast<int64_t>(seconds * kSampleRate)), 0);
    const auto from = static_cast<int64_t>((seconds - 0.5) * kSampleRate);
    return rms(slice(left, from, from + kSampleRate / 2)) * std::sqrt(2.0);
}

}  // namespace

TEST_CASE("Digital's band-limit at rates it wasn't measured at keeps its gain") {
    // Resampled from the nearest measured rate: the same response in time, so unity at DC still.
    for (const double rate : {32000.0, 176400.0, 192000.0}) {
        INFO(std::to_string(rate));
        const std::vector<float> taps = sub::delay_character::DigitalDesign::bandLimit(rate);
        double sum = 0.0;
        for (const float t : taps) sum += t;
        CHECK_NEAR(sum, 1.0, 0.01);
    }
}

TEST_CASE("Digital band-limits each echo") {
    // Valhalla's own: flat (-0.2 dB) at 1 kHz, a droop to -1.3 dB at 5 kHz, the band ending near 7.8 kHz.
    CHECK_APPROX_REL(toneThrough(1000.0, 0.05), 0.05 * std::pow(10.0, -0.21 / 20), 0.02);
    CHECK_APPROX_REL(toneThrough(5000.0, 0.05), 0.05 * std::pow(10.0, -1.25 / 20), 0.03);
    CHECK(toneThrough(10000.0, 0.05) < 0.05 * 0.03);  // -34 dB there
    CHECK(toneThrough(5.0, 0.05, {}, 3.0) < 0.05 * 0.4);  // the high-passes (5, 10, 10 Hz)
}

TEST_CASE("Digital's compressor holds loud echoes down, the treble sooner") {
    // Valhalla's gains for a sine at full scale, over a quiet one's: 0.79 at 400 Hz, 0.69 at 1 kHz, 0.46 at 3 kHz.
    CHECK_APPROX_REL(toneThrough(400.0, 1.0) / toneThrough(400.0, 0.05) * 0.05, 0.785, 0.04);
    CHECK_APPROX_REL(toneThrough(1000.0, 1.0) / toneThrough(1000.0, 0.05) * 0.05, 0.686, 0.04);
    CHECK_APPROX_REL(toneThrough(3000.0, 1.0) / toneThrough(3000.0, 0.05) * 0.05, 0.462, 0.08);
}

TEST_CASE("Digital's modulation sways the time ±2.67 ms on a 0.5 Hz triangle") {
    sub::delay_character::DigitalModulation modulation;
    modulation.prepare(kSampleRate);
    double low = 1e9, high = -1e9;
    std::vector<float> values;
    for (int i = 0; i < 8 * kSampleRate; ++i) values.push_back(modulation.next());
    for (size_t i = 2 * kSampleRate; i < values.size(); ++i) {
        low = std::min<double>(low, values[i]);
        high = std::max<double>(high, values[i]);
    }
    // The 2 Hz rounding takes the triangle's corners in a little: 227.8 samples top to bottom, as measured.
    CHECK_NEAR(high - low, 227.8, 1.0);
    CHECK_NEAR(values[0], 0.0, 1.0);  // from the middle, rising
    CHECK(values[kSampleRate / 4] > 30.f);
    for (size_t i = kSampleRate; i + 2 * kSampleRate < values.size(); i += 997)  // (once the rounding has settled)
        CHECK_NEAR(values[i], values[i + 2 * kSampleRate], 0.05);
}

TEST_CASE("dry and wet cross over at equal power") {
    DelayEngine e;
    auto& engine = e.engine;
    const uint32_t track = clickTrack(engine, 0.1f, 0.1f);
    delay(engine, track, {{"l_sync", 0.f}, {"l_time", 100.f}, {"r_sync", 0.f}, {"r_time", 100.f}, {"mix", 50.f}});
    const Samples left = channel(engine.renderOffline(0.0, kSampleRate / 2), 0);
    CHECK_APPROX_REL(left[kClick], 0.1 * std::sqrt(0.5), 1e-3);
    CHECK_APPROX_REL(maxAbs(slice(left, kClick + 1000, kSampleRate / 2)), 0.1 * std::sqrt(0.5) * bandLimitHeight(), 0.03);
}
