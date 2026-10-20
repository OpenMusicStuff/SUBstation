#include "devices/SnarlScope.h"

#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QLinearGradient>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sub::ui {

SnarlScope::SnarlScope(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(160, 80);
    setAcceptedMouseButtons(Qt::NoButton);  // (clicks go on to the editor, the frame)
    scale_.snap(0.25);
}

std::array<double, SnarlScope::kPoints> SnarlScope::shown() const {
    std::array<double, kPoints> values{};
    for (int i = 0; i < kPoints; ++i) values[size_t(i)] = points_[size_t(i)].value;
    return values;
}

QColor SnarlScope::colorFor(double heat) {
    static const QColor acid = QColor::fromHslF(0.27, 0.9, 0.55);
    static const QColor hot = QColor::fromHslF(0.93, 0.85, 0.62);
    heat = std::clamp(heat, 0.0, 1.0);
    return heat < 0.5 ? mixColor(Theme::accent(), acid, heat * 2.0) : mixColor(acid, hot, heat * 2.0 - 1.0);
}

void SnarlScope::punch() {
    punch_ = 1.0;
    Q_EMIT animated();
    update();
}

void SnarlScope::refreshDisplays() {
    const double dt = tickSeconds();
    // The latest whole cycle read: the device publishes them 64 values at a time, so each starts at a
    // multiple of 64 in the display.
    const auto [at, values] = readDisplayAt(QStringLiteral("scope"));
    bool came = false;
    if (!values.empty()) {
        const qint64 end = at + qint64(values.size());
        const qint64 start = (end / kPoints) * kPoints - kPoints;
        if (start >= at) {
            double peak = 0.0;
            for (int i = 0; i < kPoints; ++i) {
                const double v = values[size_t(start - at + i)];
                points_[size_t(i)].target = v;
                peak = std::max(peak, std::abs(v));
            }
            scale_.target = std::max(0.05, peak);
            came = true;
        }
    }
    quiet_ = came ? 0.0 : quiet_ + dt;
    const bool sounding = quiet_ < kQuietSeconds;
    if (!sounding) {  // fading: it flattens as it goes
        for (Eased& point : points_) point.target = 0.0;
    }
    if (const std::vector<float> growl = readRecent(QStringLiteral("growl"), 0.03); !growl.empty())
        heat_.target = std::clamp(double(growl.back()), 0.0, 1.0);

    activity_.target = sounding ? 1.0 : 0.0;
    bool moved = activity_.step(easeFraction(dt, sounding ? kEaseSeconds : kFadeSeconds), 1e-3);
    moved = heat_.step(easeFraction(dt, 0.12), 1e-3) || moved;
    moved = scale_.step(easeFraction(dt, 0.3), 1e-4) || moved;
    const double ease = easeFraction(dt, sounding ? kEaseSeconds : kFadeSeconds);
    for (Eased& point : points_) moved = point.step(ease, 1e-4) || moved;
    if (punch_ > 0.0) {
        punch_ = std::max(0.0, punch_ - dt / kPunchSeconds);
        moved = true;
    }
    if (moved) {
        Q_EMIT animated();
        update();
    }
}

void SnarlScope::paint(SgPainter& p) {
    const QRectF r = p.rect().adjusted(0.5, 0.5, -0.5, -0.5);
    if (r.width() < 8 || r.height() < 8)
        return;
    p.setAntialiasing(true);
    p.fillRoundedRect(r, 3, 3, Theme::meterBg());
    const double middle = r.center().y();
    const QColor grid = withAlpha(Theme::scopeAxis(), 70);
    for (int i = 1; i < 4; ++i) {
        const double x = r.left() + r.width() * i / 4.0;
        p.drawLine(QPointF(x, r.top() + 2), QPointF(x, r.bottom() - 2), withAlpha(grid, i == 2 ? 70 : 35));
    }
    p.drawLine(QPointF(r.left() + 2, middle), QPointF(r.right() - 2, middle), grid);

    const double lit = activity_.value;
    if (lit <= 0.002) {
        p.drawRoundedRect(r, 3, 3, Theme::border());
        return;
    }
    // Two cycles across, scaled so the loudest nearly fills the height; a punch swells them for a moment.
    const double swell = 1.0 + 0.18 * smoothstep(punch_) * std::sin(punch_ * 3.14159265358979);
    const double gain = 0.86 * swell / std::max(0.05, scale_.value);
    const double half = r.height() / 2 - 3;
    std::vector<QPointF> line;
    const int count = kCycles * kPoints;
    line.reserve(size_t(count) + 1);
    for (int i = 0; i <= count; ++i) {
        const double v = points_[size_t(i % kPoints)].value * gain;
        line.emplace_back(r.left() + 1 + (r.width() - 2) * i / count, middle - std::clamp(v, -1.0, 1.0) * half);
    }
    const QColor color = colorFor(heat_.value);
    QLinearGradient fill(r.topLeft(), r.bottomLeft());
    fill.setColorAt(0.0, withAlpha(color, int(70 * lit)));
    fill.setColorAt(0.5, withAlpha(color, int(10 * lit)));
    fill.setColorAt(1.0, withAlpha(color, int(70 * lit)));
    p.fillToBaseline(line.data(), int(line.size()), middle, fill);
    drawGlowPolyline(p, line, withAlpha(color, int(255 * lit)), 1.5);
    p.drawRoundedRect(r, 3, 3, mixColor(Theme::border(), color, 0.35 * lit));
}

}  // namespace sub::ui
