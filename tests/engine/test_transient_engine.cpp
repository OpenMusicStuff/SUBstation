// The built-in Transient Shaper: its listing; passing its input bit for bit at
// its defaults; its envelope smooth on a tone of any pitch; Attack turning
// onsets up and down and Sustain tails, leaving what is steady alone; the gain
// held to its range; the threshold leaving what is under it untouched; Output,
// Dry/Wet and Clip; lookahead as latency, the gain moving that much early;
// every control changing without a click; automation to the sample, whatever
// the block size; reset, NaN and infinity, the extremes and silence; one
// channel; its displays; and the hit its editor draws, shaped as the device
// shapes one.

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Engine.h"
#include "builtin/TransientDesign.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"

using namespace subtest;
namespace transient = sub::transient;

namespace {

using Values = ParamValues;
using Change = ParamChange;

class Shaper : public Standalone {
public:
    explicit Shaper(const Values& values = {}, double rate = kSampleRate) : Standalone("transient", rate, values) {}

    // The same on both channels: the left that comes out (checking the right is the same).
    Samples playStereo(const Samples& mono, const std::vector<Change>& changes = {}, int block = 256) {
        Samples l = mono, r = mono;
        run({&l, &r}, changes, block);
        CHECK_ARRAY_EQUAL(r, l);
        return l;
    }
};

// A drum-like hit on a `freq` tone every `periodMs` from `firstMs`: the editor's hit envelope (1 ms
// up to -6 dBFS, then 60 dB down over 450 ms), cut off when the next starts.
Samples hits(double freq, double seconds, double periodMs = 500.0, double firstMs = 50.0, double rate = kSampleRate) {
    Samples x(static_cast<size_t>(seconds * rate));
    for (size_t i = 0; i < x.size(); ++i) {
        const double ms = 1000.0 * static_cast<double>(i) / rate;
        const double since = ms < firstMs ? -1.0 : std::fmod(ms - firstMs, periodMs);
        x[i] = static_cast<float>(transient::hitEnvelope(since) * std::sin(2.0 * kPi * freq * static_cast<double>(i) / rate));
    }
    return x;
}

// `amplitude` of a `freq` tone from frame `from` on (silence before: an onset there, a step).
Samples toneFrom(double freq, double seconds, int64_t from, double amplitude = 0.25) {
    Samples x = sine(freq, seconds, amplitude);
    std::fill(x.begin(), x.begin() + from, 0.f);
    return x;
}

// The gain over [from, to) in dB: the output's RMS against the input's.
double gainDb(const Samples& out, const Samples& in, int64_t from, int64_t to) {
    return rmsDb(out, from, to) - rmsDb(in, from, to);
}

int64_t frameAt(double ms) { return static_cast<int64_t>(std::lround(ms * 0.001 * kSampleRate)); }

}  // namespace

TEST_CASE("the transient shaper is listed with its parameters") {
    const sub::BuiltinInfo info = builtinInfo("transient");
    CHECK_EQ(info.name, std::string("Transient Shaper"));
    CHECK(!info.isInstrument());
    CHECK(paramIds(info.params) == (std::vector<std::string>{"attack", "sustain", "attack_time", "release",
                                                             "threshold", "output", "mix", "clip", "lookahead"}));
    struct Expected {
        const char* name;
        const char* unit;
        float min, max, value;
        bool log;
    };
    const std::vector<Expected> expected = {
        {"Attack", "%", -100.f, 100.f, 0.f, false},     {"Sustain", "%", -100.f, 100.f, 0.f, false},
        {"Attack Time", "ms", 1.f, 100.f, 20.f, true},  {"Release", "ms", 20.f, 2000.f, 300.f, true},
        {"Threshold", "dB", -80.f, 0.f, -60.f, false},  {"Output", "dB", -24.f, 24.f, 0.f, false},
        {"Dry/Wet", "%", 0.f, 100.f, 100.f, false},     {"Clip", "", 0.f, 1.f, 0.f, false},
        {"Lookahead", "", 0.f, 2.f, 0.f, false},
    };
    REQUIRE(info.params.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        const sub::ParamInfo& p = info.params[i];
        const Expected& e = expected[i];
        INFO(p.id);
        CHECK_EQ(p.name, std::string(e.name));
        CHECK_EQ(p.unit, std::string(e.unit));
        CHECK_APPROX(p.minValue, e.min);
        CHECK_APPROX(p.maxValue, e.max);
        CHECK_APPROX(p.defaultValue, e.value);
        CHECK_EQ(p.isLog(), e.log);
        CHECK_EQ(p.automatable, p.id != "lookahead");
    }
    CHECK(info.params[7].valueLabels == (std::vector<std::string>{"Off", "On"}));
    CHECK(info.params[8].valueLabels == (std::vector<std::string>{"0 ms", "1 ms", "5 ms"}));

    Shaper defaults;
    CHECK_EQ(defaults.processor().typeId(), std::string("builtin:transient"));
    CHECK_EQ(defaults.processor().name(), std::string("Transient Shaper"));
    CHECK(!defaults.processor().hasSidechain());
    CHECK_EQ(defaults.processor().latencySamples(), 0);

    // The engine makes it.
    sub::Engine engine;
    const uint32_t track = engine.addTrack();
    const uint32_t id = engine.addBuiltinProcessor(engine.trackChain(track), "transient", -1);
    const sub::ProcessorInfo processor = engine.processorInfo(id);
    CHECK_EQ(processor.name, std::string("Transient Shaper"));
    CHECK_EQ(processor.latency, 0);
}

TEST_CASE("at its defaults the transient shaper passes its input bit for bit") {
    // Hits over a little noise: everything the detector reacts to.
    Samples x = hits(300, 1.0, 250.0, 10.0);
    const Samples hiss = noise(x.size(), 3, 0.01f);
    for (size_t i = 0; i < x.size(); ++i) x[i] += hiss[i];
    for (const int block : {1, 64, 1024}) {
        Shaper shaper;
        INFO(std::to_string(block));
        CHECK_ARRAY_EQUAL(shaper.playStereo(x, {}, block), x);
    }
}

TEST_CASE("the transient shaper's envelope is smooth on a steady tone of any pitch") {
    // The analytic signal's magnitude: the tone's amplitude, all but flat, where a rectified tone
    // would fall to zero twice a cycle.
    for (const double freq : {40.0, 100.0, 1000.0, 10000.0, 20000.0}) {
        INFO(std::to_string(freq));
        transient::Hilbert hilbert;
        const Samples x = sine(freq, 1.0, 0.5);
        double low = 1e9, high = 0.0;
        for (size_t i = 0; i < x.size(); ++i) {
            const double m = hilbert.magnitude(x[i]);
            if (i >= x.size() / 2) {
                low = std::min(low, m);
                high = std::max(high, m);
            }
        }
        CHECK(db(high / 0.5) < 0.1);
        CHECK(db(low / 0.5) > -0.1);
    }
}

TEST_CASE("the transient shaper's Attack turns onsets up and down and leaves what is steady") {
    const int64_t onset = frameAt(100);
    const Samples x = toneFrom(500, 0.6, onset);
    const Samples up = Shaper({{"attack", 100.f}}).playStereo(x);
    const Samples down = Shaper({{"attack", -100.f}}).playStereo(x);
    // The first 5 ms of the onset: turned up, or down.
    CHECK(gainDb(up, x, onset, onset + frameAt(5)) > 6.0);
    CHECK(gainDb(down, x, onset, onset + frameAt(5)) < -6.0);
    // Once the tone is steady, both followers are level with it: nothing changes.
    const int64_t later = onset + frameAt(250);
    CHECK_NEAR(gainDb(up, x, later, later + frameAt(100)), 0.0, 0.05);
    CHECK_NEAR(gainDb(down, x, later, later + frameAt(100)), 0.0, 0.05);
    // Attack -100 % with a long Attack Time softens the onset more than with a short one.
    const Samples slow = Shaper({{"attack", -100.f}, {"attack_time", 80.f}}).playStereo(x);
    const Samples quick = Shaper({{"attack", -100.f}, {"attack_time", 2.f}}).playStereo(x);
    CHECK(gainDb(slow, x, onset, onset + frameAt(20)) < gainDb(quick, x, onset, onset + frameAt(20)) - 3.0);
}

TEST_CASE("the transient shaper's Sustain turns tails up and down and leaves what is steady") {
    const Samples x = hits(800, 1.0, 1000.0, 50.0);
    const Samples up = Shaper({{"sustain", 100.f}}).playStereo(x);
    const Samples down = Shaper({{"sustain", -100.f}}).playStereo(x);
    // 200 ms into the tail (24 dB down by then): held up, or pushed down.
    const int64_t tail = frameAt(250);
    CHECK(gainDb(up, x, tail, tail + frameAt(20)) > 6.0);
    CHECK(gainDb(down, x, tail, tail + frameAt(20)) < -6.0);
    // The onset is not what Sustain shapes.
    const int64_t onset = frameAt(50);
    CHECK(std::abs(gainDb(up, x, onset, onset + frameAt(2))) < 1.0);
    // A steady tone has no tail (once the held level has let go of its onset: five Releases on).
    const Samples steady = toneFrom(500, 2.0, 0);
    const Samples held = Shaper({{"sustain", 100.f}}).playStereo(steady);
    CHECK_NEAR(gainDb(held, steady, frameAt(1500), frameAt(2000)), 0.0, 0.05);
    // A longer Release holds a tail up more.
    const Samples longer = Shaper({{"sustain", 100.f}, {"release", 1500.f}}).playStereo(x);
    const Samples shorter = Shaper({{"sustain", 100.f}, {"release", 60.f}}).playStereo(x);
    CHECK(gainDb(longer, x, tail, tail + frameAt(20)) > gainDb(shorter, x, tail, tail + frameAt(20)) + 3.0);
}

TEST_CASE("the transient shaper's gain stays within its range") {
    const Samples x = hits(300, 2.0, 400.0, 20.0);
    for (const float attack : {-100.f, 100.f}) {
        for (const float sustain : {-100.f, 100.f}) {
            const Samples out = Shaper({{"attack", attack}, {"sustain", sustain}, {"release", 2000.f},
                                        {"threshold", -80.f}})
                                    .playStereo(x);
            for (size_t i = 0; i < x.size(); ++i) {
                if (std::abs(x[i]) < 1e-3f) continue;  // (a ratio of near zeros says little)
                const double g = db(std::abs(static_cast<double>(out[i]) / x[i]));
                if (g > transient::kMaxBoostDb + 0.01 || g < -transient::kMaxCutDb - 0.01) {
                    ::subtest::fail(__FILE__, __LINE__, "a gain out of range: " + std::to_string(g) + " dB at " + std::to_string(i));
                    break;
                }
            }
        }
    }
}

TEST_CASE("the transient shaper leaves what is under its threshold untouched") {
    // Hits peaking at -46 dBFS against a threshold of -20 dB: more than the knee under it.
    Samples x = hits(500, 1.0);
    for (float& v : x) v *= 0.01f;
    const Samples out = Shaper({{"attack", 100.f}, {"sustain", 100.f}, {"threshold", -20.f}}).playStereo(x);
    CHECK_ARRAY_EQUAL(out, x);
    // The same hits at full level are shaped: the threshold was what kept them.
    const Samples loud = hits(500, 1.0);
    const Samples shaped = Shaper({{"attack", 100.f}, {"sustain", 100.f}, {"threshold", -20.f}}).playStereo(loud);
    CHECK(gainDb(shaped, loud, frameAt(50), frameAt(55)) > 3.0);
}

TEST_CASE("the transient shaper's Output, Dry/Wet and Clip") {
    const Samples x = hits(500, 1.0);
    // Output scales everything, after the blend.
    const Samples louder = Shaper({{"output", 6.f}}).playStereo(x);
    const float six = sub::dbToGain(6.f);
    for (size_t i = 0; i < x.size(); i += 97) CHECK_APPROX_REL(louder[i], x[i] * six, 1e-6);
    // Dry, the shaping isn't heard.
    const Samples dry = Shaper({{"attack", 100.f}, {"mix", 0.f}}).playStereo(x);
    CHECK_ARRAY_EQUAL(dry, x);
    // Half wet is halfway between, sample by sample.
    const Samples wet = Shaper({{"attack", 100.f}}).playStereo(x);
    const Samples half = Shaper({{"attack", 100.f}, {"mix", 50.f}}).playStereo(x);
    for (size_t i = 0; i < x.size(); i += 13) CHECK_NEAR(half[i], 0.5f * (x[i] + wet[i]), 1e-6);

    // Clip: never over full scale, however hot; untouched under its knee.
    Samples hot = sine(200, 0.5, 3.0);
    const Samples clipped = Shaper({{"attack", 100.f}, {"clip", 1.f}}).playStereo(hot);
    CHECK(maxAbs(clipped) <= 1.0);
    CHECK(maxAbs(clipped) > 0.95);
    const Samples quiet = sine(200, 0.5, transient::kClipKnee * 0.99);
    CHECK_ARRAY_EQUAL(Shaper({{"clip", 1.f}}).playStereo(quiet), quiet);
    CHECK_APPROX(transient::softClip(-2.f), -transient::softClip(2.f));
    CHECK(transient::softClip(100.f) <= 1.f);
}

TEST_CASE("the transient shaper's lookahead is latency: the gain moves that much before the audio") {
    for (const auto& [choice, frames] : std::vector<std::pair<float, int>>{{0.f, 0}, {1.f, 48}, {2.f, 240}}) {
        INFO(std::to_string(choice));
        Shaper shaper({{"lookahead", choice}});
        CHECK_EQ(shaper.processor().latencySamples(), frames);
        CHECK_EQ(shaper.processor().tailSamples(), frames);
        // At the defaults: the input, that much later.
        const Samples x = hits(400, 0.5, 200.0, 10.0);
        CHECK_ARRAY_EQUAL(shaper.playStereo(x), delayed(x, frames));
    }
    // The gain is the same as without lookahead, but applied that much earlier: frame t of the input
    // gets the gain frame t + L got without it.
    const Samples x = hits(400, 0.6, 200.0, 10.0);
    const Values shaping = {{"attack", 100.f}, {"sustain", -60.f}};
    const Samples now = Shaper(shaping).playStereo(x);
    const int lookahead = 240;
    const Samples ahead = Shaper(shaping + Values{{"lookahead", 2.f}}).playStereo(x);
    int compared = 0;
    for (size_t t = 0; t + lookahead < x.size(); ++t) {
        if (std::abs(x[t]) < 0.01f || std::abs(x[t + lookahead]) < 0.01f) continue;
        const double gainThen = static_cast<double>(now[t + lookahead]) / x[t + lookahead];
        CHECK_APPROX_REL(ahead[t + lookahead], x[t] * gainThen, 1e-4);
        ++compared;
    }
    CHECK(compared > 1000);

    // idle() tells the engine when the latency changes.
    Shaper shaper;
    CHECK(!shaper.processor().idle());
    shaper.set("lookahead", 1.f);
    CHECK(shaper.processor().idle());
    CHECK(!shaper.processor().idle());
}

TEST_CASE("changing any transient shaper control is click-free") {
    // Smooth hits (no click of their own), each control changed mid-hit: the change adds no click to
    // what the shaper does without it.
    Samples x = hits(220, 1.2, 300.0, 20.0);
    const Values base = {{"attack", 30.f}, {"sustain", 30.f}};
    const int64_t at = frameAt(330);  // (10 ms into the second hit)
    const struct {
        const char* id;
        float from, to;
        bool direct;
    } changes[] = {
        {"attack", -100.f, 100.f, false},  {"sustain", -100.f, 100.f, false}, {"attack_time", 1.f, 100.f, false},
        {"release", 20.f, 2000.f, false},  {"threshold", -80.f, 0.f, false},  {"output", -24.f, 24.f, false},
        {"mix", 0.f, 100.f, false},        {"clip", 0.f, 1.f, false},         {"lookahead", 0.f, 2.f, true},
    };
    for (const auto& c : changes) {
        INFO(c.id);
        const Values start = base + Values{{c.id, c.from}};
        const Samples steady = Shaper(start).playStereo(x);
        const Samples moved = Shaper(start).playStereo(x, {{at, c.id, c.to, c.direct}});
        const Samples settled = Shaper(base + Values{{c.id, c.to}}).playStereo(x);
        const double own = std::max(clickiness(steady), clickiness(settled));
        CHECK(clickiness(moved, at - 64, at + frameAt(30)) <= 2.0 * own + 1e-3);
    }
}

TEST_CASE("the transient shaper's automation plays to the sample, whatever the block size") {
    const Samples x = hits(300, 1.0, 250.0, 10.0);
    const std::vector<Change> changes = {
        {4801, "attack", 80.f},    {9603, "sustain", -50.f}, {14407, "attack_time", 5.f},
        {19211, "release", 900.f}, {24019, "output", -6.f},  {28807, "mix", 70.f},
        {33601, "clip", 1.f},      {38411, "threshold", -30.f},
    };
    const Samples reference = Shaper().playStereo(x, changes, 1);
    for (const int block : {7, 128, 1024}) {
        INFO(std::to_string(block));
        CHECK_ARRAY_EQUAL(Shaper().playStereo(x, changes, block), reference);
    }
    // Something did change.
    CHECK(!allclose(reference, x, 0.0, 1e-3));
}

TEST_CASE("reset starts the transient shaper afresh, at its settings") {
    const Samples x = hits(300, 0.8, 250.0, 10.0);
    Shaper shaper({{"attack", 70.f}, {"sustain", 40.f}, {"lookahead", 1.f}});
    const Samples first = shaper.playStereo(x);
    shaper.processor().reset();
    CHECK_ARRAY_EQUAL(shaper.playStereo(x), first);
    // A new rate: the same times in seconds (an onset turned up as much).
    Shaper fast({{"attack", 100.f}}, 96000.0);
    const Samples x96 = [] {
        Samples s(96000 / 2);
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = i < 9600 ? 0.f : static_cast<float>(0.25 * std::sin(2.0 * kPi * 500.0 * static_cast<double>(i) / 96000.0));
        return s;
    }();
    const Samples out96 = fast.playStereo(x96);
    const Samples x48 = toneFrom(500, 0.5, frameAt(100));
    const Samples out48 = Shaper({{"attack", 100.f}}).playStereo(x48);
    const double gain96 = rmsDb(out96, 9600, 9600 + 480) - rmsDb(x96, 9600, 9600 + 480);
    const double gain48 = gainDb(out48, x48, frameAt(100), frameAt(105));
    CHECK_NEAR(gain96, gain48, 1.0);
}

TEST_CASE("the transient shaper takes NaN and infinity as silence, and stays finite at its extremes") {
    Samples x = hits(300, 0.5, 100.0, 5.0);
    x[1000] = std::numeric_limits<float>::quiet_NaN();
    x[2000] = std::numeric_limits<float>::infinity();
    x[3000] = -std::numeric_limits<float>::infinity();
    const Samples out = Shaper({{"attack", 100.f}, {"sustain", 100.f}}).playStereo(x);
    CHECK(allFinite(out));
    CHECK_EQ(out[1000], 0.f);

    for (const double rate : {8000.0, 44100.0, 192000.0}) {
        INFO(std::to_string(rate));
        for (const float attack : {-100.f, 100.f}) {
            for (const float times : {0.f, 1.f}) {
                const Values extreme = {{"attack", attack},
                                        {"sustain", -attack},
                                        {"attack_time", times > 0.f ? 100.f : 1.f},
                                        {"release", times > 0.f ? 2000.f : 20.f},
                                        {"threshold", -80.f},
                                        {"output", 24.f},
                                        {"lookahead", 2.f}};
                Shaper shaper(extreme, rate);
                Samples loud = noise(static_cast<size_t>(rate / 2), 7, 1.f);
                Samples r = loud;
                shaper.run({&loud, &r});
                CHECK(allFinite(loud));
            }
        }
    }
}

TEST_CASE("after a hit the transient shaper rings out to exact zeros") {
    Samples x = hits(300, 1.5, 10000.0, 10.0);
    std::fill(x.begin() + frameAt(400), x.end(), 0.f);  // (the hit cut off, then silence)
    const Samples out = Shaper({{"attack", 100.f}, {"sustain", 100.f}, {"lookahead", 2.f}}).playStereo(x);
    const Samples last = slice(out, frameAt(400) + 240);
    CHECK(allEqual(last, 0.0));
}

TEST_CASE("on one channel the transient shaper shapes as two equal channels do") {
    const Samples x = hits(300, 0.8, 250.0, 10.0);
    const Values shaping = {{"attack", 60.f}, {"sustain", -40.f}};
    const Samples mono = Shaper(shaping).play(x);
    CHECK_ARRAY_EQUAL(mono, Shaper(shaping).playStereo(x));
    // Back to two: the right starts afresh (not from what it held before).
    Shaper shaper(shaping);
    shaper.play(x);
    Samples l(4800, 0.f), r(4800, 0.f);
    shaper.run({&l, &r});
    CHECK(allEqual(r, 0.0));
}

TEST_CASE("the transient shaper's displays: levels in and out, the gain's parts, the transient") {
    Shaper shaper;
    std::vector<std::pair<std::string, int>> displays;
    for (const auto& d : shaper.processor().displays()) displays.emplace_back(d.id, d.samplesPerValue);
    CHECK(displays == (std::vector<std::pair<std::string, int>>{
                          {"input", 128}, {"output", 128}, {"attack", 128}, {"sustain", 128}, {"detect", 128}}));
    // A steady 0.5: in and out at -6 dB; no gain at the defaults.
    shaper.playStereo(sine(1000, 0.5, 0.5));
    const std::vector<float> in = shaper.display("input"), out = shaper.display("output");
    REQUIRE(in.size() == 187);  // (24000 / 128, whole values)
    CHECK_NEAR(in.back(), db(0.5), 0.05);  // (the envelope's, within its ripple)
    CHECK_NEAR(out.back(), db(0.5), 0.05);
    // Envelopes: a low tone's level is as steady as a high one's (sample peaks over a stretch would
    // beat against its cycles by several dB).
    Shaper low;
    low.playStereo(sine(110, 0.5, 0.5));
    const std::vector<float> lowIn = slice(low.display("input"), 40);
    CHECK(maxOf(lowIn) - minOf(lowIn) < 0.1);
    CHECK(allEqual(shaper.display("attack"), 0.0));
    CHECK(allEqual(shaper.display("sustain"), 0.0));
    CHECK(shaper.display("detect").back() < 0.1);  // (the envelope's own ripple, hundredths of a dB)

    // A hit: the transient shows at its onset, Attack's part with it; Sustain's in its tail.
    Shaper shaping({{"attack", 50.f}, {"sustain", -50.f}});
    shaping.playStereo(hits(1000, 0.5, 1000.0, 50.0));
    const std::vector<float> detect = shaping.display("detect"), attack = shaping.display("attack"),
                             sustain = shaping.display("sustain");
    const size_t onset = static_cast<size_t>(frameAt(50) / 128);
    CHECK(*std::max_element(detect.begin() + onset, detect.begin() + onset + 3) > 10.f);
    CHECK(*std::max_element(attack.begin() + onset, attack.begin() + onset + 3) > 5.f);
    CHECK(sustain[static_cast<size_t>(frameAt(300) / 128)] < -3.f);
    CHECK_EQ(detect[2], 0.f);  // (silence before it)

    // The five streams always hold the same count, whatever the block sizes.
    for (const int block : {1, 7, 100, 1024}) {
        Samples x = Rng(15).uniformSamples(3001, -0.5, 0.5), y = x;
        shaping.run({&x, &y}, {}, block);
        std::vector<float> ignored;
        const uint64_t count = shaping.processor().readDisplay(0, 0, ignored);
        for (int i = 1; i < 5; ++i) CHECK_EQ(shaping.processor().readDisplay(i, 0, ignored), count);
    }
}

TEST_CASE("the hit the transient shaper's editor draws is shaped as the device shapes one") {
    std::vector<double> times;
    for (double ms = 0.5; ms < transient::kHitWindowMs; ms *= 1.15) times.push_back(ms);
    const int n = static_cast<int>(times.size());
    std::vector<float> in(times.size()), out(times.size()), attack(times.size()), sustain(times.size());
    const auto response = [&](float a, float s, float threshold = -80.f) {
        transient::hitResponse(a, s, 20.f, 300.f, threshold, times.data(), n, in.data(), out.data(), attack.data(),
                               sustain.data());
    };
    // At 0 % nothing changes.
    response(0.f, 0.f);
    CHECK(in == out);
    // Attack +100 % lifts the peak, -100 % lowers it; Sustain ±100 % the tail, 300 ms in.
    const auto peak = [](const std::vector<float>& v) { return *std::max_element(v.begin(), v.end()); };
    const auto at = [&](double ms) {
        return static_cast<size_t>(std::lower_bound(times.begin(), times.end(), ms) - times.begin());
    };
    response(100.f, 0.f);
    CHECK(peak(out) > peak(in) + 6.f);
    response(-100.f, 0.f);
    CHECK(peak(out) < peak(in) - 6.f);
    response(0.f, 100.f);
    CHECK(out[at(300)] > in[at(300)] + 10.f);
    response(0.f, -100.f);
    CHECK(out[at(300)] < in[at(300)] - 10.f);
    // The parts add up to the whole.
    response(70.f, -40.f);
    for (int i = 0; i < n; ++i) CHECK_NEAR(out[size_t(i)] - in[size_t(i)], attack[size_t(i)] + sustain[size_t(i)], 1e-4);
    // A higher threshold shapes less, never more.
    response(100.f, 100.f, -80.f);
    const std::vector<float> full = out;
    response(100.f, 100.f, -20.f);
    for (int i = 0; i < n; ++i) CHECK(std::abs(out[size_t(i)] - in[size_t(i)]) <= std::abs(full[size_t(i)] - in[size_t(i)]) + 1e-4f);
    // Once the held level (falling over Release) is the knee under the threshold, nothing is: with
    // a 60 ms Release, a -10 dB threshold lets the hit go well before 300 ms.
    transient::hitResponse(100.f, 100.f, 20.f, 60.f, -10.f, times.data(), n, in.data(), out.data(), nullptr, nullptr);
    CHECK(out[at(5)] != in[at(5)]);
    for (size_t i = at(300); i < times.size(); ++i) CHECK_EQ(out[i], in[i]);

    // The device, on the hit as a 3 kHz tone, shapes it as drawn (within a dB, the envelope's own
    // delay and the tone's cycles aside).
    const Values shaping = {{"attack", 60.f}, {"sustain", 60.f}};
    const Samples x = hits(3000, 0.7, 1000.0, 50.0);
    const Samples played = Shaper(shaping).playStereo(x);
    response(60.f, 60.f, -60.f);
    for (const double ms : {20.0, 100.0, 300.0}) {
        INFO(std::to_string(ms));
        const int64_t from = frameAt(50.0 + ms) - 48, to = frameAt(50.0 + ms) + 48;
        const size_t i = at(ms);
        CHECK_NEAR(gainDb(played, x, from, to), out[i] - in[i], 1.0);
    }
}
