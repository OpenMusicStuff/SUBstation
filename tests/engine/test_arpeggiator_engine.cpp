// The built-in Arpeggiator, a MIDI effect: the notes its chain's instrument plays (whatever its place in the
// chain), its styles, octaves, gate, swing, free rate, hold, and switching it off.

#include <algorithm>
#include <cmath>
#include <map>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

enum { WAVE, ATTACK, DECAY, SUSTAIN, RELEASE, CUTOFF };
constexpr int64_t kSixteenth = kBeat / 4;  // at the engine's 120 BPM
const std::vector<int> kChord{60, 64, 67};

double keyFreq(int key) { return 440.0 * std::pow(2.0, (key - 69) / 12.0); }

// A track whose Synth (a sine, quick to start and stop) plays `notes` through an Arpeggiator with `values`,
// put after the synth in the chain: (engine's render of `seconds`, left channel).
struct Arp {
    sub::Engine engine;
    uint32_t track = 0, synth = 0, arp = 0;

    Arp(const std::vector<sub::NoteDesc>& notes, const std::map<std::string, float>& values = {}) {
        track = engine.addTrack();
        synth = engine.addBuiltinProcessor(engine.trackChain(track), "synth", -1);
        for (const auto& [index, value] :
             std::map<int, float>{{WAVE, 0.f}, {ATTACK, 1.f}, {RELEASE, 1.f}, {SUSTAIN, 100.f}, {CUTOFF, 20000.f}})
            engine.setProcessorParam(synth, index, value);
        arp = engine.addBuiltinProcessor(engine.trackChain(track), "arpeggiator", -1);
        for (const auto& [name, value] : values) setParam(engine, arp, name, value);
        engine.setTrackNotes(track, notes);
    }
    Samples render(double seconds = 1.0) { return channel(engine.renderOffline(0.0, seconds * kSampleRate), 0); }
};

std::vector<sub::NoteDesc> chord(double beats = 1.0) {
    std::vector<sub::NoteDesc> notes;
    for (const int key : kChord) notes.push_back({0.0, beats, key});
    return notes;
}

// The pitch heard over [from, from + length).
double pitchAt(const Samples& x, int64_t from, int64_t length = 2048) { return dominantFreq(slice(x, from, from + length)); }

void checkKeys(const Samples& out, const std::vector<int>& keys, int64_t step = kSixteenth) {
    for (size_t k = 0; k < keys.size(); ++k) {
        INFO("step " + std::to_string(k));
        CHECK_APPROX_REL(pitchAt(out, static_cast<int64_t>(k) * step + 300), keyFreq(keys[k]), 0.01);
    }
}

}  // namespace

TEST_CASE("the arpeggiator is listed as a MIDI effect") {
    const sub::BuiltinInfo info = builtinInfo("arpeggiator");
    CHECK_EQ(info.name, std::string("Arpeggiator"));
    CHECK(info.isMidiEffect());
    CHECK(!info.isInstrument());
    // MIDI effects come after the instruments, before the audio effects.
    const auto& all = sub::BuiltinRegistry::instance().devices();
    const auto at = [&](const std::string& id) {
        return std::find_if(all.begin(), all.end(), [&](const sub::BuiltinInfo& i) { return i.id == id; }) - all.begin();
    };
    CHECK(at("synth") < at("arpeggiator"));
    CHECK(at("arpeggiator") < at("delay"));
}

TEST_CASE("up plays the chord a note at a time, a sixteenth each, half a step long") {
    Arp a(chord());
    const Samples out = a.render();
    checkKeys(out, {60, 64, 67, 60});
    for (int k = 0; k < 4; ++k) {
        INFO("step " + std::to_string(k));
        CHECK(rms(slice(out, k * kSixteenth + 200, k * kSixteenth + 2800)) > 0.05);   // the gate's half
        CHECK(rms(slice(out, k * kSixteenth + 3300, (k + 1) * kSixteenth - 50)) < 1e-3);  // then quiet
    }
    CHECK(rms(slice(out, kBeat + 500, kBeat + 4000)) < 1e-3);  // the chord let go: no more steps
}

TEST_CASE("down over two octaves") {
    Arp a(chord(2.0), {{"style", 1.f}, {"octaves", 2.f}});
    checkKeys(a.render(), {79, 76, 72, 67, 64, 60, 79});
}

TEST_CASE("up-down doesn't repeat its ends") {
    Arp a(chord(2.0), {{"style", 2.f}});
    checkKeys(a.render(), {60, 64, 67, 64, 60, 64});
}

TEST_CASE("converge: lowest, highest, then inwards") {
    Arp a(chord(2.0), {{"style", 4.f}, {"octaves", 2.f}});
    checkKeys(a.render(), {60, 79, 64, 76, 67, 72});
}

TEST_CASE("chord: all at once, an octave up each step") {
    Arp a(chord(2.0), {{"style", 8.f}, {"octaves", 2.f}});
    const Samples out = a.render();
    for (const int key : kChord) {
        INFO("key " + std::to_string(key));
        CHECK(toneAmplitude(out, keyFreq(key), 200, 2400) > 0.05);
        CHECK(toneAmplitude(out, keyFreq(key + 12), kSixteenth + 200, 2400) > 0.05);
    }
}

TEST_CASE("a long gate overlaps the steps") {
    Arp a(chord(), {{"gate", 150.f}});
    const Samples out = a.render();
    CHECK(toneAmplitude(out, keyFreq(60), kSixteenth + 300, 2400) > 0.05);  // still sounding
    CHECK(toneAmplitude(out, keyFreq(64), kSixteenth + 300, 2400) > 0.05);
}

TEST_CASE("swing puts the odd steps late") {
    Arp a(chord(), {{"swing", 100.f}});
    const Samples out = a.render();
    CHECK(rms(slice(out, kSixteenth + 100, kSixteenth + kSixteenth / 2 - 100)) < 1e-3);  // half a step late
    CHECK_APPROX_REL(pitchAt(out, kSixteenth + kSixteenth / 2 + 300), keyFreq(64), 0.01);
    CHECK_APPROX_REL(pitchAt(out, 2 * kSixteenth + 300), keyFreq(67), 0.01);  // the even ones on time
}

TEST_CASE("a free rate, in Hz") {
    Arp a(chord(), {{"sync", 0.f}, {"rate", 10.f}});
    checkKeys(a.render(), {60, 64, 67, 60}, kSampleRate / 10);
}

TEST_CASE("hold keeps playing after the notes are let go") {
    Arp a(chord(0.25), {{"hold", 1.f}});
    const Samples out = a.render();
    checkKeys(out, {60, 64, 67, 60, 64, 67, 60});  // on past the notes' quarter of a beat
}

TEST_CASE("the arpeggiator switched off passes the notes as they are") {
    Arp a(chord());
    a.engine.setProcessorEnabled(a.arp, false);
    const Samples out = a.render();
    for (const int key : kChord) {
        INFO("key " + std::to_string(key));
        CHECK(toneAmplitude(out, keyFreq(key), 300, 4096) > 0.05);  // the chord, all at once
    }
}

TEST_CASE("an arpeggiator in a rack's chain plays that chain's instrument") {
    Arp a(chord());
    // The synth and the arpeggiator in a rack's chain instead.
    sub::Engine& engine = a.engine;
    engine.removeProcessor(a.arp);
    engine.removeProcessor(a.synth);
    const uint32_t rack = engine.addRack(engine.trackChain(a.track), -1);
    const uint32_t chain = engine.addRackChain(rack, -1);
    const uint32_t synth = engine.addBuiltinProcessor(chain, "synth", -1);
    for (const auto& [index, value] :
         std::map<int, float>{{WAVE, 0.f}, {ATTACK, 1.f}, {RELEASE, 1.f}, {SUSTAIN, 100.f}, {CUTOFF, 20000.f}})
        engine.setProcessorParam(synth, index, value);
    engine.addBuiltinProcessor(chain, "arpeggiator", -1);
    checkKeys(a.render(), {60, 64, 67, 60});
}
