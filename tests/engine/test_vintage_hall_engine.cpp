// The built-in Vintage Hall: its listing; its ring (where each side's first echo
// comes, the loop's length, the other side's echo later); the decay following
// Decay; the 1970s band-limit at a quarter of the host's rate, whatever the
// rate; Early and Late Diffusion filling it in; Attack moving the weight from
// the first echo to the third; the modulation (none at Depth 0, a moving tail
// otherwise); Dry/Wet's equal-power blend; Predelay; one channel; changes of
// Size and Predelay while it plays; reset; its tail; and its editor's map of
// the loop and displays.

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include "builtin/VintageHallDesign.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"

using namespace subtest;
namespace vh = sub::vintage_hall;

namespace {

using Values = ParamValues;

// Fully wet, no predelay; anything else from `values`.
class Hall : public Standalone {
public:
    explicit Hall(const Values& values = {}, double rate = kSampleRate)
        : Standalone("vintage_hall", rate, with({{"mix", 100.f}, {"predelay", 0.f}}, values)) {}
};

// The ring bare: no diffusion, no modulation, the first tap alone at Attack 0 (and the other three).
const Values kBare = {{"early_diffusion", 0.f}, {"late_diffusion", 0.f}, {"mod_depth", 0.f}, {"attack", 0.f}};

// The left channel's output for `in` on the left alone (one channel feeds both sides).
Samples leftOnly(Hall&& hall, const Samples& in) { return hall.play(in, Samples(in.size())).first; }

// Where a channel is loudest in [from, to).
int64_t loudest(const Samples& x, int64_t from, int64_t to) {
    std::vector<double> magnitude(static_cast<size_t>(to - from));
    for (int64_t i = from; i < to; ++i) magnitude[static_cast<size_t>(i - from)] = std::abs(x[static_cast<size_t>(i)]);
    return from + static_cast<int64_t>(argmax(magnitude));
}

// The time for the tail to fall from -5 to -25 dB (its energy left from each moment on), times 3: an RT60.
double rt60(const Samples& x, double rate = kSampleRate) {
    std::vector<double> left(x.size() + 1, 0.0);
    for (size_t i = x.size(); i-- > 0;) left[i] = left[i + 1] + double(x[i]) * x[i];
    const auto when = [&](double db) {
        for (size_t i = 0; i < x.size(); ++i)
            if (10.0 * std::log10(std::max(left[i], 1e-300) / left[0]) < db) return double(i);
        return double(x.size());
    };
    return 3.0 * (when(-25.0) - when(-5.0)) / rate;
}

}  // namespace

TEST_CASE("the vintage hall is listed with its parameters") {
    const sub::BuiltinInfo info = builtinInfo("vintage_hall");
    CHECK_EQ(info.name, std::string("Vintage Hall"));
    CHECK(!info.isInstrument());
    CHECK(paramIds(info.params) ==
          (std::vector<std::string>{"mix", "predelay", "decay", "size", "attack", "bass_mult", "bass_xover",
                                    "high_shelf", "high_freq", "early_diffusion", "late_diffusion", "mod_rate",
                                    "mod_depth", "high_cut", "low_cut"}));
    const auto param = [&](const std::string& id) {
        return *std::find_if(info.params.begin(), info.params.end(), [&](const auto& p) { return p.id == id; });
    };
    CHECK_EQ(param("decay").defaultValue, 4000.f);
    CHECK(param("decay").isLog());
    CHECK_EQ(param("mix").defaultValue, 100.f);
    CHECK_EQ(param("high_shelf").defaultValue, -24.f);
    CHECK_EQ(param("mod_depth").defaultValue, 38.f);
}

TEST_CASE("each side's first echo comes where the original's does, and the loop is 560 ms") {
    // The left comes in at the ring's start, the right half way round: the left's first echo
    // 1362 samples after it at 48 kHz, the right's 1046 (as the original's), each side's again a
    // loop of 13442 samples at the reverb's 24 kHz later.
    for (int side = 0; side < 2; ++side) {
        INFO("side " + std::to_string(side));
        Hall hall(kBare);
        Samples in[2] = {Samples(48000), Samples(48000)};
        in[side][0] = 1.f;
        auto [l, r] = hall.play(in[0], in[1]);
        const Samples& own = side == 0 ? l : r;
        const Samples& other = side == 0 ? r : l;
        const int64_t first = loudest(own, 0, 4000);
        CHECK_NEAR(first, side == 0 ? 1362 : 1046, 2);
        CHECK_NEAR(loudest(own, first + 26000, first + 27800) - first, 2 * 13442, 3);
        CHECK(maxAbs(slice(other, 0, first + 2000)) < 1e-6);  // the other side hears it only later
        CHECK(maxAbs(other) > 0.01);
    }
}

TEST_CASE("the tail decays as Decay says") {
    // Flat damping (no bass multiplier, no high shelf), no modulation: the original's tail falls
    // 60 dB in about 1.25 times Decay (its nested all-passes hold some of it back), and so does this.
    // (Short ones longer still, as the original's: 1.5 times at 1 s.)
    for (const auto& [decay, low, high] : std::vector<std::tuple<float, double, double>>{{1000.f, 1.3, 1.8},
                                                                                          {4000.f, 1.1, 1.4}}) {
        INFO("decay " + std::to_string(decay));
        Hall hall({{"decay", decay}, {"bass_mult", 1.f}, {"high_shelf", 0.f}, {"mod_depth", 0.f}});
        const Samples out = hall.play(impulse(static_cast<size_t>(kSampleRate * decay / 1000.0 * 3.0)));
        const double rt = rt60(out);
        INFO("rt60 " + std::to_string(rt));
        CHECK(rt > low * decay / 1000.0);
        CHECK(rt < high * decay / 1000.0);
    }
}

TEST_CASE("the reverb is band-limited to a quarter of the host's rate, at any rate") {
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        INFO("rate " + std::to_string(rate));
        Hall hall({{"high_shelf", 0.f}, {"high_cut", 20000.f}}, rate);
        const Samples out = hall.play(noise(static_cast<size_t>(rate), 7, 0.5f));
        const double reverbRate = rate / vh::rateDivider(rate);
        const double pass = bandDb(out, 0.15 * reverbRate, 0.35 * reverbRate, rate);
        const double stop = bandDb(out, 0.56 * reverbRate, 0.75 * reverbRate, rate);
        CHECK(stop < pass - 60.0);
    }
}

TEST_CASE("the first echo of the ring at another rate comes as many milliseconds later") {
    for (const double rate : {44100.0, 96000.0}) {
        INFO("rate " + std::to_string(rate));
        const Samples out = leftOnly(Hall(kBare, rate), impulse(static_cast<size_t>(rate / 2)));
        const double ms = 1000.0 * double(loudest(out, 0, static_cast<int64_t>(rate / 10))) / rate;
        INFO("ms " + std::to_string(ms));
        CHECK(std::abs(ms - 1362.0 / 48.0) < 0.3);
    }
}

TEST_CASE("diffusion fills in the echoes") {
    // Bare, the first 100 ms are the first echo (at 1362) and silence; with Early Diffusion its
    // all-passes spread it over them (the share of the energy away from the echo).
    const auto spread = [](const Values& values) {
        const Samples out = slice(leftOnly(Hall(with(kBare, values)), impulse(9600)), 0, 4800);
        double near = 0.0, all = 0.0;
        for (size_t i = 0; i < out.size(); ++i) {
            all += double(out[i]) * out[i];
            if (i >= 1362 - 48 && i < 1362 + 48) near += double(out[i]) * out[i];
        }
        return 1.0 - near / all;
    };
    CHECK(spread({}) < 0.01);
    CHECK(spread({{"early_diffusion", 100.f}}) > 0.4);
    // The late all-passes' direct paths reach the third echo early: with only it heard (Attack
    // 100 %), something comes A and B's lengths before it, at 1362 + 2 (5838 - 1917 - 2737).
    const Samples late = leftOnly(Hall(with(kBare, {{"late_diffusion", 100.f}, {"attack", 100.f}})), impulse(9600));
    const Samples none = leftOnly(Hall(with(kBare, {{"attack", 100.f}})), impulse(9600));
    CHECK_NEAR(loudest(late, 0, 4800), 1362 + 2 * (5839 - 1917 - 2737), 4);
    CHECK(maxAbs(slice(none, 0, 4800)) < 1e-6);
}

TEST_CASE("attack moves the weight from the first echo to the third") {
    // The first echo at 1362 samples, the third at 1362 + 11675.
    const Samples low = Hall(with(kBare, {{"attack", 0.f}})).play(impulse(16000));
    const Samples high = Hall(with(kBare, {{"attack", 100.f}})).play(impulse(16000));
    CHECK(maxAbs(slice(low, 1300, 1450)) > 0.1);
    CHECK(maxAbs(slice(high, 0, 12000)) < 1e-6);  // swelling: nothing until the third
    CHECK(maxAbs(slice(high, 12900, 13200)) > 0.1);
    CHECK_NEAR(vh::tapWeights(0.5)[0], 23.5f / 31.f, 1e-6);  // half way between 1/3 and 2/3
}

TEST_CASE("without modulation the reverb repeats exactly; with it, its tail moves") {
    const Samples a = Hall({{"mod_depth", 0.f}}).play(impulse(96000));
    const Samples b = Hall({{"mod_depth", 0.f}}).play(impulse(96000));
    CHECK(a == b);
    const Samples moving = Hall({{"mod_depth", 100.f}}).play(impulse(96000));
    double difference = 0.0, total = 0.0;
    for (size_t i = 48000; i < a.size(); ++i) {
        difference += (double(moving[i]) - a[i]) * (double(moving[i]) - a[i]);
        total += double(a[i]) * a[i];
    }
    CHECK(difference > 0.1 * total);
}

TEST_CASE("dry/wet blends with equal power") {
    for (const float mix : {0.f, 25.f, 50.f}) {
        INFO("mix " + std::to_string(mix));
        Hall hall({{"mix", mix}});
        const Samples out = hall.play(impulse(4800, 10, 0.5f));
        CHECK_NEAR(out[10], 0.5 * std::cos(0.5 * kPi * mix / 100.0), 1e-6);
    }
    const Samples dry = Hall({{"mix", 0.f}}).play(noise(4800, 3));
    CHECK(dry == noise(4800, 3));
}

TEST_CASE("predelay delays the reverb") {
    const Samples out = leftOnly(Hall(with(kBare, {{"predelay", 100.f}})), impulse(12000));
    CHECK_NEAR(loudest(out, 0, 10000), 1362 + 4800, 2);
}

TEST_CASE("one channel plays both sides' reverb") {
    Hall hall;
    const Samples out = hall.play(impulse(48000));
    CHECK(allFinite(out));
    CHECK(maxAbs(slice(out, 1300, 1500)) > 0.01);
}

TEST_CASE("size and predelay glide while it plays") {
    // No step from one sample to the next bigger than playing it unchanged makes.
    const Samples in = noise(4 * 48000, 5, 0.25f);
    const Samples still = Hall().play(in);
    const Samples out = Hall().play(in, {{48000, "size", 40.f}, {96000, "predelay", 300.f}, {144000, "size", 100.f}});
    CHECK(allFinite(out));
    CHECK(maxAbs(out) < 2.0);
    CHECK(largestStep(out, 1) < 1.5 * largestStep(still, 1));
}

TEST_CASE("reset silences it; its tail lasts as Decay says") {
    Hall hall({{"decay", 2000.f}});
    hall.play(noise(9600, 2));
    hall.processor().reset();
    CHECK(maxAbs(hall.play(Samples(9600))) == 0.0);
    const int tail = hall.processor().tailSamples();
    CHECK(tail > static_cast<int>(2.0 * 1.5 * kSampleRate));
    CHECK(tail < static_cast<int>(10.0 * kSampleRate));
}

TEST_CASE("the ring's map is the loop that plays") {
    const vh::RingMap full = vh::ringMap(1.0);
    CHECK_NEAR(full.length, 13442.0, 1e-9);
    CHECK_NEAR(full.half1, vh::kHalf1, 1e-9);
    CHECK_NEAR(full.at[vh::RingMap::L3] - full.at[vh::RingMap::L1], 5839.0, 1e-9);  // (as the diffusion test hears it)
    for (int k = 1; k < vh::RingMap::kPoints; ++k) CHECK(full.at[k] >= full.at[k - 1]);
    // Smaller, the loop is as much shorter as the ring's all-passes.
    for (const double size : {0.0, 0.3, 0.7}) {
        INFO("size " + std::to_string(size));
        CHECK_NEAR(vh::ringMap(size).length / full.length, vh::ringScale(size), 0.01);
    }
}

TEST_CASE("its editor's displays: the levels, each tap's and the modulation") {
    Hall hall(with(kBare, {{"mod_depth", 100.f}}));
    std::vector<std::pair<std::string, int>> displays;
    for (const auto& d : hall.processor().displays()) displays.emplace_back(d.id, d.samplesPerValue);
    std::vector<std::pair<std::string, int>> expected = {{"input", 256}, {"output", 256}};
    for (const char* id : {"l1", "l2", "l3", "l4", "r1", "r2", "r3", "r4", "mod_b", "mod_d"}) expected.emplace_back(id, 256);
    CHECK(displays == expected);
}

TEST_CASE("its taps' displays light up when the echo passes them") {
    Hall hall(with(kBare, {{"mod_depth", 100.f}}));
    hall.play(impulse(48000), Samples(48000));
    const std::vector<float> in = hall.display("input"), l1 = hall.display("l1"), l3 = hall.display("l3");
    const std::vector<float> mod = hall.display("mod_b");
    REQUIRE(in.size() == 48000 / 256);
    CHECK(in[0] > -12.f);  // the impulse (through High Cut)
    CHECK(in[20] == vh::kDisplayFloorDb);
    // The left's first echo at 1362 samples (value 5) is L1's; L3 hears it 5839 reverb samples later.
    CHECK(std::max_element(l1.begin(), l1.begin() + 40) - l1.begin() == 1362 / 256);
    const auto third = std::max_element(l3.begin(), l3.end()) - l3.begin();
    CHECK(std::abs(third - (1362 + 2 * 5839) / 256) <= 1);
    CHECK(*std::max_element(mod.begin(), mod.end()) - *std::min_element(mod.begin(), mod.end()) > 0.05f);
}
