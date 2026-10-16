// The tempo map and the time signatures (TempoMap.h): beats to seconds and back
// over steady stretches, ramps and jumps; bars across signature changes.

#include <cmath>

#include "music/TempoMap.h"
#include "harness/Fixtures.h"

using namespace subtest;
using sub::MeterMap;
using sub::TempoMap;

namespace {

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

}  // namespace

TEST_CASE("a steady tempo is beats times 60 over BPM") {
    const TempoMap map(120.0);
    CHECK(map.constant());
    CHECK(near(map.secondsAt(8.0), 4.0));
    CHECK(near(map.beatAt(4.0), 8.0));
    CHECK(near(map.secondsAt(-2.0), -1.0));  // (before the song: the first tempo backwards)
    CHECK(map.sampleAt(1.0, 48000.0) == 24000);
    CHECK(near(map.bpmAt(100.0), 120.0));
    // Out of range and no points.
    CHECK(near(TempoMap(5000.0).bpmAt(0.0), TempoMap::kMaxBpm));
    CHECK(near(TempoMap(std::vector<TempoMap::Point>{}).bpmAt(3.0), 120.0));
}

TEST_CASE("a tempo change takes effect at its beat") {
    // 120 BPM to beat 4, then a jump to 60.
    const TempoMap map({{0.0, 120.0}, {4.0, 120.0}, {4.0, 60.0}});
    CHECK(!map.constant());
    CHECK(near(map.secondsAt(4.0), 2.0));
    CHECK(near(map.secondsAt(6.0), 4.0));  // two beats at 60: two seconds more
    CHECK(near(map.beatAt(3.0), 5.0));
    CHECK(near(map.bpmAt(3.999), 120.0));
    CHECK(near(map.bpmAt(4.0), 60.0));
    // A first point after beat 0: its tempo from the start.
    const TempoMap late({{8.0, 90.0}});
    CHECK(near(late.secondsAt(3.0), 2.0));
}

TEST_CASE("a ramp's seconds are its tempo's integral, both ways") {
    // From 60 to 120 BPM over 8 beats: t(b) = 60 * 8 / 60 * ln(T(b) / 60).
    const TempoMap map({{0.0, 60.0}, {8.0, 120.0}});
    CHECK(near(map.bpmAt(4.0), 90.0));
    CHECK(near(map.secondsAt(8.0), 8.0 * std::log(2.0)));
    CHECK(near(map.secondsAt(4.0), 8.0 * std::log(1.5)));
    CHECK(near(map.secondsAt(10.0), 8.0 * std::log(2.0) + 1.0));  // steady at 120 after it
    // Against small steps (the midpoint rule).
    double seconds = 0.0;
    constexpr int kSteps = 100000;
    for (int s = 0; s < kSteps; ++s) seconds += 60.0 / map.bpmAt((s + 0.5) * 8.0 / kSteps) * 8.0 / kSteps;
    CHECK(near(map.secondsAt(8.0), seconds, 1e-6));
    // Round trips, slowing down too, and nearly steady.
    for (const TempoMap& m : {map, TempoMap({{0.0, 140.0}, {16.0, 70.0}}), TempoMap({{0.0, 120.0}, {4.0, 120.000001}})}) {
        for (double beat = 0.0; beat < 20.0; beat += 0.37) CHECK(near(m.beatAt(m.secondsAt(beat)), beat, 1e-9));
    }
}

TEST_CASE("samples follow the map at a rate") {
    const TempoMap map({{0.0, 120.0}, {4.0, 120.0}, {4.0, 60.0}});
    CHECK(map.sampleAt(4.0, 48000.0) == 96000);
    CHECK(map.sampleAt(5.0, 48000.0) == 144000);
    CHECK(near(map.beatAtSample(144000.0, 48000.0), 5.0));
}

TEST_CASE("bars follow the time signature, and its changes") {
    const MeterMap common;
    CHECK(common.constant());
    const MeterMap::Bar fifth = common.barAt(17.0);
    CHECK(fifth.index == 4);
    CHECK(near(fifth.start, 16.0));
    CHECK(near(fifth.length, 4.0));
    CHECK(near(common.barStart(10), 40.0));
    CHECK(common.barAt(-1.0).index == -1);

    // 4/4 for two bars, then 3/4 for two, then 6/8.
    const MeterMap map({{0.0, 4, 4}, {8.0, 3, 4}, {14.0, 6, 8}});
    CHECK(map.barAt(7.9).index == 1);
    CHECK(map.barAt(8.0).index == 2);
    CHECK(map.barAt(8.0).numerator == 3);
    CHECK(map.barAt(11.0).index == 3);
    CHECK(near(map.barAt(11.0).start, 11.0));
    CHECK(map.barAt(14.0).index == 4);
    CHECK(near(map.barAt(17.5).start, 17.0));  // 6/8: three beats a bar
    CHECK(map.barAt(17.5).denominator == 8);
    for (int64_t bar = 0; bar < 8; ++bar) CHECK(map.barAt(map.barStart(bar)).index == bar);
}

TEST_CASE("a time signature change off a bar line cuts the bar before short") {
    const MeterMap map({{0.0, 4, 4}, {6.0, 3, 4}});  // bar 1 is 2 beats long
    CHECK(map.barAt(5.0).index == 1);
    CHECK(near(map.barAt(5.0).length, 2.0));
    CHECK(map.barAt(6.0).index == 2);
    CHECK(near(map.barStart(2), 6.0));
    CHECK(near(map.barStart(3), 9.0));
    // A first change after beat 0 holds from 0; invalid ones are left out; the last of two at a beat wins.
    const MeterMap late({{4.0, 3, 4}, {4.0, 7, 8}, {9.0, 5, 3}});
    CHECK(late.at(0.0).numerator == 7);
    CHECK(late.changes().size() == 2);
}

TEST_CASE("a curved ramp's seconds are its tempo's integral, both ways") {
    const auto integral = [](const TempoMap& map, double to) {  // the midpoint rule, in small steps
        double seconds = 0.0;
        constexpr int kSteps = 200000;
        for (int s = 0; s < kSteps; ++s) seconds += 60.0 / map.bpmAt((s + 0.5) * to / kSteps) * to / kSteps;
        return seconds;
    };
    // Its shape is an automation segment's: x along it, T0 + (T1 - T0) * expm1(a x) / expm1(a), a = -6 curve
    // (turned round going down).
    const TempoMap up({{0.0, 60.0, 0.5}, {8.0, 120.0}});
    const double a = -0.5 * TempoMap::kCurvature;
    CHECK(near(up.bpmAt(4.0), 60.0 + 60.0 * std::expm1(a * 0.5) / std::expm1(a)));
    CHECK(up.bpmAt(4.0) > 90.0);  // bulging upward
    const TempoMap down({{0.0, 120.0, 0.5}, {8.0, 60.0}});
    CHECK(down.bpmAt(4.0) > 90.0);  // upward too, going down
    CHECK(near(up.bpmAt(8.0), 120.0));
    CHECK(near(up.bpmAt(0.0), 60.0));
    // Speeding up, slowing down, bent either way; the widest range; nearly a pure exponential
    // (20 to 20 e^3 BPM bent so its tempo is K e^(3x)); a slight bend.
    const std::vector<TempoMap> maps{up,
                                     down,
                                     TempoMap({{0.0, 60.0, -0.8}, {8.0, 120.0}}),
                                     TempoMap({{0.0, 120.0, -1.0}, {8.0, 60.0}}),
                                     TempoMap({{0.0, 20.0, 0.7}, {16.0, 999.0}}),
                                     TempoMap({{0.0, 999.0, -1.0}, {16.0, 20.0}}),
                                     TempoMap({{0.0, 20.0, -0.5}, {12.0, 20.0 * std::exp(3.0)}}),
                                     TempoMap({{0.0, 90.0, 0.0001}, {4.0, 100.0}})};
    for (const TempoMap& m : maps) {
        const double end = m.points().back().beat;
        CHECK(near(m.secondsAt(end), integral(m, end), 1e-6 * m.secondsAt(end)));
        CHECK(near(m.secondsAt(end / 3.0), integral(m, end / 3.0), 1e-6 * m.secondsAt(end)));
        for (double beat = 0.0; beat < end + 4.0; beat += 0.37) CHECK(near(m.beatAt(m.secondsAt(beat)), beat, 1e-9));
    }
    // A curve on a point that stays steady, or after the last, changes nothing.
    CHECK(TempoMap({{0.0, 120.0, 1.0}, {4.0, 120.0}}).constant());
    CHECK(near(TempoMap({{0.0, 120.0, 1.0}, {4.0, 120.0}}).secondsAt(4.0), 2.0));
    CHECK(near(TempoMap({{0.0, 60.0}, {8.0, 120.0, 1.0}}).secondsAt(9.0), 8.0 * std::log(2.0) + 0.5));
    CHECK(TempoMap({{0.0, 60.0, 7.0}}).points()[0].curve == 1.0);  // (clamped)
}
