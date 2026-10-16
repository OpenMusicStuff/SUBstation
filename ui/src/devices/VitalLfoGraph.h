#pragma once

// One of Vital's LFOs in its editor: its shape over a cycle as Vital's engine
// plays it (sub::app::vitalLfoShape: read again only when its shape changes),
// filled from the bottom, its points marked, the grid in quarters; and live,
// the newest voice's phase in it (the device's display "lfo_<n>") as a glowing
// dot riding the curve with a short trail, fading once no voice plays.

#include "devices/VitalGraph.h"

#include <QPointF>

#include <deque>
#include <vector>

namespace sub::ui {

class VitalLfoGraph : public VitalGraph {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)  // a voice moves it

public:
    static constexpr int kSamples = 512;  // the shape's values across a cycle

    explicit VitalLfoGraph(QQuickItem* parent = nullptr);

    bool playing() const { return dot_.value > 0.0; }
    // The shape's value at `phase` (0..1), as drawn.
    double valueAt(double phase) const;
    const std::vector<float>& points() const { return points_; }
    double phase() const { return phase_; }

    QRectF area() const;
    QPointF toScreen(double phase, double value) const;

Q_SIGNALS:
    void playingChanged();

protected:
    void readSection() override;
    bool hasSwitch() const override { return false; }
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;

private:
    QString state_;   // the device's state last read
    QString shapes_;  // the LFOs' state value the shape was read from
    int shapeIndex_ = -1;
    std::vector<float> values_;
    std::vector<float> points_;
    double phase_ = -1.0;
    double stale_ = 0.0;
    Eased dot_;
    std::deque<double> trail_;  // phases, newest last
    TrailPace shrink_;  // the pace it shrinks at, once nothing plays
};

}  // namespace sub::ui
