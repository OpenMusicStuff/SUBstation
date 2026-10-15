// Constraining delay compensation (Engine::setConstrainLatency()): while a
// track is monitored, the devices slower than the threshold on its way out (its
// own, its groups', the master's) are bypassed, latency and all, and come back
// once nothing is monitored; offline renders hear them all. Live, a track
// playing its MIDI input then isn't delay-compensated (with the fake ASIO driver:
// skipped without the ASIO SDK).

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/MidiInputTest.h"
#include "harness/Signal.h"

using namespace subtest;

TEST_CASE("constrained, the slow devices a monitored track is heard through are bypassed") {
    requireTestPlugins();
    sub::Engine engine;
    engine.setClipFadeMs(0);
    const uint32_t bus = engine.addTrack();
    const uint32_t played = engine.addTrack();
    engine.setTrackOutput(played, bus);
    const uint32_t other = clipTrack(engine, clickWav(0.25), 1.0);
    const uint32_t own = latentEffect(engine, played, 200);
    const uint32_t quick = latentEffect(engine, played, 20);  // under the threshold
    const uint32_t onBus = latentEffect(engine, bus, 300);
    const uint32_t elsewhere = latentEffect(engine, other, 400);  // not on the way out
    const uint32_t onMaster = latentEffect(engine, sub::Engine::kMaster, 100);
    engine.setProcessorParam(onMaster, FX_GAIN, 1.f);  // twice as loud: shows when it plays

    const auto bypassed = [&] {
        std::vector<bool> out;
        for (const uint32_t device : {own, quick, onBus, elsewhere, onMaster})
            out.push_back(engine.processorInfo(device).constrained);
        return out;
    };
    const std::vector<bool> none(5, false), slowOnTheWay{true, false, true, false, true};

    engine.setConstrainLatency(true, 1.0);  // 48 samples
    CHECK(engine.constrainLatency());
    CHECK_EQ(engine.constrainThresholdMs(), 1.0);
    CHECK(bypassed() == none);  // nothing is monitored
    CHECK_EQ(engine.outputLatency(), 220 + 300 + 100);

    engine.setTrackArmed(played, true);  // Auto: monitored
    CHECK(bypassed() == slowOnTheWay);
    CHECK_EQ(engine.trackLatency(played), 20);
    CHECK_EQ(engine.trackLatency(bus), 0);
    CHECK_EQ(engine.outputLatency(), 400);  // the other track's, now the latest
    // Still switched on: offline renders hear them, and they are bypassed again after.
    const Samples out = engine.renderOffline(0.0, 2 * kBeat);
    CHECK(above(channel(out, 0), 1e-6) == std::vector<int64_t>{kBeat});
    CHECK_NEAR(at(out, kBeat, 0), 0.5, 1e-4);
    CHECK(bypassed() == slowOnTheWay);
    CHECK_EQ(engine.outputLatency(), 400);

    engine.setConstrainLatency(true, 5.0);  // 240 samples: only the bus's and the other track's are slower
    CHECK(bypassed() == (std::vector<bool>{false, false, true, false, false}));
    engine.setConstrainLatency(true, 1.0);
    engine.setTrackArmed(played, false);
    CHECK(bypassed() == none);
    engine.setTrackMonitor(played, sub::MonitorMode::In);
    CHECK(bypassed() == slowOnTheWay);
    engine.setTrackFrozen(played, true);  // a frozen track plays no input
    CHECK(bypassed() == none);
    engine.setTrackFrozen(played, false);
    CHECK(bypassed() == slowOnTheWay);
    engine.setConstrainLatency(false);
    CHECK(bypassed() == none);
    CHECK_EQ(engine.outputLatency(), 220 + 300 + 100);
    CHECK_THROWS_AS(engine.setConstrainLatency(true, -1.0), std::invalid_argument);
}

TEST_CASE("constrained, live MIDI is heard without delay compensation") {
    RecordingTest t;
    auto& driver = t.driver;
    auto& engine = t.engine;
    const uint32_t track = keys(engine, driver);  // armed, stopped: it hears its MIDI input
    latentEffect(engine, engine.addTrack(), 100);  // another track: the master lines the played one up with it
    const uint32_t onMaster = latentEffect(engine, sub::Engine::kMaster, 60);
    driver.process(2);
    const auto note = [&] {
        send(engine, {kNoteOn, 60, 127}, 10);
        send(engine, {kNoteOff, 60, 0}, 20);
        return heard(driver, 2);
    };
    CHECK_ALLCLOSE(note(), held({{170, 180, 127}}, 2 * kBuffer), 1e-7, 1e-6);
    const sub::LiveLatency compensated = engine.liveLatency(track);
    CHECK(!compensated.live);
    CHECK_EQ(compensated.devices, 60);       // the master's
    CHECK_EQ(compensated.compensation, 100);  // lined up with the other track
    engine.setConstrainLatency(true, 1.0);
    driver.process(2);
    CHECK(engine.processorInfo(onMaster).constrained);
    const sub::LiveLatency live = engine.liveLatency(track);
    CHECK(live.live);
    CHECK_EQ(live.midiDelay, kBuffer);
    CHECK_EQ(live.devices, 0);
    CHECK_EQ(live.compensation, 0);
    CHECK_ALLCLOSE(note(), held({{10, 20, 127}}, 2 * kBuffer), 1e-7, 1e-6);
    engine.play();  // playing along with the song: as soon too
    driver.process(2);
    CHECK_ALLCLOSE(note(), held({{10, 20, 127}}, 2 * kBuffer), 1e-7, 1e-6);
    engine.stop();
    driver.process(2);
    engine.setConstrainLatency(false);
    driver.process(2);
    CHECK_ALLCLOSE(note(), held({{170, 180, 127}}, 2 * kBuffer), 1e-7, 1e-6);
}

TEST_CASE("MIDI input played at once starts the next block") {
    RecordingTest t;
    auto& driver = t.driver;
    auto& engine = t.engine;
    keys(engine, driver);
    engine.setMidiImmediate(true);
    CHECK(engine.midiImmediate());
    CHECK_EQ(engine.audioClock().midiDelay, 0);
    const auto sendAt = [&](const std::vector<uint8_t>& message, int64_t offset) {
        const sub::AudioClockStatus clock = engine.audioClock();
        REQUIRE(clock.running);
        engine.sendMidiInput("Keys", message, clock.hostTimeNs + std::llround(static_cast<double>(offset) * 1e9 / kRate));
    };
    sendAt({kNoteOn, 60, 127}, 100);  // (it came in during the last buffer: it plays as the next begins)
    CHECK_ALLCLOSE(heard(driver), held({{0, kBuffer, 127}}), 1e-7, 1e-6);
    sendAt({kNoteOff, 60, 0}, 30);
    CHECK(allEqual(heard(driver), 0.0));
    engine.setMidiImmediate(false);
    CHECK_EQ(engine.audioClock().midiDelay, kBuffer);
}
