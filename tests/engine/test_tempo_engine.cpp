// The engine on a tempo map and time signature changes (Engine::setTempoMap(),
// setMeter()): notes, automation, clips and the metronome land where the map
// puts their beats; the playhead and sounding notes stay on their beats when
// the map changes; exports last what the map makes their beats last.

#include <cmath>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;
using sub::MeterMap;
using sub::TempoMap;

namespace {

// 120 BPM for four beats, then 60: beat 4 is at 2 s, beat 6 at 4 s.
TempoMap slowsAtBeat4() { return TempoMap({{0.0, 120.0}, {4.0, 120.0}, {4.0, 60.0}}); }

// The first sample above `threshold` at or after `from` (-1: none).
int64_t firstAbove(const Samples& samples, int64_t from, double threshold = 1e-4) {
    for (size_t i = static_cast<size_t>(std::max<int64_t>(0, from)); i < samples.size(); ++i) {
        if (std::abs(samples[i]) > threshold) return static_cast<int64_t>(i);
    }
    return -1;
}

uint32_t synthTrack(sub::Engine& engine, const std::vector<sub::NoteDesc>& notes) {
    const uint32_t track = engine.addTrack();
    engine.addBuiltinProcessor(engine.trackChain(track), "synth", -1);
    engine.setTrackNotes(track, notes);
    return track;
}

}  // namespace

TEST_CASE("notes play where the tempo map puts their beats") {
    sub::Engine engine;
    engine.setTempoMap(slowsAtBeat4());
    synthTrack(engine, {{6.0, 0.5, 69}});  // beat 6: four seconds in
    const Samples out = channel(engine.renderOffline(0.0, 5 * kSampleRate), 0);
    const int64_t onset = firstAbove(out, 0);
    CHECK(4 * kSampleRate <= onset);
    CHECK(onset < 4 * kSampleRate + 10);
    // Half a beat at 60 BPM: half a second; it still sounds just before its end.
    CHECK(maxAbs(slice(out, 4 * kSampleRate + kSampleRate / 2 - 2000, 4 * kSampleRate + kSampleRate / 2 - 1000)) > 0.01);
    // On a ramp too: from 60 to 120 BPM over 8 beats, beat 4 is 8 ln(1.5) seconds in.
    sub::Engine ramped;
    ramped.setTempoMap(TempoMap({{0.0, 60.0}, {8.0, 120.0}}));
    synthTrack(ramped, {{4.0, 0.5, 69}});
    const auto expected = static_cast<int64_t>(std::llround(8.0 * std::log(1.5) * kSampleRate));
    const int64_t rampOnset = firstAbove(channel(ramped.renderOffline(0.0, 5 * kSampleRate), 0), 0);
    CHECK(expected <= rampOnset);
    CHECK(rampOnset < expected + 10);
}

TEST_CASE("automation points land on their beats by the map") {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    engine.setTempoMap(slowsAtBeat4());
    const std::string wav = makeWav(std::vector<float>(2 * 6 * kSampleRate, 0.5f), 2);  // 6 s
    engine.loadSource(wav);
    const uint32_t track = engine.addTrack();
    engine.setTrackClips(track, {clip(wav, 0.0, 6.0)});
    // Silent until beat 5 (three seconds in), then full: a step there.
    engine.setTrackAutomation(track, {{0, "volume", {{0.0, 0.f, 0.f}, {5.0, 0.f, 0.f}, {5.0, 0.85f, 0.f}}}});
    const Samples out = channel(engine.renderOffline(0.0, 4 * kSampleRate), 0);
    CHECK(maxAbs(slice(out, 0, 3 * kSampleRate - 10)) < 1e-3);
    CHECK(maxAbs(slice(out, 3 * kSampleRate + 100, 4 * kSampleRate)) > 0.1);
}

TEST_CASE("a warped clip's ends stay on their beats through a tempo change") {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    engine.setTempoMap(slowsAtBeat4());
    const std::string wav = makeWav(std::vector<float>(4 * kSampleRate, 0.5f), 1);  // 4 s
    engine.loadSource(wav);
    const uint32_t track = engine.addTrack();
    sub::ClipDesc warped = clip(wav, 2.0, 2.0);  // two seconds of it at 120 BPM: four beats
    warped.warp = true;
    warped.segmentBpm = 120.0;
    warped.warpMode = sub::WarpMode::RePitch;
    engine.setTrackClips(track, {warped});
    // From beat 2 (1 s) to beat 6 (4 s), over the change.
    const Samples out = channel(engine.renderOffline(0.0, 5 * kSampleRate), 0);
    const int64_t onset = firstAbove(out, 0);
    CHECK(kSampleRate - 2 <= onset);
    CHECK(onset <= kSampleRate + 2);
    CHECK(maxAbs(slice(out, 4 * kSampleRate - 400, 4 * kSampleRate - 200)) > 0.1);
    CHECK(maxAbs(slice(out, 4 * kSampleRate + 200, 5 * kSampleRate)) < 1e-3);
}

TEST_CASE("the playhead stays on its beat when the tempo map changes") {
    sub::Engine engine;
    engine.setPositionBeats(8.0);
    CHECK_APPROX(engine.positionBeats(), 8.0);
    engine.setTempoMap(slowsAtBeat4());
    engine.idle();
    CHECK_APPROX(engine.positionBeats(), 8.0);
    engine.setTempo(90.0);
    engine.idle();
    CHECK_APPROX(engine.positionBeats(), 8.0);
}

TEST_CASE("the metronome follows the tempo map and the bars of each time signature") {
    sub::Engine engine;
    engine.setTempoMap(slowsAtBeat4());
    // Two bars of 2/4 (beats 0 and 2 accented), then 3/4 from beat 4.
    engine.setMeter(MeterMap({{0.0, 2, 4}, {4.0, 3, 4}}));
    const Samples level = channel(engine.renderOffline(0.0, 8 * kSampleRate, false, true), 0);
    // Beats 0-4 every half second, then every second: beats 4, 5, 6, 7 at 2, 3, 4, 5 s.
    const std::vector<double> seconds{0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0};
    for (const double s : seconds) {
        INFO("tick at " + std::to_string(s) + " s");
        const auto onset = static_cast<int64_t>(s * kSampleRate);
        CHECK(maxAbs(slice(level, onset, onset + 200)) > 0.1);
    }
    CHECK(maxAbs(slice(level, kSampleRate / 4 + 4000, kSampleRate / 2 - 10)) < 1e-3);  // (none between)
    CHECK(maxAbs(slice(level, 2 * kSampleRate + 8000, 3 * kSampleRate - 10)) < 1e-3);
    // Accents: beat 2 (a 2/4 bar's first) is, beat 3 isn't; beat 4 (the 3/4's first) is, beat 5 isn't; beat 7 is.
    const auto peakAt = [&](double s) {
        const auto onset = static_cast<int64_t>(s * kSampleRate);
        return maxAbs(slice(level, onset, onset + 2000));
    };
    CHECK(peakAt(1.0) > peakAt(1.5));
    CHECK(peakAt(2.0) > peakAt(3.0));
    CHECK(peakAt(5.0) > peakAt(4.0));
}

TEST_CASE("an export lasts what the tempo map makes its beats last") {
    sub::Engine engine;
    engine.setTempoMap(slowsAtBeat4());
    const std::filesystem::path path = tempDir() / "tempo_map_export.wav";
    engine.exportWav(path.string(), 2.0, 6.0, 24);  // from 1 s to 4 s
    const Wav wav = readWav(path);
    CHECK_EQ(static_cast<int64_t>(wav.samples().size() / 2), int64_t{3 * kSampleRate});
    std::filesystem::remove(path);
}

TEST_CASE("one tempo throughout plays as it always has") {
    // A steady map is the old single tempo, to the bit: setTempo() and a one-point map alike.
    const auto render = [](const auto& setUp) {
        sub::Engine engine;
        setUp(engine);
        synthTrack(engine, {{1.0, 1.0, 60}, {2.5, 0.75, 64}});
        return engine.renderOffline(0.0, 3 * kSampleRate, false, true);
    };
    const auto a = render([](sub::Engine& e) { e.setTempo(97.0); });
    const auto b = render([](sub::Engine& e) { e.setTempoMap(TempoMap({{0.0, 97.0}, {12.0, 97.0}})); });
    CHECK(a == b);
}

namespace {

// A 4-second file, silent but for short clicks at these seconds.
std::string clicksAt(const std::vector<double>& seconds) {
    std::vector<float> samples(4 * kSampleRate, 0.f);
    for (const double s : seconds) {
        const auto at = static_cast<size_t>(s * kSampleRate);
        for (size_t i = at; i < at + 8 && i < samples.size(); ++i) samples[i] = 0.9f;
    }
    return makeWav(samples, 1);
}

// Where the loudest sample is between two seconds.
int64_t loudestBetween(const Samples& samples, double from, double to) {
    const auto a = static_cast<size_t>(from * kSampleRate), b = static_cast<size_t>(to * kSampleRate);
    size_t best = a;
    for (size_t i = a; i < b && i < samples.size(); ++i) {
        if (std::abs(samples[i]) > std::abs(samples[best])) best = i;
    }
    return static_cast<int64_t>(best);
}

Samples warpedRender(const TempoMap& map, sub::WarpMode mode, const std::string& wav) {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    engine.setTempoMap(map);
    engine.loadSource(wav);
    const uint32_t track = engine.addTrack();
    sub::ClipDesc warped = clip(wav, 2.0, 3.0);  // three seconds of it at 120 BPM: six beats, from beat 2
    warped.warp = true;
    warped.segmentBpm = 120.0;
    warped.warpMode = mode;
    engine.setTrackClips(track, {warped});
    return channel(engine.renderOffline(0.0, 9 * kSampleRate), 0);
}

}  // namespace

TEST_CASE("a warped clip follows the tempo map within it, beat by beat") {
    // Clicks on the clip's beats 2 and 3 (one and one and a half seconds into the file): at 120 BPM
    // to beat 4 then 60, timeline beats 4 and 5 are at 2 and 3 seconds.
    const std::string wav = clicksAt({1.0, 1.5});
    const Samples repitched = warpedRender(slowsAtBeat4(), sub::WarpMode::RePitch, wav);
    CHECK(std::abs(loudestBetween(repitched, 1.8, 2.2) - 2 * kSampleRate) <= 2);
    CHECK(std::abs(loudestBetween(repitched, 2.8, 3.2) - 3 * kSampleRate) <= 2);
    // Stretched (the stretcher smears a click a little): within a few milliseconds.
    const Samples stretched = warpedRender(slowsAtBeat4(), sub::WarpMode::Standard, wav);
    CHECK(std::abs(loudestBetween(stretched, 1.8, 2.2) - 2 * kSampleRate) <= 240);
    CHECK(std::abs(loudestBetween(stretched, 2.8, 3.2) - 3 * kSampleRate) <= 240);
}

TEST_CASE("a warped clip follows a tempo ramp") {
    // From 60 to 120 BPM over beats 0 to 8: beat b (up to 8) is 8 ln(1 + b / 8) seconds in. The clip's
    // beat 2 (a second into the file) plays at timeline beat 4.
    const TempoMap ramp({{0.0, 60.0}, {8.0, 120.0}});
    const Samples out = warpedRender(ramp, sub::WarpMode::RePitch, clicksAt({1.0}));
    const auto expected = static_cast<int64_t>(std::llround(8.0 * std::log(1.5) * kSampleRate));
    CHECK(std::abs(loudestBetween(out, 3.0, 3.5) - expected) <= 2);
}
