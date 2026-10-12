// The built-in Prisma (a spectral pitch quantizer): its listing; its latency, to the sample;
// levels passed through untouched with no notes or all twelve; notes kept and boosted, notes taken
// out, notes a semitone off moved onto the enabled ones; MIDI notes in place of the buttons;
// changing the notes without a click; and other rates, mono and silence. High Quality: its latency,
// the sound passed with no notes, and a moved note landing on the enabled one's pitch exactly.

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

const sub::BuiltinInfo& prismaInfo() {
    for (const auto& info : sub::BuiltinRegistry::instance().devices())
        if (info.id == "prisma") return info;
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

TEST_CASE("prisma: listing") {
    const sub::BuiltinInfo& info = prismaInfo();
    CHECK_EQ(info.name, std::string("Prisma"));
    CHECK(!info.isInstrument());
    std::vector<std::string> ids;
    for (const auto& p : info.params) ids.push_back(p.id);
    CHECK(ids == (std::vector<std::string>{"start", "end", "color", "wet_gain", "pre_shift", "post_shift", "mute_low",
                                           "mute_high", "quality", "midi", "note_c", "note_cs", "note_d", "note_ds", "note_e",
                                           "note_f", "note_fs", "note_g", "note_gs", "note_a", "note_as", "note_b"}));
    for (const auto& p : info.params) {
        INFO(p.id);
        if (p.id.rfind("note_", 0) == 0) CHECK_EQ(p.defaultValue, 0.f);  // no notes: the sound passes
    }
}

TEST_CASE("prisma: latency, to the sample") {
    // With no notes the band split is all there is. Noise comes out lined up with what went in, the latency
    // taken away (within a few samples: the crossovers' all-pass phase delays the lows a little more).
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    for (const float quality : {0.f, 1.f}) {
        INFO(std::to_string(rate) + (quality > 0 ? " high quality" : " low latency"));
        Standalone prisma("prisma", rate, {{"quality", quality}});
        const int latency = prisma.processor().latencySamples();
        CHECK(latency > 0);
        const Samples in = noise(static_cast<size_t>(rate), 3, 0.25f);
        Samples padded = in;  // (and silence after, for it all to come out)
        padded.resize(in.size() + static_cast<size_t>(latency) + 4096, 0.f);
        const Samples out = prisma.play(padded);
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

TEST_CASE("prisma: no notes, or all twelve, pass the sound") {
    for (const float quality : {0.f, 1.f})
    for (const bool all : {false, true}) {
        INFO(std::string(all ? "all twelve" : "none") + (quality > 0 ? ", high quality" : ""));
        Standalone prisma("prisma", kRate, {{"quality", quality}});
        if (all)
            for (const char* id : {"note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f", "note_fs", "note_g",
                                   "note_gs", "note_a", "note_as", "note_b"})
                prisma.set(id, 1.f);
        for (const double freq : {60.0, 220.0, 466.2, 1000.0, 3000.0, 9000.0}) {
            INFO(std::to_string(freq));
            CHECK(std::abs(toneGainDb(prisma, freq)) < 0.3);
        }
    }
}

TEST_CASE("prisma: notes kept, taken out and moved") {
    Standalone prisma("prisma", kRate, {{"note_a", 1.f}});
    CHECK(toneGainDb(prisma, 440.0) > 6.0);          // A, the only note: louder (the gain of a set of one)
    CHECK(toneGainDb(prisma, 392.0) < -15.0);        // G, two semitones away: taken out
    CHECK(toneGainDb(prisma, 523.25) < -20.0);       // C
    CHECK(std::abs(toneGainDb(prisma, 60.0)) < 1.0); // below the range: the low band, as it was
    // A#, a semitone above: moved down onto A (its strongest line within a few hertz of 440)
    const Samples out = prisma.play(sine(466.16, 3.0, 0.25));
    const Samples steady = slice(out, 48000, 48000 * 3);
    CHECK(std::abs(dominantFreq(steady) - 440.0) < 6.0);
}

TEST_CASE("prisma: High Quality moves a note onto the enabled one's pitch") {
    // each class through its own pitch-synchronous shifter: A# lands on 440 Hz itself, its octave on 880
    Standalone prisma("prisma", kRate, {{"quality", 1.f}, {"note_a", 1.f}});
    for (const double freq : {466.16, 932.33}) {
        INFO(std::to_string(freq));
        const Samples out = prisma.play(sine(freq, 3.0, 0.25));
        const Samples steady = slice(out, 48000, 48000 * 3);
        CHECK(std::abs(dominantFreq(steady) - freq * 440.0 / 466.16) < 1.5);
    }
}

TEST_CASE("prisma: MIDI notes in place of the buttons") {
    Standalone prisma("prisma", kRate, {{"midi", 1.f}, {"note_c", 1.f}});
    // the held A3 (57) makes the set {A}; the C button is ignored while MIDI is on
    std::vector<sub::ProcessEvent> events{sub::ProcessEvent::noteOn(0, 57, 100)};
    Samples in = sine(440.0, 2.0, 0.25);
    prisma.run({&in}, {}, 256, [&](int64_t start, int) {
        prisma.context().inEvents = start == 0 ? sub::EventList{events.data(), events.size()} : sub::EventList{};
    });
    const Samples dry = sine(440.0, 2.0, 0.25);
    CHECK(db(windowedAmplitude(in, 440.0, 48000, 24000) / windowedAmplitude(dry, 440.0, 48000, 24000)) > 6.0);

    // nothing held: the sound passes
    Standalone none("prisma", kRate, {{"midi", 1.f}, {"note_a", 1.f}});
    CHECK(std::abs(toneGainDb(none, 440.0)) < 0.3);
}

TEST_CASE("prisma: changing the notes does not click") {
    Standalone prisma("prisma", kRate, {{"note_a", 1.f}});
    const Samples in = sine(220.0, 3.0, 0.25);
    const Samples steady = prisma.play(in);
    const double before = largestStep(steady, 48000, 96000);
    Standalone changing("prisma", kRate, {{"note_a", 1.f}});
    const Samples out = changing.play(in, {{72000, "note_e", 1.f, true}, {96000, "note_a", 0.f, true}});
    CHECK(largestStep(out, 48000, 140000) < 3 * before + 0.01);
}

TEST_CASE("prisma: mono, silence and extremes") {
    Standalone prisma("prisma", kRate, {{"note_a", 1.f}, {"note_e", 1.f}, {"color", 200.f}});
    const Samples out = prisma.play(noise(48000, 7, 0.5f));
    for (const float v : out) REQUIRE(std::isfinite(v));
    // silence after: it rings out to (almost) nothing
    const Samples quiet = prisma.play(Samples(96000, 0.f));
    CHECK(maxAbs(slice(quiet, 72000)) < 1e-4);
}

TEST_CASE("prisma: Quality switched while playing") {
    // the same latency either way; the one mode's moved notes crossfade into the other's, without a click
    Standalone prisma("prisma", kRate, {{"note_a", 1.f}});
    const int latency = prisma.processor().latencySamples();
    const Samples in = sine(466.16, 3.0, 0.25);  // A#: moved
    const double before = largestStep(prisma.play(in), 48000, 96000);
    Standalone switching("prisma", kRate, {{"note_a", 1.f}});
    const Samples out = switching.play(in, {{72000, "quality", 1.f, true}, {108000, "quality", 0.f, true}});
    for (const float v : out) REQUIRE(std::isfinite(v));
    CHECK(largestStep(out, 48000, 140000) < 3 * before + 0.01);
    CHECK_EQ(switching.processor().latencySamples(), latency);
    CHECK(!switching.processor().idle());
}
