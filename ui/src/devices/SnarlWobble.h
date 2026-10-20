#pragma once

// Snarl's wobble: a cycle of its shape (Wobble Shape, drawn by the engine's own
// maths: SnarlResponse.h; Random as a few of its held steps), filled faintly
// under it, and while it plays a dot riding it where the wobble is now (the
// displays `phase` across and `lfo` up), glowing, with a short trail. Still once
// nothing plays (nothing repaints).

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QPointF>
#include <QtQml/qqmlregistration.h>

#include <deque>

namespace sub::ui {

class SnarlWobble : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double phase READ phase NOTIFY animated)  // where in its cycle the wobble is (0..1)
    Q_PROPERTY(double level READ level NOTIFY animated)  // where it is (0..1)
    Q_PROPERTY(double lit READ lit NOTIFY animated)      // 0..1, eased: the dot's glow (1 while it runs)
    Q_PROPERTY(int shape READ shape NOTIFY animated)

public:
    static constexpr int kSteps = 96;              // the curve's points
    static constexpr int kRandomSteps = 4;         // Random's held steps drawn
    static constexpr double kQuietSeconds = 0.15;  // nothing for this long: the dot fades
    static constexpr int kTrail = 10;              // the trail's points (TrailPace: one every 16 ms)

    explicit SnarlWobble(QQuickItem* parent = nullptr);

    double phase() const { return phase_; }
    double level() const { return level_; }
    double lit() const { return lit_.value; }
    int shape() const { return shape_; }
    // The curve's height (0..1) at `phase`, as drawn.
    double curveAt(double phase) const;

Q_SIGNALS:
    void animated();

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;

private:
    QRectF plot() const;

    int shape_ = 0;
    double phase_ = 0.0, level_ = 0.0;
    double quiet_ = 1.0;
    Eased lit_;
    TrailPace pace_;
    std::deque<QPointF> trail_;  // (phase, level), oldest first
};

}  // namespace sub::ui
