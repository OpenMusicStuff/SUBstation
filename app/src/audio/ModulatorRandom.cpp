#include "audio/ModulatorRandom.h"

#include "builtin/ModulatorDesign.h"

#include <cstdint>

namespace sub::app {

std::vector<ModulatorPoint> modulatorRandomPoints(int seed, double start) {
    sub::modulator::Point points[sub::modulator::kRandomPoints];
    const int count = sub::modulator::randomPoints(static_cast<uint32_t>(seed < 0 ? 0 : seed),
                                                   static_cast<float>(start), points);
    std::vector<ModulatorPoint> out;
    for (int i = 0; i < count; ++i) out.push_back({points[i].x, points[i].y, points[i].curve});
    return out;
}

}  // namespace sub::app
