// Drum Racks in the engine: a rack chain given a key to receive (a pad:
// Engine::setChainReceiveNote) hears only that key's notes, as C3, so each
// pad's Sampler plays its sample at its own pitch, and a sidechain taken from a
// pad hears that pad alone; and, constrained, a live
// track's own clips' notes played late by TrackRender::clipDelay (only its
// input comes early), across a loop's wrap too. The renderer's events are
// watched with a device of the test's own, in snapshots made by hand.

#include <cmath>
#include <map>
#include <memory>

#include "Engine.h"
#include "Rack.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

// A device that keeps every note event it is given, with where on the timeline
// it was heard (the stretch's position plus its offset).
class NoteLog final : public sub::Processor {
public:
    struct Seen {
        int64_t time;
        sub::ProcessEvent event;
    };
    std::vector<Seen> seen;

    std::string typeId() const override { return "test:notes"; }
    std::string name() const override { return "Note Log"; }
    void prepare(double, int) override {}
    void process(const sub::ProcessContext& ctx, float* const*, int, int) override {
        for (size_t i = 0; i < ctx.inEvents.count; ++i) {
            seen.push_back({ctx.samplePos + ctx.inEvents.events[i].sampleOffset, ctx.inEvents.events[i]});
        }
    }
    const std::vector<sub::ParamInfo>& params() const override {
        static const std::vector<sub::ParamInfo> kNone;
        return kNone;
    }
    float getParam(int) const override { return 0.f; }
    void setParam(int, float) override {}
    bool acceptsMidi() const override { return true; }

    std::vector<Seen> ons() const {
        std::vector<Seen> found;
        for (const Seen& s : seen)
            if (s.event.startsNote()) found.push_back(s);
        return found;
    }
};

sub::NoteRender noteOf(int64_t start, int64_t end, int key) {
    sub::NoteRender note;
    note.start = start;
    note.end = end;
    note.key = static_cast<uint8_t>(key);
    note.velocity = 100;
    return note;
}

sub::TrackRender trackOf(std::vector<std::shared_ptr<sub::Processor>> inserts, std::vector<sub::NoteRender> notes) {
    sub::TrackRender track;
    track.id = 1;
    track.params = std::make_shared<sub::TrackParams>();
    track.inserts = std::move(inserts);
    track.buffers = std::make_shared<sub::TrackBuffers>(sub::Renderer::kMaxBlock);
    track.notes = std::move(notes);
    return track;
}

std::shared_ptr<sub::RenderSnapshot> snapshotOf(sub::TrackRender track) {
    auto snap = std::make_shared<sub::RenderSnapshot>();
    snap->sampleRate = kSampleRate;
    snap->tempoMap = std::make_shared<const sub::TempoMap>(120.0);
    snap->tracks.push_back(std::move(track));
    return snap;
}

void render(sub::Renderer& renderer, const sub::RenderSnapshot& snap, int64_t frames, bool loop = false) {
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    renderer.prepare(kSampleRate);
    renderer.setPosition(0);
    renderer.setPlaying(true);
    renderer.renderOffline(snap, out.data(), frames, loop);
}

std::vector<uint8_t> sampleState(const std::string& path) {
    std::string text = "sample=";
    for (const char c : path) text += c == '\\' ? std::string("\\\\") : std::string(1, c);
    text += "\n";
    return {text.begin(), text.end()};
}

}  // namespace

TEST_CASE("a Drum Rack's pads each hear their own key, as C3") {
    // Snapshots by hand: a rack of three chains, two pads (C1, D1) and a chain hearing every note.
    auto kick = std::make_shared<NoteLog>(), snare = std::make_shared<NoteLog>(), all = std::make_shared<NoteLog>();
    auto rack = std::make_shared<sub::RackRender>();
    const auto chainOf = [](const std::shared_ptr<NoteLog>& log, int key, uint32_t id) {
        sub::ChainRender chain;
        chain.id = id;
        chain.params = std::make_shared<sub::TrackParams>();
        chain.inserts = {log};
        chain.receiveNote = key;
        return chain;
    };
    rack->chains.push_back(chainOf(kick, 36, 10));
    rack->chains.push_back(chainOf(snare, 38, 11));
    rack->chains.push_back(chainOf(all, -1, 12));
    sub::TrackRender track = trackOf({std::make_shared<sub::RackProcessor>()},
                                     {noteOf(100, 200, 36), noteOf(300, 400, 38), noteOf(500, 600, 40)});
    track.racks = {rack};
    auto snap = snapshotOf(std::move(track));
    sub::Renderer renderer;
    render(renderer, *snap, 1000);
    REQUIRE(kick->ons().size() == 1);
    CHECK_EQ(kick->ons()[0].time, 100);
    CHECK_EQ(kick->ons()[0].event.key(), 60);  // as C3
    CHECK_EQ(kick->seen.size(), size_t{2});    // and its note-off, as C3 too
    CHECK_EQ(kick->seen[1].event.key(), 60);
    CHECK_EQ(kick->seen[1].time, 200);
    REQUIRE(snare->ons().size() == 1);
    CHECK_EQ(snare->ons()[0].time, 300);
    CHECK_EQ(snare->ons()[0].event.key(), 60);
    CHECK_EQ(all->ons().size(), size_t{3});  // (every key, as it is)
    CHECK_EQ(all->ons()[2].event.key(), 40);
}

TEST_CASE("a Drum Rack's Samplers play their samples at their own pitch") {
    sub::Engine engine;
    const uint32_t track = engine.addTrack();
    const uint32_t rack = engine.addRack(engine.trackChain(track), -1);
    const auto pad = [&](int key, double freq) {
        const uint32_t chain = engine.addRackChain(rack, -1);
        engine.setChainReceiveNote(chain, key);
        const uint32_t sampler = engine.addBuiltinProcessor(chain, "sampler", -1);
        setParam(engine, sampler, "velocity", 0.f);
        setParam(engine, sampler, "release", 1.f);
        engine.setProcessorState(sampler, sampleState(makeWav(sine(freq, 1.0))));
        return chain;
    };
    const uint32_t low = pad(36, 440.0);
    pad(38, 660.0);
    CHECK_EQ(engine.chainReceiveNote(low), 36);
    const auto heard = [&](int key) {
        engine.setTrackNotes(track, {{0.0, 1.0, key, 127}});
        return channel(frames(engine.renderOffline(0.0, kSampleRate / 2), 1000, kSampleRate / 2), 0);
    };
    CHECK_APPROX_REL(dominantFreq(heard(36)), 440, 1e-3);
    CHECK_APPROX_REL(dominantFreq(heard(38)), 660, 1e-3);
    CHECK(maxAbs(heard(40)) < 1e-6);  // no pad there
    engine.setChainReceiveNote(low, -1);  // every note again: C1 two octaves down
    CHECK_APPROX_REL(dominantFreq(heard(36)), 110, 1e-2);
    CHECK_THROWS_AS(engine.setChainReceiveNote(engine.trackChain(track), 36), std::invalid_argument);
}

TEST_CASE("a sidechain taken from a Drum Rack's pad hears that pad alone") {
    requireTestPlugins();
    sub::Engine engine;
    const uint32_t track = engine.addTrack();
    const uint32_t rack = engine.addRack(engine.trackChain(track), -1);
    const auto pad = [&](int key, double freq) {
        const uint32_t chain = engine.addRackChain(rack, -1);
        engine.setChainReceiveNote(chain, key);
        const uint32_t sampler = engine.addBuiltinProcessor(chain, "sampler", -1);
        setParam(engine, sampler, "velocity", 0.f);
        setParam(engine, sampler, "release", 1.f);
        engine.setProcessorState(sampler, sampleState(makeWav(sine(freq, 1.0))));
        return chain;
    };
    const uint32_t kick = pad(36, 440.0);
    const uint32_t snare = pad(38, 660.0);
    engine.setTrackNotes(track, {{0.0, 1.0, 36, 127}, {0.0, 1.0, 38, 127}});  // both at once
    engine.setTrackGain(track, 0.f);  // heard only through the sidechain
    const uint32_t keyed = addTestPlugin(engine, engine.trackChain(engine.addTrack()), "SUB Test Sidechain");
    const auto heard = [&](uint32_t chain, sub::SidechainTap tap) {
        engine.setProcessorSidechain(keyed, track, tap, 0, chain);
        return channel(frames(engine.renderOffline(0.0, kSampleRate / 2), 1000, kSampleRate / 2), 0);
    };
    CHECK_APPROX_REL(dominantFreq(heard(kick, sub::SidechainTap::PreFader)), 440, 1e-3);
    CHECK_APPROX_REL(dominantFreq(heard(snare, sub::SidechainTap::PostFader)), 660, 1e-3);
    engine.setChainMute(snare, true);
    CHECK(maxAbs(heard(snare, sub::SidechainTap::PostFader)) < 1e-6);  // a muted pad keys nothing after its fader
    CHECK_APPROX_REL(dominantFreq(heard(snare, sub::SidechainTap::PreFader)), 660, 1e-3);  // but still before it
    CHECK(maxAbs(heard(kick, sub::SidechainTap::PreFx)) < 1e-6);  // before its Sampler: the rack's input, silence
}

TEST_CASE("a live track's clips' notes play clipDelay late") {
    auto log = std::make_shared<NoteLog>();
    sub::TrackRender track = trackOf({log}, {noteOf(1000, 2000, 60)});
    track.clipDelay = 300;
    auto snap = snapshotOf(std::move(track));
    sub::Renderer renderer;
    render(renderer, *snap, 4000);
    REQUIRE(log->seen.size() == 2);
    CHECK_EQ(log->seen[0].time, 1300);
    CHECK_EQ(log->seen[1].time, 2300);
}

TEST_CASE("looping, a live track's clips' notes late by clipDelay come round with the loop") {
    auto log = std::make_shared<NoteLog>();
    // The loop is the first 4000 samples: a note near its start, one near its end.
    sub::TrackRender track = trackOf({log}, {noteOf(100, 150, 60), noteOf(3800, 3850, 62)});
    track.clipDelay = 300;
    auto snap = snapshotOf(std::move(track));
    snap->loopEnabled = true;
    snap->loopStart = 0;
    snap->loopEnd = 4000;
    sub::Renderer renderer;
    render(renderer, *snap, 12000, true);
    // Each pass: the first at 400; the last, 300 late, after the wrap (at 100 of the next pass).
    std::vector<std::pair<int64_t, int>> ons;
    for (const auto& on : log->ons()) ons.emplace_back(on.time, on.event.key());
    CHECK(ons == (std::vector<std::pair<int64_t, int>>{{400, 60}, {100, 62}, {400, 60}, {100, 62}, {400, 60}}));
}
