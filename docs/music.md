# Musical time

[music/src/music](../music/src/music), the static library `sub_music`: the song's tempo and time signatures along the
timeline, and the conversions that follow from them; and grooves, the swing and feel every layer places notes with.
Plain C++ with nothing of the other layers, so the engine, the application layer, the UI and the intelligence module
all stand on it and count time the same way (`ctest -R boundaries` checks:
[architecture.md](architecture.md#the-boundaries-checked)). Headers are included by their folder:
`#include "music/TempoMap.h"`.

Positions on the timeline are quarter-note beats everywhere in SUBstation. What a beat is in seconds, and which bar it
is in, is this library's.

## TempoMap

Tempo breakpoints `(beat, bpm, curve)`, `kMinBpm` to `kMaxBpm` (20 to 999). Between two the tempo goes in beats from
one to the other (a ramp; the same BPM twice: steady): in a straight line, or bent by the first's `curve` (-1..1) in
the shape of an automation segment (`kCurvature` 6, the same as `automation::kCurvature`: x along it,
`T0 + (T1 - T0) * expm1(a x) / expm1(a)` with `a = -6 curve`, the curve turned round when it goes down, so positive
bulges upward either way). Two at the same beat are a jump, in their order.
Before the first and after the last it stays as they are, and a map whose first point is after beat 0 has that tempo
from beat 0. Made with no points: 120 BPM.

Seconds are the tempo's integral, exact: over a ramp from T0 to T1 BPM across L beats, a beat `x` into it is
`60 L / (T1 - T0) * ln(1 + (T1 - T0) x / (L T0))` seconds in (`log1p`, so a ramp of a hundredth of a BPM is as exact
as one of a hundred), and back by its inverse (`expm1`). A curved ramp's tempo is `P + K e^(a x)`, whose integral is
closed too: `60 L / P * (x - ln(T(x) / T0) / a)`, or, where `P` is all but 0 (nearly a pure exponential, where that
form would cancel), its series in `P / K`; back by Newton's method, kept within a bracket, to the last bit. Each
point's seconds are worked out once, when the map is made; a lookup is a binary search and one segment's formula.

| | |
|---|---|
| `bpmAt(beat)` | the tempo there (of a jump, the one after it) |
| `secondsAt(beat)`, `beatAt(seconds)` | from beat 0, both ways (before beat 0: the first tempo, backwards) |
| `sampleAt(beat, rate)`, `beatAtSample(sample, rate)` | the same in samples (rounded to the nearest) |
| `secondsBetween(from, to)` | how long a stretch lasts where it is |
| `constant()` | one tempo throughout |

## MeterMap

Time signature changes `(beat, numerator, denominator)`, each starting a bar. A bar is `numerator * 4 / denominator`
beats long. A change that doesn't fall on a bar line of the one before starts its bar there all the same, cutting the
bar before short, as Live does. Bars are counted from 0 at beat 0. A first change after beat 0 holds from 0; of two
at one beat the last wins; invalid signatures (a numerator outside 1 to 32, a denominator not a power of two up to 32)
are left out. Made with no changes: 4/4.

| | |
|---|---|
| `barAt(beat)` | its bar: index, start, length (a bar cut short is shorter) and signature |
| `barStart(bar)` | the beat a bar starts at |
| `at(beat)` | the signature in force |
| `valid(n, d)` | whether a signature is one SUBstation plays |

## Groove

[Groove.h](../music/src/music/Groove.h) (`namespace sub::groove`): swing and feel as a value, which the engine's Groove
device, Follow Groove, clips' grooves and Apply Groove all place notes with ([groove.md](groove.md) has the feature).
A `Groove` is a cycle of 1, 2 or 4 bars of four beats (`kBarBeats`), counted from beat 0 whatever the time
signatures, cut into steps of `stepBeats` (at most `kMaxSteps`, 256, a cycle). Each `Lane` has a `Step` for each step
of the cycle: `timing` (a fraction of a step, ±0.5: `kMaxTiming`), `timingMs` (±50 ms), `velocity` (added, ±1 of
127) and `length` (a factor, 0.25 to 2). The default lane (no keys) plays every key no other lane names; a lane with
keys plays those (the hats, the snare). `base` is the grid a note is found on (Ableton's Base).

`place(groove, beat, key, velocity, lengthBeats, tempo, amounts)` says where a note plays and how: a `Placed`, its
beat, velocity (1..127), length in beats and the step of the cycle it fell on. In the lane that plays its key:

- **Quantize** first: the note pulled toward the nearest point of the base grid, by `amounts.quantize`.
- **Timing** is a warp. Step k of the song (of cycle ⌊k / steps⌋) lands at the time of its beat moved by its offset
  in steps (`timing` times the Timing amount, plus Random's jitter), in seconds through the tempo map, plus `timingMs`
  times the Timing amount; a note between two steps lands in proportion of where it was between them, in seconds. A
  step that would land at or before an earlier one is held just after it (a thousandth of the time between them), so
  the warp only rises and a lane's notes can't change order.
- **Random**: a triangular jitter in (-1, 1), more often small than large, times `kRandomSteps` (a quarter of a step)
  at 100 %, from a hash of the seed, the cycle and the step: the same every render, different each cycle.
- **Velocity and length** come from the step at the base grid's point the note falls on (between two steps, the two
  interpolated): the velocity plus the step's times 127 times the Velocity amount (negative: turned round), held to
  1..127; the length times 1 + (the step's - 1) times the Length amount.
- **Milliseconds** are added in seconds where the tempo map puts the step, so they stay milliseconds under any tempo.

It neither allocates nor throws: the Groove device calls it on the audio thread. A groove not `normalized()` (its
lanes' steps not one for each step of the cycle) plays the note as it is.

| | |
|---|---|
| `Amounts` | how much of a groove plays: `timing` 0..2 (`kMaxTimingAmount`), `velocity` -1..1, `random` 0..1, `quantize` 0..1, `length` 0..1, and Random's `seed` |
| `normalized(groove)` | held to what a groove may be: a step dividing a beat into whole steps (else the nearest of `kSteps`), 1, 2 or 4 bars (fewer while that would be over 256 steps), a base grid over 0, each lane's steps one for each step of the cycle (missing ones neutral) with their values in range, the default lane first (made if none), keys in order and each in one lane only (the first naming it) |
| `laneFor(groove, key)` | the lane that plays a key: the first naming it, else the default lane |
| `stepsPerCycle(groove)`, `cycleBeats(groove)` | the cycle's steps and beats |
| `swing(percent, stepBeats)` | MPC swing: every second step late, so that it falls `percent` of the way through its pair of steps (`kMinSwing` 50: straight; `kTripletSwing` 66.7: a triplet feel; `kMaxSwing` 75); one bar, one lane, its base the step |
| `swingOf(groove)` | the swing a groove is, if it is one (made by `swing()`, or alike): its percent |
| `fromNotes(notes, stepBeats, bars, perKeyLanes)` | a groove from played notes (`NoteTime`: beat, key, velocity, length): each step's offset from the grid (the notes nearest it, averaged), its velocity from the notes' average and its length from theirs, over `bars` bars from beat 0 (later bars folded onto the cycle); one lane, or with `perKeyLanes` a lane a key besides the default (all the notes). Steps no note falls on take their neighbours' values, as the warp would pass them |
| `fromOnsets(onsetBeats, levels, stepBeats, bars)` | the same from an audio clip's transients: their velocities each level's share of the loudest |
| `toText(groove)`, `fromText(text)` | as text and back (lines: `groove 1`, `step`, `bars`, `base`, then each lane's `lane` and its keys, and a line `s timing ms velocity length` a step; anything else: none), normalized as it is read. Devices keep their groove in their state so |
| `kSteps`, `stepIndex(beats)` | the steps the device, the clip view and the piano roll offer (1/8, 1/8T, 1/16, 1/16T, 1/32; `kDefaultStep`: 1/16), and the one nearest a length |

## Tests

[tests/engine/test_tempo_map.cpp](../tests/engine/test_tempo_map.cpp) (in `engine_tests`): steady tempos, jumps,
ramps and curved ramps against the integral worked out in small steps, round trips (beats to seconds and back to within 1e-9 of a
beat, ramps speeding up and slowing down, nearly steady), samples, bars across changes, a change off a bar line.

[tests/engine/test_groove.cpp](../tests/engine/test_groove.cpp) (`engine_tests groove`): swing at 50 % moving
nothing, an offbeat 16th at 66.7 % on the triplet, offsets never reordering a key's notes, milliseconds staying
milliseconds under a tempo ramp, Random repeating with its seed and varying by cycle, Quantize 100 % then Timing 0 as
plain quantizing, velocity and length from a note's step, lanes, grooves from notes and from onsets, text and back.
