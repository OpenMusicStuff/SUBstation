#include "devices/VitalLfoGraph.h"

#include "model/Vital.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QLinearGradient>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {
constexpr double kDotSeconds = 0.08;
constexpr double kStaleSeconds = 0.12;
constexpr std::size_t kTrail = 14;
}  // namespace

VitalLfoGraph::VitalLfoGraph(QQuickItem* parent) : VitalGraph(parent) { setImplicitSize(240, 100); }

QRectF VitalLfoGraph::area() const { return plot().adjusted(6, 8, -6, -8); }

QPointF VitalLfoGraph::toScreen(double phase, double value) const {
    const QRectF a = area();
    return {a.left() + std::clamp(phase, 0.0, 1.0) * a.width(), a.bottom() - std::clamp(value, 0.0, 1.0) * a.height()};
}

double VitalLfoGraph::valueAt(double phase) const {
    if (values_.empty())
        return 0.0;
    const double at = std::clamp(phase, 0.0, 1.0) * double(values_.size() - 1);
    const std::size_t i = std::min(std::size_t(at), values_.size() - 1);
    const std::size_t j = std::min(i + 1, values_.size() - 1);
    return values_[i] + (values_[j] - values_[i]) * (at - double(i));
}

void VitalLfoGraph::readSection() {
    const sub::app::Device* d = device();
    if (!d)
        return;
    // (The state is read again only when it is another: a copy of it kept, its data is the same while it is.)
    const QString state = d->state ? *d->state : QString();
    if (state.constData() == state_.constData() && state.size() == state_.size() && shapeIndex_ == index() &&
        !values_.empty())
        return;
    state_ = state;
    const QString shapes = sub::app::vitalStateValue(*d, QStringLiteral("lfos"));
    if (shapes != shapes_ || shapeIndex_ != index() || values_.empty()) {
        shapes_ = shapes;
        shapeIndex_ = index();
        sub::app::VitalLfoShape shape = sub::app::vitalLfoShape(*d, index(), kSamples);
        values_ = std::move(shape.values);
        points_ = std::move(shape.points);
    }
}

void VitalLfoGraph::refreshDisplays() {
    const double seconds = tickSeconds();
    const std::vector<float> phases = readDisplay(QStringLiteral("lfo_%1").arg(index() + 1));
    bool moved = false;
    for (std::size_t i = phases.size() > kTrail ? phases.size() - kTrail : 0; i < phases.size(); ++i) {
        if (phases[i] < 0.0f)
            continue;
        trail_.push_back(phases[i]);
        if (trail_.size() > kTrail)
            trail_.pop_front();
        moved = true;
    }
    if (!phases.empty() && phases.back() >= 0.0f) {
        phase_ = phases.back();
        stale_ = 0.0;
    } else if (!phases.empty() || (stale_ += seconds) > kStaleSeconds) {
        stale_ = kStaleSeconds;
        if (!trail_.empty()) {
            trail_.pop_front();
            moved = true;
        }
    }
    const bool wasPlaying = playing();
    dot_.target = stale_ < kStaleSeconds && phase_ >= 0.0 ? 1.0 : 0.0;
    moved |= dot_.step(easeFraction(seconds, kDotSeconds), 1e-3);
    if (playing() != wasPlaying)
        Q_EMIT playingChanged();
    if (moved)
        update();
}

void VitalLfoGraph::paint(SgPainter& p) {
    paintBackground(p);
    p.setAntialiasing(true);
    const QRectF a = area();
    const QColor color = Theme::modulation();

    for (int i = 0; i <= 8; ++i) {
        const double x = a.left() + i / 8.0 * a.width();
        p.drawLine(QPointF(x, a.top()), QPointF(x, a.bottom()), i % 2 == 0 ? gridMajor() : gridMinor());
    }
    for (int i = 0; i <= 4; ++i) {
        const double y = a.bottom() - i / 4.0 * a.height();
        p.drawLine(QPointF(a.left(), y), QPointF(a.right(), y), i % 2 == 0 ? gridMajor() : gridMinor());
    }
    if (values_.empty())
        return;

    std::vector<QPointF> line;
    line.reserve(values_.size());
    for (std::size_t i = 0; i < values_.size(); ++i)
        line.push_back(toScreen(double(i) / double(values_.size() - 1), values_[i]));
    QLinearGradient fill(a.topLeft(), a.bottomLeft());
    fill.setColorAt(0, withAlpha(color, 90));
    fill.setColorAt(1, withAlpha(color, 6));
    p.fillToBaseline(line.data(), int(line.size()), a.bottom(), fill);
    drawGlowPolyline(p, line, color, 1.7);

    // Its points.
    for (std::size_t i = 0; i + 1 < points_.size(); i += 2) {
        const QPointF at = toScreen(points_[i], points_[i + 1]);
        p.fillEllipse(at, 2.6, 2.6, backgroundBottom());
        p.drawEllipse(QRectF(at.x() - 2.6, at.y() - 2.6, 5.2, 5.2), mixColor(color, Qt::white, 0.4), 1.2);
    }

    // The live dot.
    if (dot_.value > 0.0) {
        p.save();
        p.setOpacity(dot_.value);
        for (std::size_t i = 1; i < trail_.size(); ++i) {
            if (trail_[i] < trail_[i - 1])
                continue;  // (round to the start: not across)
            const double fade = double(i) / double(trail_.size());
            const int steps = std::max(1, int((trail_[i] - trail_[i - 1]) * a.width() / 3));
            QPointF from = toScreen(trail_[i - 1], valueAt(trail_[i - 1]));
            for (int s = 1; s <= steps; ++s) {
                const double ph = trail_[i - 1] + (trail_[i] - trail_[i - 1]) * s / steps;
                const QPointF to = toScreen(ph, valueAt(ph));
                p.drawLine(from, to, withAlpha(Qt::white, int(120 * fade * fade)), 1.0 + 1.2 * fade, Qt::RoundCap);
                from = to;
            }
        }
        const QPointF head = toScreen(phase_, valueAt(phase_));
        p.drawLine(QPointF(head.x(), a.top()), QPointF(head.x(), a.bottom()), withAlpha(color, 50));
        p.fillRadialGlow(head, 8, withAlpha(color, 90), 3);
        p.fillEllipse(head, 3.0, 3.0, Qt::white);
        p.restore();
    }
}

}  // namespace sub::ui
