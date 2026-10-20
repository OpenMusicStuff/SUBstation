#include "devices/SnarlWobble.h"

#include "audio/SnarlResponse.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QLinearGradient>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sub::ui {

namespace {

constexpr int kRandom = 5;  // Wobble Shape's Random

}  // namespace

SnarlWobble::SnarlWobble(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(150, 40);
    setAcceptedMouseButtons(Qt::NoButton);  // (clicks go on to the frame)
}

QRectF SnarlWobble::plot() const { return QRectF(4, 4, std::max(1.0, width() - 8), std::max(1.0, height() - 8)); }

double SnarlWobble::curveAt(double phase) const {
    if (shape_ == kRandom) {  // a few of its held steps, one a step of the cycle drawn
        const int step = std::clamp(int(phase * kRandomSteps), 0, kRandomSteps - 1);
        return sub::app::snarlWobble(kRandom, 0.0, step + 1);
    }
    return sub::app::snarlWobble(shape_, phase, 0);
}

void SnarlWobble::sync() {
    const int shape = int(std::lround(value(QStringLiteral("wobble_shape"))));
    if (shape != shape_) {
        shape_ = shape;
        Q_EMIT animated();
    }
    update();
}

void SnarlWobble::refreshDisplays() {
    const double dt = tickSeconds();
    const std::vector<float> phases = readRecent(QStringLiteral("phase"), 0.02);
    const std::vector<float> levels = readRecent(QStringLiteral("lfo"), 0.02);
    const bool came = !phases.empty() && !levels.empty();
    quiet_ = came ? 0.0 : quiet_ + dt;
    bool moved = false;
    if (came) {
        const double phase = std::clamp(double(phases.back()), 0.0, 1.0);
        const double level = std::clamp(double(levels.back()), 0.0, 1.0);
        if (phase + 0.5 < phase_)
            trail_.clear();  // (a new cycle: no line back across)
        moved = phase != phase_ || level != level_;
        phase_ = phase;
        level_ = level;
        if (pace_.step(dt) || trail_.empty()) {
            trail_.emplace_back(phase_, level_);
            while (int(trail_.size()) > kTrail) trail_.pop_front();
        } else {
            trail_.back() = QPointF(phase_, level_);
        }
    }
    lit_.target = quiet_ < kQuietSeconds ? 1.0 : 0.0;
    moved = lit_.step(easeFraction(dt, lit_.target > 0.0 ? 0.03 : 0.25), 1e-3) || moved;
    if (lit_.value == 0.0 && !trail_.empty()) {
        trail_.clear();
        moved = true;
    }
    if (moved) {
        Q_EMIT animated();
        update();
    }
}

void SnarlWobble::paint(SgPainter& p) {
    const QRectF well = p.rect().adjusted(0.5, 0.5, -0.5, -0.5);
    p.setAntialiasing(true);
    p.fillRoundedRect(well, 3, 3, Theme::meterBg());
    const QRectF r = plot();
    const auto at = [&](double phase, double level) {
        return QPointF(r.left() + phase * r.width(), r.bottom() - level * r.height());
    };
    p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.right(), r.bottom()), withAlpha(Theme::scopeAxis(), 60));

    // The shape: a cycle across, filled faintly under it (stepped for Random and Square: no slopes between).
    std::vector<QPointF> curve;
    curve.reserve(kSteps + 8);
    const bool stepped = shape_ == kRandom;
    for (int i = 0; i <= kSteps; ++i) {
        const double phase = double(i) / kSteps;
        const double level = curveAt(std::min(phase, 1.0 - 1e-9));
        if (i > 0 && (stepped || shape_ == 4) && std::abs(level - curveAt((i - 1.0) / kSteps)) > 1e-6)
            curve.push_back(at(phase, curveAt((i - 1.0) / kSteps)));  // (a jump: straight up or down)
        curve.push_back(at(phase, level));
    }
    const QColor color = Theme::accent();
    const double lit = lit_.value;
    QLinearGradient fill(r.topLeft(), r.bottomLeft());
    fill.setColorAt(0.0, withAlpha(color, int(40 + 50 * lit)));
    fill.setColorAt(1.0, withAlpha(color, 6));
    p.fillToBaseline(curve.data(), int(curve.size()), r.bottom(), fill);
    p.drawPolyline(curve.data(), int(curve.size()), withAlpha(color, int(150 + 100 * lit)), 1.3, Qt::RoundCap);

    if (lit > 0.002) {
        // Where it is now: the playhead across, the trail behind the dot, the dot glowing.
        const QPointF dot = at(phase_, level_);
        p.drawLine(QPointF(dot.x(), r.top()), QPointF(dot.x(), r.bottom()), withAlpha(Theme::text(), int(40 * lit)));
        for (size_t i = 1; i < trail_.size(); ++i) {
            const double age = double(i) / double(trail_.size());
            p.drawLine(at(trail_[i - 1].x(), trail_[i - 1].y()), at(trail_[i].x(), trail_[i].y()),
                       withAlpha(Theme::text(), int(140 * age * lit)), 1.0 + age, Qt::RoundCap);
        }
        p.fillRadialGlow(dot, 7.0, withAlpha(color, int(150 * lit)));
        p.fillEllipse(dot, 2.6, 2.6, withAlpha(Theme::text(), int(255 * lit)));
    }
    p.drawRoundedRect(well, 3, 3, Theme::border());
}

}  // namespace sub::ui
