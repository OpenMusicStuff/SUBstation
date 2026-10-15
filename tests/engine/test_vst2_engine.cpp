// VST2 hosting in the engine (plugins/Vst2*), with the test VST2 plug-ins
// built alongside it (tests/vst2_plugins): scanning (a shell's plug-ins too),
// instruments and effects in the chain, parameters, the host's answers
// (transport, latency, refreshes), automation splitting blocks, sidechains,
// state as .fxp and .fxb, preset names, load errors, and a VST2 plug-in in a
// sandbox. Also .fxp/.fxb files on their own (Vst2Preset.h).
//
// Rendered offline, so no audio device is needed.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <thread>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "plugins/PresetName.h"
#include "plugins/Vst2Format.h"
#include "plugins/Vst2Preset.h"
#include "plugins/Vst2Processor.h"

using namespace subtest;

namespace {

// SUB VST2 Gain's and SUB VST2 Synth's parameters.
enum { V2_GAIN, V2_LATENCY, V2_INVERT };
enum { V2_LEVEL, V2_TEMPO, V2_PLAYING, V2_BEAT };

using EventType = sub::ProcessorEvent::Type;

struct Vst2Engine {
    sub::Engine engine;
    Vst2Engine() {
        requireTestVst2();
        engine.setClipFadeMs(0);
    }
};

std::pair<uint32_t, uint32_t> vst2Track(sub::Engine& engine, const std::string& kind,
                                        const std::vector<sub::NoteDesc>& notes = {}) {
    const uint32_t track = engine.addTrack();
    const uint32_t processor = addTestVst2(engine, engine.trackChain(track), kind);
    if (!notes.empty()) engine.setTrackNotes(track, notes);
    return {track, processor};
}

bool hasEvent(const std::vector<sub::ProcessorEventRecord>& events, EventType type) {
    return std::any_of(events.begin(), events.end(), [&](const auto& e) { return e.type == type; });
}

std::map<std::string, sub::PluginDescription> scanned(const std::string& path) {
    std::map<std::string, sub::PluginDescription> found;
    for (const auto& d : sub::vst2::Vst2Format::instance().scanFile(path)) found[d.name] = d;
    return found;
}

}  // namespace

TEST_CASE("VST2: scanning a plug-in's library") {
    requireTestVst2();
    auto gain = scanned(testVst2Path("Gain"));
    REQUIRE(gain.size() == 1);
    const sub::PluginDescription& g = gain.at("SUB VST2 Gain");
    CHECK_EQ(g.format, std::string("VST2"));
    CHECK_EQ(g.uid, kVst2GainUid);
    CHECK_EQ(g.vendor, std::string("SUBstation Tests"));
    CHECK_EQ(g.version, std::string("1.2.3"));
    CHECK_EQ(g.category, std::string("Fx|Delay"));
    CHECK(!g.isInstrument);
    CHECK_EQ(g.path, testVst2Path("Gain"));
    const auto synth = scanned(testVst2Path("Synth"));
    REQUIRE(synth.count("SUB VST2 Synth") == 1);
    CHECK(synth.at("SUB VST2 Synth").isInstrument);
    CHECK_EQ(synth.at("SUB VST2 Synth").category, std::string("Instrument|Synth"));
    CHECK_EQ(synth.at("SUB VST2 Synth").uid, kVst2SynthUid);

    // A library without VST 2's entry point holds none; a file that isn't there can't be read.
    if (haveTestPlugins()) {
        const auto inBundle = std::filesystem::path(testPluginsBundle()) /
                              sub::vst3::Vst3Format::binaryInBundle(
                                  std::filesystem::path(testPluginsBundle()).filename().string());
        CHECK(sub::vst2::Vst2Format::instance().scanFile(inBundle.string()).empty());
    }
    CHECK_THROWS_AS(sub::vst2::Vst2Format::instance().scanFile(testVst2Path("Missing")), std::runtime_error);
}

TEST_CASE("VST2: a shell's plug-ins") {
    requireTestVst2();
    const auto shell = scanned(testVst2Path("Shell"));
    REQUIRE(shell.size() == 2);
    CHECK_EQ(shell.at("SUB VST2 Gain").uid, kVst2GainUid);
    CHECK_EQ(shell.at("SUB VST2 Synth").uid, kVst2SynthUid);
    CHECK(shell.at("SUB VST2 Synth").isInstrument);  // (each made to say so)
    CHECK_EQ(shell.at("SUB VST2 Gain").category, std::string("Fx|Delay"));
    CHECK_EQ(shell.at("SUB VST2 Gain").path, testVst2Path("Shell"));

    Vst2Engine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, dcWav());
    const uint32_t gain = engine.addPluginProcessor(engine.trackChain(track), "VST2", testVst2Path("Shell"),
                                                    kVst2GainUid, -1);
    CHECK_EQ(engine.processorInfo(gain).name, std::string("SUB VST2 Gain"));
    engine.setProcessorParam(gain, V2_GAIN, 0.25f);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.25, 1e-7, 0.0);
    CHECK_THROWS_MATCHING(engine.addPluginProcessor(engine.trackChain(track), "VST2", testVst2Path("Shell"),
                                                    "01020304", -1),
                          std::runtime_error, "does not contain");
}

TEST_CASE("VST2: files by their format") {
    CHECK(sub::pluginFormatOfFile("C:/Plugins/Foo.vst3") == sub::pluginFormat("VST3"));
    CHECK(sub::pluginFormatOfFile("/home/me/.vst3/Foo.vst3/") == sub::pluginFormat("VST3"));
#ifdef _WIN32
    CHECK(sub::pluginFormatOfFile("C:/VSTPlugins/Foo.DLL") == sub::pluginFormat("VST2"));
    CHECK(sub::pluginFormatOfFile("/usr/lib/vst/foo.so") == nullptr);
#else
    CHECK(sub::pluginFormatOfFile("/usr/lib/vst/foo.so") == sub::pluginFormat("VST2"));
    CHECK(sub::pluginFormatOfFile("C:/VSTPlugins/Foo.dll") == nullptr);
#endif
    CHECK(sub::pluginFormatOfFile("song.wav") == nullptr);
    CHECK_EQ(sub::pluginFormat("VST2")->name(), std::string("VST2"));
    CHECK(sub::pluginFormat("CLAP") == nullptr);
}

TEST_CASE("VST2: an instrument plays its notes sample exactly") {
    Vst2Engine e;
    auto& engine = e.engine;
    const auto [track, synth] = vst2Track(engine, "Synth", {{1.0, 2.0, 60, 127}});
    CHECK(engine.processorInfo(synth).acceptsMidi);
    const Samples out = engine.renderOffline(0.0, 4 * kBeat);
    CHECK(allEqual(frames(out, 0, kBeat), 0.0));
    CHECK_ALLCLOSE(frames(out, kBeat, 3 * kBeat), 1.0, 1e-7, 0.0);
    CHECK(allEqual(frames(out, 3 * kBeat), 0.0));
    engine.setTrackNotes(track, {{0.0, 1.0, 60, 64}, {0.0, 1.0, 64, 64}});  // a chord, its velocities
    CHECK_NEAR(at(engine.renderOffline(0.0, 100), 50, 0), 2 * 64 / 127.0, 1e-6);
}

TEST_CASE("VST2: parameters as the plug-in describes them") {
    Vst2Engine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, dcWav());
    const uint32_t gain = addTestVst2(engine, engine.trackChain(track), "Gain");
    const auto params = engine.processorParams(gain);
    REQUIRE(params.size() == 3);
    CHECK_EQ(params[V2_GAIN].name, std::string("Gain"));
    CHECK_EQ(params[V2_LATENCY].name, std::string("Latency"));
    CHECK_EQ(params[V2_INVERT].name, std::string("Invert"));
    CHECK_EQ(params[V2_GAIN].id, std::string("0"));
    CHECK_EQ(params[V2_INVERT].id, std::string("2"));
    CHECK_EQ(params[V2_GAIN].steps, 0);
    CHECK_EQ(params[V2_GAIN].defaultValue, 0.5f);  // (as it started)
    CHECK(params[V2_GAIN].automatable);
    CHECK_EQ(params[V2_INVERT].steps, 1);  // a switch
    CHECK(params[V2_INVERT].valueLabels == (std::vector<std::string>{"Off", "On"}));
    CHECK_EQ(engine.processorParamIndex(gain, "1"), V2_LATENCY);
    // Its own text for the value it has (with its label), none for another.
    CHECK_EQ(engine.processorParamText(gain, V2_GAIN, 0.5f), std::string("1.00 x"));
    CHECK_EQ(engine.processorParamText(gain, V2_GAIN, 0.25f), std::string());
    const auto info = engine.processorInfo(gain);
    CHECK_EQ(info.name, std::string("SUB VST2 Gain"));
    CHECK_EQ(info.typeId, "vst2:" + kVst2GainUid);
    CHECK(info.hasSidechain);  // (four inputs: the last two)
    CHECK(!info.acceptsMidi);
    CHECK(!engine.processorInfo(addTestVst2(engine, engine.trackChain(engine.addTrack()), "Synth")).hasEditor);

    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.5, 1e-7, 0.0);  // Gain 0.5 is unity
    engine.setProcessorParam(gain, V2_GAIN, 0.25f);
    CHECK_APPROX(engine.processorParam(gain, V2_GAIN), 0.25);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.25, 1e-7, 0.0);
    engine.setProcessorParam(gain, V2_INVERT, 1.f);
    CHECK_EQ(engine.processorParam(gain, V2_INVERT), 1.f);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), -0.25, 1e-7, 0.0);
    engine.setProcessorEnabled(gain, false);  // switched off: the audio passes by
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.5, 1e-7, 0.0);
}

TEST_CASE("VST2: transport reaches the plug-in, and values it sets itself the UI") {
    Vst2Engine e;
    auto& engine = e.engine;
    const auto [track, synth] = vst2Track(engine, "Synth");
    engine.setTempo(90.0);
    engine.takeProcessorEvents();
    engine.renderOffline(2.0, 4096);
    engine.idle();  // what it set from its process() reaches the UI
    CHECK_APPROX(engine.processorParam(synth, V2_TEMPO), 0.09);
    CHECK_EQ(engine.processorParam(synth, V2_PLAYING), 1.f);
    const double beatOfLastBlock = 2.0 + 3 * 1024 / (kSampleRate * 60 / 90.0);
    CHECK_NEAR(engine.processorParam(synth, V2_BEAT) * 1000.0, beatOfLastBlock, 1e-3);
    const auto events = engine.takeProcessorEvents();
    CHECK(hasEvent(events, EventType::ParamsChanged));
    CHECK(!hasEvent(events, EventType::ParamEdited));  // (not edits of the user's)
}

TEST_CASE("VST2: automation splits the block where values change") {
    Vst2Engine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, dcWav());
    const uint32_t gain = addTestVst2(engine, engine.trackChain(track), "Gain");
    engine.setTrackAutomation(track, {{gain, std::to_string(V2_GAIN), {{0.0, 0.25f, 0.f}}}});
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.25, 1e-7, 0.0);  // automated, as if set to 0.25
    engine.takeProcessorEvents();
    engine.idle();
    CHECK_APPROX(engine.processorParam(gain, V2_GAIN), 0.25);
    CHECK(hasEvent(engine.takeProcessorEvents(), EventType::ParamsChanged));

    // A ramp: within a block the value steps, at least kMinSegment frames apart.
    engine.setTrackAutomation(track, {{gain, std::to_string(V2_GAIN), {{0.0, 0.f, 0.f}, {1.0, 1.f, 0.f}}}});
    const Samples out = channel(engine.renderOffline(0.0, 1024), 0);
    std::set<double> levels(out.begin(), out.end());
    CHECK(levels.size() > 4);
    int run = 1;
    int shortest = 1 << 20;
    for (size_t i = 1; i < out.size(); ++i) {
        if (out[i] != out[i - 1]) {
            shortest = std::min(shortest, run);
            run = 0;
        }
        ++run;
    }
    CHECK(shortest >= sub::vst2::Vst2Processor::kMinSegment);
    CHECK(std::is_sorted(out.begin(), out.end()));
}

TEST_CASE("VST2: a device hears its sidechain") {
    Vst2Engine e;
    auto& engine = e.engine;
    const uint32_t source = clipTrack(engine, clickWav(0.25), 1.0, 1000.0 / kSampleRate);
    const uint32_t track = engine.addTrack();
    const uint32_t gain = addTestVst2(engine, engine.trackChain(track), "Gain");
    const Samples alone = channel(engine.renderOffline(0.0, 2 * kBeat), 0);
    engine.setProcessorSidechain(gain, source);
    const Samples keyed = channel(engine.renderOffline(0.0, 2 * kBeat), 0);
    CHECK_NEAR(alone[kBeat], 0.25, 1e-6);
    CHECK_NEAR(keyed[kBeat], 0.5, 1e-6);  // the source, and the device's output of it
}

TEST_CASE("VST2: latency is reported and compensated") {
    Vst2Engine e;
    auto& engine = e.engine;
    const std::string wav = clickWav(0.5);
    const uint32_t late = clipTrack(engine, wav, 1.0);
    clipTrack(engine, wav, 1.0);
    const uint32_t gain = addTestVst2(engine, engine.trackChain(late), "Gain");
    engine.takeProcessorEvents();
    engine.setProcessorParam(gain, V2_LATENCY, 0.1f);  // 100 samples: it says its I/O changed
    engine.idle();
    CHECK(hasEvent(engine.takeProcessorEvents(), EventType::LatencyChanged));
    CHECK_EQ(engine.processorInfo(gain).latency, 100);
    const Samples out = channel(engine.renderOffline(0.0, 2 * kBeat), 0);
    CHECK(nonzero(out) == std::vector<int64_t>{kBeat});
    CHECK_NEAR(out[kBeat], 1.0, 1e-4);
}

TEST_CASE("VST2: a chunk's state, as an .fxb") {
    Vst2Engine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, dcWav());
    const uint32_t gain = addTestVst2(engine, engine.trackChain(track), "Gain");
    engine.setProcessorParam(gain, V2_GAIN, 0.75f);
    engine.setProcessorParam(gain, V2_INVERT, 1.f);
    const std::vector<uint8_t> state = engine.processorState(gain);
    const auto preset = sub::vst2::readPreset(state);
    REQUIRE(preset.has_value());
    CHECK(preset->kind == sub::vst2::Preset::Kind::BankChunk);
    CHECK_EQ(*sub::vst2::vst2IdFromUid(kVst2GainUid), preset->pluginId);
    CHECK(std::string(state.begin(), state.begin() + 4) == "CcnK");

    const uint32_t copy = addTestVst2(engine, engine.trackChain(engine.addTrack()), "Gain");
    engine.takeProcessorEvents();
    engine.setProcessorState(copy, state);
    CHECK_APPROX(engine.processorParam(copy, V2_GAIN), 0.75);
    CHECK_EQ(engine.processorParam(copy, V2_INVERT), 1.f);
    CHECK(hasEvent(engine.takeProcessorEvents(), EventType::ParamsChanged));

    // Its preset's name: in its chunk (it has no programs).
    engine.idle();
    CHECK_EQ(engine.processorPresetName(copy), std::string());
    sub::vst2::Preset named = *preset;
    const std::string chunk = "gain=0.5 latency=0 invert=0 presetName=\"Warm Pad\"";
    named.chunk.assign(chunk.begin(), chunk.end());
    engine.setProcessorState(copy, sub::vst2::writePreset(named));
    engine.idle();
    CHECK_EQ(engine.processorPresetName(copy), std::string("Warm Pad"));
    CHECK(hasEvent(engine.takeProcessorEvents(), EventType::PresetChanged));
    CHECK_EQ(sub::vst3::presetNameFromPreset(engine.processorState(copy).data(), engine.processorState(copy).size()),
             std::string("Warm Pad"));  // (a project's saved device, the plug-in missing)

    // Another plug-in's settings, and what isn't any.
    const auto [synthTrack, synth] = vst2Track(engine, "Synth");
    CHECK_THROWS_MATCHING(engine.setProcessorState(synth, state), std::runtime_error, "not for SUB VST2 Synth");
    const std::string garbage = "garbage";
    CHECK_THROWS_AS(engine.setProcessorState(gain, std::vector<uint8_t>(garbage.begin(), garbage.end())),
                    std::runtime_error);
}

TEST_CASE("VST2: values' state, as an .fxp, and programs") {
    Vst2Engine e;
    auto& engine = e.engine;
    const auto [track, synth] = vst2Track(engine, "Synth", {{0.0, 1.0, 60, 127}});
    engine.idle();
    CHECK_EQ(engine.processorPresetName(synth), std::string("Init"));  // its program's name
    engine.setProcessorParam(synth, V2_LEVEL, 0.25f);
    const std::vector<uint8_t> state = engine.processorState(synth);
    const auto preset = sub::vst2::readPreset(state);
    REQUIRE(preset.has_value());
    CHECK(preset->kind == sub::vst2::Preset::Kind::Program);
    CHECK_EQ(preset->name, std::string("Init"));
    REQUIRE(preset->values.size() == 4);
    CHECK_APPROX(preset->values[V2_LEVEL], 0.25);

    const auto [copyTrack, copy] = vst2Track(engine, "Synth", {{0.0, 1.0, 60, 127}});
    engine.setProcessorState(copy, state);
    CHECK_APPROX(engine.processorParam(copy, V2_LEVEL), 0.25);
    CHECK_ALLCLOSE(frames(engine.renderOffline(0.0, 100), 50, 51), 0.5 + 0.5, 1e-7, 0.0);  // both at Level 0.25

    // A program renamed in the file: the plug-in takes the name.
    sub::vst2::Preset renamed = *preset;
    renamed.name = "Lead";
    engine.setProcessorState(copy, sub::vst2::writePreset(renamed));
    engine.idle();
    CHECK_EQ(engine.processorPresetName(copy), std::string("Lead"));
}

TEST_CASE("VST2: offline renders are repeatable, without hanging notes") {
    Vst2Engine e;
    auto& engine = e.engine;
    vst2Track(engine, "Synth", {{0.0, 16.0, 60, 100}});
    const Samples first = engine.renderOffline(0.0, 2 * kBeat);
    engine.renderOffline(0.0, kBeat / 2);  // stops in the middle of the note
    CHECK_EQ(maxAbs(engine.renderOffline(4.0, kBeat)), 0.0);  // no hanging note
    CHECK_ARRAY_EQUAL(engine.renderOffline(0.0, 2 * kBeat), first);
}

TEST_CASE("VST2: load errors") {
    Vst2Engine e;
    auto& engine = e.engine;
    const uint32_t chain = engine.trackChain(engine.addTrack());
    CHECK_THROWS_AS(engine.addPluginProcessor(chain, "VST2", testVst2Path("Missing"), kVst2GainUid, -1),
                    std::runtime_error);
    CHECK_THROWS_MATCHING(engine.addPluginProcessor(chain, "VST2", testVst2Path("Gain"), kVst2SynthUid, -1),
                          std::runtime_error, "does not contain");
    CHECK_THROWS_AS(engine.addPluginProcessor(chain, "VST2", testVst2Path("Gain"), "not an id", -1),
                    std::invalid_argument);
}

TEST_CASE("VST2: a plug-in in a sandbox") {
    Vst2Engine e;
    auto& engine = e.engine;
    engine.setPluginHostProgram(SUBSTATION_PLUGIN_HOST);
    engine.setPluginHosting(sub::PluginHosting::Together);
    engine.setSandboxRetireDelay(0);
    const uint32_t track = clipTrack(engine, dcWav());
    const uint32_t gain = engine.addPluginProcessor(engine.trackChain(track), "VST2", testVst2Path("Gain"), kVst2GainUid,
                                                    -1, {"SUB VST2 Gain", "SUBstation Tests"});
    REQUIRE(engine.waitForPlugins(10000));
    CHECK_EQ(engine.processorInfo(gain).typeId, "vst2:" + kVst2GainUid);
    REQUIRE(engine.processorParams(gain).size() == 3);
    engine.setProcessorParam(gain, V2_GAIN, 0.25f);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 1000), 0.25, 1e-7, 0.0);
    CHECK_EQ(engine.processorParamText(gain, V2_GAIN, 0.25f), std::string("0.50 x"));
    const std::vector<uint8_t> state = engine.processorState(gain);
    REQUIRE(sub::vst2::readPreset(state).has_value());
    CHECK_APPROX(engine.processorParam(gain, V2_GAIN), 0.25);
}

// ---------------------------------------------------------------------------
// .fxp and .fxb files on their own

TEST_CASE("VST2 presets: programs and banks round-trip") {
    using sub::vst2::Preset;
    Preset program;
    program.kind = Preset::Kind::Program;
    program.pluginId = 0x53754732;
    program.pluginVersion = 7;
    program.name = "A Name Longer Than Twenty-Seven Bytes";
    program.values = {0.f, 0.5f, 1.f};
    const std::vector<uint8_t> bytes = sub::vst2::writePreset(program);
    CHECK_EQ(bytes.size(), size_t{56 + 3 * 4});
    CHECK(std::string(bytes.begin() + 8, bytes.begin() + 12) == "FxCk");
    CHECK_EQ(bytes[7], uint8_t(bytes.size() - 8));  // (the size after it, big-endian)
    auto read = sub::vst2::readPreset(bytes);
    REQUIRE(read.has_value());
    CHECK(read->kind == Preset::Kind::Program);
    CHECK_EQ(read->pluginId, 0x53754732);
    CHECK_EQ(read->pluginVersion, 7);
    CHECK_EQ(read->name, std::string("A Name Longer Than Twenty-S"));  // 27 bytes and its end
    CHECK(read->values == program.values);

    Preset bank;
    bank.kind = Preset::Kind::Bank;
    bank.pluginId = 1;
    bank.currentProgram = 1;
    bank.programs = {program, program};
    bank.programs[1].name = "Second";
    read = sub::vst2::readPreset(sub::vst2::writePreset(bank));
    REQUIRE(read.has_value());
    CHECK(read->kind == Preset::Kind::Bank);
    CHECK_EQ(read->currentProgram, 1);
    REQUIRE(read->programs.size() == 2);
    CHECK_EQ(read->programs[1].name, std::string("Second"));
    CHECK(read->programs[0].values == program.values);
    CHECK_EQ(read->programs[1].pluginId, 1);  // (the bank's)

    Preset chunk;
    chunk.kind = Preset::Kind::BankChunk;
    chunk.pluginId = 2;
    chunk.numPrograms = 16;
    chunk.chunk = {1, 2, 3, 0, 255};
    read = sub::vst2::readPreset(sub::vst2::writePreset(chunk));
    REQUIRE(read.has_value());
    CHECK(read->kind == Preset::Kind::BankChunk);
    CHECK_EQ(read->numPrograms, 16);
    CHECK(read->chunk == chunk.chunk);

    // Cut short, or not one at all.
    std::vector<uint8_t> cut = sub::vst2::writePreset(chunk);
    cut.pop_back();
    CHECK(!sub::vst2::readPreset(cut).has_value());
    CHECK(!sub::vst2::readPreset(nullptr, 0).has_value());
    const std::string text = "VST3 is not this";
    CHECK(!sub::vst2::readPreset(reinterpret_cast<const uint8_t*>(text.data()), text.size()).has_value());
}

TEST_CASE("VST2 presets: ids and names") {
    CHECK_EQ(sub::vst2::vst2Uid(0x53754732), std::string("53754732"));
    CHECK_EQ(sub::vst2::vst2Uid(-1), std::string("FFFFFFFF"));
    CHECK_EQ(*sub::vst2::vst2IdFromUid("ffffffff"), -1);
    CHECK(!sub::vst2::vst2IdFromUid("5375473").has_value());
    CHECK(!sub::vst2::vst2IdFromUid("5375473G").has_value());

    sub::vst2::Preset program;
    program.name = "Prog 3";  // a placeholder says nothing
    program.values = {0.5f};
    auto bytes = sub::vst2::writePreset(program);
    CHECK_EQ(sub::vst3::presetNameFromPreset(bytes.data(), bytes.size()), std::string());
    program.name = " Bass Drop ";
    bytes = sub::vst2::writePreset(program);
    CHECK_EQ(sub::vst3::presetNameFromPreset(bytes.data(), bytes.size()), std::string("Bass Drop"));
}
