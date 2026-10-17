// PipeWire (Linux), against a daemon of the test's own (harness/PipeWireDaemon.h):
// its devices listed, the default followed, what the device runs at, playing
// and its inputs (the duplex test device loops what is played back to its
// inputs), output channels, and the device or the daemon going away. Skipped
// where the engine was built without PipeWire, or the daemon isn't installed.

#if SUBSTATION_HAS_PIPEWIRE

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/PipeWireDaemon.h"
#include "harness/Test.h"

using namespace subtest;

namespace {

void requireRunning(const PipeWireDaemon& daemon) {
    if (!daemon.running()) SKIP(daemon.whyNot());
}

// The daemon, then an engine (closed before the daemon ends).
struct PipeWireTest {
    PipeWireDaemon daemon;
    sub::Engine engine;
    PipeWireTest() { requireRunning(daemon); }
};

bool waitUntil(const std::function<bool()>& predicate, double timeout = 5.0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

sub::DeviceConfig pipeWire(const std::string& name = {}, uint32_t sampleRate = 0, uint32_t bufferFrames = 0,
                           std::vector<int> inputs = {}, std::vector<int> outputs = {}) {
    sub::DeviceConfig config;
    config.driver = "PipeWire";
    config.name = name;
    config.sampleRate = sampleRate;
    config.bufferFrames = bufferFrames;
    config.inputChannels = std::move(inputs);
    config.outputChannels = std::move(outputs);
    return config;
}

// The next device event, polling as the application does (the engine closes a stopped device in idle()).
std::string nextEvent(sub::Engine& engine, double timeout = 5.0) {
    std::string event;
    waitUntil(
        [&] {
            engine.idle();
            event = engine.takeDeviceEvent();
            return !event.empty();
        },
        timeout);
    return event;
}

}  // namespace

TEST_CASE("pipewire: the default driver, before System") {
    CHECK(sub::Engine::driverTypes() == (std::vector<std::string>{"PipeWire", "System"}));
    CHECK_EQ(std::string(sub::kDefaultDriver), std::string("PipeWire"));
}

TEST_CASE("pipewire: devices listed, the default marked") {
    PipeWireTest t;
    std::vector<std::pair<std::string, bool>> devices;
    for (const auto& d : t.engine.devices("PipeWire")) devices.emplace_back(d.name, d.isDefault);
    CHECK(devices == (std::vector<std::pair<std::string, bool>>{{"Test Duplex", true}, {"Test Sink", false}}));
}

TEST_CASE("pipewire: status and capabilities") {
    PipeWireTest t;
    t.engine.openDevice(pipeWire("Test Sink", 0, 128));
    const sub::DeviceStatus status = t.engine.deviceStatus();
    CHECK(status.open);
    CHECK_EQ(status.backend, std::string("PipeWire"));
    CHECK_EQ(status.name, std::string("Test Sink"));
    CHECK_EQ(status.sampleRate, 48000u);
    CHECK_EQ(status.bufferFrames, 128u);  // the quantum the graph runs at: the latency asked for
    CHECK_EQ(t.engine.sampleRate(), 48000.0);
    CHECK_APPROX(status.latencyMs, 128 / 48.0);
    CHECK(status.inputChannels.empty());
    CHECK(status.outputChannels == (std::vector<int>{0, 1}));
    const sub::DeviceCaps caps = t.engine.deviceCapabilities();
    CHECK(caps.outputNames == (std::vector<std::string>{"Front Left", "Front Right", "Front Center", "LFE"}));
    CHECK(caps.inputNames.empty());  // a sink, of no card, and no default source
    CHECK(caps.sampleRates == (std::vector<uint32_t>{44100, 48000}));
    CHECK(caps.bufferSizes == (std::vector<uint32_t>{32, 64, 128, 256, 512, 1024, 2048}));
    CHECK_EQ(caps.preferredBufferFrames, 256u);
    CHECK(!caps.hasControlPanel);
}

TEST_CASE("pipewire: the default device, its own buffer size") {
    PipeWireTest t;
    t.engine.openDevice(pipeWire());
    const sub::DeviceStatus status = t.engine.deviceStatus();
    CHECK_EQ(status.name, std::string("Test Duplex"));
    CHECK_EQ(status.bufferFrames, 256u);  // clock.quantum
    const sub::DeviceCaps caps = t.engine.deviceCapabilities();
    CHECK(caps.inputNames == (std::vector<std::string>{"Front Left", "Front Right"}));  // a duplex device's own
}

TEST_CASE("pipewire: the rate asked for") {
    PipeWireTest t;
    t.engine.openDevice(pipeWire("Test Duplex", 44100, 256));
    CHECK_EQ(t.engine.deviceStatus().sampleRate, 44100u);  // the graph changed rate: it may
    CHECK_EQ(t.engine.sampleRate(), 44100.0);
}

TEST_CASE("pipewire: plays, and its inputs come back") {
    PipeWireTest t;
    auto& engine = t.engine;
    const uint32_t track = engine.addTrack();
    const std::string ramp = rampWav();
    engine.loadSource(ramp);
    engine.setTrackClips(track, {clip(ramp, 0.0, 1.0)});
    engine.openDevice(pipeWire("Test Duplex", 48000, 256, {0, 1}));
    CHECK(engine.deviceStatus().inputChannels == (std::vector<int>{0, 1}));
    engine.takeInputMeters();
    engine.play();
    // The transport follows the graph's clock, and what was played is captured.
    CHECK(waitUntil([&] { return engine.positionBeats() > 0.5; }));
    float peak = 0.f;
    CHECK(waitUntil([&] {
        for (float level : engine.takeInputMeters()) peak = std::max(peak, level);
        return peak > 0.01f;
    }));
    engine.stop();
}

TEST_CASE("pipewire: dropouts counted") {
    PipeWireTest t;
    t.engine.openDevice(pipeWire("Test Duplex", 48000, 256));
    // Cycles as they should be: about 19 in 100 ms, a clock read wrong counting a dropout at each. (Not
    // none: on a busy machine a callback is now and then late, a dropout all right.)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const uint64_t before = t.engine.deviceStatus().dropouts;
    CHECK(before < 8);
    // A freeze makes the device's clock skip only now and then (when it stops,
    // or when it starts again, by a cycle or all it missed): again until it has.
    for (int i = 0; i < 20 && t.engine.deviceStatus().dropouts <= before; ++i) {
        t.daemon.freeze(std::chrono::milliseconds(200));
        waitUntil([&] { return t.engine.deviceStatus().dropouts > before; }, 0.2);
    }
    const uint64_t frozen = t.engine.deviceStatus().dropouts;
    CHECK(frozen > before);
    t.engine.openDevice(pipeWire("Test Duplex", 48000, 256));
    CHECK(t.engine.deviceStatus().dropouts < frozen);  // counted again for each device opened (from 0)
}

TEST_CASE("pipewire: output channels") {
    PipeWireTest t;
    t.engine.openDevice(pipeWire("Test Sink", 0, 0, {}, {2, 3}));
    CHECK(t.engine.deviceStatus().outputChannels == (std::vector<int>{2, 3}));
    CHECK_THROWS_AS(t.engine.openDevice(pipeWire("Test Sink", 0, 0, {}, {4})), std::runtime_error);  // it has four
    CHECK_THROWS_AS(t.engine.openDevice(pipeWire("Test Sink", 0, 0, {0})), std::runtime_error);      // and no inputs
    CHECK_THROWS_AS(t.engine.openDevice(pipeWire("No Such Device")), std::runtime_error);
    CHECK(!t.engine.deviceStatus().open);
}

TEST_CASE("pipewire: follows the default output") {
    PipeWireTest t;
    t.engine.openDevice(pipeWire());
    REQUIRE(PipeWireDaemon::setDefaultSink("test-sink"));
    CHECK_EQ(nextEvent(t.engine), std::string("reset"));
    t.engine.reopenDevice();
    CHECK_EQ(t.engine.deviceStatus().name, std::string("Test Sink"));
    // A device opened by name stays.
    t.engine.openDevice(pipeWire("Test Sink"));
    REQUIRE(PipeWireDaemon::setDefaultSink("test-duplex"));
    CHECK_EQ(nextEvent(t.engine, 0.5), std::string());
}

TEST_CASE("pipewire: the device going away") {
    PipeWireTest t;
    t.engine.openDevice(pipeWire("Test Sink"));
    REQUIRE(PipeWireDaemon::destroyNode("test-sink"));
    CHECK_EQ(nextEvent(t.engine), std::string("stopped"));
    CHECK(!t.engine.deviceStatus().open);
    std::vector<std::string> names;
    for (const auto& d : t.engine.devices("PipeWire")) names.push_back(d.name);
    CHECK(names == std::vector<std::string>{"Test Duplex"});
}

TEST_CASE("pipewire: the daemon going away, and back") {
    auto daemon = std::make_unique<PipeWireDaemon>();
    requireRunning(*daemon);
    sub::Engine engine;
    engine.openDevice(pipeWire());
    daemon->stop();
    CHECK_EQ(nextEvent(engine), std::string("stopped"));
    CHECK(!engine.deviceStatus().open);
    CHECK(engine.devices("PipeWire").empty());
    CHECK_THROWS_AS(engine.openDevice(pipeWire()), std::runtime_error);
    daemon = std::make_unique<PipeWireDaemon>();  // connects again
    requireRunning(*daemon);
    engine.openDevice(pipeWire());
    CHECK_EQ(engine.deviceStatus().name, std::string("Test Duplex"));
    engine.closeDevice();
}

TEST_CASE("pipewire: a plug-in holding up the audio thread: what it uses is neither freed nor shared") {
    PipeWireTest t;
    auto& engine = t.engine;
    // SUB Test Effect on the master: while `hold` exists its process() doesn't return.
    const std::filesystem::path hold = tempDir() / "hold-audio-thread";
    std::filesystem::remove(hold);
    setenv("SUB_TEST_EFFECT_HOLD_FILE", hold.string().c_str(), 1);
    addTestPlugin(engine, engine.trackChain(sub::Engine::kMaster), "SUB Test Effect");
    unsetenv("SUB_TEST_EFFECT_HOLD_FILE");
    const uint32_t track = engine.addTrack();
    engine.setTrackInput(track, {0});
    engine.openDevice(pipeWire("Test Duplex", 48000, 256, {0, 1}));
    engine.startRecording({{track, (tempDir() / "held.wav").string()}});
    REQUIRE(waitUntil([&] { return engine.positionBeats() > 0.1; }));
    // Nothing held up: a change that waits for the audio thread goes through at once.
    const int threads = engine.audioThreads();
    auto started = std::chrono::steady_clock::now();
    engine.setAudioThreads(threads == 1 ? 2 : 1);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(sub::kCallbackWaitMs));

    std::ofstream(hold).put('x');
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // a callback is in its process() now
    const double held = engine.positionBeats();
    // An offline render would share its processors, a new scheduler would be swapped under it: refused.
    started = std::chrono::steady_clock::now();
    CHECK_THROWS_MATCHING(engine.renderOffline(0.0, 64), std::runtime_error, "audio thread is busy");
    CHECK(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(sub::kCallbackWaitMs));
    CHECK_THROWS_MATCHING(engine.setAudioThreads(threads), std::runtime_error, "audio thread is busy");
    // The recording stops all the same, with its take; its session is kept until that callback is done.
    CHECK_EQ(engine.stopRecording().size(), size_t{1});
    engine.idle();
    CHECK_EQ(engine.positionBeats(), held);

    std::filesystem::remove(hold);
    CHECK(waitUntil([&] { return engine.positionBeats() > held + 0.1; }));
    engine.idle();  // (frees the session)
    CHECK_NOTHROW(engine.renderOffline(0.0, 64));
    CHECK_NOTHROW(engine.setAudioThreads(threads));
}

#endif  // SUBSTATION_HAS_PIPEWIRE
