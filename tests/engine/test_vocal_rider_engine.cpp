// The built-in Vocal Rider: its listing; riding quiet and loud phrases to the target; its range; holding between
// phrases; Speed; the music on its sidechain moving the target; Output; the weighting; its displays.

#include <cmath>
#include <string>
#include <vector>

#include "harness/Standalone.h"

using namespace subtest;

namespace {

// A Vocal Rider on its own (harness/Standalone.h), with a sidechain if given.
struct Rider : Standalone {
    explicit Rider(const ParamValues& values = {}, double rate = kSampleRate) : Standalone("vocal_rider", rate, values) {}

    std::pair<Samples, Samples> playKeyed(Samples left, Samples right, const Samples& key) {
        processor().setSidechainConnected(true);
        run({&left, &right}, {}, 256, [&](int64_t start, int) {
            processor().setSidechain(key.data() + start, key.data() + start);
        });
        processor().setSidechain(nullptr, nullptr);
        return {std::move(left), std::move(right)};
    }
};

const ParamValues kFlat{{"weighting", 1.f}};

// A 1 kHz tone `seconds` long at an RMS level in dB.
Samples phrase(double rmsLevelDb, double seconds) {
    return sine(1000.0, seconds, std::sqrt(2.0) * std::pow(10.0, rmsLevelDb / 20.0));
}

Samples join(const Samples& a, const Samples& b) {
    Samples out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

// The RMS in dB over the last `seconds`.
double tailDb(const Samples& x, double seconds = 0.2) {
    return rmsDb(x, int64_t(x.size()) - int64_t(seconds * kSampleRate));
}

}  // namespace

TEST_CASE("vocal rider: listed as an audio effect with a sidechain") {
    const sub::BuiltinInfo info = builtinInfo("vocal_rider");
    CHECK_EQ(info.name, std::string("Vocal Rider"));
    CHECK(info.category == sub::BuiltinCategory::AudioEffect);
    const std::vector<std::string> ids{"target", "min_gain", "max_gain", "speed",
                                       "sensitivity", "music_sens", "weighting", "output"};
    CHECK(paramIds(info.params) == ids);
    Rider rider;
    CHECK(rider.processor().hasSidechain());
}

TEST_CASE("vocal rider: quiet and loud phrases ride to the target") {
    for (const double level : {-30.0, -24.0, -12.0, -8.0}) {
        INFO(std::to_string(level));
        Rider rider(kFlat);
        const Samples out = rider.play(phrase(level, 2.0));
        CHECK_NEAR(tailDb(out), -18.0, 0.5);
    }
    // At the target already: left as it is.
    Rider rider(kFlat);
    const Samples in = phrase(-18.0, 2.0);
    const Samples out = rider.play(in);
    CHECK_NEAR(tailDb(out), -18.0, 0.2);
}

TEST_CASE("vocal rider: the gain stays in its range") {
    Rider up(with(kFlat, {{"max_gain", 6.f}}));
    CHECK_NEAR(tailDb(up.play(phrase(-40.0, 2.0))), -34.0, 0.3);
    Rider down(with(kFlat, {{"min_gain", -3.f}}));
    CHECK_NEAR(tailDb(down.play(phrase(-6.0, 2.0))), -9.0, 0.3);
}

TEST_CASE("vocal rider: between phrases the gain holds") {
    // A quiet phrase brought up, then a noise floor under Vocal Sens.: not brought up any more than the phrase.
    Rider rider(kFlat);
    const Samples floor = noise(size_t(kSampleRate), 5, 0.0005f);  // about -70 dB
    const Samples in = join(phrase(-24.0, 2.0), floor);
    const Samples out = rider.play(in);
    const std::vector<float> gain = rider.display("gain");
    REQUIRE(gain.size() > 100);
    const float during = gain[size_t(1.9 * kSampleRate / 256)];
    CHECK_NEAR(during, 6.f, 0.5f);
    for (size_t i = size_t(1.9 * kSampleRate / 256); i < gain.size(); ++i) REQUIRE(gain[i] <= during + 1.f);
    // The floor's level out: as it came in, raised by the phrase's gain and no more.
    CHECK(tailDb(out, 0.5) < rmsDb(floor) + 7.0);
}

TEST_CASE("vocal rider: Speed is how quickly it rides") {
    const Samples in = join(phrase(-18.0, 1.0), phrase(-30.0, 1.0));
    Rider fast(with(kFlat, {{"speed", 50.f}}));
    Rider slow(with(kFlat, {{"speed", 1000.f}}));
    const Samples a = fast.play(in);
    const Samples b = slow.play(in);
    // 300 ms after the drop the fast one is at the target, the slow one still well under it.
    const auto at = int64_t(1.3 * kSampleRate);
    CHECK_NEAR(rmsDb(a, at, at + 4800), -18.0, 1.0);
    CHECK(rmsDb(b, at, at + 4800) < -22.0);
}

TEST_CASE("vocal rider: Music Sens. moves the target towards the music's level") {
    const Samples vocal = phrase(-24.0, 3.0);
    const Samples music = noise(vocal.size(), 9, 0.3f * std::sqrt(3.f) * 0.5f);  // about -16 dB
    const double musicDb = rmsDb(music);
    Rider none(with(kFlat, {{"music_sens", 0.f}}));
    CHECK_NEAR(tailDb(none.playKeyed(vocal, vocal, music).first), -18.0, 0.5);
    Rider full(with(kFlat, {{"music_sens", 100.f}}));
    CHECK_NEAR(tailDb(full.playKeyed(vocal, vocal, music).first), musicDb, 0.7);
    Rider half(with(kFlat, {{"music_sens", 50.f}}));
    CHECK_NEAR(tailDb(half.playKeyed(vocal, vocal, music).first), 0.5 * (musicDb - 18.0), 0.7);
    // Silence on the sidechain: the target as it is.
    Rider quiet(with(kFlat, {{"music_sens", 100.f}}));
    CHECK_NEAR(tailDb(quiet.playKeyed(vocal, vocal, Samples(vocal.size(), 0.f)).first), -18.0, 0.5);
}

TEST_CASE("vocal rider: Output after the ride") {
    Rider rider(with(kFlat, {{"output", 6.f}}));
    CHECK_NEAR(tailDb(rider.play(phrase(-30.0, 2.0))), -12.0, 0.5);
}

TEST_CASE("vocal rider: the vocal weighting hears highs louder than lows") {
    // A low tone reads quieter weighted (the high-pass): brought up more than unweighted.
    const Samples low = sine(80.0, 2.0, std::sqrt(2.0) * 0.1);  // -20 dB
    Rider vocal;
    Rider flat(kFlat);
    CHECK(tailDb(vocal.play(low)) > tailDb(flat.play(low)) + 2.0);
    CHECK(allFinite(vocal.play(low)));
}

TEST_CASE("vocal rider: displays") {
    Rider rider(kFlat);
    rider.play(phrase(-30.0, 2.0));
    const std::vector<float> level = rider.display("level");
    const std::vector<float> gain = rider.display("gain");
    const std::vector<float> target = rider.display("target");
    const std::vector<float> music = rider.display("music");
    REQUIRE(level.size() == size_t(2.0 * kSampleRate / 256));
    CHECK_NEAR(level.back(), -30.f, 0.5f);
    CHECK_NEAR(gain.back(), 12.f, 0.5f);
    CHECK_EQ(target.back(), -18.f);
    CHECK_EQ(music.back(), -96.f);
}
