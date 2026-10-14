#pragma once

// One of Vital's envelopes in its editor: delay, attack, hold, decay, sustain
// and release drawn as Vital's engine plays them (its stages' power curves,
// vital::Envelope's), each stage as wide as its knob is turned (Vital's own
// time scale), the sustain a fixed stretch, the whole fitted to the width.
//
// Handles: the delay's and the attack's ends and the hold's (across), the
// decay's end (across for the decay, up and down for the sustain) and the
// release's (across); and a smaller one on each curve (up and down for its
// power). A drag moves the handle under the mouse relative to where it was
// pressed (one undo step a drag, Shift finely); double-clicking a handle sets
// its parameters back to their defaults. Hovering one grows it.
//
// Live: the newest voice's position in it (the device's displays "env_<n>",
// its stage and how far into it, and "env_<n>_value") as a glowing dot with a
// short trail, fading once no voice plays.

#include "devices/VitalGraph.h"

#include <QPointF>
#include <QStringList>

#include <array>
#include <deque>
#include <vector>

namespace sub::ui {

class VitalEnvelopeGraph : public VitalGraph {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)  // a voice is in it

public:
    // Vital's ranges (synth_parameters.cpp): delay and hold, attack, decay and release, the powers.
    static constexpr double kMaxDelay = 1.4142135624;
    static constexpr double kMaxTime = 2.37842;
    static constexpr double kMaxPower = 20.0;
    static constexpr double kSustainWidth = 0.35;  // the sustain's stretch, in the stages' units
    static constexpr double kMinSpan = 1.5;        // at least this much shown across

    enum Handle { None = -1, Delay = 0, Attack, Hold, Decay, Release, AttackCurve, DecayCurve, ReleaseCurve,
                  HandleCount };

    explicit VitalEnvelopeGraph(QQuickItem* parent = nullptr);

    bool playing() const { return dot_.value > 0.0; }

    // Vital's envelope (vital::Envelope): a stage's level `t` of the way through it (0..1).
    static double attackValue(double t, double power);
    static double decayValue(double t, double power, double sustain);
    static double releaseValue(double t, double power, double from);

    // Graph coordinates of the stages' units (x) and the level (y), and a handle's place.
    double xOf(double units) const;
    double yOf(double level) const;
    Q_INVOKABLE QPointF handlePosition(int handle) const;
    // Where the live dot is drawn for a stage and position (Vital's: 1 delay .. 5 release, 6 killed).
    double xOfPhase(double phase) const;
    int hovered() const { return hovered_; }

Q_SIGNALS:
    void playingChanged();

protected:
    void readSection() override;
    bool hasSwitch() const override { return false; }
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    struct Stages {
        double delay = 0, attack = 0, hold = 0, decay = 0, sustain = 1, release = 0;
        double attackPower = 0, decayPower = 0, releasePower = 0;
    };
    QRectF area() const;
    double scale() const;  // px per unit
    double span() const;   // units across
    Handle handleNear(const QPointF& pos) const;
    // The curve's polyline, in graph coordinates.
    std::vector<QPointF> curve() const;
    // A handle's parameters (their suffixes).
    static QStringList paramsOf(Handle handle);

    Stages s_;
    // The drag.
    Handle pressed_ = None;
    Handle hovered_ = None;
    QPointF anchor_;
    Stages start_;
    double startScale_ = 1.0;
    std::array<Eased, HandleCount> grow_;
    // Live.
    double phase_ = -1.0, level_ = 0.0;
    double stale_ = 0.0;        // seconds without a voice
    Eased dot_;                 // the dot's opacity
    std::deque<QPointF> trail_;  // (phase, level), newest last
};

}  // namespace sub::ui
