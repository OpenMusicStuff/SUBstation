#pragma once

// The Vintage Hall's display: its reverb as what it is, one loop the sound runs
// round, drawn as a ring (sub::app::vintageHallRing: every part where Size puts
// it, clockwise from the top where the left comes in; the left half amber, the
// right blue, from where the right comes in). Inside the ring its all-passes as
// arcs, as strong as Late Diffusion makes them (the two the modulation moves
// wavering with it), and the input's as Early Diffusion makes them. On the ring:
// - the energy at each output tap now (the device's displays), glowing round
//   the loop between them, wavering where the modulation moves it;
// - comets: each onset heard sets one off from where each side comes in (after
//   the predelay), running round at the loop's own speed (a lap in 560 ms at
//   Size 100 %), fading as Decay says and smearing out lap by lap as Late
//   Diffusion spreads it;
// - the taps, sized by their weights for Attack (hollow: subtracted), lit as
//   they are heard.
// In the middle a soft bloom as loud as the reverb's output, and Decay with the
// loop's length. Drag it up and down for Decay, across for Size (Shift:
// finely), one undo step a drag; double-click resets both.
//
// Everything moves on in refreshDisplays() by the time since the last tick; it
// repaints only while something moves, and rests once the sound has died away.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include "audio/VintageHallResponse.h"

#include <QFont>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class VintageHallLoop : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool animating READ animating NOTIFY animatingChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(int cometCount READ cometCount NOTIFY animatingChanged)
    Q_PROPERTY(double loopSeconds READ loopSeconds NOTIFY loopChanged)

public:
    static constexpr int kWidth = 110;
    static constexpr int kMinimumHeight = 96;
    static constexpr double kFloorDb = -60.0;  // the glow's floor: quieter is dark
    static constexpr int kMaxComets = 40;
    static constexpr int kTaps = 8;  // L1..L4, R1..R4 (the displays "l1".."r4")

    // A comet: born at a side's input after the predelay (age < 0 until then), as loud as the onset.
    struct Comet {
        int side = 0;          // 0: from the left input, 1: from the right
        double age = 0.0;      // s
        double levelDb = 0.0;  // when it set off
    };

    explicit VintageHallLoop(QQuickItem* parent = nullptr);

    bool animating() const { return animating_; }
    bool dragging() const { return !gesture_.isEmpty(); }
    int cometCount() const { return int(comets_.size()); }
    double loopSeconds() const { return ring_.seconds; }
    const std::vector<Comet>& comets() const { return comets_; }
    // A tap's level as the ring shows it (dB, eased), L1..L4 then R1..R4.
    Q_INVOKABLE double tapLevelDb(int tap) const;
    // A tap's weight as drawn (eased towards Attack's).
    Q_INVOKABLE double tapWeight(int tap) const;
    Q_INVOKABLE double outputLevelDb() const { return output_.level; }

    // Geometry: the ring's centre and radius (as Size puts it, eased), and a point of the loop
    // (a fraction of a lap) on a circle of `radius`.
    QPointF center() const;
    double maximumRadius() const;
    double radius() const { return radius_.value; }
    QPointF pointAt(double fraction, double radius) const;
    double fractionOf(sub::app::VintageHallRing::Point point) const { return shown_[std::size_t(point)].value; }

Q_SIGNALS:
    void animatingChanged();
    void draggingChanged();
    void loopChanged();

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
    double targetRadius() const;
    double energyAt(double fraction) const;  // 0..1, the glow round the ring between the taps
    double wobbleAt(double fraction) const;  // px out from the ring, where the modulation moves it
    bool advance(double dt);                 // moves every animation on: whether anything moved
    void launch(double levelDb);             // an onset: a comet from each side
    void endDrag();
    QColor sideColor(int side) const;
    QColor colorAt(double fraction) const;  // the half's colour, blended across where they meet

    // The parameters (sync()).
    double size_ = 100.0, decayMs_ = 4000.0, predelayMs_ = 20.0, attack_ = 50.0;
    double early_ = 100.0, late_ = 100.0, modRate_ = 2.53, modDepth_ = 38.0, mix_ = 100.0;
    sub::app::VintageHallRing ring_;

    // What eases (refreshDisplays()).
    Eased radius_;
    std::array<Eased, sub::app::VintageHallRing::kPoints> shown_{};  // the parts' places on the loop
    std::array<Eased, 4> weights_{};                                  // the taps' weights
    Eased earlyShown_, lateShown_;  // the diffusions (0..1)
    std::array<MeterBallistics, kTaps> taps_{};
    MeterBallistics input_, output_;
    double inputEnvelope_ = -120.0;  // follows the input: an onset rises well above it
    double sinceLaunch_ = 1e9;
    std::array<Eased, 2> mod_{};  // B's and D's moving delays (0..1)
    double shimmer_ = 0.0;        // the modulation's ripple, its phase (radians)
    std::vector<Comet> comets_;
    bool animating_ = false;
    bool started_ = false;

    // Dragging.
    QString gesture_;
    QPointF lastPos_;
    double dragDecay_ = 4000.0, dragSize_ = 100.0;
    bool pressed_ = false;
};

}  // namespace sub::ui
