// Plug-ins in sandboxes (plugins/sandbox, docs/engine/sandboxes.md): processes
// of their own, of the plug-in host program built with the tests
// (substation-plugin-host), grouped as each hosting mode says. With the test
// plug-ins (tests/vst3_plugins), SUB Test Crasher among them: it crashes, hangs
// or loads slowly when asked to.
//
// A plug-in in a sandbox plays, and reports, as one in the engine's own process
// does; adding one never waits for it to load; a crash takes down its sandbox
// and the plug-ins in it, which pass their input on, and nothing else; a hung
// plug-in holds up the audio thread for one block; reloading brings one back as
// it was, where the hosting mode now says. Renders are offline unless said.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "plugins/sandbox/PluginSandboxes.h"
#include "plugins/sandbox/SandboxedProcessor.h"

using namespace subtest;

namespace {

using EventType = sub::ProcessorEvent::Type;
using sub::PluginHosting;
using sub::PluginStatus;

enum { GAIN, WAVE, TEMPO, PLAYING };  // SUB Test Synth's first parameters

std::string pluginHost() { return SUBSTATION_PLUGIN_HOST; }

// The test plug-ins' makers, as the scan finds them.
const std::string& vendorOf(const std::string& name) {
    static const std::map<std::string, std::string> vendors = [] {
        std::map<std::string, std::string> found;
        for (const sub::PluginDescription& d : sub::vst3::Vst3Format::instance().scanFile(testPluginsBundle()))
            found[d.name] = d.vendor;
        return found;
    }();
    return vendors.at(name);
}

// An engine whose plug-ins load in sandboxes, as `hosting` groups them (or in its own process).
struct SandboxEngine {
    sub::Engine engine;
    explicit SandboxEngine(PluginHosting hosting = PluginHosting::Together) {
        requireTestPlugins();
        engine.setClipFadeMs(0);
        engine.setPluginHostProgram(pluginHost());
        engine.setPluginHosting(hosting);
        engine.setSandboxRetireDelay(0);
    }
};

// One of the test plug-ins in a chain, as the application adds it (with its name and maker).
uint32_t addPlugin(sub::Engine& engine, uint32_t chain, const std::string& name) {
    return engine.addPluginProcessor(chain, "VST3", testPluginsBundle(), testPluginUids().at(name), -1,
                                     {name, vendorOf(name)});
}

// Runs the engine's main-thread work until `done`, at most `seconds`.
bool until(sub::Engine& engine, const std::function<bool()>& done, double seconds = 10.0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        engine.idle();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

bool hasEvent(const std::vector<sub::ProcessorEventRecord>& events, EventType type, uint32_t processor) {
    return std::any_of(events.begin(), events.end(),
                       [&](const auto& e) { return e.type == type && e.processorId == processor; });
}

// Sandboxes running that hold plug-ins: their labels and how many each holds.
std::multiset<std::pair<std::string, size_t>> running(sub::Engine& engine) {
    std::multiset<std::pair<std::string, size_t>> found;
    for (const auto& s : engine.pluginSandboxes()) {
        if (s.running && s.plugins > 0) found.insert({s.label, s.plugins});
    }
    return found;
}

void setEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

// The variable set while it lives (the sandboxes started meanwhile inherit it).
struct ScopedEnvironment {
    const char* name;
    ScopedEnvironment(const char* n, const char* value) : name(n) { setEnvironment(name, value); }
    ~ScopedEnvironment() { setEnvironment(name, nullptr); }
};

// What each engine renders of: a synth playing DC at gain 0.25 from beat 0, and a
// constant clip through the effect at gain 0.25 (half of unity), on two tracks.
Samples renderScene(sub::Engine& engine, bool sandboxed) {
    const uint32_t synthTrack = engine.addTrack();
    const uint32_t synth = addPlugin(engine, engine.trackChain(synthTrack), "SUB Test Synth");
    engine.setTrackNotes(synthTrack, {{0.0, 1.0, 60, 127}, {1.5, 0.5, 64, 64}});
    const uint32_t clip = clipTrack(engine, dcWav());
    const uint32_t effect = addPlugin(engine, engine.trackChain(clip), "SUB Test Effect");
    if (sandboxed) REQUIRE(engine.waitForPlugins(10000));
    engine.setProcessorParam(synth, WAVE, 0);
    engine.setProcessorParam(synth, GAIN, 0.25f);
    engine.setTrackAutomation(clip, {{effect, std::to_string(FX_GAIN), {{0.0, 0.25f, 0.f}, {2.0, 0.5f, 0.f}}}});
    return engine.renderOffline(0.0, 3 * kBeat);
}

}  // namespace

TEST_CASE("sandbox: a plug-in there plays as it does in the engine's own process") {
    SandboxEngine inProcess(PluginHosting::InProcess);
    SandboxEngine sandboxed(PluginHosting::Together);
    const Samples here = renderScene(inProcess.engine, false);
    const Samples there = renderScene(sandboxed.engine, true);
    REQUIRE(maxAbs(here) > 0.1);
    CHECK_ARRAY_EQUAL(there, here);  // sample for sample: notes, parameters, automation
    CHECK(running(sandboxed.engine) == (std::multiset<std::pair<std::string, size_t>>{{"All plug-ins", 2}}));
    CHECK(running(inProcess.engine).empty());
}

TEST_CASE("sandbox: parameters, their texts, transport and state come from there") {
    SandboxEngine e;
    auto& engine = e.engine;
    const uint32_t track = engine.addTrack();
    const uint32_t synth = addPlugin(engine, engine.trackChain(track), "SUB Test Synth");
    CHECK(engine.processorInfo(synth).status == PluginStatus::Loading);
    CHECK(engine.processorParams(synth).empty());  // (not known yet)
    REQUIRE(engine.waitForPlugins(10000));
    const auto events = engine.takeProcessorEvents();
    CHECK(hasEvent(events, EventType::Loaded, synth));
    const sub::ProcessorInfo info = engine.processorInfo(synth);
    CHECK(info.status == PluginStatus::Ready);
    CHECK_EQ(info.name, std::string("SUB Test Synth"));
    CHECK_EQ(info.sandbox, std::string("All plug-ins"));
    CHECK(info.sandboxPid > 0);
    CHECK(info.hasEditor);
    CHECK(info.acceptsMidi);

    SandboxEngine local(PluginHosting::InProcess);
    const uint32_t localSynth = addPlugin(local.engine, local.engine.trackChain(local.engine.addTrack()), "SUB Test Synth");
    CHECK(paramIds(engine.processorParams(synth)) == paramIds(local.engine.processorParams(localSynth)));

    // A value set shows at once; its text comes from the plug-in.
    engine.setProcessorParam(synth, GAIN, 0.25f);
    CHECK_APPROX(engine.processorParam(synth, GAIN), 0.25);
    CHECK_EQ(engine.processorParamText(synth, WAVE, 0.f), local.engine.processorParamText(localSynth, WAVE, 0.f));

    // What it reports of the transport it gets (output parameters) comes back.
    engine.setTempo(90.0);
    engine.renderOffline(2.0, 4096);
    CHECK(until(engine, [&] { return std::abs(engine.processorParam(synth, TEMPO) - 0.09f) < 1e-6f; }));
    CHECK_EQ(engine.processorParam(synth, PLAYING), 1.f);
    CHECK(engine.processorParamText(synth, TEMPO, engine.processorParam(synth, TEMPO)).rfind("90", 0) == 0);

    // Its state, and another plug-in's refused.
    const std::vector<uint8_t> state = engine.processorState(synth);
    REQUIRE(state.size() >= 4);
    CHECK(std::string(state.begin(), state.begin() + 4) == "VST3");
    engine.setProcessorParam(synth, GAIN, 0.75f);
    engine.setProcessorState(synth, state);
    CHECK(until(engine, [&] { return std::abs(engine.processorParam(synth, GAIN) - 0.25f) < 1e-6f; }));
    const uint32_t effect = addPlugin(engine, engine.trackChain(track), "SUB Test Effect");
    REQUIRE(engine.waitForPlugins(10000));
    CHECK_THROWS_MATCHING(engine.setProcessorState(effect, state), std::runtime_error, "not for SUB Test Effect");
}

TEST_CASE("sandbox: a state set as it loads is its state from its first block") {
    SandboxEngine e;
    auto& engine = e.engine;
    const uint32_t clip = clipTrack(engine, dcWav());
    const uint32_t first = addPlugin(engine, engine.trackChain(clip), "SUB Test Effect");
    REQUIRE(engine.waitForPlugins(10000));
    engine.setProcessorParam(first, FX_GAIN, 0.1f);
    const std::vector<uint8_t> state = engine.processorState(first);
    engine.removeProcessor(first);
    const uint32_t effect = addPlugin(engine, engine.trackChain(clip), "SUB Test Effect");
    engine.setProcessorState(effect, state);  // (as the application does once it has added one)
    CHECK(engine.processorInfo(effect).status == PluginStatus::Loading);
    REQUIRE(engine.waitForPlugins(10000));
    CHECK_APPROX(engine.processorParam(effect, FX_GAIN), 0.1);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.1, 1e-6, 0.0);  // gain 0.1: a fifth of unity
    // While loading, its state is the one it loads with.
    const uint32_t other = addPlugin(engine, engine.trackChain(clip), "SUB Test Effect");
    engine.setProcessorState(other, state);
    CHECK(engine.processorState(other).empty() || engine.processorState(other) == state);
}

TEST_CASE("sandbox: adding a plug-in doesn't wait for it to load") {
    const ScopedEnvironment slow("SUB_TEST_PLUGIN_SLOW_LOAD_MS", "700");
    SandboxEngine e;
    auto& engine = e.engine;
    const uint32_t clip = clipTrack(engine, dcWav());
    const auto started = std::chrono::steady_clock::now();
    std::vector<uint32_t> crashers;
    for (int i = 0; i < 3; ++i) crashers.push_back(addPlugin(engine, engine.trackChain(clip), "SUB Test Crasher"));
    const auto took = std::chrono::steady_clock::now() - started;
    CHECK(took < std::chrono::milliseconds(500));  // (they take 2.1 s to load, one after another)
    for (const uint32_t crasher : crashers) CHECK(engine.processorInfo(crasher).status == PluginStatus::Loading);
    CHECK_EQ(engine.pluginsLoading(), size_t{3});
    // Meanwhile the track plays, without them.
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.5, 1e-7, 0.0);
    // The last one first, if asked.
    engine.prioritizeProcessors({crashers[2]});
    CHECK(until(engine, [&] { return engine.processorInfo(crashers[2]).status == PluginStatus::Ready; }));
    CHECK(engine.processorInfo(crashers[1]).status == PluginStatus::Loading);
    REQUIRE(engine.waitForPlugins(10000));
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 4.0, 1e-6, 0.0);  // 0.5, doubled thrice
}

TEST_CASE("sandbox: one for every plug-in, maker, plug-in or instance, as the mode says") {
    struct Expected {
        PluginHosting hosting;
        std::multiset<std::pair<std::string, size_t>> sandboxes;
    };
    const std::vector<Expected> modes{
        {PluginHosting::Together, {{"All plug-ins", 4}}},
        {PluginHosting::ByManufacturer, {{"SUBstation", 3}, {"SUB Crash Works", 1}}},
        {PluginHosting::ByPlugin, {{"SUB Test Synth", 1}, {"SUB Test Effect", 2}, {"SUB Test Crasher", 1}}},
        {PluginHosting::Individually,
         {{"SUB Test Synth", 1}, {"SUB Test Effect", 1}, {"SUB Test Effect", 1}, {"SUB Test Crasher", 1}}},
        {PluginHosting::InProcess, {}},
    };
    for (const Expected& mode : modes) {
        INFO("mode " + std::to_string(static_cast<int>(mode.hosting)));
        SandboxEngine e(mode.hosting);
        auto& engine = e.engine;
        const uint32_t chain = engine.trackChain(engine.addTrack());
        std::vector<uint32_t> plugins;
        for (const char* name : {"SUB Test Synth", "SUB Test Effect", "SUB Test Effect", "SUB Test Crasher"}) {
            plugins.push_back(addPlugin(engine, chain, name));
        }
        REQUIRE(engine.waitForPlugins(10000));
        CHECK(running(engine) == mode.sandboxes);
        std::set<int> pids;
        for (const uint32_t p : plugins) pids.insert(engine.processorInfo(p).sandboxPid);
        CHECK_EQ(pids.size(), mode.hosting == PluginHosting::InProcess ? size_t{1} : mode.sandboxes.size());
        CHECK_EQ(engine.processorInfo(plugins[0]).sandbox.empty(), mode.hosting == PluginHosting::InProcess);
        CHECK(engine.sandboxProcessIds().size() == (mode.hosting == PluginHosting::InProcess ? 0 : mode.sandboxes.size()));
    }
}

TEST_CASE("sandbox: a crash takes down its sandbox's plug-ins, which pass their input on, and no other") {
    SandboxEngine e(PluginHosting::ByManufacturer);
    auto& engine = e.engine;
    const uint32_t crashing = clipTrack(engine, dcWav());
    const uint32_t crasher = addPlugin(engine, engine.trackChain(crashing), "SUB Test Crasher");
    const uint32_t other = clipTrack(engine, dcWav());
    const uint32_t effect = addPlugin(engine, engine.trackChain(other), "SUB Test Effect");
    REQUIRE(engine.waitForPlugins(10000));
    engine.setProcessorParam(effect, FX_GAIN, 0.25f);
    engine.setTrackGain(other, 0.f);  // (the first track alone, then the second)
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 1.0, 1e-6, 0.0);  // doubled
    const int crasherPid = engine.processorInfo(crasher).sandboxPid;

    // Note 0 crashes it as it plays.
    engine.setTrackNotes(crashing, {{0.0, 1.0, 0, 100}});
    engine.renderOffline(0.0, 1000);
    CHECK(until(engine, [&] { return engine.processorInfo(crasher).status == PluginStatus::Crashed; }));
    CHECK(hasEvent(engine.takeProcessorEvents(), EventType::Crashed, crasher));
    CHECK(engine.processorInfo(crasher).problem.find("SUB Crash Works") != std::string::npos);
    CHECK(engine.processorInfo(effect).status == PluginStatus::Ready);  // another maker's sandbox
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.5, 1e-7, 0.0);  // it passes its input on now
    engine.setTrackGain(crashing, 0.f);
    engine.setTrackGain(other, 1.f);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.25, 1e-6, 0.0);  // the effect plays on
    CHECK(running(engine) == (std::multiset<std::pair<std::string, size_t>>{{"SUBstation", 1}}));

    // Loaded again: in a new sandbox, as it was.
    engine.setTrackNotes(crashing, {});
    engine.setTrackGain(crashing, 1.f);
    engine.setTrackGain(other, 0.f);
    engine.reloadProcessor(crasher);
    CHECK(engine.processorInfo(crasher).status == PluginStatus::Loading);
    REQUIRE(engine.waitForPlugins(10000));
    CHECK(engine.processorInfo(crasher).sandboxPid != crasherPid);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 1.0, 1e-6, 0.0);
}

TEST_CASE("sandbox: together, a crash takes them all down; each loads again as it was last known") {
    SandboxEngine e(PluginHosting::Together);
    auto& engine = e.engine;
    const uint32_t clip = clipTrack(engine, dcWav());
    const uint32_t effect = addPlugin(engine, engine.trackChain(clip), "SUB Test Effect");
    const uint32_t crasher = addPlugin(engine, engine.trackChain(clip), "SUB Test Crasher");
    REQUIRE(engine.waitForPlugins(10000));
    engine.setProcessorParam(effect, FX_GAIN, 0.25f);
    const std::vector<uint8_t> saved = engine.processorState(effect);  // (as saving the project would)
    engine.setProcessorParam(effect, FX_GAIN, 0.1f);  // (lost with the crash: not saved)
    // A state that says "crash" ends it as it is restored, on the sandbox's main thread.
    std::vector<uint8_t> crash = engine.processorState(crasher);  // (a .vstpreset: its own state says "fine!")
    const std::string fine = "fine!";
    const auto said = std::search(crash.begin(), crash.end(), fine.begin(), fine.end());
    REQUIRE(said != crash.end());
    std::copy_n("crash", 5, said);
    engine.setProcessorState(crasher, crash);
    CHECK(until(engine, [&] {
        return engine.processorInfo(effect).status == PluginStatus::Crashed &&
               engine.processorInfo(crasher).status == PluginStatus::Crashed;
    }));
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.5, 1e-7, 0.0);  // both pass their input on
    CHECK(engine.processorState(effect) == saved);  // the last state known stands in
    engine.reloadProcessor(effect);
    REQUIRE(engine.waitForPlugins(10000));
    CHECK_APPROX(engine.processorParam(effect, FX_GAIN), 0.25);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.25, 1e-6, 0.0);
}

TEST_CASE("sandbox: a hung plug-in holds up the audio thread for one block at most") {
    requireTestPlugins();
    sub::sandbox::PluginSandboxes sandboxes;
    sandboxes.setProgram(pluginHost());
    sandboxes.setHosting(PluginHosting::Individually);
    auto crasher = std::make_shared<sub::sandbox::SandboxedProcessor>(
        sandboxes, sub::sandbox::PluginOrigin{testPluginsBundle(), testPluginUids().at("SUB Test Crasher"),
                                              "SUB Test Crasher", "SUB Crash Works"},
        std::vector<uint8_t>{}, kSampleRate, 1024);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (crasher->status() == sub::sandbox::SandboxedProcessor::Status::Loading &&
           std::chrono::steady_clock::now() < deadline) {
        crasher->idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(crasher->status() == sub::sandbox::SandboxedProcessor::Status::Ready);

    constexpr int kFrames = 256;  // live: 5.3 ms
    std::vector<float> left(kFrames), right(kFrames);
    float* channels[2] = {left.data(), right.data()};
    const auto block = [&](const std::vector<sub::ProcessEvent>& events) {
        std::fill(left.begin(), left.end(), 0.25f);
        std::fill(right.begin(), right.end(), 0.25f);
        sub::ProcessContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.inEvents = {events.data(), events.size()};
        const auto started = std::chrono::steady_clock::now();
        crasher->process(ctx, channels, 2, kFrames);
        return std::chrono::steady_clock::now() - started;
    };
    block({});
    CHECK_EQ(left[0], 0.5f);  // doubled
    CHECK_EQ(crasher->lateBlocks(), uint64_t{0});
    // Note 1 hangs it: the block passes through after a little longer than it lasts...
    const auto hung = block({sub::ProcessEvent::noteOn(0, 1, 100)});
    CHECK(hung < std::chrono::milliseconds(100));
    CHECK_EQ(left[0], 0.25f);
    CHECK_EQ(crasher->lateBlocks(), uint64_t{1});
    // ...and the next ones at once, without waiting.
    CHECK(block({}) < std::chrono::milliseconds(2));
    CHECK_EQ(left[0], 0.25f);
    CHECK_EQ(crasher->lateBlocks(), uint64_t{2});
    crasher.reset();  // (its sandbox leaves it as it is, and is killed when it doesn't quit)
}

TEST_CASE("sandbox: latency, sidechains and notes from other tracks, as in the engine's own process") {
    const auto scene = [](sub::Engine& engine, bool sandboxed) {
        const std::string wav = clickWav(0.5);
        const uint32_t late = clipTrack(engine, wav, 1.0);
        clipTrack(engine, wav, 1.0);
        const uint32_t effect = addPlugin(engine, engine.trackChain(late), "SUB Test Effect");
        const uint32_t keyed = clipTrack(engine, dcWav());
        const uint32_t sidechain = addPlugin(engine, engine.trackChain(keyed), "SUB Test Sidechain");
        if (sandboxed) REQUIRE(engine.waitForPlugins(10000));
        CHECK(engine.processorInfo(sidechain).hasSidechain);
        engine.setProcessorSidechain(sidechain, late);
        engine.setProcessorParam(effect, FX_LATENCY, 100);
        CHECK(until(engine, [&] { return engine.processorInfo(effect).latency == 100; }));
        return engine.renderOffline(0.0, 2 * kBeat);
    };
    SandboxEngine inProcess(PluginHosting::InProcess);
    SandboxEngine sandboxed(PluginHosting::ByPlugin);
    const Samples here = scene(inProcess.engine, false);
    const Samples there = scene(sandboxed.engine, true);
    REQUIRE(maxAbs(here) > 0.1);
    CHECK_ARRAY_EQUAL(there, here);
}

TEST_CASE("sandbox: reloading moves a plug-in to where the hosting mode says now, as it is") {
    SandboxEngine e(PluginHosting::InProcess);
    auto& engine = e.engine;
    const uint32_t clip = clipTrack(engine, dcWav());
    const uint32_t effect = addPlugin(engine, engine.trackChain(clip), "SUB Test Effect");
    engine.setProcessorParam(effect, FX_GAIN, 0.25f);
    CHECK(engine.processorInfo(effect).sandbox.empty());
    engine.setPluginHosting(PluginHosting::Together);
    engine.reloadProcessor(effect);
    CHECK(hasEvent(engine.takeProcessorEvents(), EventType::ParamInfoChanged, effect));
    REQUIRE(engine.waitForPlugins(10000));
    CHECK_EQ(engine.processorInfo(effect).sandbox, std::string("All plug-ins"));
    CHECK_APPROX(engine.processorParam(effect, FX_GAIN), 0.25);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.25, 1e-6, 0.0);
    // And back.
    engine.setPluginHosting(PluginHosting::InProcess);
    engine.reloadProcessor(effect);
    CHECK(engine.processorInfo(effect).sandbox.empty());
    CHECK_APPROX(engine.processorParam(effect, FX_GAIN), 0.25);
    // A sandbox nothing uses any more goes.
    CHECK(until(engine, [&] { return engine.pluginSandboxes().empty(); }));
    CHECK_THROWS_AS(engine.reloadProcessor(engine.addBuiltinProcessor(engine.trackChain(clip), "builtin:utility", -1)),
                    std::invalid_argument);
}

TEST_CASE("sandbox: without its host program, a plug-in can't be added there") {
    SandboxEngine e;
    auto& engine = e.engine;
    engine.setPluginHostProgram(pluginHost() + ".missing");
    CHECK_THROWS_MATCHING(addPlugin(engine, engine.trackChain(engine.addTrack()), "SUB Test Effect"),
                          std::runtime_error, "Could not start");
}

TEST_CASE("sandbox: a plug-in that can't be loaded there says why") {
    SandboxEngine e;
    auto& engine = e.engine;
    const uint32_t track = engine.addTrack();
    const uint32_t missing = engine.addPluginProcessor(engine.trackChain(track), "VST3", testPluginsBundle(),
                                                       std::string(32, '0'), -1, {"Gone", "Nobody"});
    CHECK(until(engine, [&] { return engine.processorInfo(missing).status == PluginStatus::Failed; }));
    CHECK(hasEvent(engine.takeProcessorEvents(), EventType::LoadFailed, missing));
    CHECK(engine.processorInfo(missing).problem.find("does not contain this plug-in") != std::string::npos);
}
