#pragma once

// Stereo peak meter with a falling display: -60 to +6 dB, falling
// kFallPerUpdate of the scale per update (updates come at about 30 Hz), with a
// clip light at full scale that a click clears. Levels (linear peaks) are
// pushed with setLevels() once per meter update, e.g. from the app layer's
// meters signal; the fall needs every update, even unchanged ones.
//
// `compact`: as Ableton's meters between devices: two thin bars on a faint
// track, no clip light (nor a click to clear it), each with its peak held as a
// tick for about a second before it falls.

#include "sg/SgCanvas.h"

#include <QtQml/qqmlregistration.h>

namespace sub::ui {

class Meter : public SgCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal leftLevel READ leftLevel NOTIFY levelsChanged)    // what shows, 0 to 1 of the scale
    Q_PROPERTY(qreal rightLevel READ rightLevel NOTIFY levelsChanged)
    Q_PROPERTY(bool clipped READ clipped NOTIFY clippedChanged)
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY compactChanged)

public:
    static constexpr double kFloorDb = -60.0;
    static constexpr double kCeilingDb = 6.0;
    static constexpr double kFallPerUpdate = 0.035;  // fraction of the scale per update (~30 Hz)
    static constexpr int kHoldUpdates = 30;          // a compact meter's peak tick holds this long (~1 s)

    explicit Meter(QQuickItem* parent = nullptr);

    // A linear peak level as a fraction of the scale, 0 to 1.
    static double fraction(double level);

    qreal leftLevel() const { return display_[0]; }
    qreal rightLevel() const { return display_[1]; }
    bool clipped() const { return clipped_; }
    bool compact() const { return compact_; }
    void setCompact(bool compact);

    // One meter update: the peaks since the last (linear; 1.0 is full scale).
    Q_INVOKABLE void setLevels(qreal left, qreal right);
    Q_INVOKABLE void reset();
    Q_INVOKABLE void clearClip();

Q_SIGNALS:
    void levelsChanged();
    void clippedChanged();
    void compactChanged();

protected:
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void paintCompact(SgPainter& painter);

    double display_[2] = {0.0, 0.0};
    double held_[2] = {0.0, 0.0};  // the peak ticks (compact)
    int holding_[2] = {0, 0};      // updates they still hold for
    bool clipped_ = false;
    bool compact_ = false;
};

}  // namespace sub::ui
