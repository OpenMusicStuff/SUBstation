// Batches (Engine::Batch): the edits made while one lives build one snapshot,
// when the last ends; whatever reads the snapshot meanwhile sees every edit made
// so far; sources decoded without publishing (decodeSource()) are heard once
// something publishes, and by any read before that.

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

// A project of `tracks` clip tracks (each playing the file a beat later than the last), a group the
// odd ones go into, and an EQ on each.
void buildProject(sub::Engine& engine, const std::string& path, int tracks) {
    const uint32_t group = engine.addTrack();
    for (int t = 0; t < tracks; ++t) {
        engine.loadSource(path);
        const uint32_t track = engine.addTrack();
        engine.setTrackClips(track, {clip(path, t, 0.5)});
        engine.addBuiltinProcessor(engine.trackChain(track), "eq", -1);
        if (t % 2 == 1) engine.setTrackOutput(track, group);
    }
}

}  // namespace

TEST_CASE("a batch's edits build one snapshot, and play as unbatched edits do") {
    const std::string wav = dcWav();
    const auto frames = static_cast<int64_t>(10 * kSpb);
    sub::Engine plain;
    plain.setClipFadeMs(0);
    const uint64_t plainBefore = plain.snapshotBuilds();
    buildProject(plain, wav, 8);
    REQUIRE(plain.snapshotBuilds() - plainBefore > 8);  // (one an edit)

    sub::Engine batched;
    batched.setClipFadeMs(0);
    const uint64_t before = batched.snapshotBuilds();
    {
        const sub::Engine::Batch batch(batched);
        buildProject(batched, wav, 8);
        REQUIRE(batched.snapshotBuilds() == before);
    }
    REQUIRE(batched.snapshotBuilds() == before + 1);
    REQUIRE(batched.renderOffline(0.0, frames) == plain.renderOffline(0.0, frames));
}

TEST_CASE("reads in a batch see the edits made so far") {
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const std::string wav = dcWav();
    const sub::Engine::Batch batch(engine);
    const uint32_t track = engine.addTrack();
    engine.loadSource(wav);
    engine.setTrackClips(track, {clip(wav, 0.0, 0.5)});
    const std::vector<float> out = engine.renderOffline(0.0, 1000);
    CHECK_APPROX(out[0], 0.5);  // (the render published the snapshot: the clip plays)
    REQUIRE(engine.trackLatency(track) == 0);
    REQUIRE(engine.liveLatency(track).devices == 0);
}

TEST_CASE("nested batches publish when the outermost ends") {
    sub::Engine engine;
    const uint64_t before = engine.snapshotBuilds();
    {
        const sub::Engine::Batch outer(engine);
        {
            const sub::Engine::Batch inner(engine);
            engine.addTrack();
        }
        REQUIRE(engine.snapshotBuilds() == before);
        engine.addTrack();
    }
    REQUIRE(engine.snapshotBuilds() == before + 1);
    // A batch with no edits builds nothing.
    { const sub::Engine::Batch idle(engine); }
    REQUIRE(engine.snapshotBuilds() == before + 1);
}

TEST_CASE("a source decoded without publishing plays once published, or read") {
    const std::string wav = dcWav();
    {
        sub::Engine engine;
        engine.setClipFadeMs(0);
        const uint32_t track = engine.addTrack();
        engine.setTrackClips(track, {clip(wav, 0.0, 0.5)});
        const uint64_t before = engine.snapshotBuilds();
        REQUIRE(engine.decodeSource(wav) != nullptr);
        REQUIRE(engine.snapshotBuilds() == before);  // stale, not built
        CHECK_APPROX(engine.renderOffline(0.0, 1000)[0], 0.5);  // a read publishes it first
    }
    {
        sub::Engine engine;
        engine.setClipFadeMs(0);
        const uint32_t track = engine.addTrack();
        engine.setTrackClips(track, {clip(wav, 0.0, 0.5)});
        engine.decodeSource(wav);
        const uint64_t before = engine.snapshotBuilds();
        engine.idle();  // nobody published it: idle() does
        REQUIRE(engine.snapshotBuilds() == before + 1);
        // Several decoded together, in a batch: one snapshot for them all.
        const std::string other = makeWav(std::vector<float>(kSampleRate, 0.25f), 1);
        const uint32_t second = engine.addTrack();
        engine.setTrackClips(second, {clip(other, 0.0, 0.5)});
        const uint64_t again = engine.snapshotBuilds();
        {
            const sub::Engine::Batch batch(engine);
            engine.decodeSource(other);
            engine.loadSource(wav);  // (cached already: nothing to publish)
        }
        REQUIRE(engine.snapshotBuilds() == again + 1);
    }
}
