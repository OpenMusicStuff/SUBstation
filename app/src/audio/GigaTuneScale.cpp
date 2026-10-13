#include "audio/GigaTuneScale.h"

#include <algorithm>

#include "builtin/GigaTuneDesign.h"

namespace sub::app {

int gigatuneScaleNotes(int key, int scale, int songTonic, bool songMinor) {
    const int index = std::clamp(scale, 0, static_cast<int>(gigatune::Scale::Count) - 1);
    return gigatune::scaleNotes(std::clamp(key, 0, 12), static_cast<gigatune::Scale>(index), songTonic, songMinor);
}

}  // namespace sub::app
