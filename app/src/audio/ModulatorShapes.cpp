#include "audio/ModulatorShapes.h"

namespace sub::app {

const std::vector<ModulatorShape>& modulatorShapes(bool periodic) {
    // The LFO's first is its device's default (ModulatorDesign.h's trianglePoints); its Sine is sinePoints (each
    // bend fitted to the sine's eighth).
    static const std::vector<ModulatorShape> lfo{
        {QStringLiteral("Triangle"), {{0.0, 0.0, 0.0}, {0.5, 1.0, 0.0}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Sine"),
         {{0.0, 0.5, 0.055},
          {0.125, 0.853553, 0.329},
          {0.25, 1.0, 0.329},
          {0.375, 0.853553, 0.055},
          {0.5, 0.5, -0.055},
          {0.625, 0.146447, -0.329},
          {0.75, 0.0, -0.329},
          {0.875, 0.146447, -0.055},
          {1.0, 0.5, 0.0}}},
        {QStringLiteral("Saw Up"), {{0.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}},
        {QStringLiteral("Saw Down"), {{0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Square"), {{0.0, 1.0, 0.0}, {0.5, 1.0, 0.0}, {0.5, 0.0, 0.0}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Pulse"), {{0.0, 1.0, 0.0}, {0.25, 1.0, 0.0}, {0.25, 0.0, 0.0}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Steps"),
         {{0.0, 0.0, 0.0},
          {0.25, 0.0, 0.0},
          {0.25, 1.0 / 3.0, 0.0},
          {0.5, 1.0 / 3.0, 0.0},
          {0.5, 2.0 / 3.0, 0.0},
          {0.75, 2.0 / 3.0, 0.0},
          {0.75, 1.0, 0.0},
          {1.0, 1.0, 0.0}}},
        {QStringLiteral("Hump"), {{0.0, 0.0, 0.5}, {0.5, 1.0, 0.5}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Swell"), {{0.0, 0.0, -0.6}, {1.0, 1.0, 0.0}}},
    };
    // The Envelope's first is its device's default (ModulatorDesign.h's pluckPoints).
    static const std::vector<ModulatorShape> envelope{
        {QStringLiteral("Pluck"), {{0.0, 0.0, 0.0}, {0.02, 1.0, -0.55}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Attack-Decay"), {{0.0, 0.0, 0.35}, {0.3, 1.0, -0.45}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Swell"), {{0.0, 0.0, -0.6}, {1.0, 1.0, 0.0}}},
        {QStringLiteral("Ramp Down"), {{0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Ramp Up"), {{0.0, 0.0, 0.0}, {1.0, 1.0, 0.0}}},
        {QStringLiteral("Gate"), {{0.0, 1.0, 0.0}, {0.75, 1.0, 0.0}, {0.75, 0.0, 0.0}, {1.0, 0.0, 0.0}}},
        {QStringLiteral("Duck"), {{0.0, 0.0, 0.0}, {0.1, 0.0, -0.45}, {1.0, 1.0, 0.0}}},
        {QStringLiteral("Stutter"),
         {{0.0, 1.0, 0.0},
          {0.15, 1.0, 0.0},
          {0.15, 0.0, 0.0},
          {0.3, 0.0, 0.0},
          {0.3, 1.0, 0.0},
          {0.45, 1.0, 0.0},
          {0.45, 0.0, 0.0},
          {0.6, 0.0, 0.0},
          {0.6, 1.0, 0.0},
          {0.75, 1.0, 0.0},
          {0.75, 0.0, 0.0},
          {1.0, 0.0, 0.0}}},
    };
    return periodic ? lfo : envelope;
}

QString modulatorPointParam(int index, const QString& name) {
    return QLatin1Char('p') + QString::number(index + 1) + QLatin1Char('_') + name;
}

OrderedMap<QString, double> modulatorPointValues(const std::vector<ModulatorPoint>& points) {
    OrderedMap<QString, double> values;
    for (int i = 0; i < kModulatorPoints; ++i) {
        const bool used = i < static_cast<int>(points.size());
        const ModulatorPoint p = used ? points[static_cast<size_t>(i)] : ModulatorPoint{1.0, 0.0, 0.0};
        values.insert(modulatorPointParam(i, QStringLiteral("used")), used ? 1.0 : 0.0);
        values.insert(modulatorPointParam(i, QStringLiteral("x")), p.x);
        values.insert(modulatorPointParam(i, QStringLiteral("y")), p.y);
        values.insert(modulatorPointParam(i, QStringLiteral("curve")), used ? p.curve : 0.0);
    }
    return values;
}

}  // namespace sub::app
