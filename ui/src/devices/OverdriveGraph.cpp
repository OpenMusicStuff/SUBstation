#include "devices/OverdriveGraph.h"

#include "devices/EditorPaint.h"
#include "input/GestureKey.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace sub::ui {

OverdriveGraph::OverdriveGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::SizeAllCursor);
}

QRectF OverdriveGraph::curveRect() const {
    const double side = std::min<double>(kCurveWidth, height() - 2);
    return {width() - 1 - side, 1 + (height() - 2 - side) / 2, side, side};
}

QRectF OverdriveGraph::bandRect() const {
    return QRectF(1, 1, std::max(10.0, curveRect().left() - kGap - 1), height() - 2);
}

LogAxis OverdriveGraph::frequencyAxis() const {
    const QRectF r = bandRect();
    return {kLow, kHigh, r.left(), r.width()};
}

double OverdriveGraph::xOf(double freq) const { return frequencyAxis().position(freq); }

double OverdriveGraph::freqAt(double x) const { return frequencyAxis().valueAt(x); }

double OverdriveGraph::yOfWidth(double width) const {
    const QRectF inner = bandRect().adjusted(0, kDotRadius + 3, 0, -(kDotRadius + 3));
    const double t = (width - sub::app::kOverdriveMinWidth) / (sub::app::kOverdriveMaxWidth - sub::app::kOverdriveMinWidth);
    return inner.bottom() - std::clamp(t, 0.0, 1.0) * inner.height();
}

double OverdriveGraph::widthAt(double y) const {
    const QRectF inner = bandRect().adjusted(0, kDotRadius + 3, 0, -(kDotRadius + 3));
    const double t = std::clamp((inner.bottom() - y) / inner.height(), 0.0, 1.0);
    return sub::app::kOverdriveMinWidth + t * (sub::app::kOverdriveMaxWidth - sub::app::kOverdriveMinWidth);
}

double OverdriveGraph::driveActivity() const {
    return std::clamp(driveLevel_.value / sub::app::overdriveDriveGain(drive_), 0.0, 1.0);
}

QPointF OverdriveGraph::dot() const { return {xOf(freq_), yOfWidth(width_)}; }

void OverdriveGraph::sync() {
    freq_ = value(QStringLiteral("freq"));
    width_ = value(QStringLiteral("width"));
    drive_ = value(QStringLiteral("drive"));
    tone_ = value(QStringLiteral("tone"));
    updateCurves();
    update();
}

void OverdriveGraph::updateCurves() {
    const QRectF r = bandRect();
    const int points = std::max(2, int(r.width() / kColumn) + 1);
    QList<double> frequencies;
    frequencies.reserve(points);
    for (int i = 0; i < points; ++i) frequencies.append(freqAt(r.left() + i * r.width() / (points - 1)));
    const QList<double> band = sub::app::overdriveBandDb(freq_, width_, sampleRate(), frequencies);
    const QList<double> tone = sub::app::overdriveToneDb(tone_, sampleRate(), frequencies);
    const double top = r.top() + 8;
    const auto y = [&](double db) { return top + std::clamp(db / kFloorDb, 0.0, 1.0) * (r.bottom() - top); };
    band_.clear();
    tone_line_.clear();
    for (int i = 0; i < points; ++i) {
        const double x = r.left() + i * r.width() / (points - 1);
        band_.emplace_back(x, y(band[i]));
        tone_line_.emplace_back(x, y(tone[i]));
    }
    // The curve over -1..1 in, -1..1 out.
    const QRectF c = curveRect().adjusted(3, 3, -3, -3);
    transfer_.clear();
    const int steps = std::max(16, int(c.width()));
    for (int i = 0; i <= steps; ++i) {
        const double x = -1.0 + 2.0 * i / steps;
        const double out = std::clamp(sub::app::overdriveTransfer(x, drive_), -1.0, 1.0);
        transfer_.emplace_back(c.center().x() + x * c.width() / 2, c.center().y() - out * c.height() / 2);
    }
}

void OverdriveGraph::updateColumns() {
    const int columns = std::max(2, int(bandRect().width() / kColumn));
    columns_ = spectrum_.columns(columns, kLow, kHigh);
}

void OverdriveGraph::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    DeviceCanvas::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updateCurves();
        updateColumns();
    }
}

void OverdriveGraph::refreshDisplays() {
    const double dt = tickSeconds();
    bool moving = false;
    const std::vector<float> samples = readDisplay(QStringLiteral("input"));
    if (spectrum_.add(samples.data(), samples.size(), sampleRate(), dt)) {
        updateColumns();
        moving = true;
    }
    const auto peakOf = [this](const QString& id, double& into) {
        const std::vector<float> values = readRecent(id, kRecentSeconds);
        if (values.empty())
            return false;
        into = 0.0;
        for (const float v : values) {
            if (std::isfinite(v))
                into = std::max(into, double(v));
        }
        return true;
    };
    double drive = 0.0, output = 0.0;
    const bool came = peakOf(QStringLiteral("drive"), drive);
    peakOf(QStringLiteral("output"), output);
    // Up quickly, down over a quarter second; to rest in silence.
    driveLevel_.target = came ? drive : 0.0;
    outputLevel_.target = came ? std::min(output, 1.0) : 0.0;
    const auto ease = [dt](Eased& e) {
        return e.step(easeFraction(dt, e.target > e.value ? 0.02 : 0.25), 1e-3);
    };
    moving |= ease(driveLevel_);
    moving |= ease(outputLevel_);
    animating_ = moving;
    if (moving) {
        update();
        Q_EMIT driveActivityChanged();
    }
}

// --- Dragging -------------------------------------------------------------------------

void OverdriveGraph::setDragging(bool dragging) {
    if (dragging_ == dragging)
        return;
    dragging_ = dragging;
    Q_EMIT draggingChanged();
}

void OverdriveGraph::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !bandRect().contains(event->position())) {
        lastGesture_.clear();
        event->ignore();
        return;
    }
    if (secondPressOfDoubleClick(event))
        return;
    gesture_ = lastGesture_ = newGestureKey();
    setDragging(true);
    touch(QStringLiteral("freq"));
    dragTo(event->position());
}

void OverdriveGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!gesture_.isEmpty())
        dragTo(event->position());
}

void OverdriveGraph::mouseReleaseEvent(QMouseEvent*) {
    gesture_.clear();
    setDragging(false);
}

void OverdriveGraph::mouseUngrabEvent() {
    gesture_.clear();
    setDragging(false);
}

void OverdriveGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !bandRect().contains(event->position()))
        return;
    gesture_.clear();
    setDragging(false);
    setParams({{QStringLiteral("freq"), defaultValue(QStringLiteral("freq"))},
               {QStringLiteral("width"), defaultValue(QStringLiteral("width"))}},
              lastGesture_.isEmpty() ? newGestureKey() : lastGesture_);
}

void OverdriveGraph::dragTo(const QPointF& pos) {
    const double freq = std::clamp(freqAt(pos.x()), sub::app::kOverdriveMinFrequency, sub::app::kOverdriveMaxFrequency);
    const double width = std::nearbyint(widthAt(pos.y()) * 100.0) / 100.0;
    setParams({{QStringLiteral("freq"), freq}, {QStringLiteral("width"), width}}, gesture_);
}

// --- Painting ---------------------------------------------------------------------------

void OverdriveGraph::paint(SgPainter& p) {
    p.setAntialiasing(true);
    const QRectF r = bandRect();
    const QRectF c = curveRect();
    p.fillRect(r.adjusted(-1, -1, 1, 1), Theme::meterBg());
    p.fillRect(c.adjusted(-1, -1, 1, 1), Theme::meterBg());
    drawDecadeGrid(p, r, frequencyAxis());

    // The input's spectrum.
    const int columns = int(columns_.size());
    const double floor = sub::app::analysis::FallingSpectrum::kFloorDb;
    if (columns >= 2 && *std::max_element(columns_.begin(), columns_.end()) > floor) {
        std::vector<QPointF> shape;
        shape.reserve(std::size_t(columns));
        for (int i = 0; i < columns; ++i) {
            const double h = std::clamp((columns_[std::size_t(i)] - floor) / -floor, 0.0, 1.0) * r.height();
            shape.emplace_back(r.left() + (i + 0.5) * r.width() / columns, r.bottom() - h);
        }
        p.fillToBaseline(shape.data(), int(shape.size()), r.bottom(), withAlpha(Theme::text(), 30));
    }

    // The band, glowing with the output; Tone's low-pass dashed over it.
    const double glow = outputLevel_.value;
    if (band_.size() >= 2) {
        const QColor color = Theme::scopeLine();
        p.fillToBaseline(band_.data(), int(band_.size()), r.bottom(), withAlpha(color, 26 + int(40 * glow)));
        if (glow > 0.02)
            p.drawPolyline(band_.data(), int(band_.size()), withAlpha(color, int(60 * glow)), 4.0);
        p.drawPolyline(band_.data(), int(band_.size()), color, 1.5);
    }
    if (tone_line_.size() >= 2)
        drawDashedPolyline(p, tone_line_, withAlpha(Theme::textDim(), 160), 1.0);
    const QPointF at = dot();
    p.drawEllipse(QRectF(at.x() - kDotRadius, at.y() - kDotRadius, 2 * kDotRadius, 2 * kDotRadius),
                  dragging_ ? Theme::text() : Theme::accent(), 2);

    // The curve: dim end to end, lit from the middle out as far as the sound drives it, a dot riding it there.
    const QRectF inner = c.adjusted(3, 3, -3, -3);
    p.drawLine(QPointF(inner.left(), inner.center().y()), QPointF(inner.right(), inner.center().y()),
               withAlpha(Theme::text(), 30));
    p.drawLine(QPointF(inner.center().x(), inner.top()), QPointF(inner.center().x(), inner.bottom()),
               withAlpha(Theme::text(), 30));
    if (transfer_.size() >= 2) {
        p.drawPolyline(transfer_.data(), int(transfer_.size()), withAlpha(Theme::accent(), 90), 1.5);
        const double reach = driveActivity();
        if (reach > 0.005) {
            const int n = int(transfer_.size());
            const int half = n / 2;
            const int span = std::max(1, int(std::lround(reach * half)));
            const int from = std::max(0, half - span), to = std::min(n - 1, half + span);
            p.drawPolyline(transfer_.data() + from, to - from + 1, withAlpha(Theme::accent(), 70), 4.0);
            p.drawPolyline(transfer_.data() + from, to - from + 1, Theme::accent(), 1.5);
            const QPointF rider = transfer_[std::size_t(to)];
            p.fillRadialGlow(rider, 6.0, withAlpha(Theme::accent(), 120));
            p.fillEllipse(rider, 2.0, 2.0, Theme::text());
        }
    }
}

}  // namespace sub::ui
