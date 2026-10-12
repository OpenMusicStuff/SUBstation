#pragma once
// The Vintage Hall device's maths, as its editor draws it: from the engine's
// own (engine/src/builtin/VintageHallDesign.h), so the loop drawn is the one
// that plays: its parts where Size puts them, the time a lap takes, the output
// taps' weights for Attack, and its displays.

#include <array>

namespace sub::app {

// The loop for Size (0..100 %), as fractions of a lap from where the left comes in.
struct VintageHallRing {
    enum Point { LeftIn = 0, L1, A, L2, R1, B, L3, RightIn, R2, C, L4, R3, D, R4, kPoints };
    std::array<double, kPoints> at{};
    double seconds = 0.0;  // a lap
    // Each all-pass's (A..D) levels, outermost first: their lengths as shares of the outermost's (0: none).
    std::array<std::array<double, 3>, 4> levels{};
};
VintageHallRing vintageHallRing(double sizePercent);

// The weights of a side's four taps for Attack (%): its own three, then the other half's.
std::array<float, 4> vintageHallTapWeights(double attackPercent);

// The modulation's range at full Mod Depth (ms).
double vintageHallModRangeMs();

// Audio samples per value of the device's displays, and their floor (dB).
int vintageHallDisplaySamples();
double vintageHallDisplayFloorDb();

}  // namespace sub::app
