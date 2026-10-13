// The built-in Hue (a spectral pitch quantizer): its listing; its latency, to the sample;
// levels passed through untouched with no notes or all twelve; notes kept and boosted, notes taken
// out, notes a semitone off moved onto the enabled ones; MIDI notes in place of the buttons;
// changing the notes without a click; and other rates, mono and silence. High Quality: its latency,
// the sound passed with no notes, and a moved note landing on the enabled one's pitch exactly. Its
// editor's displays: the input and output a sample a value, the notes in use (the held ones from MIDI). Auto:
// the song key's scale, the buttons without a key or with Auto off.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "builtin/BuiltinRegistry.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"
#include "harness/Test.h"

using namespace subtest;

namespace {

constexpr double kRate = 48000.0;

const sub::BuiltinInfo& hueInfo() {
    for (const auto& info : sub::BuiltinRegistry::instance().devices())
        if (info.id == "hue") return info;
    REQUIRE(false);
    return sub::BuiltinRegistry::instance().devices().front();
}

// A tone's level after the device, in dB re its level going in, measured over the steady second half.
double toneGainDb(Standalone& device, double freq, double seconds = 2.0) {
    const Samples in = sine(freq, seconds, 0.25);
    const Samples out = device.play(in);
    const auto n = static_cast<int64_t>(in.size());
    const int64_t from = n / 2, length = n / 4;
    return db(windowedAmplitude(out, freq, from, length) / windowedAmplitude(in, freq, from, length));
}

}  // namespace

TEST_CASE("hue: listing") {
    const sub::BuiltinInfo& info = hueInfo();
    CHECK_EQ(info.name, std::string("Hue"));
    CHECK(!info.isInstrument());
    std::vector<std::string> ids;
    for (const auto& p : info.params) ids.push_back(p.id);
    CHECK(ids == (std::vector<std::string>{"start", "end", "color", "wet_gain", "pre_shift", "post_shift", "mute_low",
                                           "mute_high", "quality", "midi", "auto", "note_c", "note_cs", "note_d", "note_ds", "note_e",
                                           "note_f", "note_fs", "note_g", "note_gs", "note_a", "note_as", "note_b"}));
    for (const auto& p : info.params) {
        INFO(p.id);
        if (p.id.rfind("note_", 0) == 0) CHECK_EQ(p.defaultValue, 0.f);  // no notes: the sound passes
    }
}

TEST_CASE("hue: latency, to the sample") {
    // With no notes the band split is all there is. Noise comes out lined up with what went in, the latency
    // taken away (within a few samples: the crossovers' all-pass phase delays the lows a little more).
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    for (const float quality : {0.f, 1.f}) {
        INFO(std::to_string(rate) + (quality > 0 ? " high quality" : " low latency"));
        Standalone hue("hue", rate, {{"quality", quality}});
        const int latency = hue.processor().latencySamples();
        CHECK(latency > 0);
        const Samples in = noise(static_cast<size_t>(rate), 3, 0.25f);
        Samples padded = in;  // (and silence after, for it all to come out)
        padded.resize(in.size() + static_cast<size_t>(latency) + 4096, 0.f);
        const Samples out = hue.play(padded);
        const int reach = static_cast<int>(16 * rate / 48000.0);
        double best = 0;
        int lag = 0;
        for (int l = -reach - 50; l <= reach + 50; ++l) {
            double sum = 0;
            for (size_t i = 4096; i + 4096 < in.size(); i += 3)
                sum += static_cast<double>(in[i]) * out[static_cast<size_t>(static_cast<int64_t>(i) + latency + l)];
            if (sum > best) {
                best = sum;
                lag = l;
            }
        }
        CHECK(std::abs(lag) <= reach);
    }
}

TEST_CASE("hue: no notes, or all twelve, pass the sound") {
    for (const float quality : {0.f, 1.f})
    for (const bool all : {false, true}) {
        INFO(std::string(all ? "all twelve" : "none") + (quality > 0 ? ", high quality" : ""));
        Standalone hue("hue", kRate, {{"quality", quality}});
        if (all)
            for (const char* id : {"note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f", "note_fs", "note_g",
                                   "note_gs", "note_a", "note_as", "note_b"})
                hue.set(id, 1.f);
        for (const double freq : {60.0, 220.0, 466.2, 1000.0, 3000.0, 9000.0}) {
            INFO(std::to_string(freq));
            CHECK(std::abs(toneGainDb(hue, freq)) < 0.3);
        }
    }
}

TEST_CASE("hue: notes kept, taken out and moved") {
    Standalone hue("hue", kRate, {{"note_a", 1.f}});
    CHECK(toneGainDb(hue, 440.0) > 6.0);          // A, the only note: louder (the gain of a set of one)
    CHECK(toneGainDb(hue, 392.0) < -15.0);        // G, two semitones away: taken out
    CHECK(toneGainDb(hue, 523.25) < -20.0);       // C
    CHECK(std::abs(toneGainDb(hue, 60.0)) < 1.0); // below the range: the low band, as it was
    // A#, a semitone above: moved down onto A (its strongest line within a few hertz of 440)
    const Samples out = hue.play(sine(466.16, 3.0, 0.25));
    const Samples steady = slice(out, 48000, 48000 * 3);
    CHECK(std::abs(dominantFreq(steady) - 440.0) < 6.0);
}

TEST_CASE("hue: High Quality moves a note onto the enabled one's pitch") {
    // each class through its own pitch-synchronous shifter: A# lands on 440 Hz itself, its octave on 880
    Standalone hue("hue", kRate, {{"quality", 1.f}, {"note_a", 1.f}});
    for (const double freq : {466.16, 932.33}) {
        INFO(std::to_string(freq));
        const Samples out = hue.play(sine(freq, 3.0, 0.25));
        const Samples steady = slice(out, 48000, 48000 * 3);
        CHECK(std::abs(dominantFreq(steady) - freq * 440.0 / 466.16) < 1.5);
    }
}

TEST_CASE("hue: MIDI notes in place of the buttons") {
    Standalone hue("hue", kRate, {{"midi", 1.f}, {"note_c", 1.f}});
    // the held A3 (57) makes the set {A}; the C button is ignored while MIDI is on
    std::vector<sub::ProcessEvent> events{sub::ProcessEvent::noteOn(0, 57, 100)};
    Samples in = sine(440.0, 2.0, 0.25);
    hue.run({&in}, {}, 256, [&](int64_t start, int) {
        hue.context().inEvents = start == 0 ? sub::EventList{events.data(), events.size()} : sub::EventList{};
    });
    const Samples dry = sine(440.0, 2.0, 0.25);
    CHECK(db(windowedAmplitude(in, 440.0, 48000, 24000) / windowedAmplitude(dry, 440.0, 48000, 24000)) > 6.0);

    // nothing yet: the key's notes (A, kept and louder); MIDI Notes off: the key's whatever is held
    Standalone none("hue", kRate, {{"note_a", 1.f}});
    CHECK(toneGainDb(none, 440.0) > 6.0);
    Standalone off("hue", kRate, {{"midi", 0.f}, {"note_e", 1.f}});
    Samples a = sine(440.0, 2.0, 0.25);
    off.run({&a}, {}, 256, [&](int64_t start, int) {
        off.context().inEvents = start == 0 ? sub::EventList{events.data(), events.size()} : sub::EventList{};
    });
    CHECK(db(windowedAmplitude(a, 440.0, 48000, 24000) / windowedAmplitude(dry, 440.0, 48000, 24000)) < -20.0);

    // a chord held until the next: A released, the notes stay {A}
    Standalone latched("hue", kRate, {{"note_e", 1.f}});
    std::vector<sub::ProcessEvent> pressed{sub::ProcessEvent::noteOn(0, 57, 100)};
    std::vector<sub::ProcessEvent> released{sub::ProcessEvent::noteOff(0, 57)};
    Samples b = sine(440.0, 2.0, 0.25);
    latched.run({&b}, {}, 256, [&](int64_t start, int) {
        latched.context().inEvents = start == 0       ? sub::EventList{pressed.data(), pressed.size()}
                                     : start == 2560 ? sub::EventList{released.data(), released.size()}
                                                      : sub::EventList{};
    });
    CHECK(db(windowedAmplitude(b, 440.0, 72000, 24000) / windowedAmplitude(dry, 440.0, 72000, 24000)) > 6.0);
}

TEST_CASE("hue: changing the notes does not click") {
    Standalone hue("hue", kRate, {{"note_a", 1.f}});
    const Samples in = sine(220.0, 3.0, 0.25);
    const Samples steady = hue.play(in);
    const double before = largestStep(steady, 48000, 96000);
    Standalone changing("hue", kRate, {{"note_a", 1.f}});
    const Samples out = changing.play(in, {{72000, "note_e", 1.f, true}, {96000, "note_a", 0.f, true}});
    CHECK(largestStep(out, 48000, 140000) < 3 * before + 0.01);
}

TEST_CASE("hue: mono, silence and extremes") {
    Standalone hue("hue", kRate, {{"note_a", 1.f}, {"note_e", 1.f}, {"color", 200.f}});
    const Samples out = hue.play(noise(48000, 7, 0.5f));
    for (const float v : out) REQUIRE(std::isfinite(v));
    // silence after: it rings out to (almost) nothing
    const Samples quiet = hue.play(Samples(96000, 0.f));
    CHECK(maxAbs(slice(quiet, 72000)) < 1e-4);
}

TEST_CASE("hue: Quality switched while playing") {
    // the same latency either way; the one mode's moved notes crossfade into the other's, without a click
    Standalone hue("hue", kRate, {{"note_a", 1.f}});
    const int latency = hue.processor().latencySamples();
    const Samples in = sine(466.16, 3.0, 0.25);  // A#: moved
    const double before = largestStep(hue.play(in), 48000, 96000);
    Standalone switching("hue", kRate, {{"note_a", 1.f}});
    const Samples out = switching.play(in, {{72000, "quality", 1.f, true}, {108000, "quality", 0.f, true}});
    for (const float v : out) REQUIRE(std::isfinite(v));
    CHECK(largestStep(out, 48000, 140000) < 3 * before + 0.01);
    CHECK_EQ(switching.processor().latencySamples(), latency);
    CHECK(!switching.processor().idle());
}

TEST_CASE("hue: its editor's displays") {
    Standalone hue("hue", kRate, {{"note_a", 1.f}, {"note_e", 1.f}});
    std::vector<std::pair<std::string, int>> displays;
    for (const auto& d : hue.processor().displays()) displays.emplace_back(d.id, d.samplesPerValue);
    CHECK(displays == (std::vector<std::pair<std::string, int>>{{"input", 1}, {"output", 1}, {"notes", 1024}}));
    const Samples in = sine(440.0, 0.15, 0.25);  // (within what a display keeps: 8192 values)
    const Samples out = hue.play(in);
    const std::vector<float> shownIn = hue.display("input"), shownOut = hue.display("output");
    REQUIRE(shownIn.size() == in.size());
    REQUIRE(shownOut.size() == out.size());
    for (const size_t i : {size_t(0), size_t(1000), in.size() - 1}) {
        CHECK_NEAR(shownIn[i], in[i], 1e-6);  // (one channel: as it is)
        CHECK_NEAR(shownOut[i], out[i], 1e-6);
    }
    const std::vector<float> notes = hue.display("notes");
    REQUIRE(notes.size() == in.size() / 1024);
    CHECK_EQ(static_cast<int>(notes.back()), (1 << 9) | (1 << 4));

    // MIDI: the notes held
    Standalone midi("hue", kRate, {});
    std::vector<sub::ProcessEvent> events{sub::ProcessEvent::noteOn(0, 60, 100), sub::ProcessEvent::noteOn(0, 67, 100)};
    Samples tone = sine(262.0, 0.2, 0.25);
    midi.run({&tone}, {}, 256, [&](int64_t start, int) {
        midi.context().inEvents = start == 0 ? sub::EventList{events.data(), events.size()} : sub::EventList{};
    });
    const std::vector<float> held = midi.display("notes");
    REQUIRE(!held.empty());
    CHECK_EQ(static_cast<int>(held.back()), (1 << 0) | (1 << 7) | (1 << 12));  // (and: MIDI's)
}

TEST_CASE("hue: Auto follows the song's key") {
    // A minor: A kept, A# (out of the key) moved away (onto A); the display says the notes are the key's
    Standalone hue("hue", kRate, {});
    hue.context().keyTonic = 9;
    hue.context().keyMinor = true;
    CHECK(toneGainDb(hue, 440.0) > -2.0);
    const std::vector<float> notes = hue.display("notes");
    REQUIRE(!notes.empty());
    const int aMinor = (1 << 9) | (1 << 11) | (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7);
    CHECK_EQ(static_cast<int>(notes.back()), aMinor | (1 << 13));
    Standalone moved("hue", kRate, {});
    moved.context().keyTonic = 9;
    moved.context().keyMinor = true;
    CHECK(toneGainDb(moved, 466.16) < -10.0);
    // A major: C# in, C out (moved onto C#... or B: either way away)
    Standalone major("hue", kRate, {});
    major.context().keyTonic = 9;
    CHECK(toneGainDb(major, 523.25) < -10.0);  // (C5: well inside the band)

    // no key known, or Auto off: the buttons (none: the sound passes)
    Standalone none("hue", kRate, {});
    CHECK(std::abs(toneGainDb(none, 440.0)) < 0.3);
    Standalone off("hue", kRate, {{"auto", 0.f}});
    off.context().keyTonic = 9;
    off.context().keyMinor = true;
    CHECK(std::abs(toneGainDb(off, 466.16)) < 0.3);
}
