#include "audio/SupersawLayout.h"

#include "builtin/SupersawDesign.h"

namespace sub::app {

int supersawStacks() { return sub::supersaw::kStacks; }

int supersawMaxSaws() { return sub::supersaw::kMaxSaws; }

std::vector<SupersawSaw> supersawLayout(int saws, double detunePercent, double curvePercent, double blendPercent,
                                        double widthPercent) {
    sub::supersaw::Shape shape;
    shape.saws = saws;
    shape.detune = detunePercent;
    shape.curve = curvePercent;
    shape.blend = blendPercent;
    shape.width = widthPercent;
    sub::supersaw::Saw placed[sub::supersaw::kMaxSaws];
    const int count = sub::supersaw::layout(shape, placed);
    std::vector<SupersawSaw> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) out.push_back({placed[i].cents, placed[i].pan, placed[i].gain});
    return out;
}

double supersawDetuneCents(double detunePercent) { return sub::supersaw::detuneCents(detunePercent); }

}  // namespace sub::app
