#pragma once
// The Supersaw's stacks, as its editor draws them: where each saw of a stack
// sits in pitch and in the stereo field and how loud it is, worked out by the
// engine's own maths (engine/src/builtin/SupersawDesign.h), so the saws drawn
// are the saws that play. In the parameters' units: a count, percent.

#include <vector>

namespace sub::app {

struct SupersawSaw {
    double cents = 0.0;  // from the stack's pitch
    double pan = 0.0;    // -1 (left) .. 1 (right)
    double gain = 0.0;   // its share of the stack's level: the gains' squares add up to 1
};

// The stacks a note plays, and the most saws a stack has.
int supersawStacks();
int supersawMaxSaws();

// A stack's saws, from the flattest to the sharpest (Saws held to 1..supersawMaxSaws()).
std::vector<SupersawSaw> supersawLayout(int saws, double detunePercent, double curvePercent, double blendPercent,
                                        double widthPercent);

// The outermost saws' detune for a Detune in percent, in cents.
double supersawDetuneCents(double detunePercent);

}  // namespace sub::app
