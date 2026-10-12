// The built-in modulators (the LFO and the Envelope) and modulation: their
// curves at their rates and lengths, synced and free, Depth, Offset and Phase,
// retriggering, notes and sidechain hits, velocity; then the engine running a
// track's modulators before its devices and moving the parameters mapped to
// them, every 64 samples, over a range, upside down, in racks, ahead of
// automation, held while switched off, and never touching the audio.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Engine.h"
#include "builtin/ModulatorDesign.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"

using namespace subtest;

namespace {

constexpr double kLevel = 0.5;  // dcWav()'s
constexpr int kStep = 64;       // the renderer's kAutomationStep

using Point = sub::modulator::Point;

// A modulator's curve as its parameters (every slot: the unused ones off).
ParamValues curveValues(const std::vector<Point>& points) {
    ParamValues values;
    for (int i = 0; i < sub::modulator::kPoints; ++i) {
        const bool used = i < static_cast<int>(points.size());
        const Point p = used ? points[static_cast<size_t>(i)] : Point{1.f, 0.f, 0.f};
        const std::string id = "p" + std::to_string(i + 1) + "_";
        values.insert(values.end(),
                      {{id + "used", used ? 1.f : 0.f}, {id + "x", p.x}, {id + "y", p.y}, {id + "curve", p.curve}});
    }
    return values;
}

const std::vector<Point> kRamp{{0.f, 0.f, 0.f}, {1.f, 1.f, 0.f}};  // 0 up to 1, straight
const std::vector<Point> kFall{{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};  // 1 down to 0, straight

// An LFO free at 1 Hz on a ramp (a cycle: a second), unsmoothed, then `more`.
ParamValues lfoValues(const ParamValues& more = {}) {
    return with(ParamValues{{"sync", 0.f}, {"rate", 1.f}, {"smooth", 0.f}} + curveValues(kRamp), more);
}

// An Envelope over 100 ms falling from 1 to 0, unsmoothed, then `more`.
ParamValues envelopeValues(const ParamValues& more = {}) {
    return with(ParamValues{{"sync", 0.f}, {"length", 100.f}, {"smooth", 0.f}} + curveValues(kFall), more);
}

// What a modulator puts out over `frames` (its first channel), with `before` called before each block.
Samples modulatorOutput(Standalone& device, int64_t frames, const Standalone::BeforeBlock& before = {},
                        int block = 256) {
    Samples left(static_cast<size_t>(frames), 0.f), right(static_cast<size_t>(frames), 0.f);
    device.run({&left, &right}, {}, block, before);
    return left;
}

// Notes for a Standalone device: each (frame, velocity) handed over in its block.
struct Notes {
    std::vector<std::pair<int64_t, int>> notes;
    std::vector<sub::ProcessEvent> events;
    Standalone::BeforeBlock feeding(Standalone& device) {
        return [this, &device](int64_t start, int frames) {
            events.clear();
            for (const auto& [frame, velocity] : notes) {
                if (frame >= start && frame < start + frames) {
                    events.push_back(sub::ProcessEvent::noteOn(static_cast<int32_t>(frame - start), 60,
                                                               static_cast<uint8_t>(velocity)));
                }
            }
            device.context().inEvents = {events.data(), events.size()};
        };
    }
};

// A Sidechain device that turns a DC track down by its Depth: no sidechain, no
// smoothing, its curve resting at 0, so the track plays at 0.5 * (1 - depth).
uint32_t ducker(sub::Engine& engine, uint32_t chain) {
    const uint32_t device = engine.addBuiltinProcessor(chain, "sidechain", -1);
    setParam(engine, device, "smooth", 0.f);
    setParam(engine, device, "p3_y", 0.f);
    setParam(engine, device, "depth", 0.f);
    return device;
}

uint32_t modulatorOn(sub::Engine& engine, uint32_t chain, const std::string& kind, const ParamValues& values,
                     int index = -1) {
    const uint32_t device = engine.addBuiltinProcessor(chain, kind, index);
    for (const auto& [id, value] : values) setParam(engine, device, id, value);
    return device;
}

// The ducker's depth (0..1) at each frame of a render of the DC track.
std::vector<double> depthsOf(const Samples& out) {
    std::vector<double> depths;
    for (const float sample : channel(out, 0)) depths.push_back(1.0 - sample / kLevel);
    return depths;
}

// The LFO's ramp as the renderer hands it on: its value at each step's first frame.
double steppedRamp(int64_t frame, double cycle = kSampleRate) {
    const int64_t step = frame / kStep * kStep;
    return frac(static_cast<double>(step) / cycle);
}

struct ModulationEngine {
    sub::Engine engine;
    uint32_t track = 0;
    ModulationEngine() {
        engine.setClipFadeMs(0);
        track = clipTrack(engine, dcWav());
    }
    uint32_t chain() { return engine.trackChain(track); }
};

}  // namespace

// --- The devices on their own -------------------------------------------------------------

TEST_CASE("the LFO and the Envelope are listed as modulators") {
    for (const char* kind : {"lfo", "envelope"}) {
        INFO(kind);
        const sub::BuiltinInfo info = builtinInfo(kind);
        CHECK(info.isModulator());
        CHECK(!info.isInstrument());
        const auto device = sub::BuiltinRegistry::instance().create(kind);
        CHECK(device->isModulator());
        CHECK(device->acceptsMidi());
        const std::vector<std::string> ids = paramIds(info.params);
        REQUIRE(ids.size() == (std::string(kind) == "lfo" ? 8u : 7u) + 32 * 4);
        CHECK_EQ(ids.back(), std::string("p32_curve"));
        for (const sub::ParamInfo& p : info.params) {
            if (p.id[0] != 'p' || p.id == "phase") continue;
            INFO(p.id);
            CHECK(p.hidden);
            CHECK(!p.automatable);
        }
    }
    CHECK(paramIds(builtinInfo("lfo").params)[0] == "rate");
    CHECK(paramIds(builtinInfo("envelope").params)[0] == "threshold");
    CHECK(sub::BuiltinRegistry::instance().create("envelope")->hasSidechain());
    CHECK(!sub::BuiltinRegistry::instance().create("lfo")->hasSidechain());
}

TEST_CASE("the LFO plays its curve at its rate") {
    Standalone lfo("lfo", kSampleRate, lfoValues());
    const Samples out = modulatorOutput(lfo, 2 * kSampleRate);
    for (const int64_t n : {0, 1000, 12000, 24000, 47999, 48000, 60000, 95000}) {
        INFO(std::to_string(n));
        CHECK_NEAR(out[static_cast<size_t>(n)], frac(static_cast<double>(n) / kSampleRate), 1e-4);
    }
    // At 4 Hz, four cycles a second.
    Standalone fast("lfo", kSampleRate, lfoValues({{"rate", 4.f}}));
    const Samples quick = modulatorOutput(fast, kSampleRate);
    CHECK_NEAR(quick[6000], 0.5, 1e-4);
    CHECK_NEAR(quick[12000 + 3000], 0.25, 1e-4);
}

TEST_CASE("the LFO's triangle, as it starts, and its sine") {
    Standalone triangle("lfo", kSampleRate, {{"sync", 0.f}, {"rate", 1.f}});  // its default curve
    const Samples up = modulatorOutput(triangle, kSampleRate);
    CHECK_NEAR(up[0], 0.0, 1e-6);
    CHECK_NEAR(up[12000], 0.5, 1e-4);
    CHECK_NEAR(up[24000], 1.0, 1e-4);
    CHECK_NEAR(up[36000], 0.5, 1e-4);
    Standalone lfo("lfo", kSampleRate, ParamValues{{"sync", 0.f}, {"rate", 1.f}} + curveValues(sub::modulator::sinePoints()));
    const Samples out = modulatorOutput(lfo, kSampleRate);
    double worst = 0.0;
    for (int64_t n = 0; n < kSampleRate; n += 37) {
        const double expected = 0.5 + 0.5 * std::sin(2 * kPi * static_cast<double>(n) / kSampleRate);
        worst = std::max(worst, std::abs(out[static_cast<size_t>(n)] - expected));
    }
    CHECK(worst < 0.006);
}

TEST_CASE("the LFO's depth, offset and phase") {
    Standalone half("lfo", kSampleRate, lfoValues({{"depth", 50.f}}));
    Samples out = modulatorOutput(half, kSampleRate);
    CHECK_NEAR(out[0], 0.25, 1e-4);  // round the middle: 0.25 .. 0.75
    CHECK_NEAR(out[36000], 0.625, 1e-4);

    Standalone up("lfo", kSampleRate, lfoValues({{"depth", 50.f}, {"offset", 50.f}}));
    out = modulatorOutput(up, kSampleRate);
    CHECK_NEAR(out[0], 0.5, 1e-4);  // a quarter of the range up
    CHECK_NEAR(out[47000], 0.5 + (47000.0 / kSampleRate - 0.5) * 0.5 + 0.25, 1e-4);

    Standalone clipped("lfo", kSampleRate, lfoValues({{"offset", 60.f}}));
    out = modulatorOutput(clipped, kSampleRate);
    CHECK_NEAR(out[0], 0.3, 1e-4);
    CHECK_EQ(*std::max_element(out.begin(), out.end()), 1.f);  // held to 0..1

    Standalone shifted("lfo", kSampleRate, lfoValues({{"phase", 90.f}}));
    out = modulatorOutput(shifted, kSampleRate);
    CHECK_NEAR(out[0], 0.25, 1e-4);
    CHECK_NEAR(out[36000 + 100], frac((36000.0 + 100.0) / kSampleRate + 0.25), 1e-4);
}

TEST_CASE("a synced LFO follows the song while it plays, and runs on at the tempo when stopped") {
    // 1/4 at 120 BPM: a cycle a beat (24000 samples).
    const ParamValues synced = lfoValues({{"sync", 1.f}, {"synced", 9.f}});
    Standalone playing("lfo", kSampleRate, synced);
    playing.context().playing = true;
    playing.context().tempo = 120.0;
    Samples out = modulatorOutput(playing, kSampleRate);
    for (const int64_t n : {0, 6000, 23999, 24000, 30000}) {
        INFO(std::to_string(n));
        CHECK_NEAR(out[static_cast<size_t>(n)], frac(static_cast<double>(n) / kSpb), 1e-4);
    }
    // 1 Bar: four beats.
    Standalone bar("lfo", kSampleRate, with(synced, {{"synced", 15.f}}));
    bar.context().playing = true;
    out = modulatorOutput(bar, kSampleRate);
    CHECK_NEAR(out[48000 - 1], 0.5, 1e-4);

    Standalone stopped("lfo", kSampleRate, synced);
    stopped.context().playing = false;
    out = modulatorOutput(stopped, kSampleRate);
    CHECK_NEAR(out[12000], 0.5, 1e-4);
}

TEST_CASE("Retrigger starts the LFO's cycle at each note") {
    Standalone lfo("lfo", kSampleRate, lfoValues({{"retrigger", 1.f}}));
    Notes notes{{{10000, 100}, {30000, 1}}};
    const Samples out = modulatorOutput(lfo, kSampleRate, notes.feeding(lfo));
    CHECK_NEAR(out[9999], 9999.0 / kSampleRate, 1e-4);
    CHECK_NEAR(out[10000], 0.0, 1e-6);
    CHECK_NEAR(out[20000], 10000.0 / kSampleRate, 1e-4);
    CHECK_NEAR(out[30000], 0.0, 1e-6);
    CHECK_NEAR(out[30480], 480.0 / kSampleRate, 1e-4);
    // Without it, notes change nothing.
    Standalone free("lfo", kSampleRate, lfoValues());
    Notes again{{{10000, 100}}};
    const Samples on = modulatorOutput(free, kSampleRate, again.feeding(free));
    CHECK_NEAR(on[10000], 10000.0 / kSampleRate, 1e-4);
    // Its "key" display marks where a note came.
    const std::vector<float> keys = lfo.display("key");
    CHECK_EQ(std::count(keys.begin(), keys.end(), 1.f), 2);
}

TEST_CASE("smoothing eases the LFO's steps") {
    const std::vector<Point> square{{0.f, 1.f, 0.f}, {0.5f, 1.f, 0.f}, {0.5f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
    Standalone hard("lfo", kSampleRate, lfoValues() + curveValues(square));
    const Samples steps = modulatorOutput(hard, kSampleRate);
    CHECK_EQ(steps[23999], 1.f);
    CHECK_EQ(steps[24001], 0.f);
    Standalone soft("lfo", kSampleRate, lfoValues({{"smooth", 10.f}}) + curveValues(square));
    const Samples eased = modulatorOutput(soft, kSampleRate);
    CHECK(eased[24000 + 48] > 0.5);  // 1 ms on: still most of the way up
    CHECK(eased[24000 + 48 * 50] < 0.01);  // 50 ms on: down
}

TEST_CASE("the Envelope plays its curve from each note, then rests") {
    Standalone envelope("envelope", kSampleRate, envelopeValues());
    Notes notes{{{10000, 100}, {30000, 100}, {31200, 100}}};
    const Samples out = modulatorOutput(envelope, kSampleRate, notes.feeding(envelope));
    CHECK_EQ(out[9999], 0.f);  // at rest: its last point's value
    CHECK_NEAR(out[10000], 1.0, 1e-6);
    CHECK_NEAR(out[10000 + 2400], 0.5, 1e-4);  // 100 ms long: halfway at 50
    CHECK_EQ(out[10000 + 4801], 0.f);
    CHECK_NEAR(out[31200 - 1], 1.0 - 1199.0 / 4800.0, 1e-4);
    CHECK_NEAR(out[31200], 1.0, 1e-6);  // a note while it plays starts it again
    // The phase display: -1 at rest, from 0 at a note.
    const std::vector<float> phase = envelope.display("phase");
    CHECK_EQ(phase[0], -1.f);
    CHECK(std::any_of(phase.begin(), phase.end(), [](float p) { return p >= 0.f && p < 0.02f; }));
}

TEST_CASE("the Envelope's length, synced, depth and velocity") {
    Standalone synced("envelope", kSampleRate, envelopeValues({{"sync", 1.f}, {"synced", 9.f}}));  // a beat
    Notes notes{{{0, 127}}};
    Samples out = modulatorOutput(synced, kSampleRate, notes.feeding(synced));
    CHECK_NEAR(out[12000], 0.5, 1e-4);

    Standalone deep("envelope", kSampleRate, envelopeValues({{"depth", 40.f}}));
    Notes once{{{0, 127}}};
    out = modulatorOutput(deep, kSampleRate, once.feeding(deep));
    CHECK_NEAR(out[0], 0.4, 1e-6);

    Standalone byVelocity("envelope", kSampleRate, envelopeValues({{"velocity", 100.f}}));
    Notes soft{{{0, 64}, {20000, 127}}};
    out = modulatorOutput(byVelocity, kSampleRate, soft.feeding(byVelocity));
    CHECK_NEAR(out[0], 64.0 / 127.0, 1e-5);
    CHECK_NEAR(out[20000], 1.0, 1e-6);
    Standalone halfVelocity("envelope", kSampleRate, envelopeValues({{"velocity", 50.f}}));
    Notes quiet{{{0, 1}}};
    out = modulatorOutput(halfVelocity, kSampleRate, quiet.feeding(halfVelocity));
    CHECK_NEAR(out[0], 1.0 - 0.5 * (1.0 - 1.0 / 127.0), 1e-5);
}

TEST_CASE("the Envelope triggered by its sidechain, once one is chosen, and by notes") {
    // Bursts in the key at 5000 and 20000 (and one 10 ms after the second: too soon).
    Samples key(kSampleRate, 0.f);
    for (const int64_t at : {5000, 20000, 20480}) {
        for (int64_t n = 0; n < 200; ++n) key[static_cast<size_t>(at + n)] = 0.5f;
    }
    const auto feeding = [&key](Standalone& device, bool connected) {
        return [&key, &device, connected](int64_t start, int) {
            device.processor().setSidechainConnected(connected);
            device.processor().setSidechain(key.data() + start, key.data() + start);
        };
    };
    Standalone keyed("envelope", kSampleRate, envelopeValues());
    Samples out = modulatorOutput(keyed, kSampleRate, feeding(keyed, true));
    CHECK_EQ(out[4999], 0.f);
    CHECK_NEAR(out[5000], 1.0, 1e-6);
    CHECK_NEAR(out[20000], 1.0, 1e-6);
    CHECK_NEAR(out[20480], 1.0 - 480.0 / 4800.0, 1e-4);  // (not again)
    // Notes trigger it too, with a sidechain or without.
    Standalone both("envelope", kSampleRate, envelopeValues());
    Notes notes{{{12000, 127}}};
    const auto notesAndKey = [&](int64_t start, int frames) {
        notes.feeding(both)(start, frames);
        feeding(both, true)(start, frames);
    };
    out = modulatorOutput(both, kSampleRate, notesAndKey);
    CHECK_NEAR(out[5000], 1.0, 1e-6);
    CHECK_NEAR(out[12000], 1.0, 1e-6);
    // Above the key's level, and without a sidechain chosen: no hits from it.
    Standalone high("envelope", kSampleRate, envelopeValues({{"threshold", -3.f}}));
    out = modulatorOutput(high, kSampleRate, feeding(high, true));
    CHECK_EQ(*std::max_element(out.begin(), out.end()), 0.f);
    Standalone unconnected("envelope", kSampleRate, envelopeValues());
    out = modulatorOutput(unconnected, kSampleRate, feeding(unconnected, false));
    CHECK_EQ(*std::max_element(out.begin(), out.end()), 0.f);
}

TEST_CASE("a modulator's output is the same however the blocks fall") {
    Standalone a("lfo", kSampleRate, lfoValues({{"rate", 3.3f}, {"smooth", 7.f}}) + curveValues(sub::modulator::sinePoints()));
    Standalone b("lfo", kSampleRate, lfoValues({{"rate", 3.3f}, {"smooth", 7.f}}) + curveValues(sub::modulator::sinePoints()));
    const Samples one = modulatorOutput(a, kSampleRate / 2, {}, 1024);
    const Samples other = modulatorOutput(b, kSampleRate / 2, {}, 37);
    CHECK(allclose(one, other, 0.0, 1e-6));
}

// --- Modulation in the engine ---------------------------------------------------------------

TEST_CASE("a modulator moves a parameter of a device on its track, every 64 samples") {
    ModulationEngine e;
    auto& engine = e.engine;
    const uint32_t lfo = modulatorOn(engine, e.chain(), "lfo", lfoValues());
    const uint32_t target = ducker(engine, e.chain());
    engine.setTrackModulation(e.track, {{lfo, target, "depth", 0.f, 1.f, false}});
    const std::vector<double> depths = depthsOf(engine.renderOffline(0.0, kSampleRate / 2));
    for (const int64_t n : {0, 63, 64, 1000, 5000, 12345, 23999}) {
        INFO(std::to_string(n));
        CHECK_NEAR(depths[static_cast<size_t>(n)], steppedRamp(n), 1e-5);
    }
    // The LFO itself leaves the audio alone: alone, the track plays as it came.
    engine.setTrackModulation(e.track, {});
    engine.removeProcessor(target);
    CHECK(allclose(channel(engine.renderOffline(0.0, kSampleRate / 4), 0), kLevel));
}

TEST_CASE("modulation over a range, and upside down") {
    ModulationEngine e;
    auto& engine = e.engine;
    const uint32_t lfo = modulatorOn(engine, e.chain(), "lfo", lfoValues());
    const uint32_t target = ducker(engine, e.chain());
    engine.setTrackModulation(e.track, {{lfo, target, "depth", 0.2f, 0.6f, true}});
    const std::vector<double> depths = depthsOf(engine.renderOffline(0.0, kSampleRate / 2));
    for (const int64_t n : {0, 6000, 20000}) {
        INFO(std::to_string(n));
        CHECK_NEAR(depths[static_cast<size_t>(n)], 0.2 + (1.0 - steppedRamp(n)) * 0.4, 1e-5);
    }
    // High under low turns it the other way too.
    engine.setTrackModulation(e.track, {{lfo, target, "depth", 0.9f, 0.1f, false}});
    const std::vector<double> back = depthsOf(engine.renderOffline(0.0, kSampleRate / 2));
    CHECK_NEAR(back[12000], 0.9 - steppedRamp(12000) * 0.8, 1e-5);
}

TEST_CASE("a modulator after its target in the chain moves it all the same") {
    ModulationEngine e;
    auto& engine = e.engine;
    const uint32_t target = ducker(engine, e.chain());
    const uint32_t lfo = modulatorOn(engine, e.chain(), "lfo", lfoValues());
    engine.setTrackModulation(e.track, {{lfo, target, "depth", 0.f, 1.f, false}});
    const std::vector<double> depths = depthsOf(engine.renderOffline(0.0, kSampleRate / 4));
    CHECK_NEAR(depths[3000], steppedRamp(3000), 1e-5);
}

TEST_CASE("a synced modulator follows the song's position") {
    ModulationEngine e;
    auto& engine = e.engine;
    const uint32_t lfo = modulatorOn(engine, e.chain(), "lfo", lfoValues({{"sync", 1.f}, {"synced", 15.f}}));  // a bar
    const uint32_t target = ducker(engine, e.chain());
    engine.setTrackModulation(e.track, {{lfo, target, "depth", 0.f, 1.f, false}});
    const std::vector<double> depths = depthsOf(engine.renderOffline(1.0, kSampleRate / 4));  // from beat 1
    CHECK_NEAR(depths[0], 0.25, 1e-5);
    CHECK_NEAR(depths[6000], 0.25 + (6000 / kStep * kStep) / (4 * kSpb), 1e-5);
}

TEST_CASE("modulation wins over automation, which plays again once it stops") {
    ModulationEngine e;
    auto& engine = e.engine;
    const uint32_t lfo = modulatorOn(engine, e.chain(), "lfo", lfoValues());
    const uint32_t target = ducker(engine, e.chain());
    engine.setTrackAutomation(e.track, {{target, "depth", {{0.0, 0.25f, 0.f}}}});
    engine.setTrackModulation(e.track, {{lfo, target, "depth", 0.f, 1.f, false}});
    CHECK_NEAR(depthsOf(engine.renderOffline(0.0, kSampleRate / 4))[9000], steppedRamp(9000), 1e-5);
    engine.setTrackModulation(e.track, {});
    CHECK_NEAR(depthsOf(engine.renderOffline(0.0, kSampleRate / 4))[9000], 0.25, 1e-5);
}

TEST_CASE("modulators reach into racks and out of them") {
    ModulationEngine e;
    auto& engine = e.engine;
    // An LFO in a rack's chain moves a device after the rack.
    const uint32_t rack = engine.addRack(e.chain(), -1);
    const uint32_t inner = engine.addRackChain(rack, -1);
    const uint32_t lfo = modulatorOn(engine, inner, "lfo", lfoValues());
    const uint32_t outside = ducker(engine, e.chain());
    engine.setTrackModulation(e.track, {{lfo, outside, "depth", 0.f, 1.f, false}});
    CHECK_NEAR(depthsOf(engine.renderOffline(0.0, kSampleRate / 4))[7000], steppedRamp(7000), 1e-5);
    // An LFO on the track moves a device in another rack's chain (the rack passes the LFO's chain's
    // input on untouched, so the track's level stays as the inner ducker makes it).
    engine.removeProcessor(outside);
    const uint32_t rack2 = engine.addRack(e.chain(), -1);
    const uint32_t chain2 = engine.addRackChain(rack2, -1);
    const uint32_t inside = ducker(engine, chain2);
    const uint32_t lfo2 = modulatorOn(engine, e.chain(), "lfo", lfoValues({{"rate", 2.f}}), 0);
    engine.setTrackModulation(e.track, {{lfo2, inside, "depth", 0.f, 1.f, false}});
    CHECK_NEAR(depthsOf(engine.renderOffline(0.0, kSampleRate / 4))[7000], steppedRamp(7000, kSampleRate / 2.0), 1e-5);
}

TEST_CASE("modulating a modulator") {
    ModulationEngine e;
    auto& engine = e.engine;
    // LFO 2 (after LFO 1, so a chunk late) holds LFO 1's depth at 50 %; LFO 1 moves the ducker.
    const uint32_t lfo1 = modulatorOn(engine, e.chain(), "lfo", lfoValues({{"rate", 10.f}}));
    const uint32_t lfo2 = modulatorOn(engine, e.chain(), "lfo", lfoValues({{"depth", 0.f}}));
    const uint32_t target = ducker(engine, e.chain());
    engine.setTrackModulation(e.track, {{lfo1, target, "depth", 0.f, 1.f, false},
                                        {lfo2, lfo1, "depth", 0.f, 1.f, false}});
    const std::vector<double> depths = depthsOf(engine.renderOffline(0.0, kSampleRate / 4));
    for (const int64_t n : {5000, 7000, 9100}) {
        INFO(std::to_string(n));
        CHECK_NEAR(depths[static_cast<size_t>(n)], 0.5 + (steppedRamp(n, kSampleRate / 10.0) - 0.5) * 0.5, 1e-5);
    }
}

TEST_CASE("a modulator switched off holds its value; one elsewhere or not a modulator moves nothing") {
    ModulationEngine e;
    auto& engine = e.engine;
    const uint32_t lfo = modulatorOn(engine, e.chain(), "lfo", lfoValues());
    const uint32_t target = ducker(engine, e.chain());
    engine.setTrackModulation(e.track, {{lfo, target, "depth", 0.f, 1.f, false}});
    engine.renderOffline(0.0, 10000);
    engine.setProcessorEnabled(lfo, false);
    const std::vector<double> held = depthsOf(engine.renderOffline(0.0, 5000));
    CHECK_NEAR(held[0], held[4999], 1e-7);

    // On another track, or a device that isn't a modulator: nothing moves (its own value plays).
    const uint32_t other = clipTrack(engine, dcWav());
    const uint32_t far = modulatorOn(engine, engine.trackChain(other), "lfo", lfoValues());
    const uint32_t utility = engine.addBuiltinProcessor(e.chain(), "utility", -1);
    setParam(engine, target, "depth", 50.f);
    engine.setTrackGain(other, 0.f);
    engine.setTrackModulation(e.track, {{far, target, "depth", 0.f, 1.f, false},
                                        {utility, target, "depth", 0.f, 1.f, false},
                                        {lfo, lfo, "depth", 0.f, 1.f, false}});
    CHECK_NEAR(depthsOf(engine.renderOffline(0.0, 5000))[4000], 0.5, 1e-6);
}

TEST_CASE("an Envelope triggered by its sidechain moves a device") {
    ModulationEngine e;
    auto& engine = e.engine;
    // The key: a click at 12000.
    const uint32_t key = clipTrack(engine, clickWav(0.8, kSampleRate, 12000));
    engine.setTrackGain(key, 0.f);
    const uint32_t envelope = modulatorOn(engine, e.chain(), "envelope", envelopeValues());
    engine.setProcessorSidechain(envelope, key, sub::SidechainTap::PreFader);
    const uint32_t target = ducker(engine, e.chain());
    engine.setTrackModulation(e.track, {{envelope, target, "depth", 0.f, 1.f, false}});
    const std::vector<double> depths = depthsOf(engine.renderOffline(0.0, kSampleRate / 2));
    CHECK_NEAR(depths[11000], 0.0, 1e-6);
    // The hit lands at 12000; the depth follows from the next 64-sample step it reaches.
    const int64_t step = (12000 / kStep + 1) * kStep;
    CHECK_NEAR(depths[static_cast<size_t>(step)], 1.0 - static_cast<double>(step - 12000) / 4800.0, 1e-4);
    CHECK_NEAR(depths[12000 + 5000], 0.0, 1e-6);
}
