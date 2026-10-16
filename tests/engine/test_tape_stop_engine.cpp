// The built-in Tape Stop: passing the input as it is until stopped, the pitch falling to a halt over Stop Time,
// silence while stopped, spinning up again and catching up with the input, its curve, the mix and its display.

#include <cmath>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

using Points = std::vector<sub::AutomationPoint>;

// A track playing a 440 Hz sine (0.5) for four seconds, with a Tape Stop when `withDevice`.
struct Tape {
    sub::Engine engine;
    uint32_t track = 0, device = 0;

    explicit Tape(bool withDevice = true) {
        engine.setClipFadeMs(0);
        const std::string path = makeWav(stereo(sine(440.0, 4.0, 0.5)), 2);
        engine.loadSource(path);
        track = engine.addTrack();
        engine.setTrackClips(track, {clip(path, 0.0, 4.0, 0.0, 1.f)});
        if (withDevice) device = engine.addBuiltinProcessor(engine.trackChain(track), "tape_stop", -1);
    }
    void set(const std::string& id, float value) { setParam(engine, device, id, value); }
    // Stop pressed from beat `from` (0.5 s a beat) to beat `to`.
    void stopBetween(double from, double to) {
        engine.setTrackAutomation(
            track, {{device, "stop", Points{{0.0, 0.f, 0.f}, {from, 0.f, 0.f}, {from, 1.f, 0.f}, {to, 1.f, 0.f}, {to, 0.f, 0.f}}}});
    }
    Samples render(double seconds = 4.0) { return channel(engine.renderOffline(0.0, seconds * kSampleRate), 0); }
};

int64_t at(double seconds) { return static_cast<int64_t>(seconds * kSampleRate); }

}  // namespace

TEST_CASE("the tape stop is listed as an audio effect") {
    const sub::BuiltinInfo info = builtinInfo("tape_stop");
    CHECK_EQ(info.name, std::string("Tape Stop"));
    CHECK(info.category == sub::BuiltinCategory::AudioEffect);
    const std::vector<std::string> ids{"stop", "stop_time", "start_time", "curve", "tone", "mix"};
    CHECK(paramIds(info.params) == ids);
}

TEST_CASE("running, the tape stop passes the input as it is") {
    const Samples dry = Tape(false).render();
    const Samples out = Tape().render();
    REQUIRE(out.size() == dry.size());
    for (size_t i = 0; i < out.size(); ++i) REQUIRE(out[i] == dry[i]);
}

TEST_CASE("stopped, the pitch falls to a halt and stays silent") {
    Tape tape;
    tape.set("stop_time", 500.f);
    tape.set("tone", 0.f);
    tape.stopBetween(2.0, 7.0);  // from 1 s to 3.5 s
    const Samples out = tape.render();
    CHECK(allFinite(out));
    // Before: the input.
    CHECK_NEAR(dominantFreq(slice(out, at(0.5), at(0.5) + 4096)), 440.0, 15);
    // A quarter of the way (an even slope): three quarters of the speed, and the pitch.
    CHECK_NEAR(dominantFreq(slice(out, at(1.125) - 1024, at(1.125) + 1024)), 330.0, 33);
    // Half way: half.
    CHECK_NEAR(dominantFreq(slice(out, at(1.25) - 1024, at(1.25) + 1024)), 220.0, 25);
    // Stopped: silent.
    CHECK(maxAbs(slice(out, at(1.5), at(3.5))) < 1e-6);
}

TEST_CASE("let go, it spins up and catches up with the input") {
    Tape dryTape(false);
    const Samples dry = dryTape.render();
    Tape tape;
    tape.set("stop_time", 300.f);
    tape.set("start_time", 400.f);
    tape.stopBetween(2.0, 4.0);  // stopped from 1 s; let go at 2 s
    const Samples out = tape.render();
    // Spinning up: lower than the input, then up to it.
    CHECK(dominantFreq(slice(out, at(2.2) - 1024, at(2.2) + 1024)) < 330.0);
    CHECK(rms(slice(out, at(2.2), at(2.4))) > 0.1);
    // Up to speed by 2.4 s, caught up 20 ms later: the input as it is.
    for (int64_t i = at(2.5); i < at(4.0); ++i) REQUIRE(out[static_cast<size_t>(i)] == dry[static_cast<size_t>(i)]);
}

TEST_CASE("let go partway, it speeds up from where it got to") {
    Tape dryTape(false);
    const Samples dry = dryTape.render();
    Tape tape;
    tape.set("stop_time", 1000.f);
    tape.set("start_time", 100.f);
    tape.stopBetween(2.0, 2.6);  // pressed for 0.3 s of the 1 s stop
    const Samples out = tape.render();
    // Never silent on the way (no jump to the input, nor a fade out).
    CHECK(rms(slice(out, at(1.0), at(1.5))) > 0.25);
    for (int64_t i = at(1.6); i < at(4.0); ++i) REQUIRE(out[static_cast<size_t>(i)] == dry[static_cast<size_t>(i)]);
}

TEST_CASE("the curve holds the speed up or lets it fall at once") {
    // A quarter of the way into a 1 s stop: the speed is 1 - p^k above (k = 5.7 at 100%: 0.9996), (1 - p)^k
    // below (0.20).
    Tape high, low;
    for (Tape* tape : {&high, &low}) {
        tape->set("stop_time", 1000.f);
        tape->set("tone", 0.f);
        tape->stopBetween(2.0, 8.0);
    }
    high.set("curve", 100.f);
    low.set("curve", -100.f);
    const Samples held = high.render(), fallen = low.render();
    CHECK(dominantFreq(slice(held, at(1.25) - 1024, at(1.25) + 1024)) > 400.0);
    CHECK(dominantFreq(slice(fallen, at(1.25) - 1024, at(1.25) + 1024)) < 150.0);
}

TEST_CASE("dry, the tape stop passes the input") {
    const Samples dry = Tape(false).render();
    Tape tape;
    tape.set("mix", 0.f);
    tape.stopBetween(1.0, 7.0);
    const Samples out = tape.render();
    for (size_t i = 0; i < out.size(); ++i) REQUIRE(std::abs(out[i] - dry[i]) < 1e-6f);
}

TEST_CASE("the speed display falls to 0 and rises again") {
    Tape tape;
    tape.set("stop_time", 200.f);
    tape.set("start_time", 200.f);
    tape.stopBetween(2.0, 4.0);
    const auto displays = tape.engine.processorDisplays(tape.device);
    REQUIRE(displays.size() == 1);
    CHECK_EQ(displays[0].id, std::string("speed"));
    CHECK_EQ(displays[0].samplesPerValue, 256);
    tape.render(3.0);
    std::vector<float> values;
    tape.engine.readProcessorDisplay(tape.device, 0, 0, values);
    REQUIRE(values.size() > static_cast<size_t>(at(2.9) / 256));
    CHECK(values[static_cast<size_t>(at(0.5) / 256)] == 1.f);
    CHECK(values[static_cast<size_t>(at(1.5) / 256)] == 0.f);
    CHECK(values[static_cast<size_t>(at(2.5) / 256)] == 1.f);
}
