// Freezing in the engine: one track's signal before its fader rendered offline
// (lined up with the timeline, solo ignored, into a WAV file with its tail), and
// frozen tracks playing their clips through their faders without their devices,
// notes or what goes into them; what goes only into frozen tracks isn't rendered.

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {


struct FreezeEngine {
    sub::Engine engine;
    FreezeEngine() { engine.setClipFadeMs(0); }
};

// Renders the track to `path` and plays that instead, frozen (as the app does).
int64_t freeze(sub::Engine& engine, uint32_t track, const std::filesystem::path& path, double endBeat,
               double tailSeconds = 0.0) {
    const int64_t written = engine.renderTrackToWav(track, path.string(), 0.0, endBeat, tailSeconds);
    engine.loadSource(path.string());
    engine.setTrackClips(track, {clip(path.string(), 0.0, static_cast<double>(written) / kSampleRate)});
    engine.setTrackNotes(track, {});
    engine.setTrackFrozen(track, true);
    return written;
}

std::vector<int64_t> clicks(const Samples& out) { return above(channel(out, 0), 1e-6); }

// A click of 0.25 at sample 1000 of a second.
std::string level(float value, int seconds = 1) { return makeWav(full(static_cast<size_t>(seconds) * kSampleRate * 2, value), 2); }

// The track rendered into a WAV file with its tail (renderTrackToWav, which
// stops once it stays silent) is what rendering all of the tail and cutting it
// where it falls below -100 dB makes, to the bit. The frames of tail written.
int64_t tailAsCut(sub::Engine& engine, uint32_t track, double endBeat, double tailSeconds) {
    static int renders = 0;  // (a file of its own each time: sources are cached by path)
    const auto path = tempDir() / ("tail" + std::to_string(++renders) + ".wav");
    const int64_t written = engine.renderTrackToWav(track, path.string(), 0.0, endBeat, tailSeconds);
    const auto frames = static_cast<int64_t>(std::llround(endBeat * kSpb));
    const auto tail = static_cast<int64_t>(std::llround(tailSeconds * kSampleRate));
    const Samples whole = engine.renderTrackOffline(track, 0.0, frames + tail);
    int64_t kept = frames + tail;
    const auto silent = [&](int64_t f) {
        return std::abs(whole[static_cast<size_t>(f) * 2]) < 1e-5f && std::abs(whole[static_cast<size_t>(f) * 2 + 1]) < 1e-5f;
    };
    while (kept > frames && silent(kept - 1)) --kept;
    CHECK_EQ(written, kept);
    const auto source = engine.loadSource(path.string());
    REQUIRE(source->frames() == written);
    bool same = true;
    for (int64_t f = 0; f < written; ++f) {
        for (uint32_t c = 0; c < 2; ++c) same = same && source->channelData(c)[f] == whole[static_cast<size_t>(f) * 2 + c];
    }
    CHECK(same);
    return written - frames;
}

}  // namespace

// --- Rendering one track ------------------------------------------------------------

TEST_CASE("a track renders after its devices, before its fader") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, level(0.5f));
    utility(engine, track, -6.0206f);  // halves it
    engine.setTrackGain(track, 0.1f);
    engine.setTrackPan(track, 1.f);
    engine.setTrackMute(track, true);
    const Samples out = engine.renderTrackOffline(track, 0.0, 4000);
    CHECK_EQ(out.size(), size_t{4000 * 2});
    CHECK_ALLCLOSE(frames(out, -100), 0.25, 1e-7, 1e-4);  // (once the utility's gain has settled)
}

TEST_CASE("a group renders its bus") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t bus = engine.addTrack();
    clipTrack(engine, level(0.5f), 0.0, 1.0, bus);
    const uint32_t quiet = clipTrack(engine, level(0.25f), 0.0, 1.0, bus);
    engine.setTrackGain(quiet, 0.5f);  // what goes into the bus: after the tracks' faders
    utility(engine, bus, -6.0206f);
    CHECK_ALLCLOSE(frames(engine.renderTrackOffline(bus, 0.0, 4000), -100), (0.5 + 0.125) / 2, 1e-7, 1e-4);
}

TEST_CASE("solo is ignored") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t bus = engine.addTrack();
    clipTrack(engine, level(0.5f), 0.0, 1.0, bus);
    const uint32_t other = clipTrack(engine, level(0.25f));
    engine.setTrackSolo(other, true);  // would leave the group silent
    CHECK_ALLCLOSE(engine.renderTrackOffline(bus, 0.0, 100), 0.5, 1e-7, 1e-4);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 100), 0.25, 1e-7, 1e-4);  // (playing, it counts)
}

TEST_CASE("an unknown track is refused") {
    FreezeEngine e;
    auto& engine = e.engine;
    CHECK_THROWS_AS(engine.renderTrackOffline(99, 0.0, 10), std::invalid_argument);
    CHECK_THROWS_AS(engine.renderTrackToWav(engine.addTrack(), (tempDir() / "x.wav").string(), 1.0, 1.0, 0.0),
                    std::invalid_argument);
}

TEST_CASE("the render lines up with the timeline") {
    requireTestPlugins();
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, clickWav(0.25, kSampleRate, 1000));
    latentEffect(engine, track, 300);
    CHECK(clicks(engine.renderTrackOffline(track, 0.0, 4000)) == std::vector<int64_t>{1000});
}

TEST_CASE("a group's render lines up with the timeline") {
    requireTestPlugins();
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t bus = engine.addTrack();
    const uint32_t late = clipTrack(engine, clickWav(0.25, kSampleRate, 1000), 0.0, 1.0, bus);
    latentEffect(engine, late, 200);  // the bus hears its tracks 200 late
    latentEffect(engine, bus, 100);
    CHECK(clicks(engine.renderTrackOffline(bus, 0.0, 4000)) == std::vector<int64_t>{1000});
}

TEST_CASE("the WAV has the range and its tail") {
    FreezeEngine e;
    auto& engine = e.engine;
    Samples tone(kSampleRate * 2, 0.f);
    std::fill(tone.begin(), tone.begin() + kSampleRate, 0.5f);  // half a second of sound in a one-second clip
    const uint32_t track = clipTrack(engine, makeWav(tone, 2));
    const auto path = tempDir() / "frozen.wav";
    const int64_t written = engine.renderTrackToWav(track, path.string(), 0.0, 0.5, 2.0);  // a quarter second, then on
    CHECK_EQ(written, int64_t{kSampleRate / 2});  // the tail, until it fell silent
    const auto source = engine.loadSource(path.string());
    CHECK_EQ(source->frames(), written);
    CHECK_EQ(source->channels(), 2u);
    for (uint32_t c = 0; c < source->channels(); ++c) {
        const Samples samples(source->channelData(c), source->channelData(c) + written);
        CHECK_ALLCLOSE(samples, 0.5, 1e-7, 1e-4);
    }
}

TEST_CASE("the tail ends where it falls silent") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = engine.addTrack();
    const uint32_t synth = engine.addBuiltinProcessor(engine.trackChain(track), "synth", -1);
    setParam(engine, synth, "release", 300.f);
    engine.setTrackNotes(track, {{0.0, 1.0, 60, 100}, {1.5, 0.5, 64, 100}});
    const int64_t tail = tailAsCut(engine, track, 2.0, 10.0);  // (the release rings past the range)
    CHECK(tail > kSampleRate / 10);
    CHECK(tail < kSampleRate);
}

TEST_CASE("a delay's tail isn't cut at the silence between its echoes") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = stereoClickTrack(engine, 0.5f, 0.5f, 100, 0.5);
    const uint32_t delay = engine.addBuiltinProcessor(engine.trackChain(track), "delay", -1);
    for (const auto& [name, value] : std::vector<std::pair<std::string, float>>{
             {"l_sync", 0.f}, {"r_sync", 0.f}, {"l_time", 1500.f}, {"r_time", 1500.f}, {"feedback", 30.f},
             {"filter", 0.f}, {"mix", 100.f}}) {
        setParam(engine, delay, name, value);
    }
    // Echoes every 1.5 s, silent between them: several of them come in the tail.
    CHECK(tailAsCut(engine, track, 1.0, 10.0) > 4 * kSampleRate);
}

TEST_CASE("a reverb's tail") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = stereoClickTrack(engine, 0.5f, 0.5f, 100, 0.5);
    const uint32_t reverb = engine.addBuiltinProcessor(engine.trackChain(track), "reverb", -1);
    setParam(engine, reverb, "decay", 4000.f);
    const int64_t tail = tailAsCut(engine, track, 1.0, 10.0);
    CHECK(tail > kSampleRate);
}

TEST_CASE("the tail goes on through a clip or note sounding again after silence") {
    FreezeEngine e;
    auto& engine = e.engine;
    // A clip sounding again 2 s after the range's end, past a silence.
    Samples gap(static_cast<size_t>(kSampleRate) * 4 * 2, 0.f);
    std::fill(gap.end() - kSampleRate / 2, gap.end(), 0.25f);
    const uint32_t clip = clipTrack(engine, makeWav(gap, 2), 0.0, 4.0);
    CHECK(tailAsCut(engine, clip, 2.0, 10.0) > 2 * kSampleRate);
    // A note starting 3 s after it.
    const uint32_t keys = engine.addTrack();
    engine.addBuiltinProcessor(engine.trackChain(keys), "synth", -1);
    engine.setTrackNotes(keys, {{0.0, 0.5, 60, 100}, {8.0, 0.5, 60, 100}});
    CHECK(tailAsCut(engine, keys, 2.0, 10.0) > 3 * kSampleRate);
}

// --- Rendering only what a track needs ---------------------------------------------

namespace {

// A project whose tracks feed each other every way there is: a group of a synth
// and a stretched clip; a return they send to (one before its fader); a
// compressor keyed by the synth (before its fader), and a track going into its
// sidechain; a synth playing the first one's notes; a track taking another's
// output as its input (monitored); a rack of two synths; a soloed track; more
// stretched clips elsewhere (sharing the stretchers); effects on the master.
struct Graph {
    uint32_t group, synth, stretched, ret, keyed, intoKey, fed, input, rack, elsewhere;
};

uint32_t synthTrack(sub::Engine& engine, int key, uint32_t output = sub::Engine::kMaster) {
    const uint32_t track = engine.addTrack();
    engine.addBuiltinProcessor(engine.trackChain(track), "synth", -1);
    std::vector<sub::NoteDesc> notes;
    for (int beat = 0; beat < 8; ++beat) notes.push_back({beat * 0.5, 0.4, key + beat % 3 * 4, 90});
    engine.setTrackNotes(track, notes);
    engine.setTrackOutput(track, output);
    return track;
}

uint32_t stretchedTrack(sub::Engine& engine, const std::string& wav, double transpose, uint32_t output) {
    const uint32_t track = engine.addTrack();
    sub::ClipDesc clip = subtest::clip(wav, 0.0, 3.0);
    clip.warp = true;
    clip.segmentBpm = 100.0;
    clip.transpose = transpose;
    engine.setTrackClips(track, {clip});
    engine.setTrackOutput(track, output);
    return track;
}

Graph graph(sub::Engine& engine) {
    const std::string wav = makeWav(noise(static_cast<size_t>(kSampleRate) * 3 * 2, 7, 0.3f), 2);
    engine.loadSource(wav);
    Graph g{};
    g.group = engine.addTrack();
    engine.addBuiltinProcessor(engine.trackChain(g.group), "saturator", -1);
    g.ret = engine.addTrack();
    engine.addBuiltinProcessor(engine.trackChain(g.ret), "reverb", -1);
    g.synth = synthTrack(engine, 48, g.group);
    engine.addBuiltinProcessor(engine.trackChain(g.synth), "eq", -1);
    g.stretched = stretchedTrack(engine, wav, 3.0, g.group);
    engine.setTrackSend(g.synth, g.ret, 0.5f, false);
    engine.setTrackSend(g.stretched, g.ret, 0.7f, true);
    g.keyed = synthTrack(engine, 60);
    const uint32_t compressor = engine.addBuiltinProcessor(engine.trackChain(g.keyed), "compressor", -1);
    engine.setProcessorSidechain(compressor, g.synth, sub::SidechainTap::PreFader);
    g.intoKey = stretchedTrack(engine, wav, -2.0, sub::Engine::kMaster);
    engine.setTrackOutputSidechain(g.intoKey, compressor);
    g.fed = engine.addTrack();
    const uint32_t fedSynth = engine.addBuiltinProcessor(engine.trackChain(g.fed), "synth", -1);
    engine.setProcessorMidiInput(fedSynth, g.synth);
    g.input = engine.addTrack();
    engine.setTrackInputTrack(g.input, g.keyed);
    engine.setTrackMonitor(g.input, sub::MonitorMode::In);
    engine.addBuiltinProcessor(engine.trackChain(g.input), "delay", -1);
    g.rack = engine.addTrack();
    engine.setTrackNotes(g.rack, {{0.0, 2.0, 36, 100}, {2.0, 2.0, 43, 100}});
    const uint32_t rack = engine.addRack(engine.trackChain(g.rack), -1);
    for (int c = 0; c < 2; ++c) engine.addBuiltinProcessor(engine.addRackChain(rack, -1), c == 0 ? "synth" : "chorus", -1);
    g.elsewhere = stretchedTrack(engine, wav, 5.0, sub::Engine::kMaster);
    stretchedTrack(engine, wav, 7.0, g.ret);
    engine.setTrackSolo(synthTrack(engine, 72), true);
    engine.addBuiltinProcessor(engine.trackChain(sub::Engine::kMaster), "limiter", -1);
    return g;
}

// The track rendered alone (with what feeds it) is the track rendered with
// everything, to the bit.
void sameAsWhole(sub::Engine& engine, uint32_t track) {
    engine.setPruneTrackRenders(false);
    const Samples whole = engine.renderTrackOffline(track, 0.0, 3 * kSampleRate);
    engine.setPruneTrackRenders(true);
    const Samples pruned = engine.renderTrackOffline(track, 0.0, 3 * kSampleRate);
    CHECK(maxAbs(whole) > 1e-3);
    CHECK(pruned == whole);
}

}  // namespace

TEST_CASE("a track renders as it does with everything else") {
    for (const int threads : {1, 3}) {
        FreezeEngine e;
        auto& engine = e.engine;
        engine.setAudioThreads(threads);
        const Graph g = graph(engine);
        for (const uint32_t track : {g.group, g.synth, g.stretched, g.ret, g.keyed, g.intoKey, g.fed, g.input, g.rack,
                                     g.elsewhere}) {
            sameAsWhole(engine, track);
        }
    }
}

TEST_CASE("several tracks render at once as each does alone") {
    for (const int threads : {1, 3}) {
        FreezeEngine e;
        auto& engine = e.engine;
        engine.setAudioThreads(threads);
        const Graph g = graph(engine);
        const std::vector<uint32_t> tracks{g.group, g.synth, g.ret, g.keyed, g.fed, g.input, g.rack, g.elsewhere};
        std::vector<sub::TrackFile> together;
        std::vector<int64_t> alone;
        for (const uint32_t track : tracks) {
            const auto path = tempDir() / ("alone" + std::to_string(track) + ".wav");
            alone.push_back(engine.renderTrackToWav(track, path.string(), 0.0, 4.0, 3.0));
            together.push_back({track, (tempDir() / ("together" + std::to_string(track) + ".wav")).string()});
        }
        CHECK(engine.renderTracksToWav(together, 0.0, 4.0, 3.0) == alone);
        for (const uint32_t track : tracks) {
            CHECK(readBytes(tempDir() / ("together" + std::to_string(track) + ".wav")) ==
                  readBytes(tempDir() / ("alone" + std::to_string(track) + ".wav")));
        }
    }
}

TEST_CASE("the WAV keeps what is louder than full scale") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, level(0.5f));
    utility(engine, track, 12.f);  // about 2.0
    const auto path = tempDir() / "loud.wav";
    engine.renderTrackToWav(track, path.string(), 0.0, 1.0, 0.0);
    const auto source = engine.loadSource(path.string());
    float peak = 0.f;
    for (uint32_t c = 0; c < source->channels(); ++c)
        for (int64_t i = 4000; i < 4100; ++i) peak = std::max(peak, source->channelData(c)[i]);
    CHECK(peak > 1.5f);  // 32-bit float: not clipped
}

// --- Frozen tracks -----------------------------------------------------------------

TEST_CASE("a frozen track plays its render through its fader") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, level(0.5f));
    const uint32_t effect = utility(engine, track, -6.0206f);
    engine.setTrackGain(track, 0.5f);
    engine.renderOffline(0.0, 4000);  // (a new utility glides to its gain once)
    const Samples before = engine.renderOffline(0.0, kSampleRate / 2);
    freeze(engine, track, tempDir() / "f.wav", 1.0);
    CHECK(engine.trackFrozen(track));
    CHECK_ALLCLOSE(engine.renderOffline(0.0, kSampleRate / 2), before, 1e-7, 1e-6);
    // Its devices are left out (it plays what they made); its fader stays live.
    setParam(engine, effect, "gain", 0.f);
    CHECK_ALLCLOSE(frames(engine.renderOffline(0.0, 4000), -100), 0.125, 1e-7, 1e-4);
    engine.setTrackGain(track, 1.f);
    CHECK_ALLCLOSE(frames(engine.renderOffline(0.0, 4000), -100), 0.25, 1e-7, 1e-4);
    engine.setTrackFrozen(track, false);  // its devices again (on what it plays now)
    CHECK_ALLCLOSE(frames(engine.renderOffline(0.0, 4000), -100), 0.25, 1e-7, 1e-4);
}

TEST_CASE("a frozen MIDI track plays no notes") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = engine.addTrack();
    engine.addBuiltinProcessor(engine.trackChain(track), "synth", -1);
    engine.setTrackNotes(track, {{0.0, 1.0, 60, 100}});
    CHECK(maxAbs(engine.renderOffline(0.0, kBeat)) > 0.01);
    engine.setTrackClips(track, {});
    engine.setTrackFrozen(track, true);  // without a render: nothing at all
    CHECK_EQ(maxAbs(engine.renderOffline(0.0, kBeat)), 0.0);
}

TEST_CASE("a frozen group doesn't hear its tracks") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t bus = engine.addTrack();
    const uint32_t child = clipTrack(engine, level(0.5f), 0.0, 1.0, bus);
    utility(engine, bus, -6.0206f);
    freeze(engine, bus, tempDir() / "bus.wav", 1.0);
    CHECK_ALLCLOSE(frames(engine.renderOffline(0.0, 4000), -100), 0.25, 1e-7, 1e-4);
    engine.setTrackGain(child, 0.f);  // inside the frozen audio
    CHECK_ALLCLOSE(frames(engine.renderOffline(0.0, 4000), -100), 0.25, 1e-7, 1e-4);
    engine.setTrackGain(bus, 0.5f);  // live
    CHECK_ALLCLOSE(frames(engine.renderOffline(0.0, 4000), -100), 0.125, 1e-7, 1e-4);
}

TEST_CASE("what goes only into frozen tracks isn't rendered") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t bus = engine.addTrack();
    const uint32_t inner = engine.addTrack();
    engine.setTrackOutput(inner, bus);
    const uint32_t child = clipTrack(engine, level(0.5f), 0.0, 1.0, inner);
    utility(engine, child, 0.f);
    const uint32_t ret = engine.addTrack();
    const uint32_t sender = clipTrack(engine, level(0.25f), 0.0, 1.0, bus);
    engine.setTrackSend(sender, ret, 1.f, false);  // also heard elsewhere: rendered
    freeze(engine, bus, tempDir() / "bus.wav", 1.0);
    // The bus (both tracks, frozen), and the send into the return, live.
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 100), 0.75 + 0.25, 1e-7, 1e-4);
    engine.setTrackGain(sender, 0.f);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 100), 0.75, 1e-7, 1e-4);
    engine.setTrackFrozen(bus, false);  // all of it again
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 100), 0.5 + 0.75, 1e-7, 1e-4);
}

TEST_CASE("sends tap the frozen audio") {
    FreezeEngine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, level(0.5f));
    utility(engine, track, -6.0206f);
    const uint32_t ret = engine.addTrack();
    engine.setTrackOutput(track, ret);  // (all of it through the return)
    const uint32_t pre = engine.addTrack();
    engine.setTrackSend(track, pre, 1.f, true);
    freeze(engine, track, tempDir() / "t.wav", 1.0);
    engine.setTrackGain(track, 0.5f);
    // Into the return after the fader (0.125), the pre-fader send before it (0.25).
    CHECK_ALLCLOSE(frames(engine.renderOffline(0.0, 4000), -100), 0.125 + 0.25, 1e-7, 1e-4);
}

TEST_CASE("a frozen track adds no latency") {
    // Were its devices' latency still counted, everything else would wait for
    // it, and its frozen audio (on time) would come that much early.
    requireTestPlugins();
    FreezeEngine e;
    auto& engine = e.engine;
    const std::string wav = clickWav(0.25, kSampleRate, 1000);
    const uint32_t track = clipTrack(engine, wav);
    latentEffect(engine, track, 300);
    clipTrack(engine, wav, 1.0);
    CHECK(clicks(engine.renderOffline(0.0, kBeat + 4000)) == (std::vector<int64_t>{1000, kBeat + 1000}));
    freeze(engine, track, tempDir() / "t.wav", 1.0);
    CHECK(clicks(engine.renderOffline(0.0, kBeat + 4000)) == (std::vector<int64_t>{1000, kBeat + 1000}));
}

TEST_CASE("a frozen group lines up with nothing inside it") {
    // Its bus doesn't hear its tracks, so it isn't as late as they are.
    requireTestPlugins();
    FreezeEngine e;
    auto& engine = e.engine;
    const std::string wav = clickWav(0.25, kSampleRate, 1000);
    const uint32_t bus = engine.addTrack();
    const uint32_t child = clipTrack(engine, wav, 0.0, 1.0, bus);
    latentEffect(engine, child, 500);  // would make everything else 500 late
    clipTrack(engine, wav, 1.0);
    freeze(engine, bus, tempDir() / "bus.wav", 1.0);
    const Samples out = engine.renderOffline(0.0, kBeat + 4000);
    CHECK(clicks(out) == (std::vector<int64_t>{1000, kBeat + 1000}));
}
