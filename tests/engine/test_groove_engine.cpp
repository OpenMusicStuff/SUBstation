// The built-in Groove device, a MIDI effect: the notes reaching it moved by a
// groove (music/Groove.h). Later (swing, a groove of its own), note-offs and
// bends with their notes, velocity and Length, a Drum Rack's pad, Timing
// automated note by note, switched off mid-note; and early: the clips' notes
// handed early by the renderer (Processor::earlyBeats(), ProcessContext::
// earlyEvents) played early, sample-exact, across a chunk's end and the loop's
// wrap, on a pad, never twice, the same however the render is cut into chunks.
// The renderer's events are watched with a device of the test's own, in
// snapshots made by hand.

#include <algorithm>
#include <map>
#include <memory>

#include "Engine.h"
#include "Rack.h"
#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/DelayCharacter.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"
#include "music/Groove.h"

using namespace subtest;

namespace {

constexpr int64_t kSixteenth = kBeat / 4;  // 6000 samples at 120 BPM

// A device that keeps every note event it is given, with where on the timeline it was heard.
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

    std::vector<Seen> of(bool (sub::ProcessEvent::*which)() const) const {
        std::vector<Seen> found;
        for (const Seen& s : seen)
            if ((s.event.*which)()) found.push_back(s);
        return found;
    }
    std::vector<int64_t> onTimes() const {
        std::vector<int64_t> times;
        for (const Seen& s : of(&sub::ProcessEvent::startsNote)) times.push_back(s.time);
        return times;
    }
    std::vector<int64_t> offTimes() const {
        std::vector<int64_t> times;
        for (const Seen& s : of(&sub::ProcessEvent::endsNote)) times.push_back(s.time);
        return times;
    }
};

int paramIndex(const sub::Processor& device, const std::string& id) {
    const auto& params = device.params();
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].id == id) return static_cast<int>(i);
    throw std::runtime_error("no parameter " + id);
}

// A Groove device, prepared, with `values` set (by parameter id) and its state.
std::shared_ptr<sub::Processor> grooveDevice(const std::map<std::string, float>& values = {},
                                             const sub::BuiltinProcessor::StateValues& state = {}) {
    std::shared_ptr<sub::Processor> device = sub::BuiltinRegistry::instance().create("groove");
    device->prepare(kSampleRate, sub::Renderer::kMaxBlock);
    for (const auto& [id, value] : values) device->setParam(paramIndex(*device, id), value);
    if (!state.empty()) device->setState(sub::BuiltinProcessor::encodeState(state));
    return device;
}

// Its own groove: the state that makes it play `groove`.
sub::BuiltinProcessor::StateValues ownGroove(const sub::groove::Groove& groove) {
    return {{"source", "groove"}, {"groove", sub::groove::toText(groove)}};
}

// One lane of 16th steps, each `timing` of a step (and nothing else).
sub::groove::Groove timed(const std::map<int, double>& timing) {
    sub::groove::Groove groove;
    groove.lanes = {sub::groove::Lane{{}, std::vector<sub::groove::Step>(16)}};
    for (const auto& [step, value] : timing) groove.lanes.front().steps[static_cast<size_t>(step)].timing = value;
    return sub::groove::normalized(std::move(groove));
}

sub::NoteRender noteOf(int64_t start, int64_t end, int key, int velocity = 100) {
    sub::NoteRender note;
    note.start = start;
    note.end = end;
    note.startBeat = static_cast<double>(start) / kSpb;
    note.endBeat = static_cast<double>(end) / kSpb;
    note.key = static_cast<uint8_t>(key);
    note.velocity = static_cast<uint8_t>(velocity);
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

// Renders `frames` from the start, in calls of `piece` frames (chunks end where calls do).
void render(sub::Renderer& renderer, const sub::RenderSnapshot& snap, int64_t frames, bool loop = false,
            int64_t piece = 0) {
    renderer.prepare(kSampleRate);
    renderer.setPosition(0);
    renderer.setPlaying(true);
    if (piece <= 0) piece = frames;
    std::vector<float> out(static_cast<size_t>(piece) * 2);
    for (int64_t done = 0; done < frames; done += piece) {
        renderer.renderOffline(snap, out.data(), std::min(piece, frames - done), loop);
    }
}

// Notes on each 16th of the first beat, half a 16th long.
std::vector<sub::NoteRender> sixteenths(int key = 60, int count = 4) {
    std::vector<sub::NoteRender> notes;
    for (int k = 0; k < count; ++k) notes.push_back(noteOf(k * kSixteenth, k * kSixteenth + kSixteenth / 2, key));
    return notes;
}

// Every note-on has its note-off, by its id, after it (`exactly`: one; else one at least).
void checkPaired(const NoteLog& log, bool exactly = true) {
    std::map<int32_t, int> open;
    for (const NoteLog::Seen& s : log.seen) {
        if (s.event.startsNote()) ++open[s.event.noteId];
        if (s.event.endsNote()) {
            INFO("note " + std::to_string(s.event.noteId));
            CHECK(open[s.event.noteId] > 0 || !exactly);
            --open[s.event.noteId];
        }
    }
    for (const auto& [id, count] : open) {
        INFO("note " + std::to_string(id));
        CHECK(exactly ? count == 0 : count <= 0);
    }
}

}  // namespace

TEST_CASE("groove: the device is listed as a MIDI effect, with its parameters") {
    const sub::BuiltinInfo info = builtinInfo("groove");
    CHECK_EQ(info.name, std::string("Groove"));
    CHECK(info.isMidiEffect());
    CHECK(paramIds(info.params) == (std::vector<std::string>{"swing", "step", "timing", "velocity", "length",
                                                             "random", "quantize", "early", "latency"}));
}

TEST_CASE("groove: swing moves the offbeats later, their note-offs with them") {
    for (const float early : {0.f, 1.f}) {
        INFO(early > 0.f ? "allowed early" : "later only");
        auto groove = grooveDevice({{"swing", static_cast<float>(sub::groove::kTripletSwing)}, {"early", early}});
        auto log = std::make_shared<NoteLog>();
        auto snap = snapshotOf(trackOf({groove, log}, sixteenths()));
        sub::Renderer renderer;
        render(renderer, *snap, kBeat * 2);
        // The offbeat 16ths on the triplet: a third of a 16th (2000 samples) late.
        CHECK(log->onTimes() == (std::vector<int64_t>{0, 8000, 12000, 20000}));
        CHECK(log->offTimes() == (std::vector<int64_t>{3000, 11000, 15000, 23000}));
        checkPaired(*log);
    }
}

TEST_CASE("groove: straight at 50 %, the notes as they come") {
    auto groove = grooveDevice();
    auto log = std::make_shared<NoteLog>();
    auto snap = snapshotOf(trackOf({groove, log}, sixteenths()));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat * 2);
    CHECK(log->onTimes() == (std::vector<int64_t>{0, 6000, 12000, 18000}));
    CHECK(log->offTimes() == (std::vector<int64_t>{3000, 9000, 15000, 21000}));
}

TEST_CASE("groove: a note's bends follow it, by its id") {
    auto groove = grooveDevice({{"swing", 75.f}, {"early", 0.f}});
    auto log = std::make_shared<NoteLog>();
    sub::NoteRender bent = noteOf(kSixteenth, kSixteenth + 4000, 60);
    auto bend = std::make_shared<sub::NoteBendRender>();
    bend->points = {{2000.0, 2.0, 0.0}};
    bent.bend = bend;
    auto snap = snapshotOf(trackOf({groove, log}, {bent}));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat);
    REQUIRE(log->onTimes().size() == 1);
    CHECK_EQ(log->onTimes()[0], kSixteenth + 3000);  // 75 %: half a 16th late
    const auto ons = log->of(&sub::ProcessEvent::startsNote);
    int bends = 0;
    for (const NoteLog::Seen& s : log->seen) {
        if (s.event.type != sub::ProcessEvent::Type::NoteBend) continue;
        ++bends;
        CHECK_EQ(s.event.noteId, ons[0].event.noteId);
        CHECK(s.time >= ons[0].time);
        CHECK(s.time < log->offTimes()[0]);
    }
    CHECK(bends > 10);
    CHECK_EQ(log->offTimes()[0], kSixteenth + 4000 + 3000);
}

TEST_CASE("groove: velocity and Length from the groove's steps") {
    sub::groove::Groove accents = timed({});
    accents.lanes.front().steps[1].velocity = -0.25;  // 31.75 softer
    accents.lanes.front().steps[1].length = 2.0;
    auto groove = grooveDevice({{"length", 100.f}, {"early", 0.f}}, ownGroove(accents));
    auto log = std::make_shared<NoteLog>();
    auto snap = snapshotOf(trackOf({groove, log}, sixteenths(60, 2)));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat);
    const auto ons = log->of(&sub::ProcessEvent::startsNote);
    REQUIRE(ons.size() == 2);
    CHECK_EQ(int(ons[0].event.velocity()), 100);
    CHECK_EQ(int(ons[1].event.velocity()), 68);
    CHECK(log->offTimes() == (std::vector<int64_t>{3000, kSixteenth + 6000}));  // twice as long
}

TEST_CASE("groove: on a Drum Rack's pad it moves only that pad's notes") {
    auto groove = grooveDevice({{"swing", static_cast<float>(sub::groove::kTripletSwing)}});
    auto hats = std::make_shared<NoteLog>(), kick = std::make_shared<NoteLog>();
    auto rack = std::make_shared<sub::RackRender>();
    const auto chainOf = [](std::vector<std::shared_ptr<sub::Processor>> inserts, int key, uint32_t id) {
        sub::ChainRender chain;
        chain.id = id;
        chain.params = std::make_shared<sub::TrackParams>();
        chain.inserts = std::move(inserts);
        chain.receiveNote = key;
        return chain;
    };
    rack->chains.push_back(chainOf({groove, hats}, 42, 10));
    rack->chains.push_back(chainOf({kick}, 36, 11));
    std::vector<sub::NoteRender> notes = sixteenths(42);
    for (const sub::NoteRender& note : sixteenths(36)) notes.push_back(note);
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    sub::TrackRender track = trackOf({std::make_shared<sub::RackProcessor>()}, notes);
    track.racks = {rack};
    auto snap = snapshotOf(std::move(track));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat * 2, false, 1000);
    CHECK(hats->onTimes() == (std::vector<int64_t>{0, 8000, 12000, 20000}));
    CHECK(kick->onTimes() == (std::vector<int64_t>{0, 6000, 12000, 18000}));
    for (const auto& s : hats->seen) CHECK_EQ(int(s.event.key()), 60);  // (as C3, as pads hear them)
    checkPaired(*hats);
}

TEST_CASE("groove: a pad's device after the track's hears the notes as the track's plays them") {
    // The track's Groove device swings; the pad's plays straight. The notes handed early are the track's
    // device's (the first MIDI effect on their way): the pad's hears them as it plays them, late.
    auto swung = grooveDevice({{"swing", static_cast<float>(sub::groove::kTripletSwing)}});
    auto straight = grooveDevice();
    auto hats = std::make_shared<NoteLog>();
    auto rack = std::make_shared<sub::RackRender>();
    sub::ChainRender pad;
    pad.id = 10;
    pad.params = std::make_shared<sub::TrackParams>();
    pad.inserts = {straight, hats};
    pad.receiveNote = 42;
    rack->chains.push_back(std::move(pad));
    sub::TrackRender track = trackOf({swung, std::make_shared<sub::RackProcessor>()}, sixteenths(42));
    track.racks = {{}, rack};
    auto snap = snapshotOf(std::move(track));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat * 2, false, 1000);
    CHECK(hats->onTimes() == (std::vector<int64_t>{0, 8000, 12000, 20000}));
    checkPaired(*hats);
}

TEST_CASE("groove: Timing automated takes effect note by note") {
    auto groove = grooveDevice({{"swing", 75.f}, {"early", 0.f}});
    auto log = std::make_shared<NoteLog>();
    sub::TrackRender track = trackOf({groove, log}, sixteenths(60, 4));
    // Timing 100 % (0.5 normalized) for the first offbeat, 0 from the second beat-half on.
    sub::AutomationRender lane;
    lane.processor = groove;
    lane.param = paramIndex(*groove, "timing");
    lane.insert = 0;
    lane.nodes = {{0, 0.5f, 0.f}, {kBeat / 2, 0.5f, 0.f}, {kBeat / 2, 0.f, 0.f}};
    track.automation = {lane};
    auto snap = snapshotOf(std::move(track));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat * 2, false, 512);
    CHECK(log->onTimes() == (std::vector<int64_t>{0, 9000, 12000, 18000}));
}

TEST_CASE("groove: switched off mid-note, no note hangs") {
    auto groove = grooveDevice({{"swing", 75.f}, {"early", 0.f}});
    auto log = std::make_shared<NoteLog>();
    // A long offbeat note: it sounds from 9000 to 21000.
    auto snap = snapshotOf(trackOf({groove, log}, {noteOf(kSixteenth, kSixteenth * 3, 60), noteOf(kSixteenth * 3, kSixteenth * 4, 62)}));
    sub::Renderer renderer;
    renderer.prepare(kSampleRate);
    renderer.setPosition(0);
    renderer.setPlaying(true);
    std::vector<float> out(20000 * 2);
    renderer.renderOffline(*snap, out.data(), 12000);
    REQUIRE(log->onTimes().size() == 1);
    groove->setEnabled(false);
    renderer.renderOffline(*snap, out.data(), 20000);
    checkPaired(*log, false);  // (its note-off from the clip comes too, as it would without it)
    CHECK_EQ(log->offTimes().front(), 12000);  // (where the chunk it went off in starts)
    // Off, the notes go on as they come.
    CHECK(log->onTimes() == (std::vector<int64_t>{9000, 18000}));
}

TEST_CASE("groove: stopping drops the notes it holds that haven't sounded") {
    auto groove = grooveDevice({{"swing", 75.f}, {"early", 0.f}});
    auto log = std::make_shared<NoteLog>();
    auto snap = snapshotOf(trackOf({groove, log}, {noteOf(kSixteenth, kSixteenth + 1000, 60)}));
    sub::Renderer renderer;
    renderer.prepare(kSampleRate);
    renderer.setPosition(0);
    renderer.setPlaying(true);
    std::vector<float> out(20000 * 2);
    renderer.renderOffline(*snap, out.data(), kSixteenth + 1024);  // the note came in, held until 9000
    renderer.setPlaying(false);
    renderer.renderOffline(*snap, out.data(), 10000);
    CHECK(log->seen.empty());
}

// --- Early notes ---------------------------------------------------------------------------------

TEST_CASE("groove: a clip's note grooved early plays early, to the sample, across a chunk's end") {
    // The third 16th 0.4 of a step early: 2400 samples before 12000.
    const sub::groove::Groove early = timed({{2, -0.4}});
    for (const int64_t piece : {int64_t{0}, int64_t{1000}, int64_t{777}, int64_t{11000}}) {
        INFO("in pieces of " + std::to_string(piece));
        auto groove = grooveDevice({}, ownGroove(early));
        auto log = std::make_shared<NoteLog>();
        auto snap = snapshotOf(trackOf({groove, log}, sixteenths()));
        sub::Renderer renderer;
        render(renderer, *snap, kBeat * 2, false, piece);
        CHECK(log->onTimes() == (std::vector<int64_t>{0, 6000, 9600, 18000}));
        CHECK(log->offTimes() == (std::vector<int64_t>{3000, 9000, 12600, 21000}));
        checkPaired(*log);
    }
    // Allow Early off: on its time.
    auto groove = grooveDevice({{"early", 0.f}}, ownGroove(early));
    CHECK_EQ(groove->earlyBeats(), 0.0);
    auto log = std::make_shared<NoteLog>();
    auto snap = snapshotOf(trackOf({groove, log}, sixteenths()));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat * 2);
    CHECK(log->onTimes() == (std::vector<int64_t>{0, 6000, 12000, 18000}));
}

TEST_CASE("groove: early notes play no earlier than half a step") {
    auto groove = grooveDevice({{"timing", 200.f}}, ownGroove(timed({{2, -0.4}})));
    CHECK_APPROX(groove->earlyBeats(), 0.125);
    auto log = std::make_shared<NoteLog>();
    auto snap = snapshotOf(trackOf({groove, log}, sixteenths()));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat * 2, false, 1000);
    CHECK(log->onTimes() == (std::vector<int64_t>{0, 6000, 9000, 18000}));  // (0.8 of a step held to half)
}

TEST_CASE("groove: an early note comes round the loop before the loop's start") {
    // The loop is a beat; its first note is grooved 0.4 of a step early, so each time round it plays
    // 2400 samples before the loop's end (the first time, from the start, on its time).
    auto groove = grooveDevice({}, ownGroove(timed({{0, -0.4}})));
    auto log = std::make_shared<NoteLog>();
    auto snap = snapshotOf(trackOf({groove, log}, {noteOf(0, 1000, 60), noteOf(12000, 13000, 62)}));
    snap->loopEnabled = true;
    snap->loopStart = 0;
    snap->loopEnd = kBeat;
    sub::Renderer renderer;
    render(renderer, *snap, kBeat * 3, true, 1000);
    std::vector<std::pair<int64_t, int>> ons;
    for (const auto& on : log->of(&sub::ProcessEvent::startsNote)) ons.emplace_back(on.time, on.event.key());
    CHECK(ons == (std::vector<std::pair<int64_t, int>>{{0, 60}, {12000, 62}, {21600, 60}, {12000, 62}, {21600, 60},
                                                       {12000, 62}, {21600, 60}}));
    // Each played once, its note-off after it (the one before the loop's end, a 1000 samples after it).
    CHECK_EQ(log->of(&sub::ProcessEvent::endsNote).size(), size_t{7});
    CHECK_EQ(log->offTimes()[2], 22600);
    checkPaired(*log);
}

TEST_CASE("groove: a pad's notes come early to its Groove device") {
    auto groove = grooveDevice({}, ownGroove(timed({{2, -0.4}})));
    auto hats = std::make_shared<NoteLog>(), all = std::make_shared<NoteLog>();
    auto rack = std::make_shared<sub::RackRender>();
    sub::ChainRender pad;
    pad.id = 10;
    pad.params = std::make_shared<sub::TrackParams>();
    pad.inserts = {groove, hats};
    pad.receiveNote = 42;
    rack->chains.push_back(std::move(pad));
    sub::ChainRender every;
    every.id = 11;
    every.params = std::make_shared<sub::TrackParams>();
    every.inserts = {all};
    rack->chains.push_back(std::move(every));
    std::vector<sub::NoteRender> notes = sixteenths(42);
    notes.push_back(noteOf(2 * kSixteenth, 2 * kSixteenth + 100, 36));
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) {
        return a.start != b.start ? a.start < b.start : a.key < b.key;
    });
    sub::TrackRender track = trackOf({std::make_shared<sub::RackProcessor>()}, notes);
    track.racks = {rack};
    auto snap = snapshotOf(std::move(track));
    sub::Renderer renderer;
    render(renderer, *snap, kBeat * 2, false, 1000);
    CHECK(hats->onTimes() == (std::vector<int64_t>{0, 6000, 9600, 18000}));
    checkPaired(*hats);
    // The chain without one hears every note in its time, once.
    CHECK(all->onTimes() == (std::vector<int64_t>{0, 6000, 12000, 12000, 18000}));
    checkPaired(*all);
}

TEST_CASE("groove: stopping drops a note handed early that hasn't sounded") {
    auto groove = grooveDevice({}, ownGroove(timed({{2, -0.4}})));
    auto log = std::make_shared<NoteLog>();
    auto snap = snapshotOf(trackOf({groove, log}, {noteOf(2 * kSixteenth, 2 * kSixteenth + 1000, 60)}));
    sub::Renderer renderer;
    renderer.prepare(kSampleRate);
    renderer.setPosition(0);
    renderer.setPlaying(true);
    std::vector<float> out(20000 * 2);
    renderer.renderOffline(*snap, out.data(), 9216);  // handed (due at 12000, to play at 9600)
    renderer.setPlaying(false);
    renderer.renderOffline(*snap, out.data(), 10000);
    CHECK(log->seen.empty());
    // And a locate: dropped too, the note played where the playhead goes.
    log->seen.clear();
    renderer.setPosition(0);
    renderer.setPlaying(true);
    renderer.renderOffline(*snap, out.data(), 9216);
    renderer.setPosition(2 * kSixteenth - 500);  // (just before the note)
    renderer.renderOffline(*snap, out.data(), 4000);
    REQUIRE(log->onTimes().size() == 1);
    CHECK_EQ(log->onTimes()[0], 2 * kSixteenth);  // (no window ahead of it: on its time)
    checkPaired(*log);
}

// --- Follow Groove ---------------------------------------------------------------------------------

TEST_CASE("groove: the Arpeggiator's steps follow the project's groove") {
    for (const bool follow : {false, true}) {
        INFO(follow ? "following" : "not following");
        std::shared_ptr<sub::Processor> arp = sub::BuiltinRegistry::instance().create("arpeggiator");
        arp->prepare(kSampleRate, sub::Renderer::kMaxBlock);
        arp->setParam(paramIndex(*arp, "follow_groove"), follow ? 1.f : 0.f);
        auto log = std::make_shared<NoteLog>();
        auto snap = snapshotOf(trackOf({arp, log}, {noteOf(0, kBeat, 60)}));
        snap->groove = std::make_shared<const sub::groove::Groove>(sub::groove::swing(75.0, 0.25));
        sub::Renderer renderer;
        render(renderer, *snap, kBeat, false, 1000);
        const std::vector<int64_t> ons = log->onTimes();
        REQUIRE(ons.size() >= 4);
        CHECK((std::vector<int64_t>(ons.begin(), ons.begin() + 4) ==
               (follow ? std::vector<int64_t>{0, 9000, 12000, 21000} : std::vector<int64_t>{0, 6000, 12000, 18000})));
    }
}

TEST_CASE("groove: the Delay's swing follows the project's groove") {
    const sub::groove::Groove triplet = sub::groove::swing(sub::groove::kTripletSwing, 0.25);
    const sub::TempoMap tempo(120.0);
    for (const bool follow : {false, true}) {
        INFO(follow ? "following" : "not following");
        Standalone delay("delay", kSampleRate,
                         {{"l_sync", 1.f}, {"l_division", 0.f}, {"link", 1.f}, {"feedback", 0.f}, {"mix", 100.f},
                          {"filter", 0.f}, {"follow_groove", follow ? 1.f : 0.f}});
        delay.context().groove = &triplet;
        delay.context().tempoMap = &tempo;
        Samples in(kBeat, 0.f);
        in[0] = 1.f;
        const Samples out = delay.play(in);
        // The first echo: a 16th late, swung onto the triplet when it follows the groove (and where the Delay's
        // Digital character puts it: its modulation's move then, and its band-limit's lateness).
        int64_t loudest = 0;
        for (size_t i = 1; i < out.size(); ++i)
            if (std::abs(out[i]) > std::abs(out[static_cast<size_t>(loudest)])) loudest = static_cast<int64_t>(i);
        const std::vector<float> taps = sub::delay_character::DigitalDesign::bandLimit(kSampleRate);
        const auto peak = std::max_element(taps.begin(), taps.end()) - taps.begin();
        sub::delay_character::DigitalModulation modulation;
        modulation.prepare(kSampleRate);
        int64_t expected = 0;
        while (static_cast<double>(expected) - (follow ? 8000 : 6000) - modulation.next() < static_cast<double>(peak) - 0.5)
            ++expected;
        CHECK(std::abs(loudest - expected) <= 2);
    }
}

TEST_CASE("groove: a synced LFO follows the project's groove") {
    const sub::groove::Groove most = sub::groove::swing(75.0, 0.25);
    const sub::TempoMap tempo(120.0);
    for (const bool follow : {false, true}) {
        INFO(follow ? "following" : "not following");
        Standalone lfo("lfo", kSampleRate, {{"sync", 1.f}, {"synced", 6.f}, {"follow_groove", follow ? 1.f : 0.f}});
        lfo.context().playing = true;
        lfo.context().groove = &most;
        lfo.context().tempoMap = &tempo;
        lfo.play(Samples(12000, 0.f));
        const std::vector<float> phase = lfo.display("phase");
        REQUIRE(phase.size() > 140);
        // At sample 9023 (the 141st value): a 1/8 cycle's phase there, of the straight time the groove puts there.
        const double beat = 9023.0 / kSpb;
        const double straight = follow ? 0.25 + 2.0 * (beat - 0.375) : beat;
        CHECK(std::abs(phase[140] - straight / 0.5) < 0.002);
    }
}
