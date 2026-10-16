#pragma once

// The Overdrive's display, as Ableton's: at the left the band-pass that goes
// into the drive (Frequency, Width) on a 20 Hz..20 kHz log axis, Tone's
// low-pass dashed over it, the input's spectrum (the display `input`) behind;
// at the right the curve it drives into, from the engine's own maths
// (sub::app::overdriveTransfer at Drive).
//
// - Its subtle life: the curve lights up as far as the sound drives it (the
//   display `drive`, eased: a bright stretch from the middle out to the level,
//   and a dot riding the curve there), and the band glows as hard as the
//   output (the display `output`). In silence they come to rest.
// - Drag on the band (it jumps to the press) for the Frequency across and the
//   Width up and down, one undo step per drag; double-click resets both.

#include "analysis/Spectrum.h"
#include "audio/OverdriveResponse.h"
#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QPointF>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace sub::ui {

class OverdriveGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    // How hard the sound is driven now (0..1, eased): how far along the curve it goes. The editor's Drive knob warms with it.
    Q_PROPERTY(double driveActivity READ driveActivity NOTIFY driveActivityChanged)

public:
    static constexpr int kWidth = 236;
    static constexpr int kMinimumHeight = 96;
    static constexpr int kCurveWidth = 72;  // the curve's square at the right (as wide as tall, at most this)
    static constexpr int kGap = 6;
    static constexpr double kLow = 20.0;  // Hz across the band
    static constexpr double kHigh = 20000.0;
    static constexpr double kFloorDb = -30.0;  // the band's bottom
    static constexpr double kDotRadius = 5.0;
    static constexpr double kRecentSeconds = 0.035;  // what a refresh counts of the meters: about two ticks
    static constexpr int kColumn = 2;

    explicit OverdriveGraph(QQuickItem* parent = nullptr);

    QRectF bandRect() const;   // the band's plot
    QRectF curveRect() const;  // the curve's square
    LogAxis frequencyAxis() const;
    double xOf(double freq) const;
    double freqAt(double x) const;
    double yOfWidth(double width) const;  // the dot's height for a Width (octaves): wide at the top
    double widthAt(double y) const;       // held to the range
    QPointF dot() const;

    bool dragging() const { return dragging_; }
    // How far into the curve the sound goes now (the driven level, eased; 0 at rest) and how loud it comes out
    // (eased).
    double driveLevel() const { return driveLevel_.value; }
    double outputLevel() const { return outputLevel_.value; }
    double driveActivity() const;
    bool animating() const { return animating_; }

Q_SIGNALS:
    void draggingChanged();
    void driveActivityChanged();

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void updateCurves();
    void updateColumns();
    void dragTo(const QPointF& pos);
    void setDragging(bool dragging);

    double freq_ = 1500.0, width_ = 6.5, drive_ = 50.0, tone_ = 50.0;
    std::vector<QPointF> band_, tone_line_, transfer_;
    sub::app::analysis::FallingSpectrum spectrum_;
    std::vector<double> columns_;
    Eased driveLevel_, outputLevel_;
    bool animating_ = false;
    bool dragging_ = false;
    QString gesture_, lastGesture_;
};

}  // namespace sub::ui
