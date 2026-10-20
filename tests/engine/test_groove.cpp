// Groove (music/Groove.h): where place() puts notes (swing, the warp between
// steps, milliseconds under a tempo ramp, Random, Quantize, velocity and
// length, lanes), grooves made from notes and onsets, and grooves as text.

#include <cmath>
#include <vector>

#include "harness/Fixtures.h"
#include "music/Groove.h"

using namespace subtest;
using sub::TempoMap;
namespace groove = sub::groove;

namespace {

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

double placedBeat(const groove::Groove& g, double beat, const TempoMap& tempo = TempoMap(120.0),
                  const groove::Amounts& amounts = {}, int key = 60) {
    return groove::place(g, beat, key, 100.0, 0.25, tempo, amounts).beat;
}

// A groove of one lane whose steps are `steps`.
groove::Groove withSteps(std::vector<groove::Step> steps, double stepBeats = 0.25, int bars = 1) {
    groove::Groove g;
    g.stepBeats = stepBeats;
    g.base = stepBeats;
    g.bars = bars;
    g.lanes = {groove::Lane{{}, std::move(steps)}};
    return groove::normalized(std::move(g));
}

}  // namespace

TEST_CASE("groove: swing at 50 % moves nothing") {
    const groove::Groove straight = groove::swing(50.0, 0.25);
    REQUIRE(straight.lanes.size() == 1);
    CHECK(straight.lanes.front().steps.size() == 16);
    for (double beat = -1.0; beat < 9.0; beat += 0.0625) CHECK(near(placedBeat(straight, beat), beat, 1e-9));
}

TEST_CASE("groove: at 66.7 % an offbeat 16th lands on the triplet") {
    const groove::Groove triplet = groove::swing(groove::kTripletSwing, 0.25);
    CHECK(near(placedBeat(triplet, 0.0), 0.0));
    CHECK(near(placedBeat(triplet, 0.25), 1.0 / 3, 1e-9));
    CHECK(near(placedBeat(triplet, 0.5), 0.5));
    CHECK(near(placedBeat(triplet, 0.75), 0.5 + 1.0 / 3, 1e-9));
    CHECK(near(placedBeat(triplet, 7.75), 7.5 + 1.0 / 3, 1e-9));  // (every bar alike)
    // Eighths: the offbeat eighth on the triplet of the beat.
    const groove::Groove eighths = groove::swing(groove::kTripletSwing, 0.5);
    CHECK(near(placedBeat(eighths, 1.5), 1.0 + 2.0 / 3, 1e-9));
    // Between steps a note moves in proportion (the warp): halfway, half as far.
    CHECK(near(placedBeat(triplet, 0.125), (0.0 + 1.0 / 3) / 2, 1e-9));
    // Timing scales it: at 50 % halfway to the triplet.
    groove::Amounts half;
    half.timing = 0.5;
    CHECK(near(placedBeat(triplet, 0.25, TempoMap(120.0), half), 0.25 + (1.0 / 3 - 0.25) / 2, 1e-9));
    // 75 %: the offbeat half a step late.
    CHECK(near(placedBeat(groove::swing(75.0, 0.25), 0.25), 0.375, 1e-9));
    // What swing() made, swingOf() reads back.
    REQUIRE(groove::swingOf(triplet).has_value());
    CHECK(near(*groove::swingOf(triplet), groove::kTripletSwing, 1e-9));
    CHECK(!groove::swingOf(withSteps({{0.1}, {0.0}})).has_value());
}

TEST_CASE("groove: offsets never reorder notes on a key") {
    // Neighbouring steps pulling hard against each other, at 200 %, with milliseconds and Random.
    std::vector<groove::Step> steps(16);
    for (size_t i = 0; i < steps.size(); ++i) {
        steps[i].timing = i % 2 == 0 ? 0.5 : -0.5;
        steps[i].timingMs = i % 3 == 0 ? 50.0 : -50.0;
    }
    const groove::Groove fierce = withSteps(steps);
    groove::Amounts amounts;
    amounts.timing = 2.0;
    amounts.random = 1.0;
    amounts.seed = 7;
    for (const TempoMap& tempo : {TempoMap(120.0), TempoMap(300.0), TempoMap({{0.0, 60.0}, {16.0, 240.0}})}) {
        double last = -1e9;
        int out = 0;
        for (double beat = 0.0; beat < 16.0; beat += 1.0 / 64) {
            const double placed = placedBeat(fierce, beat, tempo, amounts);
            if (!(placed > last)) ++out;
            last = placed;
        }
        CHECK(out == 0);
    }
    // Swing at its most: the offbeat late by half a step, still before the next step.
    const groove::Groove most = groove::swing(75.0, 0.25);
    amounts = {};
    amounts.timing = 2.0;
    double last = -1e9;
    for (double beat = 0.0; beat < 4.0; beat += 1.0 / 128) {
        const double placed = placedBeat(most, beat, TempoMap(120.0), amounts);
        CHECK(placed > last);
        last = placed;
    }
}

TEST_CASE("groove: milliseconds stay milliseconds under a tempo ramp") {
    std::vector<groove::Step> steps(16);
    for (groove::Step& step : steps) step.timingMs = 8.0;  // a snare 8 ms late, at any tempo
    const groove::Groove late = withSteps(steps);
    const TempoMap ramp({{0.0, 70.0}, {32.0, 180.0}});
    for (double beat = 0.0; beat < 32.0; beat += 0.25) {
        const double placed = placedBeat(late, beat, ramp);
        CHECK(near(ramp.secondsAt(placed) - ramp.secondsAt(beat), 0.008, 1e-9));
    }
    // And early.
    for (groove::Step& step : steps) step.timingMs = -12.0;
    const groove::Groove early = withSteps(steps);
    CHECK(near(ramp.secondsAt(placedBeat(early, 5.0, ramp)) - ramp.secondsAt(5.0), -0.012, 1e-9));
}

TEST_CASE("groove: Random repeats with its seed and varies by cycle") {
    const groove::Groove straight = groove::swing(50.0, 0.25);
    groove::Amounts amounts;
    amounts.random = 1.0;
    amounts.seed = 42;
    const TempoMap tempo(120.0);
    std::vector<double> offsets;
    for (int bar = 0; bar < 8; ++bar) {
        const double beat = bar * 4.0 + 1.0;  // the same step, cycle after cycle
        const double a = placedBeat(straight, beat, tempo, amounts);
        CHECK(a == placedBeat(straight, beat, tempo, amounts));
        CHECK(std::abs(a - beat) <= groove::kRandomSteps * 0.25 + 1e-9);
        offsets.push_back(a - beat);
    }
    int alike = 0;
    for (size_t i = 1; i < offsets.size(); ++i) alike += offsets[i] == offsets[0] ? 1 : 0;
    CHECK(alike == 0);
    groove::Amounts other = amounts;
    other.seed = 43;
    CHECK(placedBeat(straight, 1.0, tempo, other) != placedBeat(straight, 1.0, tempo, amounts));
    amounts.random = 0.0;
    CHECK(near(placedBeat(straight, 1.0, tempo, amounts), 1.0));
}

TEST_CASE("groove: Quantize 100 % then Timing 0 is plain quantizing") {
    const groove::Groove triplet = groove::swing(groove::kTripletSwing, 0.25);
    groove::Amounts amounts;
    amounts.quantize = 1.0;
    amounts.timing = 0.0;
    for (const double beat : {0.07, 0.2, 0.3, 1.12, 2.49, 3.9}) {
        CHECK(near(placedBeat(triplet, beat, TempoMap(97.0), amounts), std::floor(beat / 0.25 + 0.5) * 0.25, 1e-9));
    }
    // Quantize 100 %, Timing 100 %: onto the grid, then the groove's place for it.
    amounts.timing = 1.0;
    CHECK(near(placedBeat(triplet, 0.29, TempoMap(120.0), amounts), 1.0 / 3, 1e-9));
    // Half: halfway to the grid first.
    amounts.quantize = 0.5;
    amounts.timing = 0.0;
    CHECK(near(placedBeat(triplet, 0.3, TempoMap(120.0), amounts), 0.275, 1e-9));
    // Quantize 0: the note keeps its place between the steps around it, as they move.
    amounts = {};
    CHECK(near(placedBeat(triplet, 0.26, TempoMap(120.0), amounts), 1.0 / 3 + 0.04 * (0.5 - 1.0 / 3), 1e-9));
}

TEST_CASE("groove: velocity and length come from the step a note falls on") {
    std::vector<groove::Step> steps(16);
    steps[2].velocity = 0.25;   // +31.75
    steps[3].velocity = -0.5;
    steps[2].length = 2.0;
    const groove::Groove g = withSteps(steps);
    const TempoMap tempo(120.0);
    groove::Amounts amounts;
    groove::Placed placed = groove::place(g, 0.52, 60, 64.0, 0.5, tempo, amounts);
    CHECK(near(placed.velocity, 64.0 + 31.75));
    CHECK(near(placed.lengthBeats, 0.5));  // (Length 0: lengths as they are)
    CHECK(placed.step == 2);
    amounts.length = 1.0;
    CHECK(near(groove::place(g, 0.5, 60, 64.0, 0.5, tempo, amounts).lengthBeats, 1.0));
    amounts.length = 0.5;
    CHECK(near(groove::place(g, 0.5, 60, 64.0, 0.5, tempo, amounts).lengthBeats, 0.75));
    // Held to 1..127; Velocity turned round, and scaled.
    CHECK(near(groove::place(g, 0.75, 60, 10.0, 0.5, tempo, {}).velocity, 1.0));
    amounts = {};
    amounts.velocity = -1.0;
    CHECK(near(groove::place(g, 0.75, 60, 100.0, 0.5, tempo, amounts).velocity, 127.0));
    amounts.velocity = 0.5;
    CHECK(near(groove::place(g, 0.5, 60, 64.0, 0.5, tempo, amounts).velocity, 64.0 + 31.75 / 2));
    // A base grid between two steps: their values interpolated.
    groove::Groove fine = g;
    fine.base = 0.125;
    CHECK(near(groove::place(fine, 0.625, 60, 64.0, 0.5, tempo, {}).velocity, 64.0 + (31.75 - 63.5) / 2));
}

TEST_CASE("groove: a lane plays its own keys, the default lane the rest") {
    std::vector<groove::Step> late(16), early(16);
    for (groove::Step& step : late) step.timing = 0.2;
    for (groove::Step& step : early) step.timing = -0.1;
    groove::Groove g;
    g.lanes = {groove::Lane{{}, early}, groove::Lane{{42, 46}, late}};
    g = groove::normalized(std::move(g));
    const TempoMap tempo(120.0);
    CHECK(near(placedBeat(g, 1.0, tempo, {}, 42), 1.05, 1e-9));
    CHECK(near(placedBeat(g, 1.0, tempo, {}, 46), 1.05, 1e-9));
    CHECK(near(placedBeat(g, 1.0, tempo, {}, 36), 0.975, 1e-9));
    // normalized(): the default lane first, a key in one lane only, steps one a step.
    groove::Groove messy;
    messy.stepBeats = 0.27;  // (not a step: the nearest)
    messy.bars = 3;
    messy.lanes = {groove::Lane{{46, 42}, {}}, groove::Lane{{42}, {}}};
    messy = groove::normalized(std::move(messy));
    CHECK(near(messy.stepBeats, 0.25));
    CHECK(messy.bars == 4);
    REQUIRE(messy.lanes.size() == 2);
    CHECK(messy.lanes[0].keys.empty());
    CHECK(messy.lanes[1].keys == (std::vector<int>{42, 46}));
    CHECK(messy.lanes[0].steps.size() == 64);
    CHECK(groove::laneFor(messy, 42) == &messy.lanes[1]);
    CHECK(groove::laneFor(messy, 60) == &messy.lanes[0]);
}

TEST_CASE("groove: from notes, each step's offset and velocity") {
    // A drummer's 16ths: the offbeats 0.06 of a beat late, the downbeats loud.
    std::vector<groove::NoteTime> notes;
    for (int bar = 0; bar < 2; ++bar) {
        for (int s = 0; s < 16; ++s) {
            const double beat = bar * 4.0 + s * 0.25 + (s % 2 == 1 ? 0.06 : 0.0);
            notes.push_back({beat, 42, s % 4 == 0 ? 120.0 : 80.0, 0.1});
        }
    }
    const groove::Groove played = groove::fromNotes(notes, 0.25, 1, false);
    REQUIRE(played.lanes.size() == 1);
    const std::vector<groove::Step>& steps = played.lanes.front().steps;
    CHECK(near(steps[1].timing, 0.24, 1e-9));
    CHECK(near(steps[0].timing, 0.0, 1e-9));
    CHECK(near(steps[0].velocity, (120.0 - 90.0) / 127.0, 1e-9));
    CHECK(near(steps[1].velocity, (80.0 - 90.0) / 127.0, 1e-9));
    // Placing straight notes with it plays them as the drummer did.
    CHECK(near(placedBeat(played, 0.25), 0.31, 1e-9));
    // Per key: the snare's own feel, the rest the default lane's.
    notes.push_back({1.03, 38, 100.0, 0.1});
    notes.push_back({3.03, 38, 100.0, 0.1});
    const groove::Groove perKey = groove::fromNotes(notes, 0.25, 1, true);
    REQUIRE(perKey.lanes.size() == 3);
    const groove::Lane* snare = groove::laneFor(perKey, 38);
    REQUIRE(snare != nullptr);
    CHECK(snare->keys == std::vector<int>{38});
    CHECK(near(snare->steps[4].timing, 0.12, 1e-9));
    // Steps no snare fell on take their neighbours' values.
    CHECK(near(snare->steps[8].timing, 0.12, 1e-9));
}

TEST_CASE("groove: from onsets, their levels as velocities") {
    const std::vector<double> onsets = {0.0, 0.27, 0.5, 0.77};
    const std::vector<float> levels = {1.f, 0.5f, 1.f, 0.5f};
    const groove::Groove heard = groove::fromOnsets(onsets, levels, 0.25, 1);
    REQUIRE(heard.lanes.size() == 1);
    CHECK(near(heard.lanes.front().steps[1].timing, 0.08, 1e-9));
    CHECK(heard.lanes.front().steps[0].velocity > 0.0);
    CHECK(heard.lanes.front().steps[1].velocity < 0.0);
}

TEST_CASE("groove: as text and back") {
    std::vector<groove::NoteTime> notes = {{0.02, 36, 110.0, 0.2}, {0.27, 42, 70.0, 0.1}, {1.0, 38, 90.0, 0.3}};
    groove::Groove g = groove::fromNotes(notes, 1.0 / 6, 2, true);
    g.lanes.front().steps[3].timingMs = -7.25;
    const std::string text = groove::toText(g);
    const std::optional<groove::Groove> back = groove::fromText(text);
    REQUIRE(back.has_value());
    CHECK(*back == g);
    CHECK(!groove::fromText("").has_value());
    CHECK(!groove::fromText("not a groove").has_value());
    CHECK(!groove::fromText("groove 1\ns 0 0 x 1\n").has_value());
}
