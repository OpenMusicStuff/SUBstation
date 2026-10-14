// The built-in Vital instrument (builtin/devices/Vital.cpp) and its patches
// (builtin/VitalPatch.h): its parameters and their texts, notes and their
// pitches, per-note bends, MIDI, modulations, a new patch taking over while a
// note is held, oversampling, sample rates, sleeping, Vital's presets read and
// written (older ones upgraded), and the device through the engine.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "Engine.h"
#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/VitalPatch.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"
#include "harness/Test.h"

using namespace subtest;
namespace vp = sub::vitalpatch;

namespace {

constexpr double kRate = 48000.0;

// What a test plays: events at absolute frames.
struct Timeline {
    std::vector<sub::ProcessEvent> events;  // sampleOffset: the absolute frame
    void note(int64_t on, int64_t off, uint8_t key, uint8_t velocity = 100, int32_t id = -1) {
        sub::ProcessEvent start = sub::ProcessEvent::noteOn(static_cast<int32_t>(on), key, velocity);
        start.noteId = id;
        events.push_back(start);
        if (off >= 0) {
            sub::ProcessEvent end = sub::ProcessEvent::noteOff(static_cast<int32_t>(off), key);
            end.noteId = id;
            events.push_back(end);
        }
    }
    void bend(int64_t at, uint8_t key, int32_t id, float semitones) {
        events.push_back(sub::ProcessEvent::noteBend(static_cast<int32_t>(at), key, id, semitones));
    }
    void midi(int64_t at, uint8_t a, uint8_t b, uint8_t c) {
        sub::ProcessEvent event;
        event.type = sub::ProcessEvent::Type::Midi;
        event.sampleOffset = static_cast<int32_t>(at);
        event.data[0] = a;
        event.data[1] = b;
        event.data[2] = c;
        events.push_back(event);
    }
};

// Plays `frames` of the timeline through the device, `block` frames at a time: (left, right).
std::pair<Samples, Samples> play(Standalone& device, Timeline timeline, int64_t frames, int block = 256,
                                 int64_t startFrame = 0) {
    std::stable_sort(timeline.events.begin(), timeline.events.end(),
                     [](const auto& a, const auto& b) { return a.sampleOffset < b.sampleOffset; });
    std::vector<sub::ProcessEvent> blockEvents;
    Samples left(static_cast<size_t>(frames)), right(static_cast<size_t>(frames));
    device.run({&left, &right}, {}, block, [&](int64_t start, int n) {
        blockEvents.clear();
        for (const sub::ProcessEvent& event : timeline.events) {
            const int64_t at = event.sampleOffset - startFrame;
            if (at >= start && at < start + n) {
                sub::ProcessEvent e = event;
                e.sampleOffset = static_cast<int32_t>(at - start);
                blockEvents.push_back(e);
            }
        }
        device.context().inEvents = {blockEvents.data(), blockEvents.size()};
    });
    device.context().inEvents = {};
    return {std::move(left), std::move(right)};
}

Samples slice(const Samples& x, int64_t from, int64_t to) {
    return Samples(x.begin() + from, x.begin() + std::min<int64_t>(to, static_cast<int64_t>(x.size())));
}

// The strongest of these frequencies in x[from, from + length).
double strongest(const Samples& x, const std::vector<double>& frequencies, int64_t from, int64_t length,
                 double rate = kRate) {
    double best = frequencies.front();
    double level = -1.0;
    for (double f : frequencies) {
        const double a = windowedAmplitude(x, f, from, length, rate);
        if (a > level) {
            level = a;
            best = f;
        }
    }
    return best;
}

double peak(const Samples& x) {
    double p = 0.0;
    for (float v : x) p = std::max(p, static_cast<double>(std::fabs(v)));
    return p;
}

std::string stateOf(sub::Processor& processor, const std::string& key) {
    return sub::BuiltinProcessor::decodeState(processor.getState())[key];
}

}  // namespace

TEST_CASE("vital: listed as an instrument, with every parameter of Vital's engine") {
    const auto& infos = sub::BuiltinRegistry::instance().devices();
    const auto it = std::find_if(infos.begin(), infos.end(), [](const auto& i) { return i.id == "vital"; });
    REQUIRE(it != infos.end());
    CHECK(it->isInstrument());
    CHECK(it->name == "Vital");
    CHECK(it->params.size() == vp::params().size());
    CHECK(vp::params().size() == 772u);  // Vital's engine's controls
    std::set<std::string> ids;
    for (const sub::ParamInfo& info : vp::params()) {
        INFO(info.id);
        CHECK(ids.insert(info.id).second);
        CHECK(info.ownText);
        CHECK(info.minValue <= info.defaultValue);
        CHECK(info.defaultValue <= info.maxValue);
        if (!info.valueLabels.empty()) CHECK(info.minValue == 0.f);
    }
    for (const char* id : {"osc_1_level", "filter_1_cutoff", "env_1_attack", "lfo_8_frequency", "random_4_style",
                           "modulation_64_amount", "reverb_dry_wet", "macro_control_4", "polyphony", "oversampling"}) {
        INFO(id);
        CHECK(vp::paramIndex(id) >= 0);
    }
    for (const char* id : {"sub_on", "filter_1_osc1_input", "compressor_low_band_unused"}) {
        INFO(id);  // (Vital's older versions': its presets' upgrade moves them)
        CHECK(vp::paramIndex(id) == -1);
    }
    for (const char* id : {"bypass", "beats_per_minute", "mpe_enabled", "view_spectrogram"}) {
        INFO(id);
        CHECK(vp::params()[static_cast<size_t>(vp::paramIndex(id))].hidden);
    }
    CHECK(!vp::params()[static_cast<size_t>(vp::paramIndex("oversampling"))].automatable);
    CHECK(vp::params()[static_cast<size_t>(vp::paramIndex("filter_1_model"))].valueLabels.size() == 8u);
    CHECK(vp::params()[static_cast<size_t>(vp::paramIndex("polyphony"))].steps == 31);
}

TEST_CASE("vital: values shown as Vital shows them") {
    const auto text = [](const char* id, float value) { return vp::paramText(vp::paramIndex(id), value); };
    CHECK(text("polyphony", 8.f) == "8 voices");
    CHECK(text("filter_1_model", 0.f) == "Analog");
    CHECK(text("osc_1_on", 1.f) == "On");
    CHECK(text("volume", 0.f) == "-80.00dB");            // (Vital's own units, as it writes them)
    CHECK(text("env_1_attack", 1.f) == "1.000 secs");    // quartic: 1⁴ seconds
    CHECK(text("env_1_attack", 0.5f) == "0.062 secs");   // 0.5⁴, five characters
    CHECK(text("chorus_dry_wet", 0.5f) == "50.00%");     // shown in percent
    CHECK(text("lfo_1_frequency", 1.f) == "0.500 secs"); // 2^1 Hz, as a period
    CHECK(text("osc_1_pan", -0.25f) == "-25.00%");
    CHECK(text("filter_1_cutoff", 69.f) == "440 Hz");    // a cutoff in hertz
    CHECK(text("filter_1_cutoff", 105.f) == "3.52 kHz");
    CHECK(text("filter_1_style", 1.f) == "24dB");        // (names as far as Vital's list has them)
    CHECK(text("filter_1_style", 7.f) == "7");
    CHECK(vp::paramText(-1, 0.f).empty());
    // The device gives the same texts.
    Standalone vital("vital", kRate);
    CHECK(vital.processor().paramText(vp::paramIndex("polyphony"), 4.f) == "4 voices");
}

TEST_CASE("vital: silent until a note, which plays at its pitch") {
    Standalone vital("vital", kRate);
    const auto [quiet, quietRight] = play(vital, {}, 4800);
    CHECK(peak(quiet) == 0.0);

    Timeline t;
    t.note(0, 24000, 69);  // A4
    const auto [left, right] = play(vital, t, 36000);
    CHECK(rmsDb(slice(left, 4800, 24000)) > -40.0);
    CHECK(strongest(left, {415.3, 440.0, 466.2, 220.0, 880.0}, 4800, 16384) == 440.0);
    // Mono at its defaults: both sides the same.
    double difference = 0.0;
    for (size_t i = 0; i < left.size(); ++i) difference = std::max(difference, double(std::fabs(left[i] - right[i])));
    CHECK(difference < 1e-6);
    // Released: gone a while after its note-off (the init patch's release is short).
    CHECK(rmsDb(slice(left, 33600, 36000)) < -90.0);
}

TEST_CASE("vital: notes play at their offsets in a block") {
    for (int offset : {0, 1, 100, 255}) {
        INFO("offset " + std::to_string(offset));
        Standalone vital("vital", kRate);
        Timeline t;
        t.note(1024 + offset, -1, 60);
        const auto [left, right] = play(vital, t, 4096, 256);
        int64_t first = -1;
        for (size_t i = 0; i < left.size(); ++i) {
            if (left[i] != 0.f) {
                first = static_cast<int64_t>(i);
                break;
            }
        }
        REQUIRE(first >= 0);
        CHECK(first >= 1024 + offset);
        CHECK(first <= 1024 + offset + 8);  // (the oversampling's filter's delay at most)
    }
}

TEST_CASE("vital: a note's bend moves that note alone, 48 semitones either way") {
    Standalone vital("vital", kRate);
    Timeline t;
    t.note(0, -1, 57, 100, 1);  // A3, 220 Hz
    t.note(0, -1, 69, 100, 2);  // A4, 440 Hz
    t.bend(4800, 57, 1, 12.f);  // the A3 up an octave: both at 440 Hz
    const auto [left, right] = play(vital, t, 4800 + 24000);
    CHECK(windowedAmplitude(left, 220.0, 0, 4096) > 10.0 * windowedAmplitude(left, 220.0, 9600, 16384));
    CHECK(strongest(left, {220.0, 440.0, 880.0}, 9600, 16384) == 440.0);

    Standalone wide("vital", kRate);
    Timeline down;
    down.note(0, -1, 81, 100, 7);   // A5, 880 Hz
    down.bend(0, 81, 7, -24.f);     // to A3
    const auto [low, lowRight] = play(wide, down, 24000);
    CHECK(strongest(low, {220.0, 440.0, 880.0}, 4800, 16384) == 220.0);
}

TEST_CASE("vital: MIDI pitch bend, mod wheel and the sustain pedal") {
    Standalone vital("vital", kRate, {{"pitch_bend_range", 12.f}});
    Timeline t;
    t.note(0, -1, 57);
    t.midi(0, 0xE0, 0x7F, 0x7F);  // full up: an octave
    const auto [left, right] = play(vital, t, 24000);
    CHECK(strongest(left, {220.0, 440.0, 880.0}, 4800, 16384) == 440.0);

    Standalone pedal("vital", kRate);
    Timeline held;
    held.midi(0, 0xB0, 64, 127);  // sustain on
    held.note(0, 4800, 69);       // released early, held by the pedal
    held.midi(24000, 0xB0, 64, 0);
    const auto [out, outRight] = play(pedal, held, 48000);
    CHECK(rmsDb(slice(out, 19200, 24000)) > -40.0);
    CHECK(rmsDb(slice(out, 45600, 48000)) < -90.0);
}

TEST_CASE("vital: a modulation changes the sound; the device's state holds it") {
    const auto render = [](const std::string& modulations, float amount) {
        Standalone vital("vital", kRate, {{"modulation_1_amount", amount}, {"lfo_1_frequency", 3.f}});
        if (!modulations.empty()) {
            sub::BuiltinProcessor::StateValues values{{vp::kModulations, modulations}};
            vital.processor().setState(sub::BuiltinProcessor::encodeState(values));
        }
        Timeline t;
        t.note(0, -1, 60);
        return play(vital, t, 48000).first;
    };
    const std::string toLevel = vp::writeModulations({{"lfo_1", "osc_1_level", ""}});
    const Samples plain = render("", 0.f);
    const Samples modulated = render(toLevel, 1.f);
    // An LFO on the level: the loudness swings (the plain note's doesn't).
    const auto swing = [](const Samples& x) {
        double lo = 1e9, hi = -1e9;
        for (int64_t at = 9600; at + 2400 <= 48000; at += 2400) {
            const double level = rmsDb(slice(x, at, at + 2400));
            lo = std::min(lo, level);
            hi = std::max(hi, level);
        }
        return hi - lo;
    };
    CHECK(swing(plain) < 1.0);
    CHECK(swing(modulated) > 6.0);
    // The same routing, its amount at 0: as the plain note.
    CHECK(swing(render(toLevel, 0.f)) < 1.0);

    Standalone vital("vital", kRate);
    sub::BuiltinProcessor::StateValues values{{vp::kModulations, toLevel}, {"unknown", "x"}};
    vital.processor().setState(sub::BuiltinProcessor::encodeState(values));
    CHECK(vp::readModulations(stateOf(vital.processor(), vp::kModulations)).front().source == "lfo_1");
    CHECK(stateOf(vital.processor(), "unknown").empty());  // (not one of its values)
    CHECK_THROWS_AS(vital.processor().setState(
                        sub::BuiltinProcessor::encodeState({{vp::kModulations, std::string("not json")}})),
                    std::exception);
    // (A state it couldn't read leaves it as it was.)
    CHECK(vp::readModulations(stateOf(vital.processor(), vp::kModulations)).front().source == "lfo_1");
}

TEST_CASE("vital: a new patch takes over while a note is held, the note playing on") {
    Standalone vital("vital", kRate);
    Timeline t;
    t.note(0, -1, 69);
    const auto [before, beforeRight] = play(vital, t, 9600);
    CHECK(rmsDb(slice(before, 4800, 9600)) > -40.0);
    vital.processor().setState(sub::BuiltinProcessor::encodeState(
        {{vp::kModulations, vp::writeModulations({{"env_1", "filter_1_cutoff", ""}})}}));
    const auto [after, afterRight] = play(vital, {}, 24000, 256, 9600);
    CHECK(rmsDb(slice(after, 4800, 24000)) > -40.0);  // still sounding, on the new engine
    CHECK(strongest(after, {415.3, 440.0, 466.2}, 4800, 16384) == 440.0);
    CHECK(largestStep(after) < 0.5);  // the old one faded out, not cut
}

TEST_CASE("vital: oversampling and the sample rate rebuild the engine; the pitch holds") {
    for (double rate : {44100.0, 96000.0}) {
        INFO("rate " + std::to_string(rate));
        Standalone vital("vital", rate);
        Timeline t;
        t.note(0, -1, 69);
        const auto [left, right] = play(vital, t, static_cast<int64_t>(rate));
        CHECK(strongest(left, {415.3, 440.0, 466.2}, static_cast<int64_t>(rate / 10), 16384, rate) == 440.0);
    }
    Standalone vital("vital", kRate);
    vital.set("oversampling", 2.f);  // 4x
    vital.processor().idle();        // (main thread: a new engine is built and handed over)
    Timeline t;
    t.note(0, -1, 69);
    const auto [left, right] = play(vital, t, 24000);
    CHECK(strongest(left, {415.3, 440.0, 466.2}, 4800, 16384) == 440.0);
    vital.prepare(96000.0);  // a new rate, audio stopped
    const auto [high, highRight] = play(vital, t, 48000);
    CHECK(strongest(high, {415.3, 440.0, 466.2}, 9600, 16384, 96000.0) == 440.0);
}

TEST_CASE("vital: asleep after a second of silence, exact zeros, woken by a note") {
    Standalone vital("vital", kRate);
    Timeline t;
    t.note(0, 2400, 69);
    t.note(4 * 48000, -1, 69);
    const auto [left, right] = play(vital, t, 5 * 48000);
    const Samples asleep = slice(left, 3 * 48000, 4 * 48000);
    CHECK(peak(asleep) == 0.0);
    CHECK(rmsDb(slice(left, 4 * 48000 + 2400, 5 * 48000)) > -40.0);
}

TEST_CASE("vital: a note waking it late in a block plays on, not put back to sleep") {
    // (The silence before the note-on in its block must not count toward sleeping again.)
    for (int block : {256, 1024}) {
        INFO("block " + std::to_string(block));
        Standalone vital("vital", kRate);
        Timeline t;
        t.note(0, 2400, 69);
        const int64_t on = 4 * 48000 + block - 100;
        t.note(on, on + 24000, 69);
        const auto [left, right] = play(vital, t, on + 30000, block);
        CHECK(peak(slice(left, 3 * 48000, 4 * 48000)) == 0.0);  // asleep before it
        CHECK(rmsDb(slice(left, on + 2400, on + 4800)) > -40.0);   // held: sounding throughout
        CHECK(rmsDb(slice(left, on + 19200, on + 24000)) > -40.0);
    }
}

TEST_CASE("vital: reset silences it") {
    Standalone vital("vital", kRate);
    Timeline t;
    t.note(0, -1, 69);
    play(vital, t, 4800);
    vital.processor().reset();
    const auto [left, right] = play(vital, {}, 4800);
    CHECK(rmsDb(left) < -90.0);
}

TEST_CASE("vital: presets written and read back, as Vital writes them") {
    vp::Patch patch;
    patch.params["osc_1_level"] = 0.3f;
    patch.params["polyphony"] = 4.f;
    patch.params["modulation_1_amount"] = 0.5f;
    patch.state[vp::kModulations] = vp::writeModulations({{"lfo_2", "filter_1_cutoff", ""}});
    patch.state[vp::kPresetName] = "Test Patch";
    patch.state[vp::macroKey(0)] = "Brightness";
    const std::string text = vp::writePreset(patch);
    CHECK(text.find("\"synth_version\"") != std::string::npos);
    CHECK(text.find("\"wavetables\"") != std::string::npos);  // (the init patch's, written out)

    const vp::Patch read = vp::readPreset(text);
    CHECK(read.params.size() == vp::params().size());
    CHECK_APPROX(read.params.at("osc_1_level"), 0.3f);
    CHECK(read.params.at("polyphony") == 4.f);
    CHECK_APPROX(read.params.at("filter_1_cutoff"), vp::params()[static_cast<size_t>(vp::paramIndex("filter_1_cutoff"))].defaultValue);
    const std::vector<vp::Modulation> modulations = vp::readModulations(read.state.at(vp::kModulations));
    REQUIRE(modulations.size() == 64u);  // every slot, as Vital writes them
    CHECK(modulations[0].source == "lfo_2");
    CHECK(modulations[0].destination == "filter_1_cutoff");
    CHECK(modulations[1].source.empty());
    CHECK(read.state.at(vp::kPresetName) == "Test Patch");
    CHECK(read.state.at(vp::macroKey(0)) == "Brightness");
    CHECK(read.state.count(vp::kWavetables) == 1);
    CHECK(read.state.count(vp::kLfos) == 1);

    // Loaded into a device: it plays.
    Standalone vital("vital", kRate);
    for (const auto& [id, value] : read.params) vital.set(id, value);
    vital.processor().setState(sub::BuiltinProcessor::encodeState(read.state));
    Timeline t;
    t.note(0, -1, 60);
    CHECK(rmsDb(play(vital, t, 24000).first) > -50.0);
}

TEST_CASE("vital: older presets upgraded, a file not Vital's refused") {
    // A preset as Vital 0.8 saved it: its filter model 4 had a blend 0.9 took away (and the modulations of it).
    const std::string old = R"({"synth_version": "0.8.5", "preset_name": "Old",
        "settings": {"osc_1_level": 0.5, "filter_1_model": 4.0, "filter_1_blend": 0.7, "filter_2_model": 0.0,
                     "modulation_1_amount": 0.5, "sub_on": 1.0,
                     "modulations": [{"source": "lfo_1", "destination": "filter_1_blend"}]}})";
    const vp::Patch patch = vp::readPreset(old);
    CHECK(patch.params.at("filter_1_blend") == 0.f);       // upgraded as Vital upgrades it
    CHECK(patch.params.at("modulation_1_amount") == 0.f);
    CHECK_APPROX(patch.params.at("osc_1_level"), 0.5f);
    CHECK(patch.params.count("sub_on") == 0);              // (no longer one of Vital's)
    CHECK(patch.state.at(vp::kPresetName) == "Old");
    CHECK(patch.params.size() == vp::params().size());
    // Named by its file when it has no name.
    CHECK(vp::readPreset(R"({"synth_version": "1.0.6", "settings": {}})", "From File").state.at(vp::kPresetName) ==
          "From File");
    CHECK_THROWS_AS(vp::readPreset("not json"), std::exception);
    CHECK_THROWS_AS(vp::readPreset(R"({"synth_version": "1.0.6"})"), std::exception);
    CHECK_THROWS_AS(vp::readPreset(R"([1, 2, 3])"), std::exception);
}

TEST_CASE("vital: modulation sources and destinations as Vital's engine has them") {
    const auto& sources = vp::modulationSources();
    const auto& destinations = vp::modulationDestinations();
    for (const char* s : {"env_1", "env_6", "lfo_1", "lfo_8", "random_1", "macro_control_1", "velocity", "note",
                          "mod_wheel", "pitch_wheel", "aftertouch"}) {
        INFO(s);
        CHECK(std::find(sources.begin(), sources.end(), s) != sources.end());
    }
    for (const char* d : {"osc_1_level", "filter_1_cutoff", "reverb_dry_wet", "lfo_1_frequency"}) {
        INFO(d);
        CHECK(std::find(destinations.begin(), destinations.end(), d) != destinations.end());
    }
    for (const std::string& d : destinations) CHECK(vp::paramIndex(d) >= 0);
    CHECK(vp::sourceName("lfo_3") == "LFO 3");
    CHECK(vp::sourceName("env_2") == "Envelope 2");
    CHECK(vp::sourceName("macro_control_4") == "Macro 4");
    CHECK(vp::sourceName("mod_wheel") == "Mod Wheel");
    CHECK(vp::sourceBipolar("lfo_1"));
    CHECK(!vp::sourceBipolar("env_1"));
}

TEST_CASE("vital: through the engine, a MIDI track's instrument") {
    sub::Engine engine;
    engine.setTempo(120.0);
    const uint32_t track = engine.addTrack();
    const uint32_t vital = engine.addBuiltinProcessor(engine.trackChain(track), "vital", -1);
    CHECK(engine.processorInfo(vital).acceptsMidi);
    sub::NoteDesc note;
    note.startBeat = 0.0;
    note.lengthBeats = 1.0;
    note.key = 69;
    engine.setTrackNotes(track, {note});
    const std::vector<float> out = engine.renderOffline(0.0, 48000);
    Samples left;
    for (size_t i = 0; i < out.size(); i += 2) left.push_back(out[i]);
    CHECK(rmsDb(slice(left, 4800, 20000)) > -40.0);
    CHECK(strongest(left, {415.3, 440.0, 466.2}, 2400, 16384) == 440.0);
    CHECK(engine.processorParamText(vital, vp::paramIndex("polyphony"), 8.f) == "8 voices");
}

namespace {

// A 16-bit mono WAV file of `samples` at `rate`, with a "clm " chunk if given (a wavetable file's).
void writeWavetableFile(const std::filesystem::path& path, const Samples& samples, int rate, const std::string& clm = {}) {
    std::vector<uint8_t> bytes;
    const auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    const auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    const auto tag = [&](const char* t) { bytes.insert(bytes.end(), t, t + 4); };
    const uint32_t data = static_cast<uint32_t>(samples.size() * 2);
    const uint32_t clmSize = static_cast<uint32_t>(clm.size());
    tag("RIFF");
    u32(4 + 8 + 16 + (clm.empty() ? 0 : 8 + clmSize + (clmSize & 1)) + 8 + data);
    tag("WAVE");
    tag("fmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate * 2));
    u16(2);
    u16(16);
    if (!clm.empty()) {
        tag("clm ");
        u32(clmSize);
        bytes.insert(bytes.end(), clm.begin(), clm.end());
        if (clmSize & 1) bytes.push_back(0);
    }
    tag("data");
    u32(data);
    for (float s : samples) u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(s, -1.f, 1.f) * 32767.f))));
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// What a Vital with these state values plays for an A4 (left).
Samples playA4(const sub::BuiltinProcessor::StateValues& state, const ParamValues& values = {}) {
    Standalone vital("vital", kRate, values);
    vital.processor().setState(sub::BuiltinProcessor::encodeState(state));
    Timeline t;
    t.note(0, -1, 69);
    return play(vital, t, 24000).first;
}

// The level of the A4's harmonics 2 to 6 against its fundamental, in dB.
double harmonicsDb(const Samples& x) {
    double harmonics = 0.0;
    for (int h = 2; h <= 6; ++h) harmonics = std::max(harmonics, windowedAmplitude(x, 440.0 * h, 4800, 16384));
    return db(harmonics / windowedAmplitude(x, 440.0, 4800, 16384));
}

}  // namespace

TEST_CASE("vital: wavetables of basic shapes, from audio files and Vital's own files") {
    // A sine: no harmonics to speak of; the init wavetable (a saw): strong ones.
    const std::string sine = vp::withWavetable("", 0, vp::wavetableOfShape(vp::Shape::Sine));
    CHECK(harmonicsDb(playA4({{vp::kWavetables, sine}})) < -40.0);
    CHECK(harmonicsDb(playA4({})) > -12.0);
    CHECK((vp::wavetableNames(sine) == std::vector<std::string>{"Sine", "Init", "Init"}));
    CHECK((vp::wavetableNames("") == std::vector<std::string>{"Init", "Init", "Init"}));
    CHECK_THROWS_AS(vp::withWavetable("", 3, vp::wavetableOfShape(vp::Shape::Sine)), std::exception);

    // A wavetable file: four frames of 2048 samples, sines (Serum's and Vital's "clm " chunk, no blending).
    Samples frames;
    for (int f = 0; f < 4; ++f)
        for (int i = 0; i < 2048; ++i) frames.push_back(0.8f * static_cast<float>(std::sin(2.0 * kPi * i / 2048.0)));
    const std::filesystem::path file = tempDir() / "Smooth.wav";
    writeWavetableFile(file, frames, 44100, "<!>2048 00000000 wavetable (vital.audio)");
    const std::string fromFile = vp::wavetableFromAudioFile(file.string(), vp::WavetableStyle::Splice);
    const std::string loaded = vp::withWavetable("", 0, fromFile);
    CHECK(vp::wavetableNames(loaded).front() == "Smooth");
    CHECK(harmonicsDb(playA4({{vp::kWavetables, loaded}})) < -30.0);
    CHECK(strongest(playA4({{vp::kWavetables, loaded}}), {415.3, 440.0, 466.2, 880.0}, 4800, 16384) == 440.0);
    // Vocoded and pitched: a pitched sound followed through, each frame two of its cycles (as Vital loads
    // them): a pure tone plays pure, an octave over the note.
    Samples sung;
    for (int i = 0; i < 44100; ++i) sung.push_back(0.5f * static_cast<float>(std::sin(2.0 * kPi * 220.0 * i / 44100.0)));
    const std::filesystem::path voice = tempDir() / "Voice.wav";
    writeWavetableFile(voice, sung, 44100);
    for (vp::WavetableStyle style : {vp::WavetableStyle::Vocode, vp::WavetableStyle::Pitch}) {
        const std::string table = vp::withWavetable("", 0, vp::wavetableFromAudioFile(voice.string(), style));
        const Samples x = playA4({{vp::kWavetables, table}});
        CHECK(strongest(x, {415.3, 440.0, 466.2, 880.0, 1320.0}, 4800, 16384) == 880.0);
        CHECK(windowedAmplitude(x, 440.0, 4800, 16384) < 0.01 * windowedAmplitude(x, 880.0, 4800, 16384));
    }
    CHECK_THROWS_AS(vp::wavetableFromAudioFile((tempDir() / "missing.wav").string(), vp::WavetableStyle::Splice),
                    std::exception);

    // Vital's own .vitaltable (a WavetableCreator's JSON), named after its file.
    const std::string own = vp::wavetableFromVitalTable(fromFile, "Renamed");
    CHECK(vp::wavetableNames(vp::withWavetable("", 2, own))[2] == "Renamed");
    CHECK_THROWS_AS(vp::wavetableFromVitalTable("{}", "x"), std::exception);
    CHECK_THROWS_AS(vp::wavetableFromVitalTable("not json", "x"), std::exception);
}

TEST_CASE("vital: an LFO's shape set, and heard") {
    const auto lfoOnLevel = [](const std::string& lfos) {
        sub::BuiltinProcessor::StateValues state{{vp::kModulations, vp::writeModulations({{"lfo_1", "osc_1_level", ""}})}};
        if (!lfos.empty()) state[vp::kLfos] = lfos;
        return playA4(state, {{"modulation_1_amount", 1.f}, {"lfo_1_frequency", 1.f}});
    };
    const Samples triangle = lfoOnLevel("");
    const Samples square = lfoOnLevel(vp::withLfo("", 0, vp::lfoOfShape(vp::Shape::Square)));
    // The same rate, another shape: another sound.
    double difference = 0.0;
    for (size_t i = 2400; i < triangle.size(); ++i)
        difference = std::max(difference, static_cast<double>(std::fabs(triangle[i] - square[i])));
    CHECK(difference > 0.05);
    CHECK(vp::lfoNames(vp::withLfo("", 3, vp::lfoOfShape(vp::Shape::SawUp)))[3] == "Saw Up");
    CHECK(vp::lfoNames("")[0] == "Triangle");
    CHECK(vp::shapeName(vp::Shape::SawDown) == "Saw Down");
}

namespace {

// Display `id`'s values since the start.
std::vector<float> displayOf(sub::Processor& processor, const std::string& id) {
    const std::vector<sub::DisplayInfo> displays = processor.displays();
    for (size_t i = 0; i < displays.size(); ++i) {
        if (displays[i].id == id) {
            std::vector<float> values;
            processor.readDisplay(static_cast<int>(i), 0, values);
            return values;
        }
    }
    INFO(id);
    REQUIRE(false);
    return {};
}

}  // namespace

TEST_CASE("vital: what its editor draws: the wavetables' frames, the LFOs' shapes") {
    // Vital's init wavetable: a frame or more, each a cycle within -1..1, not silent.
    const std::vector<float> init = vp::wavetableFrames("", 0, 128);
    REQUIRE(!init.empty());
    CHECK(init.size() % 128 == 0);
    double loudest = 0.0;
    for (float v : init) {
        CHECK(std::fabs(v) <= 1.f);
        loudest = std::max(loudest, static_cast<double>(std::fabs(v)));
    }
    CHECK(loudest > 0.5);
    // A square, set on oscillator 2: one frame, high then low.
    const std::string squares = vp::withWavetable("", 1, vp::wavetableOfShape(vp::Shape::Square));
    const std::vector<float> square = vp::wavetableFrames(squares, 1, 64);
    REQUIRE(square.size() == 64);
    CHECK(square[16] > 0.5f);
    CHECK(square[48] < -0.5f);
    CHECK(vp::wavetableFrames(squares, 0, 64) != square);  // (oscillator 1's is still the init one)
    CHECK(vp::wavetableFrames("", 3, 64).empty());         // (there are three)

    // The LFOs: Vital's triangle where none is set (three points, up then down), a saw set.
    const vp::LfoShape triangle = vp::lfoShape("", 0, 101);
    REQUIRE(triangle.values.size() == 101);
    CHECK(triangle.values[0] < 0.01f);
    CHECK(triangle.values[50] > 0.99f);
    CHECK(triangle.values[100] < 0.01f);
    CHECK(triangle.points.size() == 6);
    CHECK(triangle.points[3] == 1.f);  // (the middle point at the top: y runs up)
    const vp::LfoShape saw = vp::lfoShape(vp::withLfo("", 2, vp::lfoOfShape(vp::Shape::SawUp)), 2, 11);
    CHECK(saw.values[2] < saw.values[8]);
}

TEST_CASE("vital: its displays: the output, the voices, the LFOs' and envelopes' positions") {
    Standalone vital("vital", kRate);
    sub::Processor& processor = vital.processor();
    // Nothing playing: no voice, the positions -1.
    play(vital, {}, 4800);
    const std::vector<float> idle = displayOf(processor, "env_1");
    REQUIRE(!idle.empty());
    CHECK(idle.back() == -1.f);
    CHECK(displayOf(processor, "voices").back() == 0.f);

    Standalone held("vital", kRate);
    Timeline t;
    t.note(0, -1, 69);
    const auto [left, right] = play(held, t, 4800);
    sub::Processor& p = held.processor();
    const std::vector<float> output = displayOf(p, "output");
    CHECK(output.size() == 4800);  // a value a sample
    CHECK(std::fabs(output[3000] - 0.5f * (left[3000] + right[3000])) < 1e-6f);
    const std::vector<float> voices = displayOf(p, "voices");
    CHECK(voices.size() == 4800 / 256);
    CHECK(voices.back() == 1.f);
    // The amplitude envelope past its attack (the init patch's is short), sustaining: its stage, 4 (decay), and
    // its level.
    const float phase = displayOf(p, "env_1").back();
    CHECK(phase >= 4.f);
    CHECK(phase < 5.f);
    const float level = displayOf(p, "env_1_value").back();
    CHECK(level > 0.f);
    CHECK(level <= 1.f);
    // LFO 1 runs whether or not anything is modulated by it: a phase within its cycle.
    const float lfo = displayOf(p, "lfo_1").back();
    CHECK(lfo >= 0.f);
    CHECK(lfo <= 1.f);
}
