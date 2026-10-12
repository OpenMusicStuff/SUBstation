#pragma once

// The Transient Shaper's picture of what it does to a hit: a drum-like hit (a
// 1 ms rise, then 60 dB down over 450 ms) before the shaping, a faint ghost,
// and after it, filled and outlined, through the engine's own detector and gain
// (sub::app::transientHit), on a square-root time axis (the onset's first
// milliseconds get room beside the tail's hundreds) and a dB axis. Where the
// shaping turns the hit up or down, the band between before and after glows in
// the colour of what did it: Attack's orange at the onset, Sustain's blue along
// the tail. When a setting changes the curve morphs to its new shape.
//
// Two handles ride on the curve: Attack's at the shaped peak, Sustain's on the
// tail (kSustainMs in). Dragged up or down they set Attack or Sustain; across,
// Attack Time or Release (the direction is picked by the first few pixels, so a
// drag sets only one parameter); Shift drags finely; one undo step per drag. A
// double-click puts a handle's two parameters back to their defaults; a
// right-click gives its amount's menu. Marks on the time axis show Attack Time
// and Release.
//
// Each time the device detects a transient (its "detect" display), a glowing
// head runs along the curve in real time, as the hit it just heard plays out.
// Every animation steps in refreshDisplays() by the time since the last tick;
// at rest nothing repaints.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QFont>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class TransientShapeGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(QString hovered READ hovered NOTIFY hoveredChanged)  // "attack", "sustain" or ""

public:
    static constexpr int kWidth = 196;
    static constexpr int kMinimumHeight = 96;
    static constexpr int kColumns = 160;        // points along the curve
    static constexpr double kFloorDb = -60.0;   // the level axis
    static constexpr double kCeilingDb = 24.0;  // (the hit's peak, -6 dB, turned up as far as it goes, and room)
    static constexpr double kSustainMs = 200.0;  // where Sustain's handle sits on the tail
    static constexpr double kGrab = 9.0;         // px from a handle's centre that picks it up
    static constexpr int kMaxHeads = 4;          // playheads at once
    enum class Handle { None, Attack, Sustain };

    explicit TransientShapeGraph(QQuickItem* parent = nullptr);

    static QFont textFont();  // its labels: the UI font at 7 pt

    bool dragging() const { return drag_ != Handle::None; }
    QString hovered() const;

    QRectF plot() const;
    double xOfMs(double ms) const;  // the square-root time axis, held to the plot
    double msAt(double x) const;
    double yOf(double db) const;    // held to the plot
    // Where the handles are drawn (on the curve as it is drawn, easing included).
    QPointF attackHandle() const;
    QPointF sustainHandle() const;
    // The curve's times (ms) and its levels before and after: as the engine shapes the hit (targets) and as
    // drawn (easing towards them).
    const std::vector<double>& timesMs() const { return times_; }
    const std::vector<float>& inDb() const { return in_; }
    const std::vector<float>& targetDb() const { return target_; }
    const std::vector<float>& drawnDb() const { return drawn_; }
    // The playheads: how far each is along the hit (ms).
    std::vector<double> heads() const;
    bool animating() const { return animating_; }

Q_SIGNALS:
    void draggingChanged();
    void hoveredChanged();
    // A handle right-clicked: its amount's menu ("attack" or "sustain"; the editor opens it).
    void paramMenuRequested(const QString& paramId);

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    enum class Axis { Undecided, Amount, Time };

    Handle handleAt(const QPointF& pos) const;
    void setHover(Handle handle);
    void endDrag();
    int peakIndex() const;     // the shaped peak, as the engine has it
    int sustainIndex() const;  // the column nearest kSustainMs
    double drawnAt(double ms) const;  // the drawn curve at a time, between its points
    QString tagText(Handle handle) const;

    // The parameters, as sync() read them.
    double attack_ = 0.0, sustain_ = 0.0, attackTime_ = 20.0, release_ = 300.0, threshold_ = -60.0;
    bool synced_ = false;

    // The curve: its times, the hit's levels before the shaping, and after it (the engine's, and as drawn).
    std::vector<double> times_;
    std::vector<float> in_, target_, drawn_;
    std::vector<float> attackPart_, sustainPart_;  // the gain's parts (dB), as drawn
    std::vector<float> attackTarget_, sustainTarget_;

    // What the ticks move on.
    std::array<double, kMaxHeads> heads_{};  // ms along the hit
    int headCount_ = 0;
    bool armed_ = true;  // the transient fell back since the last head (a head starts on its rise)
    Eased hoverAttack_, hoverSustain_;
    bool animating_ = false;

    // The mouse.
    Handle hover_ = Handle::None;
    Handle drag_ = Handle::None;
    Handle clicked_ = Handle::None;  // the handle the last first press was on: what a double-click resets
    Axis axis_ = Axis::Undecided;
    QPointF pressPos_;
    QPointF lastPos_;
    double dragAmount_ = 0.0, dragTime_ = 0.0;  // as the drag has them (unclamped)
    QString gesture_;

    // paint()'s scratch.
    std::vector<QPointF> inLine_, outLine_;
    std::vector<float> tops_, bottoms_;
};

}  // namespace sub::ui
