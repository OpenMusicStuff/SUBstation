# Musical time

[music/src/music](../music/src/music), the static library `sub_music`: the song's tempo and time signatures along the
timeline, and the conversions that follow from them. Plain C++ with nothing of the other layers, so the engine, the
application layer, the UI and the intelligence module all stand on it and count time the same way (`ctest -R
boundaries` checks: [architecture.md](architecture.md#the-boundaries-checked)). Headers are included by their folder:
`#include "music/TempoMap.h"`.

Positions on the timeline are quarter-note beats everywhere in SUBstation. What a beat is in seconds, and which bar it
is in, is this library's.

## TempoMap

Tempo breakpoints `(beat, bpm)`, `kMinBpm` to `kMaxBpm` (20 to 999). Between two the tempo goes in a straight line in
beats from one to the other (a ramp; the same BPM twice: steady); two at the same beat are a jump, in their order.
Before the first and after the last it stays as they are, and a map whose first point is after beat 0 has that tempo
from beat 0. Made with no points: 120 BPM.

Seconds are the tempo's integral, exact: over a ramp from T0 to T1 BPM across L beats, a beat `x` into it is
`60 L / (T1 - T0) * ln(1 + (T1 - T0) x / (L T0))` seconds in (`log1p`, so a ramp of a hundredth of a BPM is as exact
as one of a hundred), and back by its inverse (`expm1`). Each point's seconds are worked out once, when the map is
made; a lookup is a binary search and one segment's formula.

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

## Tests

[tests/engine/test_tempo_map.cpp](../tests/engine/test_tempo_map.cpp) (in `engine_tests`): steady tempos, jumps,
ramps against the integral worked out in small steps, round trips (beats to seconds and back to within 1e-9 of a
beat, ramps speeding up and slowing down, nearly steady), samples, bars across changes, a change off a bar line.
