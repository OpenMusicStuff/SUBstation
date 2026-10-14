#pragma once
// The modulators' Random, as their editor draws it: the curve a seed makes, by
// the engine's own maths (engine/src/builtin/ModulatorDesign.h's randomPoints),
// so the curve drawn is the one that plays.

#include <vector>

namespace sub::app {

struct ModulatorPoint {
    double x = 0.0, y = 0.0, curve = 0.0;
    friend bool operator==(const ModulatorPoint&, const ModulatorPoint&) = default;
};

// The random curve of `seed` (as the device's "seed" display has it) from level `start` (its "start").
std::vector<ModulatorPoint> modulatorRandomPoints(int seed, double start);

}  // namespace sub::app
