#include "audio/VintageHallResponse.h"

#include "builtin/VintageHallDesign.h"

namespace sub::app {

namespace vh = sub::vintage_hall;

static_assert(int(VintageHallRing::kPoints) == int(vh::RingMap::kPoints) && int(VintageHallRing::R4) == int(vh::RingMap::R4),
              "the editor's ring and the engine's name the same points");

VintageHallRing vintageHallRing(double sizePercent) {
    const vh::RingMap map = vh::ringMap(sizePercent / 100.0);
    VintageHallRing ring;
    for (int k = 0; k < VintageHallRing::kPoints; ++k) ring.at[std::size_t(k)] = map.at[k] / map.length;
    ring.seconds = map.length / vh::kReferenceRate;
    const vh::Allpass* designs[4] = {&vh::kA, &vh::kB, &vh::kC, &vh::kD};
    for (std::size_t a = 0; a < 4; ++a)
        for (int k = 0; k < designs[a]->levels; ++k)
            ring.levels[a][std::size_t(k)] = double(designs[a]->length[k]) / designs[a]->length[0];
    return ring;
}

std::array<float, 4> vintageHallTapWeights(double attackPercent) { return vh::tapWeights(attackPercent / 100.0); }

double vintageHallModRangeMs() { return vh::kModRangeSeconds * 1000.0; }

int vintageHallDisplaySamples() { return vh::kDisplaySamples; }

double vintageHallDisplayFloorDb() { return vh::kDisplayFloorDb; }

}  // namespace sub::app
