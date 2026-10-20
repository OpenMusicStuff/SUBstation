#include "audio/SnarlResponse.h"

#include "builtin/SnarlDesign.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sub::app {

double snarlWobble(int shape, double phase, int cycle) {
    const int last = static_cast<int>(sub::snarl::wobbleShapeLabels().size()) - 1;
    return sub::snarl::wobble(static_cast<sub::snarl::WobbleShape>(std::clamp(shape, 0, last)),
                              phase - std::floor(phase), static_cast<uint32_t>(cycle));
}

double snarlFreqPosition(double hz) { return std::clamp(sub::snarl::freqPosition(hz), 0.0, 1.0); }

QString snarlVowel(double position) {
    const double place = std::clamp(position, 0.0, 1.0) * (sub::snarl::kVowels - 1);
    return QString(QLatin1Char(sub::snarl::kVowelNames[static_cast<size_t>(std::lround(place))]));
}

}  // namespace sub::app
