#pragma once
// The modulators' curves (the LFO's and the Envelope's,
// engine/src/builtin/ModulatorDesign.h): the shapes to start from that their
// editor offers (and an agent picks by name), and the parameters a curve is
// written as. A curve is written whole: every one of its kModulatorPoints
// slots' four parameters ("p1_used", "p1_x", "p1_y", "p1_curve"...).

#include "audio/ModulatorRandom.h"
#include "model/OrderedMap.h"

#include <QString>

#include <vector>

namespace sub::app {

inline constexpr int kModulatorPoints = 32;  // the engine's modulator::kPoints

struct ModulatorShape {
    QString name;
    std::vector<ModulatorPoint> points;
};

// The LFO's shapes (`periodic`: the first, Triangle, its default) or the Envelope's (the first, Pluck, its default).
const std::vector<ModulatorShape>& modulatorShapes(bool periodic);
// A curve point's parameter id (points from 0): "p1_x".
QString modulatorPointParam(int index, const QString& name);
// The parameters for a curve of these points (every slot: the same parameters every time).
OrderedMap<QString, double> modulatorPointValues(const std::vector<ModulatorPoint>& points);

}  // namespace sub::app
