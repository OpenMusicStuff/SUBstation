// The built-in Overdrive: its listing, its band and Tone playing as their
// design draws them, Drive's harmonics (even ones too), Dynamics, Dry/Wet, no
// DC, changes without clicks, the same in any blocks, and silence.

#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include "builtin/OverdriveDesign.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"

using namespace subtest;
namespace od = sub::overdrive;

namespace {

Samples played(const ParamValues& values, const Samples& in) {
    Standalone device("overdrive", kSampleRate, values);
    return device.play(in);
}

double amplitudeOf(const Samples& out, double freq) {
    return windowedAmplitude(out, freq, kSampleRate / 2, static_cast<int64_t>(0.4 * kSampleRate));
}

}  // namespace

TEST_CASE("the overdrive is listed as an audio effect") {
    const sub::BuiltinInfo info = builtinInfo("overdrive");
    CHECK_EQ(info.name, std::string("Overdrive"));
    CHECK(info.category == sub::BuiltinCategory::AudioEffect);
    const std::vector<std::string> ids{"freq", "width", "drive", "tone", "dynamics", "mix"};
    CHECK(paramIds(info.params) == ids);
}

TEST_CASE("a quiet tone comes through the band and Tone as the design draws them") {
    // Quiet and undriven, the curve passes it (a slope of 1 at 0); ADAA averages each sample with the last
    // (cos(pi f / sr)).
    for (const double freq : {100.0, 200.0, 1000.0, 3000.0, 9000.0}) {
        for (const float tone : {0.f, 50.f, 100.f}) {
            INFO(std::to_string(freq) + std::string(" Hz, tone ") + std::to_string(tone));
            const ParamValues values{{"drive", 0.f}, {"dynamics", 0.f}, {"freq", 800.f}, {"width", 3.f}, {"tone", tone}};
            const double got = db(amplitudeOf(played(values, sine(freq, 1.0, 0.001)), freq) / 0.001);
            const double want = od::bandResponseDb(freq, 800.0, 3.0, kSampleRate) +
                                od::toneResponseDb(freq, tone, kSampleRate) +
                                db(std::cos(od::kPi * freq / kSampleRate));
            CHECK_NEAR(got, want, 0.05);
        }
    }
}

TEST_CASE("drive adds harmonics, even ones too, and holds the level") {
    const Samples in = sine(300.0, 1.0, 0.5);
    double lastThird = 0.0;
    for (const float drive : {0.f, 30.f, 60.f, 100.f}) {
        INFO(std::string("drive ") + std::to_string(drive));
        const Samples out = played({{"drive", drive}, {"tone", 100.f}, {"dynamics", 0.f}, {"width", 9.f}}, in);
        const double fundamental = amplitudeOf(out, 300.0);
        const double second = amplitudeOf(out, 600.0) / fundamental;
        const double third = amplitudeOf(out, 900.0) / fundamental;
        if (drive < 100.f) CHECK(second > 0.005);  // the curve's lean (driven flat out, it is nearly a square)
        CHECK(third >= lastThird);
        lastThird = third;
        CHECK(maxAbs(slice(out, kSampleRate / 4)) < 1.3);
    }
    CHECK(lastThird > 0.15);
}

TEST_CASE("dynamics keeps quiet parts quieter") {
    const auto ratioDb = [](float dynamics) {
        const ParamValues values{{"drive", 80.f}, {"dynamics", dynamics}};
        const double loud = rmsDb(played(values, sine(500.0, 1.0, 0.5)), kSampleRate / 2);
        const double quiet = rmsDb(played(values, sine(500.0, 1.0, 0.05)), kSampleRate / 2);
        return loud - quiet;
    };
    const double none = ratioDb(0.f), half = ratioDb(50.f), full = ratioDb(100.f);
    CHECK(none < 4.0);  // squashed: 20 dB in, a few out
    CHECK(half > none + 3.0);
    CHECK(full > half + 3.0);
    CHECK(full < 21.0);  // about as much as came in (20 dB), no more
    CHECK_APPROX(od::dynamicsGain(0.1, 0.8, 100.0), 0.125);
    CHECK(od::dynamicsGain(0.9, 0.8, 100.0) == 1.0);
    CHECK(od::dynamicsGain(0.1, 0.8, 0.0) == 1.0);
}

TEST_CASE("dry/wet, DC, and silence") {
    const Samples in = noise(kSampleRate / 4, 7, 0.4f);
    CHECK(played({{"mix", 0.f}, {"drive", 100.f}}, in) == in);
    // The lean puts no DC out.
    const Samples out = played({{"drive", 100.f}, {"width", 9.f}, {"freq", 300.f}}, sine(300.0, 2.0, 0.8));
    CHECK(std::abs(mean(slice(out, kSampleRate))) < 1e-3);
    // Silence after a sound rings out to exact zeros.
    Samples burst = noise(kSampleRate / 10, 3, 0.5f);
    burst.resize(kSampleRate * 3, 0.f);
    CHECK(allEqual(slice(played({{"drive", 70.f}}, burst), kSampleRate * 2), 0.0));
}

TEST_CASE("turning the overdrive's controls doesn't click, and blocks don't matter") {
    const Samples in = sine(220.0, 1.0, 0.3);
    for (const auto& [id, from, to] : std::vector<std::tuple<std::string, float, float>>{
             {"drive", 0.f, 100.f}, {"freq", 200.f, 5000.f}, {"width", 0.5f, 9.f}, {"tone", 0.f, 100.f},
             {"dynamics", 0.f, 100.f}, {"mix", 0.f, 100.f}}) {
        INFO(id);
        Standalone device("overdrive", kSampleRate, {{id, from}});
        const Samples out = device.play(in, {{kSampleRate / 2, id, to}});
        const double before = largestStep(out, kSampleRate / 4, kSampleRate / 2 - 10);
        const double after = largestStep(out, kSampleRate * 3 / 4, kSampleRate - 10);
        CHECK(largestStep(out, kSampleRate / 2 - 10, kSampleRate / 2 + 2000) < 1.5 * std::max(before, after) + 1e-3);
    }
    const ParamValues settings{{"drive", 70.f}, {"dynamics", 60.f}, {"tone", 30.f}};
    Standalone a("overdrive", kSampleRate, settings), b("overdrive", kSampleRate, settings);
    const Samples sound = noise(kSampleRate, 5, 0.4f);
    CHECK(a.play(sound, {}, 37) == b.play(sound, {}, 1024));
}

TEST_CASE("the overdrive's displays") {
    Standalone device("overdrive", kSampleRate, {{"drive", 50.f}});
    device.play(sine(1000.0, 0.1, 0.5));
    const std::vector<float> drive = device.display("drive"), output = device.display("output"),
                             input = device.display("input");
    CHECK(drive.size() == size_t(kSampleRate / 10 / 128));
    CHECK(output.size() == drive.size());
    CHECK(input.size() == size_t(kSampleRate / 10));
    // Driven by 21 dB, the band's 1 kHz peak (near 0.5) goes far into the curve.
    CHECK(drive.back() > 4.0f);
    CHECK(output.back() > 0.2f);
    CHECK(output.back() < 1.2f);
}
