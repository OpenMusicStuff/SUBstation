#pragma once
// Groove: swing and feel, as a value every layer shares. The Groove device
// grooves the notes reaching it, a clip's groove moves its notes or its audio,
// and Apply Groove writes it into notes; all of them place notes with place().
// No Qt: the engine, the application layer and the tests share it.
//
// A groove is defined over one cycle of whole bars (of four beats), in beats,
// so the tempo map moves it with the song: a step every `stepBeats`, each with
// how late (or early) it plays, in a fraction of a step and in milliseconds,
// how much louder, and how much longer. Lanes give some keys a feel of their
// own (the hats, the snare); the default lane takes every other key.
//
// Timing is a warp: each step's place is moved by its offsets, and between two
// steps a note moves in proportion, so the warp only rises and place() can't
// reorder the notes of a lane. A step that would land at or before the one
// before it is held just after it. Velocity and length come from the step a
// note falls on (the nearest point of the base grid; between two steps, the
// two interpolated).

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "music/TempoMap.h"

namespace sub::groove {

struct Step {
    double timing = 0.0;    // fraction of a step, -0.5..0.5 (+ later)
    double timingMs = 0.0;  // and milliseconds, -50..50, the same at any tempo
    double velocity = 0.0;  // added, -1..1 of full scale (127)
    double length = 1.0;    // a note's length times this, 0.25..2
    friend bool operator==(const Step&, const Step&) = default;
};

struct Lane {               // an optional lane for some keys only (the hats, the snare)
    std::vector<int> keys;  // empty: the default lane, for every key no other lane takes
    std::vector<Step> steps;  // one per step of the cycle
    friend bool operator==(const Lane&, const Lane&) = default;
};

struct Groove {
    double stepBeats = 0.25;  // 1/16 (1/8, 1/8T, 1/16T, 1/32...)
    int bars = 1;             // the cycle: 1, 2 or 4 bars of four beats
    std::vector<Lane> lanes;  // at least the default lane (normalized(): first)
    double base = 0.25;       // the grid a note is found on (Ableton's Base)
    friend bool operator==(const Groove&, const Groove&) = default;
};

// How much of a groove plays. Timing scales the steps' offsets (and their
// milliseconds), 0..2; Velocity the velocities, -1..1 (negative: turned
// round); Length how far a note's length goes to its step's, 0..1; Random adds
// a triangular jitter, 0..1 (at 1: up to kRandomSteps of a step either way);
// Quantize pulls a note toward the base grid first, 0..1. The jitter comes from
// a hash of the seed, the cycle and the step: a render is the same every time,
// and each cycle varies.
struct Amounts {
    double timing = 1.0;
    double velocity = 1.0;
    double random = 0.0;
    double quantize = 0.0;
    double length = 0.0;
    uint32_t seed = 0;
    friend bool operator==(const Amounts&, const Amounts&) = default;
};

// Where a note plays, and how: its beat, velocity (1..127) and length in beats,
// and the step of the cycle it fell on (0..stepsPerCycle - 1).
struct Placed {
    double beat = 0.0;
    double velocity = 100.0;
    double lengthBeats = 0.0;
    int step = 0;
};

// A note a groove is made from (fromNotes): beat, key, velocity (1..127), length.
struct NoteTime {
    double beat = 0.0;
    int key = 60;
    double velocity = 100.0;
    double lengthBeats = 0.25;
    friend bool operator==(const NoteTime&, const NoteTime&) = default;
};

// The steps the device, the clip view and the piano roll offer: (name, beats).
struct StepChoice {
    const char* name;
    double beats;
};
inline constexpr std::array<StepChoice, 5> kSteps{{{"1/8", 0.5},
                                                   {"1/8T", 1.0 / 3},
                                                   {"1/16", 0.25},
                                                   {"1/16T", 1.0 / 6},
                                                   {"1/32", 0.125}}};
inline constexpr int kDefaultStep = 2;  // 1/16
// The step choice nearest `beats` (an index into kSteps).
int stepIndex(double beats);

inline constexpr double kBarBeats = 4.0;        // a groove's bar
inline constexpr int kMaxSteps = 256;           // in a cycle
inline constexpr double kMaxTiming = 0.5;       // of a step, either way
inline constexpr double kMaxTimingMs = 50.0;    // either way
inline constexpr double kMinLength = 0.25;
inline constexpr double kMaxLength = 2.0;
inline constexpr double kMaxTimingAmount = 2.0;
inline constexpr double kRandomSteps = 0.25;    // Random at 100 %: of a step, either way
inline constexpr double kMinSwing = 50.0;       // MPC swing, percent: straight
inline constexpr double kTripletSwing = 200.0 / 3.0;
inline constexpr double kMaxSwing = 75.0;

// Steps in a cycle, and the cycle's beats.
int stepsPerCycle(const Groove& groove);
double cycleBeats(const Groove& groove);

// A groove held to what it may be: a step that divides a beat into whole steps
// (the nearest of kSteps otherwise), 1, 2 or 4 bars (nearer), a base grid > 0,
// each lane's steps one per step of the cycle (missing ones neutral) with their
// values in range, the default lane first (made if none), keys in order, and a
// key in one lane only (the first that names it).
Groove normalized(Groove groove);

// The lane that plays `key`: the first naming it, else the default lane.
// (A groove without lanes: null.)
const Lane* laneFor(const Groove& groove, int key);

// Where a note at `beat` on `key` plays, and how: the step it falls on
// (nearest within half a step on the base grid; between steps, the offsets are
// interpolated), its offsets scaled by `amounts`. Velocity 1..127. Milliseconds
// go through the tempo map at the note's beat, so they stay milliseconds under
// any tempo. Pure and deterministic; it neither allocates nor throws (the
// Groove device calls it on the audio thread). A groove not normalized() plays
// the note as it is.
Placed place(const Groove& groove, double beat, int key, double velocity, double lengthBeats, const TempoMap& tempo,
             const Amounts& amounts);

// MPC swing: every second step of the cycle late, so that it falls `percent`
// of the way through its pair of steps (50: straight; 66.7: a triplet feel; up
// to 75). One bar, one lane, its base the step.
Groove swing(double percent, double stepBeats = 0.25);
// The swing a groove is, if it is one (made by swing(), or alike): its percent.
std::optional<double> swingOf(const Groove& groove);

// A groove from played notes: each step's offset from the grid (the notes on
// it, averaged), their velocities from the notes' average and their lengths
// from theirs, over `bars` bars from beat 0 (later bars folded onto the
// cycle). One lane, or with `perKeyLanes` a lane a key besides the default
// (all the notes), so a drummer's hats and snare keep their own feel. Steps no
// note falls on take their neighbours' values, as the warp would pass them.
Groove fromNotes(std::span<const NoteTime> notes, double stepBeats, int bars, bool perKeyLanes);
// A groove from an audio clip's transients: their beats and levels (any
// scale: velocity is each level's share of the loudest).
Groove fromOnsets(std::span<const double> onsetBeats, std::span<const float> levels, double stepBeats, int bars);

// A groove as text (lines: "groove 1", "step", "bars", "base", then each lane:
// "lane" and its keys, a line "s timing ms velocity length" a step), and back
// (none: not a groove). Devices keep their groove in their state this way.
std::string toText(const Groove& groove);
std::optional<Groove> fromText(std::string_view text);

}  // namespace sub::groove
