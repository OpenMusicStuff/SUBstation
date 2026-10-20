// The built-in Snarl bass synth (builtin/devices/Snarl.cpp, builtin/SnarlDesign.h):
// its listing; silent until a note and asleep after one; notes at their pitch
// (Octave, bends), one voice gliding between held notes and back; the latency
// of its oversampling; the wobble on the song's note values (following the song,
// or from each note with Retrig) and what it moves; every growl mode, filter and
// dirt finite and bounded, also under random automation; a change of a list
// crossfading without a click; the same output whatever the blocks; its
// displays (the scope's cycle at the note's pitch); the design's shapes and
// vowels; and through the engine.

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "Engine.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/SnarlDesign.h"
#include "builtin/SnarlTables.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"
#include "harness/Test.h"

using namespace subtest;
namespace snarl = sub::snarl;

namespace {

constexpr double kRate = 48000.0;

// What a test plays: events at absolute frames.
struct Timeline {
    std::vector<sub::ProcessEvent> events;  // sampleOffset: the absolute frame
    void note(int64_t on, int64_t off, uint8_t key, int32_t id = -1) {
        sub::ProcessEvent start = sub::ProcessEvent::noteOn(static_cast<int32_t>(on), key, 100);
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
};

// Plays `frames` of the timeline through the device, `block` frames at a time: (left, right).
std::pair<Samples, Samples> play(Standalone& device, Timeline timeline, int64_t frames, int block = 256,
                                 const std::vector<ParamChange>& changes = {}) {
    std::stable_sort(timeline.events.begin(), timeline.events.end(),
                     [](const auto& a, const auto& b) { return a.sampleOffset < b.sampleOffset; });
    std::vector<sub::ProcessEvent> blockEvents;
    Samples left(static_cast<size_t>(frames)), right(static_cast<size_t>(frames));
    device.run({&left, &right}, changes, block, [&](int64_t start, int n) {
        blockEvents.clear();
        for (const sub::ProcessEvent& event : timeline.events) {
            if (event.sampleOffset >= start && event.sampleOffset < start + n) {
                sub::ProcessEvent e = event;
                e.sampleOffset = static_cast<int32_t>(event.sampleOffset - start);
                blockEvents.push_back(e);
            }
        }
        device.context().inEvents = {blockEvents.data(), blockEvents.size()};
    });
    device.context().inEvents = {};
    return {std::move(left), std::move(right)};
}

// A plain sound to measure pitches by: one sine oscillator, no sub, nothing bending or shaping it much.
const ParamValues kClean = {{"wave", 0.f},         {"unison", 1.f},  {"sub", 0.f},        {"split", 20.f},
                            {"growl", 0.f},        {"filter_type", 0.f}, {"cutoff", 18000.f}, {"resonance", 0.f},
                            {"drive", 0.f},        {"squash", 0.f},  {"wobble_filter", 0.f}, {"attack", 0.5f},
                            {"release", 5.f},      {"glide", 0.f}};

double pitchOf(const Samples& x, int64_t from, int64_t length = 16384) {
    return dominantFreq(slice(x, from, from + length));
}

double hz(int key) { return 440.0 * std::pow(2.0, (key - 69) / 12.0); }

bool finite(const Samples& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

// Of x[from, from + 16384)'s energy from 500 Hz to 18 kHz (what is heard; the decimator's own transition band lies
// above), the share more than 12 Hz from any harmonic of `f0` (aliasing: a
// held note's sound is harmonic, what folds back mostly isn't), in dB. A 4-term Blackman-Harris window (sidelobes at
// -92 dB) keeps the harmonics' own leakage out of the count.
double inharmonicDb(const Samples& x, double f0, int64_t from, double rate = kRate) {
    constexpr size_t n = 16384;
    std::vector<double> window(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1);
        window[i] = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
    }
    const std::vector<double> s = spectrum(slice(x, from, from + static_cast<int64_t>(n)), window);
    double between = 0.0, all = 0.0;
    for (size_t k = 0; k < s.size(); ++k) {
        const double f = static_cast<double>(k) * rate / static_cast<double>(n);
        if (f < 500.0 || f > 18000.0) continue;
        const double e = s[k] * s[k];
        all += e;
        if (std::abs(f / f0 - std::round(f / f0)) * f0 > 12.0) between += e;
    }
    return 10.0 * std::log10(between / std::max(all, 1e-300) + 1e-300);
}

}  // namespace

TEST_CASE("snarl: listed as an instrument, its parameters and lists") {
    const auto& infos = sub::BuiltinRegistry::instance().devices();
    const auto it = std::find_if(infos.begin(), infos.end(), [](const auto& i) { return i.id == "snarl"; });
    REQUIRE(it != infos.end());
    CHECK(it->isInstrument());
    CHECK(it->name == "Snarl");
    CHECK(it->params.size() == 27u);
    std::set<std::string> ids;
    for (const sub::ParamInfo& info : it->params) {
        INFO(info.id);
        CHECK(ids.insert(info.id).second);
        CHECK(info.minValue <= info.defaultValue);
        CHECK(info.defaultValue <= info.maxValue);
        if (!info.valueLabels.empty()) {
            CHECK(info.minValue == 0.f);
            CHECK(info.maxValue == static_cast<float>(info.valueLabels.size() - 1));
        }
    }
    const auto labels = [&](const char* id) {
        for (const sub::ParamInfo& info : it->params)
            if (info.id == id) return info.valueLabels;
        return std::vector<std::string>{};
    };
    CHECK(labels("table") == snarl::tableLabels());
    const std::vector<std::string> tables = {"Basic", "Voice", "FM", "Hollow", "Dirty"};
    CHECK(snarl::tableLabels() == tables);
    CHECK(labels("growl_mode") == snarl::growlModeLabels());
    CHECK(labels("filter_type") == snarl::filterTypeLabels());
    CHECK(labels("dirt_type") == snarl::dirtTypeLabels());
    CHECK(labels("wobble_rate") == snarl::rateLabels());
    CHECK(labels("wobble_shape") == snarl::wobbleShapeLabels());
    CHECK(labels("ratio") == snarl::ratioLabels());
    CHECK(snarl::rateLabels().size() == 12u);
    CHECK(snarl::rateBeats(7) == 0.5);         // 1/8
    CHECK_APPROX(snarl::rateBeats(8), 1.0 / 3);  // 1/8T
    CHECK(snarl::rateBeats(3) == 1.5);         // 1/4D
    Standalone device("snarl");
    CHECK(device.processor().acceptsMidi());
}

TEST_CASE("snarl: silent until a note, and asleep to exact zeros after it") {
    Standalone device("snarl");
    Timeline t;
    t.note(4800, 24000, 45);
    const auto [left, right] = play(device, t, 72000);
    CHECK(maxAbs(slice(left, 0, 4800)) == 0.0);
    CHECK(maxAbs(slice(left, 4800 + 960, 24000)) > 0.05);
    CHECK(finite(left));
    CHECK(finite(right));
    // Released (80 ms), rung out, then asleep: exact zeros, the next block too.
    CHECK(maxAbs(slice(left, 24000 + 36000, 72000)) == 0.0);
    CHECK(maxAbs(slice(right, 24000 + 36000, 72000)) == 0.0);
    // Stereo: the unison spread apart, the sub in the middle.
    CHECK(maxAbs(slice(left, 9600, 19200)) > 0.0);
    double apart = 0.0;
    for (size_t i = 9600; i < 19200; ++i) apart = std::max(apart, std::abs(double(left[i]) - right[i]));
    CHECK(apart > 1e-3);
}

TEST_CASE("snarl: notes at their pitch, Octave moving the body, bends") {
    for (const int key : {33, 45, 57}) {
        INFO(std::to_string(key));
        Standalone device("snarl", kRate, kClean);
        Timeline t;
        t.note(0, 40000, static_cast<uint8_t>(key));
        const Samples left = play(device, t, 40000).first;
        CHECK_APPROX_REL(pitchOf(left, 4800), hz(key), 2e-3);
    }
    {  // an octave up: the body; the sub stays at the note
        Standalone device("snarl", kRate, with(kClean, {{"octave", 1.f}}));
        Timeline t;
        t.note(0, 40000, 45);
        const Samples left = play(device, t, 40000).first;
        CHECK_APPROX_REL(pitchOf(left, 4800), 2 * hz(45), 2e-3);
    }
    {  // the sub alone, at the note (the body split away above it)
        Standalone device("snarl", kRate, with(kClean, {{"sub", 100.f}, {"split", 500.f}, {"octave", 1.f}}));
        Timeline t;
        t.note(0, 40000, 33);
        const Samples left = play(device, t, 40000).first;
        CHECK_APPROX_REL(pitchOf(left, 4800), hz(33), 2e-3);
        CHECK(windowedAmplitude(left, hz(33), 8000, 16384) > 0.1);
    }
    {  // a bend of +12 semitones: an octave up, gliding there within a few ms
        Standalone device("snarl", kRate, kClean);
        Timeline t;
        t.note(0, 60000, 45, 7);
        t.bend(20000, 45, 7, 12.f);
        const Samples left = play(device, t, 60000).first;
        CHECK_APPROX_REL(pitchOf(left, 2000), hz(45), 2e-3);
        CHECK_APPROX_REL(pitchOf(left, 20480), hz(57), 2e-3);
    }
}

TEST_CASE("snarl: one voice, gliding between held notes and back") {
    // Glide 0: the new note at once, the envelope carrying on (no dip at the change).
    {
        Standalone device("snarl", kRate, with(kClean, {{"attack", 50.f}}));
        Timeline t;
        t.note(0, 72000, 45);
        t.note(24000, 48000, 52);
        const Samples left = play(device, t, 72000).first;
        CHECK_APPROX_REL(pitchOf(left, 6000), hz(45), 2e-3);
        CHECK_APPROX_REL(pitchOf(left, 26000, 16384), hz(52), 2e-3);
        CHECK_APPROX_REL(pitchOf(left, 50000, 16384), hz(45), 2e-3);  // back to the one held
        // No retrigger: the attack (50 ms) doesn't start again, the level holds through the change.
        CHECK_APPROX_REL(rms(slice(left, 24000, 24000 + 2400)), rms(slice(left, 21600, 24000)), 0.1);
    }
    // Glide 200 ms: halfway through it the pitch is between the two, then there.
    {
        Standalone device("snarl", kRate, with(kClean, {{"glide", 200.f}}));
        Timeline t;
        t.note(0, 60000, 45);
        t.note(12000, 60000, 57);
        const Samples left = play(device, t, 60000).first;
        const double middle = pitchOf(left, 12000 + 1200, 2048);
        CHECK(middle > hz(45) * 1.1);
        CHECK(middle < hz(57) * 0.95);
        CHECK_APPROX_REL(pitchOf(left, 12000 + 24000), hz(57), 3e-3);
    }
    // A note after silence starts again; its release glides nowhere.
    {
        Standalone device("snarl", kRate, with(kClean, {{"glide", 500.f}}));
        Timeline t;
        t.note(0, 12000, 45);
        t.note(24000, 60000, 57);  // nothing held: no glide
        const Samples left = play(device, t, 60000).first;
        CHECK_APPROX_REL(pitchOf(left, 24000 + 480, 8192), hz(57), 3e-3);
    }
}

TEST_CASE("snarl: its latency is the oversampling's, by the rate") {
    Standalone device("snarl", 48000.0);
    CHECK(device.processor().latencySamples() == 18);
    device.prepare(44100.0);
    CHECK(device.processor().latencySamples() == 18);
    device.prepare(96000.0);
    CHECK(device.processor().latencySamples() == 16);
    device.prepare(192000.0);
    CHECK(device.processor().latencySamples() == 0);
    // At every rate a note plays at its pitch.
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        INFO(std::to_string(rate));
        Standalone at("snarl", rate, kClean);
        Timeline t;
        t.note(0, int64_t(rate), 45);
        const Samples left = play(at, t, int64_t(rate)).first;
        CHECK_APPROX_REL(dominantFreq(slice(left, int64_t(rate / 10), int64_t(rate / 10) + 32768), rate), hz(45), 3e-3);
    }
}

TEST_CASE("snarl: the wobble on the song's note values") {
    // 1/1 at 120 BPM: a cycle every 2 s (375 display values of 256 samples). Following the song (Retrig off),
    // it is where the song is: at frame f, f / 96000 of its cycle (the displays' values at frames 255, 511, ...).
    Standalone device("snarl", kRate, {{"wobble_rate", 0.f}, {"wobble_retrig", 0.f}, {"wobble_shape", 3.f}});
    device.context().playing = true;
    device.context().tempo = 120.0;
    Timeline t;
    t.note(0, 300000, 45);
    play(device, t, 300000);
    const std::vector<float> phase = device.display("phase");
    const std::vector<float> lfo = device.display("lfo");
    REQUIRE(phase.size() >= 1000u);
    CHECK_NEAR(phase[93], (255.0 + 93 * 256) / 96000.0, 1e-5);
    for (size_t i = 0; i + 375 < phase.size(); i += 50) CHECK_NEAR(phase[i], phase[i + 375], 1e-3);
    // Ramp Up: the wobble climbs with the phase (smoothed a little), from 0 to 1.
    CHECK(*std::max_element(lfo.begin(), lfo.end()) > 0.98f);
    CHECK(*std::min_element(lfo.begin() + 10, lfo.end()) < 0.05f);
    CHECK_NEAR(lfo[200], phase[200], 0.02);

    // Retrig: from each note, at the tempo: 0 at the note's start wherever the song is. (Asleep before it, it
    // publishes nothing: its first value is 256 samples into the note.)
    Standalone retrig("snarl", kRate, {{"wobble_rate", 0.f}, {"wobble_retrig", 1.f}});
    retrig.context().playing = true;
    Timeline r;
    r.note(37 * 256, 300000, 45);
    play(retrig, r, 60000);
    const std::vector<float> phases = retrig.display("phase");
    REQUIRE(phases.size() > 40u);
    CHECK_NEAR(phases[0], 256.0 / 96000.0, 1e-6);
    CHECK_NEAR(phases[40], 41 * 256.0 / 96000.0, 1e-6);
}

TEST_CASE("snarl: the wobble moves the filter, the growl and the pitch") {
    // Wobble > Pitch +12 with a Square wobble at 1/2: the note an octave up for the first half of each cycle.
    {
        Standalone device("snarl", kRate,
                          with(kClean, {{"wobble_rate", 1.f}, {"wobble_shape", 4.f}, {"wobble_pitch", 12.f}}));
        device.context().playing = true;
        Timeline t;
        t.note(0, 96000, 45);
        const Samples left = play(device, t, 96000).first;
        CHECK_APPROX_REL(pitchOf(left, 4800, 8192), hz(57), 3e-3);   // first half of the first cycle (0.5 s)
        CHECK_APPROX_REL(pitchOf(left, 24000 + 2400, 16384), hz(45), 3e-3);  // second half
    }
    // Wobble > Filter: a saw through a low-pass at 200 Hz; the wobble opens it (the highs come with it).
    {
        const ParamValues closed =
            with(kClean, {{"wave", 50.f}, {"cutoff", 200.f}, {"wobble_shape", 4.f}, {"wobble_rate", 1.f}});
        Standalone still("snarl", kRate, closed);
        Standalone open("snarl", kRate, with(closed, {{"wobble_filter", 100.f}}));
        still.context().playing = open.context().playing = true;
        Timeline t;
        t.note(0, 48000, 33);
        const Samples a = play(still, t, 48000).first;
        const Samples b = play(open, t, 48000).first;
        const double highsStill = bandDb(slice(a, 4800, 4800 + 8192), 2000, 8000);
        const double highsOpen = bandDb(slice(b, 4800, 4800 + 8192), 2000, 8000);
        CHECK(highsOpen > highsStill + 20.0);
        // The filter display along its range: 200 Hz closed, 6 octaves higher open.
        const std::vector<float> stillFilter = still.display("filter");
        const std::vector<float> openFilter = open.display("filter");
        CHECK_NEAR(stillFilter[30], snarl::freqPosition(200.0), 1e-3);
        CHECK_NEAR(openFilter[30], snarl::freqPosition(200.0 * 64.0), 2e-3);
    }
    // Wobble > Growl adds to the growl, up to all of it: the display has it.
    {
        Standalone device("snarl", kRate,
                          {{"growl", 20.f}, {"wobble_growl", 100.f}, {"wobble_shape", 4.f}, {"wobble_rate", 0.f}});
        device.context().playing = true;
        Timeline t;
        t.note(0, 60000, 45);
        play(device, t, 60000);
        const std::vector<float> growl = device.display("growl");
        REQUIRE(growl.size() > 200u);
        CHECK_NEAR(growl[30], 1.0, 1e-3);   // (Square, a bar: on for its first half, 48000 samples)
        CHECK_NEAR(growl[200], 0.2, 1e-3);  // (off for the second)
    }
}

TEST_CASE("snarl: every growl mode, filter and dirt plays, finite and bounded") {
    for (int mode = 0; mode < 4; ++mode) {
        for (int filter = 0; filter < 4; ++filter) {
            for (int dirt = 0; dirt < 4; ++dirt) {
                INFO("mode " + std::to_string(mode) + ", filter " + std::to_string(filter) + ", dirt " +
                     std::to_string(dirt));
                Standalone device("snarl", kRate,
                                  {{"growl_mode", float(mode)}, {"filter_type", float(filter)},
                                   {"dirt_type", float(dirt)}, {"growl", 70.f}, {"drive", 70.f},
                                   {"resonance", 80.f}, {"unison", 7.f}, {"squash", 100.f}, {"wobble_growl", 50.f}});
                device.context().playing = true;
                Timeline t;
                t.note(0, 24000, 33);
                const auto [left, right] = play(device, t, 30000);
                CHECK(finite(left));
                CHECK(finite(right));
                CHECK(maxAbs(left) < 2.5);
                CHECK(maxAbs(right) < 2.5);
                CHECK(rms(slice(left, 4800, 24000)) > 0.01);
            }
        }
    }
}

TEST_CASE("snarl: each growl mode sounds its own, and none at growl 0 but the oscillator") {
    // At Growl 0 every mode is the oscillator as it is.
    Samples first;
    for (int mode = 0; mode < 4; ++mode) {
        INFO(std::to_string(mode));
        Standalone device("snarl", kRate, with(kClean, {{"wave", 50.f}, {"growl_mode", float(mode)}}));
        Timeline t;
        t.note(0, 24000, 45);
        const Samples left = play(device, t, 24000).first;
        if (mode == 0)
            first = left;
        else
            CHECK(maxAbs(slice(left, 0, 24000)) > 0.0 && allclose(left, first, 0.0, 1e-4));
    }
    // At Growl 60 each is another sound: their spectra apart.
    std::vector<Samples> outs;
    for (int mode = 0; mode < 4; ++mode) {
        Standalone device("snarl", kRate, with(kClean, {{"wave", 0.f}, {"growl_mode", float(mode)}, {"growl", 60.f}}));
        Timeline t;
        t.note(0, 24000, 45);
        outs.push_back(play(device, t, 24000).first);
    }
    for (int a = 0; a < 4; ++a) {
        for (int b = a + 1; b < 4; ++b) {
            INFO(std::to_string(a) + " " + std::to_string(b));
            CHECK(!allclose(outs[size_t(a)], outs[size_t(b)], 0.0, 1e-2));
        }
    }
    // FM's sidebands: a sine carrier at 110 Hz, Ratio 2: energy at 330 Hz (110 + 220) that a sine hasn't.
    CHECK(windowedAmplitude(outs[0], 3 * hz(45), 4800, 16384) > 0.02);
    CHECK(windowedAmplitude(first, 3 * hz(45), 4800, 16384) < windowedAmplitude(outs[0], 3 * hz(45), 4800, 16384));
}

TEST_CASE("snarl: the Vowel filter's formants add between their peaks") {
    // A saw at 43.65 Hz through the vowel A (the middle of the range): its harmonics by the first formant (730 Hz)
    // and between it and the second (1090 Hz). Summed with one sign, the formants' band-passes cancelled there (a
    // notch some 20 dB under the first; moving with the vowel, a phaser); with alternate signs they add.
    const ParamValues vowel = with(kClean, {{"wave", 50.f}, {"filter_type", 2.f}, {"resonance", 45.f},
                                            {"cutoff", static_cast<float>(std::sqrt(30.0 * 18000.0))}});
    Standalone device("snarl", kRate, vowel);
    Timeline t;
    t.note(0, 48000, 29);
    const Samples left = play(device, t, 48000).first;
    const double f0 = hz(29);
    const double first = windowedAmplitude(left, 17 * f0, 8000, 32768);   // 742 Hz
    const double between = windowedAmplitude(left, 21 * f0, 8000, 32768);  // 917 Hz
    CHECK(db(between / first) - db(17.0 / 21.0) > -12.0);  // (the saw's own fall between the two taken out)
}

TEST_CASE("snarl: folding and hard driving alias little") {
    // A held square folded hard into a band-pass and the Hard dirt driven well up (Riddim's way): what folds back
    // between the harmonics stays far under them.
    const ParamValues riddim = with(kClean, {{"wave", 75.f}, {"growl_mode", 2.f}, {"growl", 60.f}, {"filter_type", 1.f},
                                             {"cutoff", 650.f}, {"resonance", 50.f}, {"dirt_type", 1.f}, {"drive", 70.f}});
    Standalone device("snarl", kRate, riddim);
    Timeline t;
    t.note(0, 48000, 29);
    const Samples left = play(device, t, 48000).first;
    CHECK(inharmonicDb(left, hz(29), 12000) < -38.0);
    // Each dirt clean for a quiet sound at Drive 0 (6 dB of headroom): little but the harmonics, and as loud.
    for (int dirt = 0; dirt < 3; ++dirt) {
        INFO(std::to_string(dirt));
        Standalone clean("snarl", kRate, with(kClean, {{"wave", 50.f}, {"dirt_type", static_cast<float>(dirt)}}));
        Timeline c;
        c.note(0, 48000, 29);
        const Samples out = play(clean, c, 48000).first;
        CHECK(inharmonicDb(out, hz(29), 12000) < -50.0);
    }
}

TEST_CASE("snarl: the dirt's curves and their anti-aliasing") {
    using snarl::DirtType;
    for (const DirtType type : {DirtType::Tube, DirtType::Hard, DirtType::Rectify}) {
        INFO(std::to_string(static_cast<int>(type)));
        // The averaged curve over a line is the mean of the curve along it (a numerical integral), for lines
        // short and long, either way, across the knees.
        for (const auto& [u0, u] : std::vector<std::pair<double, double>>{{-3.0, 2.5}, {0.2, 0.9}, {0.9, 1.4},
                                                                          {-1.2, -0.7}, {5.0, -6.0}, {0.3, 0.3}}) {
            double mean = 0.0;
            constexpr int steps = 20000;
            for (int i = 0; i < steps; ++i)
                mean += snarl::dirtCurve(type, u0 + (u - u0) * (i + 0.5) / steps) / steps;
            CHECK_NEAR(snarl::dirtAveraged(type, u, u0), mean, 1e-6);
        }
    }
    CHECK_NEAR(snarl::logCosh(0.0), 0.0, 1e-15);
    CHECK_NEAR(snarl::logCosh(1.5), std::log(std::cosh(1.5)), 1e-14);
    CHECK_NEAR(snarl::logCosh(-800.0), 800.0 - std::log(2.0), 1e-9);  // (no overflow)
    CHECK(snarl::driveGain(0.0) == 1.0);
    CHECK_NEAR(snarl::driveGain(1.0), 16.0, 1e-5);
    // The folder averaged the same way.
    for (const auto& [x0, x] : std::vector<std::pair<double, double>>{{-1.0, 1.0}, {0.1, 0.4}, {0.5, 0.5}}) {
        const double a = 7.0;
        double mean = 0.0;
        for (int i = 0; i < 20000; ++i) mean += std::sin(a * (x0 + (x - x0) * (i + 0.5) / 20000)) / 20000;
        CHECK_NEAR(snarl::foldAveraged(a, x, x0), mean, 1e-6);
    }
}

TEST_CASE("snarl: the tables") {
    const snarl::Wavetables& tables = snarl::Wavetables::instance();
    CHECK(&tables == &snarl::Wavetables::instance());  // (built once, shared)
    const int basic = static_cast<int>(snarl::Table::Basic);
    CHECK(tables.frames(basic) == 4 * snarl::Wavetables::kStepsBetween + 1);  // five shapes
    for (int t = 1; t < snarl::kTables; ++t) CHECK(tables.frames(t) == 15 * snarl::Wavetables::kStepsBetween + 1);
    // Basic's keys are Wave's shapes, band-limited: the sine, and the saw and the square from their first 512
    // harmonics, between samples too.
    const auto series = [](double t, int first, int step, double scale) {
        double sum = 0.0;
        for (int h = first; h <= 512; h += step) sum += scale * std::sin(2.0 * kPi * h * t) / h;
        return sum;
    };
    for (const double t : {0.0, 0.1, 0.2371, 0.5, 0.77}) {
        INFO(std::to_string(t));
        CHECK_NEAR(tables.read(basic, 0.0, t, 512), std::sin(2.0 * kPi * t), 1e-4);
        CHECK_NEAR(tables.read(basic, 0.5, t, 512), series(t, 1, 1, -2.0 / kPi), 2e-3);
        CHECK_NEAR(tables.read(basic, 0.75, t, 512), series(t, 1, 2, 4.0 / kPi), 2e-3);
    }
    // Band-limited for the note: for a limit of 10 harmonics a read holds 16 at most (the fuller level at most
    // twice the limit), the saw's 17th and up gone. (Read at the levels' own samples, 64 a cycle: between them the
    // interpolation adds images of its own, which the device's oscillators keep far down, as the next test shows.)
    {
        constexpr int points = 64;
        std::vector<double> cycle(points);
        for (int p = 0; p < points; ++p) cycle[size_t(p)] = tables.read(basic, 0.5, double(p) / points, 10.0);
        const std::vector<Complex> bins = dft(cycle);
        double low = 0.0, high = 0.0;
        for (int h = 1; h < points / 2; ++h) (h <= 16 ? low : high) += std::norm(bins[size_t(h)]);
        CHECK(high < 1e-8 * low);
        CHECK(std::abs(bins[10]) > 0.0);
    }
    // The AKWF tables: each key as loud as the others, every frame finite, and a scan smooth: neighbouring
    // frames alike (blended in frequency, lined up), and never cancelling between keys.
    for (int t = 1; t < snarl::kTables; ++t) {
        INFO(snarl::tableLabels()[size_t(t)]);
        const int frames = tables.frames(t);
        std::vector<float> before;
        double lowest = 1e9;
        for (int f = 0; f < frames; ++f) {
            const std::vector<float> cycle = tables.cycle(t, double(f) / (frames - 1), 1024);
            REQUIRE(finite(cycle));
            const double level = rms(cycle);
            lowest = std::min(lowest, level);
            if (f % snarl::Wavetables::kStepsBetween == 0) CHECK_NEAR(level, snarl::Wavetables::kLoudness, 0.03);
            if (!before.empty()) {
                double dot = 0.0, a = 0.0, b = 0.0;
                for (size_t i = 0; i < cycle.size(); ++i) {
                    dot += double(cycle[i]) * before[i];
                    a += double(cycle[i]) * cycle[i];
                    b += double(before[i]) * before[i];
                }
                CHECK(dot / std::sqrt(a * b) > 0.5);
            }
            before = cycle;
        }
        CHECK(lowest > 0.3);
    }
}

TEST_CASE("snarl: every table plays clean between its harmonics") {
    // A high note (A3, 220 Hz) at each table's brightest: band-limited, nothing folds back.
    for (int t = 0; t < snarl::kTables; ++t) {
        INFO(snarl::tableLabels()[size_t(t)]);
        Standalone device("snarl", kRate, with(kClean, {{"table", float(t)}, {"wave", 100.f}}));
        Timeline n;
        n.note(0, 48000, 57);
        const Samples left = play(device, n, 48000).first;
        CHECK(rms(slice(left, 12000, 28000)) > 0.05);
        CHECK(inharmonicDb(left, hz(57), 12000) < -70.0);
    }
}

TEST_CASE("snarl: the same output whatever the blocks") {
    const ParamValues busy = {{"unison", 5.f}, {"growl", 60.f}, {"wobble_growl", 40.f}, {"filter_type", 2.f},
                              {"dirt_type", 3.f}, {"glide", 80.f}};
    Timeline t;
    t.note(1000, 30000, 40);
    t.note(15000, 40000, 47);
    t.bend(20000, 47, -1, 3.f);
    const std::vector<ParamChange> changes = {{12345, "cutoff", 3000.f}, {22222, "growl_mode", 1.f},
                                              {33333, "filter_type", 3.f}};
    Standalone a("snarl", kRate, busy), b("snarl", kRate, busy), c("snarl", kRate, busy);
    a.context().playing = b.context().playing = c.context().playing = true;
    const auto x = play(a, t, 60000, 1024, changes);
    const auto y = play(b, t, 60000, 64, changes);
    const auto z = play(c, t, 60000, 333, changes);
    CHECK(maxAbs(x.first) > 0.05);
    CHECK(x.first == y.first);
    CHECK(x.second == y.second);
    CHECK(x.first == z.first);
}

TEST_CASE("snarl: a change of a list crossfades, without a click") {
    // A sine body, held; each list changed mid-note. Across the change the output steps no more than it does
    // before or after it.
    for (const char* id : {"growl_mode", "filter_type", "dirt_type"}) {
        INFO(id);
        const ParamValues base = with(kClean, {{"growl", 30.f}, {"drive", 30.f}, {"cutoff", 2000.f}});
        Standalone device("snarl", kRate, base);
        Timeline t;
        t.note(0, 48000, 45);
        const Samples left = play(device, t, 48000, 256, {{24000, id, 2.f}}).first;
        const double steady = std::max(largestStep(left, 12000, 23000), largestStep(left, 25000, 40000));
        CHECK(largestStep(left, 23900, 24800) < 1.25 * steady + 1e-3);
    }
}

TEST_CASE("snarl: random automation of everything stays finite and bounded") {
    Standalone device("snarl", kRate);
    device.context().playing = true;
    const auto& params = device.processor().params();
    std::vector<ParamChange> changes;
    uint32_t seed = 12345;
    const auto next = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) / 16777216.0;
    };
    for (int64_t frame = 0; frame < 240000; frame += 1500) {
        const auto& info = params[size_t(next() * params.size()) % params.size()];
        if (info.id == "volume") continue;
        changes.push_back({frame, info.id, static_cast<float>(info.fromNormalized(static_cast<float>(next())))});
    }
    Timeline t;
    for (int n = 0; n < 20; ++n) t.note(n * 12000, n * 12000 + 9000 + (n % 3) * 4000, uint8_t(28 + (n * 7) % 30));
    const auto [left, right] = play(device, t, 240000, 256, changes);
    CHECK(finite(left));
    CHECK(finite(right));
    CHECK(maxAbs(left) < 3.0);
    CHECK(maxAbs(right) < 3.0);
}

TEST_CASE("snarl: its displays") {
    Standalone device("snarl", kRate, with(kClean, {{"sub", 50.f}}));
    Timeline t;
    t.note(0, 48000, 45);
    const Samples left = play(device, t, 72000).first;
    // A value of each every 256 samples while it plays (and until it sleeps).
    const std::vector<float> level = device.display("level");
    CHECK(level.size() >= 48000u / 256);
    CHECK(level.size() <= 72000u / 256 + 1);
    CHECK_NEAR(level[100], maxAbs(slice(left, 100 * 256, 101 * 256)), 1e-6);
    // The scope: 64 values a cycle, every 512 samples, while the note sounds; its cycle the output's.
    const std::vector<float> scope = device.display("scope");
    REQUIRE(scope.size() % 64 == 0);
    CHECK(scope.size() >= 64u * 80);
    CHECK(scope.size() <= 64u * (48000 / 512 + 32));
    const std::vector<float> cycle(scope.end() - 64 * 20, scope.end() - 64 * 19);
    CHECK(maxAbs(cycle) > 0.5 * maxAbs(slice(left, 24000, 48000)));
    CHECK(maxAbs(cycle) < 1.05 * maxAbs(slice(left, 24000, 48000)));
    // One cycle of a sine-ish body and sub: it crosses zero going up about once.
    int ups = 0;
    for (size_t i = 1; i < cycle.size(); ++i) ups += cycle[i - 1] < 0.f && cycle[i] >= 0.f;
    CHECK(ups >= 1);
    CHECK(ups <= 2);
    for (const char* id : {"lfo", "growl", "filter", "phase"}) {
        INFO(id);
        for (float v : device.display(id)) {
            CHECK(v >= 0.f);
            CHECK(v <= 1.f);
        }
    }
}

TEST_CASE("snarl: the design's vowels and wobbles") {
    // The vowels: the frequency picks U at the bottom, I at the top, A in the middle.
    CHECK(snarl::vowelPosition(snarl::kMinFreq) == 0.0);
    CHECK(snarl::vowelPosition(snarl::kMaxFreq) == 4.0);
    const auto a = snarl::formantsAt(2.0);
    CHECK(a[0].hz == 730.0);
    CHECK(a[1].hz == 1090.0);
    const auto between = snarl::formantsAt(2.5);  // A to E: the second formant between, in octaves
    CHECK_APPROX_REL(between[1].hz, std::sqrt(1090.0 * 1840.0), 1e-9);
    // The wobble's shapes: from 0 to 1, Sine and Triangle rising from 0, Ramp Down from 1.
    CHECK(snarl::wobble(snarl::WobbleShape::Sine, 0.0, 0) == 0.0);
    CHECK_NEAR(snarl::wobble(snarl::WobbleShape::Sine, 0.5, 0), 1.0, 1e-9);
    CHECK(snarl::wobble(snarl::WobbleShape::RampDown, 0.0, 0) == 1.0);
    CHECK(snarl::wobble(snarl::WobbleShape::Square, 0.75, 0) == 0.0);
    for (uint32_t cycle = 0; cycle < 100; ++cycle) {
        const double v = snarl::wobble(snarl::WobbleShape::Random, 0.3, cycle);
        CHECK(v >= 0.0);
        CHECK(v <= 1.0);
        CHECK(v == snarl::wobble(snarl::WobbleShape::Random, 0.9, cycle));  // held over the cycle
    }
}

TEST_CASE("snarl: through the engine") {
    sub::Engine engine;
    const uint32_t track = engine.addTrack();
    const uint32_t snarlId = engine.addBuiltinProcessor(engine.trackChain(track), "snarl", -1);
    REQUIRE(snarlId != 0);
    engine.setTrackNotes(track, {{1.0, 2.0, 33}});
    const Samples out = engine.renderOffline(0.0, 4 * kBeat);
    const Samples left = channel(out, 0);
    CHECK(maxAbs(slice(left, 0, kBeat - 100)) < 1e-6);
    CHECK(maxAbs(slice(left, kBeat + 2400, 3 * kBeat)) > 0.05);
    CHECK(finite(left));
}
