#pragma once
// The Snarl bass synth's maths, as its editor draws it: the wobble's shapes,
// and where the filter's frequency stands along its range and among the Vowel
// filter's vowels. Worked out by the engine's own functions
// (engine/src/builtin/SnarlDesign.h), so the shape drawn is the one that plays.

#include <QString>

namespace sub::app {

// The wobble's shape `shape` (Wobble Shape's index) at `phase` (0..1) of cycle
// `cycle`: 0..1, as the device moves the filter, the growl and the pitch by it.
double snarlWobble(int shape, double phase, int cycle);

// A frequency's place along the filter's range (0..1, log), and the vowel the
// Vowel filter speaks there ("U", "O", "A", "E" or "I": the nearest).
double snarlFreqPosition(double hz);
QString snarlVowel(double position);

}  // namespace sub::app
