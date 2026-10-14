#include "controls/Meter.h"

#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

namespace sub::ui {

Meter::Meter(QQuickItem* parent) : SgCanvas(parent) {
    setAcceptedMouseButtons(Qt::AllButtons);
    setImplicitSize(10, 40);  // at least 8 wide, as the Python UI's meter
}

double Meter::fraction(double level) {
    if (level <= 0.0)
        return 0.0;
    const double db = 20.0 * std::log10(level);
    return std::clamp((db - kFloorDb) / (kCeilingDb - kFloorDb), 0.0, 1.0);
}

void Meter::setCompact(bool compact) {
    if (compact == compact_)
        return;
    compact_ = compact;
    setAcceptedMouseButtons(compact ? Qt::NoButton : Qt::AllButtons);  // (no clip light to clear)
    Q_EMIT compactChanged();
    update();
}

void Meter::setLevels(qreal left, qreal right) {
    bool changed = false;
    const double levels[2] = {left, right};
    for (int i = 0; i < 2; ++i) {
        const double value = std::max(fraction(levels[i]), display_[i] - kFallPerUpdate);
        if (std::abs(value - display_[i]) > 1e-4) {
            display_[i] = value;
            changed = true;
        }
        // The peak tick: up at once, held, then falling as the bar does.
        double held = held_[i];
        if (display_[i] >= held) {
            held = display_[i];
            holding_[i] = kHoldUpdates;
        } else if (holding_[i] > 0) {
            --holding_[i];
        } else {
            held = std::max(display_[i], held - kFallPerUpdate);
        }
        if (std::abs(held - held_[i]) > 1e-4) {
            held_[i] = held;
            changed = changed || compact_;
        }
    }
    if (changed)
        Q_EMIT levelsChanged();
    if (std::max(left, right) >= 1.0 && !clipped_) {
        clipped_ = true;
        changed = true;
        Q_EMIT clippedChanged();
    }
    if (changed)
        update();
}

void Meter::reset() {
    display_[0] = display_[1] = 0.0;
    held_[0] = held_[1] = 0.0;
    holding_[0] = holding_[1] = 0;
    Q_EMIT levelsChanged();
    clearClip();
    update();
}

void Meter::clearClip() {
    if (!clipped_)
        return;
    clipped_ = false;
    Q_EMIT clippedChanged();
    update();
}

void Meter::mousePressEvent(QMouseEvent*) {
    clearClip();  // a click clears the clip light
}

void Meter::paintCompact(SgPainter& p) {
    // Two bars about 1.5 px wide, 1 px apart, centred, in whole device pixels (fillRect fills a pixel
    // whose centre is inside: a bar between pixels would come out uneven); green, yellow from -12 dB,
    // red from -1 dB.
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const double barWidth = std::max(1.0, std::round(1.5 * dpr)) / dpr;
    const double gap = std::max(1.0, std::round(1.0 * dpr)) / dpr;
    const double left = std::round((width() - (2 * barWidth + gap)) / 2 * dpr) / dpr;
    const double h = height();
    const auto yOf = [&](double db) { return h - h * (db - kFloorDb) / (kCeilingDb - kFloorDb); };
    const double yellow = yOf(-12.0), red = yOf(-1.0);
    QColor track = Theme::meterBg();
    for (int i = 0; i < 2; ++i) {
        const double x = left + i * (barWidth + gap);
        p.fillRect(QRectF(x, 0, barWidth, h), track);
        const double top = h * (1.0 - display_[i]);
        if (display_[i] > 0.0) {
            p.fillRect(QRectF(x, std::max(top, yellow), barWidth, h - std::max(top, yellow)), Theme::meterLow());
            if (top < yellow)
                p.fillRect(QRectF(x, std::max(top, red), barWidth, yellow - std::max(top, red)), Theme::meterMid());
            if (top < red)
                p.fillRect(QRectF(x, top, barWidth, red - top), Theme::meterHigh());
        }
        if (held_[i] > 0.0) {
            const double y = std::min(h - 1.0, std::floor(h * (1.0 - held_[i])));
            p.fillRect(QRectF(x, y, barWidth, 1.0), y <= red ? Theme::meterHigh() : Theme::accent());
        }
    }
}

void Meter::paint(SgPainter& p) {
    if (compact_) {
        paintCompact(p);
        return;
    }
    const QRectF rect(0, 0, width(), height());
    p.fillRect(rect, Theme::meterBg());
    const double clipHeight = 3.0;
    const QRectF bars = rect.adjusted(1, clipHeight + 1, -1, -1);
    const double gap = 1.0;
    const double barWidth = (bars.width() - gap) / 2;
    const double zeroDb = 1.0 - (0.0 - kFloorDb) / (kCeilingDb - kFloorDb);
    QLinearGradient gradient(0, bars.bottom(), 0, bars.top());
    gradient.setColorAt(0.0, Theme::meterLow());
    gradient.setColorAt(std::max(0.0, 1.0 - zeroDb - 0.18), Theme::meterLow());
    gradient.setColorAt(std::max(0.0, 1.0 - zeroDb - 0.05), Theme::meterMid());
    gradient.setColorAt(1.0 - zeroDb, Theme::meterHigh());
    gradient.setColorAt(1.0, Theme::meterHigh());
    for (int i = 0; i < 2; ++i) {
        if (display_[i] <= 0.0)
            continue;
        const double h = bars.height() * display_[i];
        p.fillRect(QRectF(bars.left() + i * (barWidth + gap), bars.bottom() - h, barWidth, h), gradient);
    }
    if (clipped_)
        p.fillRect(QRectF(rect.left() + 1, rect.top() + 1, rect.width() - 2, clipHeight - 1), Theme::meterHigh());
}

}  // namespace sub::ui
