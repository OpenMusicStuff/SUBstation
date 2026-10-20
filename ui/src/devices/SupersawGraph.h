#pragma once

// The Supersaw's display: its three stacks, a lane each, every saw where it
// plays, worked out by the engine's own maths (sub::app::supersawLayout, the
// application layer's wrapper of SupersawDesign.h). Across, its detune in cents
// (one axis for all three, as wide as the widest stack's outermost saws reach,
// ±12 cents at the least, easing as it changes); up and down, its place in the
// stereo field (left at the top, the middle the lane's dashed line); as large
// and as bright as it is loud. A stack switched off is dimmed and says so; the
// stack the editor shows (`stack`) has its lane lit. Each lane names its stack
// and, over its plot, says how far its outermost saws reach and its saws and
// transpose.
//
// While sound comes out (the display `output`, through meter ballistics) the
// saws glow with it, each shimmering at the rate it beats against its stack's
// middle at A3 (a few times a second when close, faster when far apart, as the
// stack beats). Silent, they rest, and nothing repaints.
//
// Press a lane to show its stack (stackPressed(index)); drag across for its
// Detune, up and down for its Width (dragging up widens), one undo step a drag,
// Shift four times as finely; double-click resets both.

#include "audio/SupersawLayout.h"
#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class SupersawGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int stack READ stack WRITE setStack NOTIFY stackChanged)  // the lane lit: 0..2
    Q_PROPERTY(double glow READ glow NOTIFY animated)                   // 0..1, with the output
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(double axisCents READ axisCents NOTIFY animated)  // the axis' reach either side

public:
    static constexpr int kStacks = 3;
    static constexpr int kMinimumWidth = 220;
    static constexpr int kMinimumHeight = 96;
    static constexpr double kLaneGap = 3.0;
    static constexpr double kLabelWidth = 18.0;     // the lane's number, at its left
    static constexpr double kCaptionHeight = 11.0;  // the lane's caption, over its plot
    static constexpr double kMinAxisCents = 12.0;   // the axis at the least reaches this far either side
    static constexpr double kAxisMargin = 1.15;     // past the outermost saws
    static constexpr double kDetunePixels = 240.0;  // dragged across this far, Detune moves 100 %
    static constexpr double kWidthPixels = 100.0;   // dragged up this far, Width moves 100 %
    static constexpr double kReferenceHz = 220.0;   // A3: the pitch the saws' shimmer beats at
    static constexpr double kSilentDb = -80.0;      // the output's level below which the saws rest
    static constexpr double kGlowFloorDb = -48.0;   // the glow starts here and is full kGlowRangeDb higher
    static constexpr double kGlowRangeDb = 42.0;

    explicit SupersawGraph(QQuickItem* parent = nullptr);

    int stack() const { return stack_; }
    void setStack(int stack);
    double glow() const { return glow_.value; }
    bool dragging() const { return !gesture_.isEmpty(); }
    double axisCents() const { return axis_.value; }
    // Whether the saws rest (no sound coming out).
    bool resting() const { return resting_; }

    // A stack's lane, its plot (right of its label), and where a saw of it is drawn.
    Q_INVOKABLE QRectF laneRect(int stack) const;
    QRectF plotRect(int stack) const;
    Q_INVOKABLE int sawCount(int stack) const;
    Q_INVOKABLE bool stackOn(int stack) const;
    Q_INVOKABLE QPointF sawPoint(int stack, int saw) const;
    Q_INVOKABLE double sawRadius(int stack, int saw) const;
    const std::vector<sub::app::SupersawSaw>& saws(int stack) const;
    double xOfCents(int stack, double cents) const;
    // Which lane a point is in (-1: none).
    int laneAt(const QPointF& at) const;
    // A stack's colour (its saws').
    static QColor stackColour(int stack);

Q_SIGNALS:
    void stackChanged();
    void stackPressed(int stack);
    void animated();
    void draggingChanged();

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct StackShape {
        bool on = false;
        int saws = 1;
        double detune = 0.0, curve = 0.0, blend = 0.0, width = 0.0, transpose = 0.0, fine = 0.0;
        std::vector<sub::app::SupersawSaw> layout;
        std::vector<double> beats;  // each saw's beat against the middle at kReferenceHz, Hz
    };

    QString paramOf(int stack, const char* name) const;
    void startDrag(const QPointF& at, bool fine);
    void setDraggingStack(int stack);

    std::array<StackShape, kStacks> stacks_;
    int stack_ = 0;
    bool synced_ = false;
    bool enabled_ = true;

    Eased axis_;   // cents either side
    Eased glow_;   // 0..1
    MeterBallistics meter_;
    bool resting_ = true;
    double time_ = 0.0;  // seconds the saws have shimmered (held while resting)

    // A drag: its merge key ("": none), its stack, where it started (or Shift last changed) and the values
    // then, and where it was last.
    QString gesture_;
    int dragStack_ = -1;
    QPointF pressedAt_;
    double pressedDetune_ = 0.0, pressedWidth_ = 0.0;
    double lastDetune_ = 0.0, lastWidth_ = 0.0;
    QPointF lastAt_;
    bool fine_ = false;
};

}  // namespace sub::ui
