#include "devices/ReductionGraph.h"

#include "devices/EditorPaint.h"
#include "input/GestureKey.h"
#include "model/Device.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QHoverEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sub::ui {

namespace {

constexpr const char* kStreamIds[ReductionGraph::kStreams] = {"reduction", "input", "output"};
constexpr double kThresholdMinDb = -60.0;  // the Compressor's Threshold range
constexpr double kThresholdMaxDb = 0.0;
constexpr double kLagSeconds = 0.03;      // the drawing stays this far behind the newest value,
constexpr double kLagEaseSeconds = 0.16;  // easing back to it at this pace
constexpr double kMaxLagSeconds = 0.1;    // and jumps on when it falls further behind

qint64 floorDiv(qint64 a, qint64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// What a stream holds where nothing came: no reduction, silence.
float clearValue(int stream) { return stream == ReductionGraph::Reduction ? 0.0f : float(ReductionGraph::kFloorDb); }

// A value as drawn: NaN as nothing, the infinities as the range's ends.
float clean(int stream, float value) {
    if (stream == ReductionGraph::Reduction) {
        if (std::isnan(value)) return 0.0f;
        return std::clamp(value, 0.0f, float(-ReductionGraph::kFloorDb));
    }
    if (std::isnan(value)) return float(ReductionGraph::kFloorDb);
    return std::clamp(value, float(ReductionGraph::kFloorDb), 0.0f);
}

}  // namespace

ReductionGraph::ReductionGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kMinimumWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    ends_.fill(-1);
}

void ReductionGraph::setShowReduction(bool show) {
    if (show == showReduction_) return;
    showReduction_ = show;
    Q_EMIT showChanged();
    update();
}

void ReductionGraph::setShowOutput(bool show) {
    if (show == showOutput_) return;
    showOutput_ = show;
    Q_EMIT showChanged();
    update();
}

double ReductionGraph::yOf(double db) const {
    const double fraction = (std::clamp(db, kFloorDb, 0.0) - kFloorDb) / -kFloorDb;
    return height() - 1.0 - fraction * (height() - 2.0);
}

void ReductionGraph::sync() {
    const sub::app::Device* found = device();
    threshold_ = found ? found->params.value(QStringLiteral("threshold"), -18.0) : -18.0;
    update();
}

// --- The history ---------------------------------------------------------------------------

void ReductionGraph::fitRings(double valuesPerSecond) {
    rate_ = std::max(1.0, valuesPerSecond);
    // What the plot shows, a tenth more for the columns drawn in part at its ends, and the lag.
    const double values = (kHistorySeconds * 1.1 + kMaxLagSeconds) * rate_ + 64.0;
    qint64 capacity = 64;
    while (double(capacity) < values) capacity *= 2;
    if (capacity == capacity_) return;
    capacity_ = capacity;
    for (int s = 0; s < kStreams; ++s) rings_[std::size_t(s)].assign(std::size_t(capacity_), clearValue(s));
    started_ = false;  // (what was kept has no place in the new rings: the next read starts the history again)
}

qint64 ReductionGraph::historyBegin() const {
    qint64 latest = newest_;
    for (const qint64 end : ends_) latest = std::max(latest, end);
    return std::max(begin_, latest - capacity_);
}

int ReductionGraph::bucketSize() const {
    const double px = width() / std::max(1.0, kHistorySeconds * rate_);
    return px >= 1.0 ? 1 : int(std::ceil(1.0 / px - 1e-9));
}

bool ReductionGraph::readStreams(bool& restarted) {
    std::array<std::pair<qint64, std::vector<float>>, kStreams> reads;
    bool came = false;
    for (int s = 0; s < kStreams; ++s) {
        reads[std::size_t(s)] = readDisplayAt(QString::fromLatin1(kStreamIds[s]));
        came = came || !reads[std::size_t(s)].second.empty();
    }
    if (!came) return false;
    // Its processor made again (its indices start from 0 again), or the first values: a new history.
    restarted = !started_;
    for (int s = 0; s < kStreams; ++s) {
        const auto& [first, values] = reads[std::size_t(s)];
        if (!values.empty() && ends_[std::size_t(s)] >= 0 && first < ends_[std::size_t(s)]) restarted = true;
    }
    if (restarted) {
        for (int s = 0; s < kStreams; ++s)
            std::fill(rings_[std::size_t(s)].begin(), rings_[std::size_t(s)].end(), clearValue(s));
        ends_.fill(-1);
        begin_ = std::numeric_limits<qint64>::min();
        started_ = true;
    }
    const qint64 mask = capacity_ - 1;
    for (int s = 0; s < kStreams; ++s) {
        const auto& [first, values] = reads[std::size_t(s)];
        if (values.empty()) continue;
        const qint64 end = first + qint64(values.size());
        if (ends_[std::size_t(s)] < 0) begin_ = std::max(begin_, first);  // (the history begins where all have values)
        std::vector<float>& ring = rings_[std::size_t(s)];
        for (qint64 i = std::max(first, end - capacity_); i < end; ++i)
            ring[std::size_t(i & mask)] = clean(s, values[std::size_t(i - first)]);
        ends_[std::size_t(s)] = end;
        // The figures: the latest reduction, the loudest of the levels that came.
        if (s == Reduction) {
            reduction_ = clean(s, values.back());
        } else {
            double loudest = kFloorDb;
            for (float v : values) loudest = std::max(loudest, double(clean(s, v)));
            (s == Input ? levelIn_ : levelOut_) = loudest;
        }
    }
    newest_ = std::numeric_limits<qint64>::max();
    for (const qint64 end : ends_)
        if (end >= 0) newest_ = std::min(newest_, end);
    newest_ = std::max(newest_, begin_);
    return true;
}

void ReductionGraph::refreshDisplays() {
    const double dt = tickSeconds();
    fitRings(sampleRate() / kDisplaySamples);  // (the engine's rate may have changed)
    bool restarted = false;
    const bool came = readStreams(restarted);
    // Scrolling: on by the time since the last tick, eased towards a steady lag behind the newest
    // value; never past it.
    const double before = scroll_;
    if (restarted) {
        scroll_ = std::max(double(historyBegin()), double(newest_) - kLagSeconds * rate_);
    } else if (started_) {
        scroll_ += rate_ * dt;
        const double lag = double(newest_) - scroll_;
        if (lag > kMaxLagSeconds * rate_)
            scroll_ = double(newest_) - kMaxLagSeconds * rate_;  // fell behind: jump
        else if (lag < 0.0)
            scroll_ = double(newest_);  // no more values: it stops there
        else
            scroll_ += easeFraction(dt, kLagEaseSeconds) * (lag - kLagSeconds * rate_);
    }
    scroll_ = std::min(scroll_, double(newest_));
    if (came || scroll_ != before) update();
    if (came) Q_EMIT levelsChanged();
}

// --- The mouse -----------------------------------------------------------------------------

bool ReductionGraph::onLine(const QPointF& pos) const { return std::abs(pos.y() - thresholdY()) <= kLineGrab; }

void ReductionGraph::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    if (event->button() == Qt::RightButton) {
        // On the line: its parameter's menu (automation, a macro); elsewhere, the device's (the frame's).
        if (!onLine(pos) || device() == nullptr) {
            event->ignore();
            return;
        }
        Q_EMIT paramMenuRequested(QStringLiteral("threshold"));
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (secondPressOfDoubleClick(event) || device() == nullptr) return;  // (the double-click follows)
    // Anywhere: the threshold, relative to where it was (nothing jumps).
    dragging_ = true;
    hovered_ = true;
    gesture_ = newGestureKey();
    dragValue_ = threshold_;
    lastY_ = pos.y();
    touch(QStringLiteral("threshold"));
    setCursor(Qt::SizeVerCursor);
    Q_EMIT draggingChanged();
    update();
}

void ReductionGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const double y = event->position().y();
    const double fine = event->modifiers() & Qt::ShiftModifier ? 0.1 : 1.0;
    dragValue_ += (lastY_ - y) / std::max(1.0, height() - 2.0) * -kFloorDb * fine;  // up: higher
    lastY_ = y;
    setParams({{QStringLiteral("threshold"), std::clamp(dragValue_, kThresholdMinDb, kThresholdMaxDb)}}, gesture_,
              QStringLiteral("Change Compressor Threshold"));
}

void ReductionGraph::endDrag() {
    if (!dragging_) return;
    dragging_ = false;
    gesture_.clear();
    Q_EMIT draggingChanged();
    update();
}

void ReductionGraph::mouseReleaseEvent(QMouseEvent* event) {
    endDrag();
    hovered_ = onLine(event->position());
    setCursor(hovered_ ? Qt::SizeVerCursor : Qt::ArrowCursor);
    update();
}

void ReductionGraph::mouseUngrabEvent() {
    endDrag();
    hovered_ = false;
}

void ReductionGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    endDrag();
    setParams({{QStringLiteral("threshold"), defaultValue(QStringLiteral("threshold"))}}, QString(),
              QStringLiteral("Change Compressor Threshold"));
}

void ReductionGraph::hoverMoveEvent(QHoverEvent* event) {
    if (dragging_) return;
    const bool on = onLine(event->position());
    setCursor(on ? Qt::SizeVerCursor : Qt::ArrowCursor);
    if (on != hovered_) {
        hovered_ = on;
        update();
    }
}

void ReductionGraph::hoverLeaveEvent(QHoverEvent*) {
    if (dragging_ || !hovered_) return;
    hovered_ = false;
    update();
}

// --- Painting ------------------------------------------------------------------------------

void ReductionGraph::paint(SgPainter& p) {
    const double w = width(), h = height();
    p.fillRect(QRectF(0, 0, w, h), Theme::meterBg());
    // Every 12 dB, faintly.
    for (double db = -12.0; db > kFloorDb; db -= 12.0)
        p.drawLine(QPointF(0, yOf(db)), QPointF(w, yOf(db)), withAlpha(Theme::gridBeat(), 70));
    p.setAntialiasing(true);

    // The history, newest at the right edge, in columns of whole buckets of values (aligned to the
    // values' absolute index, so nothing shimmers as it scrolls), gliding by fractions of a pixel.
    int count = 0;
    double x0 = 0.0, dx = 1.0;
    if (started_ && capacity_ > 0) {
        const int k = bucketSize();
        const double px = w / std::max(1.0, kHistorySeconds * rate_);
        qint64 first = floorDiv(qint64(std::floor(scroll_ - kHistorySeconds * rate_)), k) - 1;
        first = std::max(first, floorDiv(historyBegin() + k - 1, k));  // (only buckets the history wholly has)
        const qint64 last = std::min(floorDiv(newest_, k), floorDiv(qint64(std::ceil(scroll_)), k) + 1);
        count = int(std::max<qint64>(0, last - first));
        if (count >= 2) {
            inTops_.resize(std::size_t(count));
            bottoms_.assign(std::size_t(count), float(h));
            outline_.resize(std::size_t(count));
            reductionLine_.resize(std::size_t(count));
            dx = k * px;
            x0 = w - (scroll_ - double(first * k)) * px;
            const qint64 mask = capacity_ - 1;
            for (int i = 0; i < count; ++i) {
                const qint64 from = (first + i) * k;
                float in = float(kFloorDb), out = float(kFloorDb), reduction = 0.f;
                for (qint64 v = from; v < from + k; ++v) {
                    const std::size_t slot = std::size_t(v & mask);
                    in = std::max(in, rings_[Input][slot]);
                    out = std::max(out, rings_[Output][slot]);
                    reduction = std::max(reduction, rings_[Reduction][slot]);
                }
                const double x = x0 + (i + 0.5) * dx;
                inTops_[std::size_t(i)] = float(yOf(in));
                outline_[std::size_t(i)] = QPointF(x, yOf(out));
                reductionLine_[std::size_t(i)] = QPointF(x, yOf(-reduction));
            }
        }
    }
    p.save();
    p.setClipRect(QRectF(0, 0, w, h));
    if (count >= 2) {
        // The input, filled from the bottom; the output outlined over it.
        p.fillBand(x0, dx, inTops_.data(), bottoms_.data(), count, withAlpha(Theme::textDim(), 120));
        if (showOutput_) p.drawPolyline(outline_.data(), count, withAlpha(Theme::text(), 200), 1.0);
    }

    // The threshold, glowing while it is under the mouse or dragged.
    const double ty = std::round(thresholdY()) + 0.5;
    const QColor blue = Theme::soloOn();
    if (hovered_ || dragging_)
        drawGlowPolyline(p, {QPointF(0, ty), QPointF(w, ty)}, blue, 1.5);
    else
        p.drawLine(QPointF(0, ty), QPointF(w, ty), blue, 1.5);

    // The reduction, hanging from the top.
    if (showReduction_ && count >= 2) p.drawPolyline(reductionLine_.data(), count, Theme::accent(), 1.5);
    p.restore();

    // The reduction now, at the bottom right.
    const QString text = reduction_ >= 0.005 ? QStringLiteral("−%1 dB").arg(reduction_, 0, 'f', 2)
                                             : QStringLiteral("0.00 dB");
    p.drawText(QRectF(w - 70, h - 18, 64, 14), Qt::AlignRight | Qt::AlignVCenter, text, Theme::accent(), uiFont(8));
}

}  // namespace sub::ui
