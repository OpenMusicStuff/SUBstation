#pragma once

// The modulators' curve (the LFO's cycle, the Envelope's shape from a trigger),
// to draw on, and what they do as they play.
//
// The curve: drag a point to move it (the first and last only up and down),
// drag between points to bend the segment there (as automation bends), click
// anywhere else to add a point (and keep dragging it), double-click a point (or
// Alt-click it, or select it and press Delete) to remove it, double-click a
// bend to straighten it; the wheel bends too, Shift makes any of them fine,
// Ctrl snaps a point to the grid (sixteenths across, eighths up). Right-click
// (contextMenuRequested) for the shapes to start from, flipping, reversing and
// resetting. The curve's points are always written whole (pointsValues: every
// slot's four parameters) through ProjectEditor::setDeviceParams, so a drag is
// one undo step.
//
// As it plays (its displays "value", "phase" and "key", read on each
// DisplayClock tick and played back at the audio's pace, DisplayPlayback):
// a playhead rides the curve with a short fading trail and a soft glow; the LFO's
// output over its range (Depth and Offset) is shaded behind the curve, and what
// it puts out over its cycle or length (Depth, Offset, the LFO's Phase shifting
// it along, eased by Smooth) drawn dashed over it. The Envelope flashes at each trigger, and once a sidechain
// is chosen (its hits trigger it, as notes do), the key's level shows against
// its threshold.
//
// With Random on, it draws the random curve playing now (its displays "seed"
// and "start", through modulatorRandomPoints: the engine's own), the playhead
// riding it; the drawn curve can't be edited meanwhile, and choosing a shape,
// flipping, reversing or resetting it turns Random off.

#include "devices/DeviceCanvas.h"
#include "devices/DisplayPlayback.h"
#include "devices/EditorPaint.h"

#include "audio/ModulatorShapes.h"

#include <QElapsedTimer>
#include <QPointF>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <utility>
#include <vector>

namespace sub::ui {

class ModulatorGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool periodic READ periodic NOTIFY curveChanged)  // an LFO's (its curve repeats)
    Q_PROPERTY(QString hint READ hint NOTIFY curveChanged)       // over the curve: how to get going ("": none)
    Q_PROPERTY(QString lengthText READ lengthText NOTIFY curveChanged)
    Q_PROPERTY(int selected READ selected NOTIFY curveChanged)  // the selected point (-1: none)
    Q_PROPERTY(QStringList shapes READ shapes NOTIFY curveChanged)
    Q_PROPERTY(double output READ output NOTIFY ticked)  // what it puts out now, 0..1 (-1: nothing yet)
    Q_PROPERTY(bool random READ random NOTIFY curveChanged)  // Random on: a new random curve each cycle or trigger

public:
    static constexpr int kPoints = 32;
    static constexpr int kMinimumWidth = 200;
    static constexpr int kMinimumHeight = 80;
    static constexpr double kPointRadius = 4.0, kPointHoverRadius = 6.0;
    static constexpr double kHitRadius = 9.0;
    static constexpr double kCurveHit = 7.0;  // px from the curve where a drag bends it
    static constexpr double kMeterWidth = 7.0;
    static constexpr double kMeterFloor = -60.0;
    static constexpr int kTrail = 10;  // the playhead's trail: points TrailPace's step apart (0.16 s)
    static constexpr double kKeyFallDbPerSecond = 75.0;  // the key's meter falling (1.2 dB a 16 ms tick)
    static constexpr double kFlashDecay = 0.8, kFlashDecaySeconds = 0.016;  // a trigger's light: 0.8 every 16 ms

    using Point = sub::app::ModulatorPoint;

    explicit ModulatorGraph(QQuickItem* parent = nullptr);

    // A curve point's parameter id (points from 0): "p1_x".
    static QString pointParam(int index, const QString& name);
    // The curve at x (0..1), as the device plays it.
    static double curveValue(const std::vector<Point>& points, double x);
    // The parameters for a curve of these points (every slot: the same parameters every time).
    static sub::app::OrderedMap<QString, double> pointsValues(const std::vector<Point>& points);
    // How many beats a synced value's name stands for ("1/4": 1, "3/16": 0.75, "1.5 Bars": 6); 0: not one.
    static double syncedBeats(const QString& label);

    bool periodic() const { return periodic_; }
    QString hint() const { return hint_; }
    QString lengthText() const { return lengthText_; }
    int selected() const { return selected_; }
    QStringList shapes() const;
    double output() const { return output_; }
    const std::vector<Point>& points() const { return points_; }
    bool random() const { return random_; }
    // The curve it draws: the points, or with Random the random curve playing now.
    const std::vector<Point>& shown() const { return random_ ? randomPoints_ : points_; }
    // The LFO's Phase (0..1 of the cycle: where on the curve its cycle starts), and Smooth (ms).
    double phaseShift() const { return phaseShift_; }
    double smoothMs() const { return smoothMs_; }
    // What it puts out at `count` + 1 evenly spaced times across its cycle or length: the shown curve (the LFO's
    // read from Phase on) through Depth and Offset, eased by Smooth as its one-pole does (the LFO's as it settles
    // cycle after cycle; the Envelope's from where it rests).
    std::vector<double> outputCurve(int count) const;
    void setPoints(const std::vector<Point>& points, const QString& mergeKey = QString(),
                   const QString& text = QStringLiteral("Change Modulator Curve"));
    // The trail of where the playhead was, newest last (x, the curve's value there); empty: not playing.
    const std::vector<QPointF>& trail() const { return trail_; }

    // Geometry, for the tests too.
    QRectF plot() const;
    QPointF toScreen(double x, double y) const;
    QPointF fromScreen(const QPointF& pos) const;
    // ("point" | "segment", index); kind "" for nothing.
    std::pair<QString, int> hit(const QPointF& pos) const;

    Q_INVOKABLE void removePoint(int index);
    Q_INVOKABLE void applyShape(int index);
    Q_INVOKABLE void flip();
    Q_INVOKABLE void reverse();
    Q_INVOKABLE void resetCurve();
    // Reads the displays (as the clock ticks; the tests call it after rendering), moving on by `seconds`.
    Q_INVOKABLE void readDisplays(double seconds = 0.016) { advance(seconds); }

Q_SIGNALS:
    void curveChanged();
    void ticked();
    void contextMenuRequested(QPointF position, int point);  // point: an inner one under it (-1: none)
    void sidechainMenuRequested(QPointF position);

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Drag {
        QString kind;  // "point" or "bend"
        int index = 0;
        QPointF origin;
        std::vector<Point> points;  // as they were at the press
        QString gesture;
        QString text = QStringLiteral("Change Modulator Curve");
    };

    void advance(double seconds);
    void showRandom(int seed, double start);
    QRectF hintRect() const;
    QRectF meterRect() const;
    QString readoutText(const Point& point) const;
    QString timeText(double x) const;
    // What the device puts out for a curve value (the LFO's Depth and Offset, the Envelope's Depth).
    double played(double y) const;
    void hoverAt(const QPointF& pos);
    void endDrag();
    QString undoName(const QString& what) const;

    // The parameters, as they are now.
    std::vector<Point> points_;
    bool periodic_ = true;
    double depth_ = 1.0;
    double offset_ = 0.0;   // the LFO's, -0.5..0.5
    double phaseShift_ = 0.0;  // the LFO's Phase, 0..1
    double smoothMs_ = 0.0;
    double lengthMs_ = 500.0;
    bool keyed_ = false;    // the Envelope triggered by its sidechain (too)
    double threshold_ = -24.0;
    QString lengthText_;
    QString hint_;
    // Live.
    DisplayPlayback<5> playback_;  // value, phase, key, seed, start
    bool random_ = false;
    int randomSeed_ = -1;  // the random curve drawn (-1: none yet)
    double randomStart_ = 0.5;
    std::vector<Point> randomPoints_;
    double output_ = -1.0;
    double lastPhase_ = -1.0;
    std::vector<QPointF> trail_;  // (x, y on the curve), oldest first
    TrailPace trailPace_;
    double level_ = kMeterFloor;  // the key's level (dB), falling back slowly
    double flash_ = 0.0;          // a trigger lights it up
    // Interaction.
    std::pair<QString, int> hover_{QString(), -1};
    int selected_ = -1;
    std::optional<Drag> drag_;
    std::optional<std::pair<QPointF, QString>> readout_;  // shown while dragging
    QElapsedTimer addedClock_;
    int added_ = -1;  // the point a click last added: its double-click doesn't remove it
    QElapsedTimer wheelClock_;
    QString wheelGesture_;
};

}  // namespace sub::ui
