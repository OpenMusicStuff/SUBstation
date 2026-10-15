#include "devices/VocalRiderGraph.h"

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

constexpr const char* kStreamIds[VocalRiderGraph::kStreams] = {"level", "gain", "target", "music"};
constexpr double kTargetMinDb = -50.0;  // the Vocal Rider's Target range
constexpr double kTargetMaxDb = 0.0;
constexpr double kLagSeconds = 0.03;
constexpr double kLagEaseSeconds = 0.16;
constexpr double kMaxLagSeconds = 0.1;

qint64 floorDiv(qint64 a, qint64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

float clearValue(int stream) {
    return stream == VocalRiderGraph::Gain ? 0.0f
           : stream == VocalRiderGraph::Target ? std::numeric_limits<float>::quiet_NaN()
                                               : float(VocalRiderGraph::kFloorDb);
}

float clean(int stream, float value) {
    if (stream == VocalRiderGraph::Gain) return std::isnan(value) ? 0.0f : std::clamp(value, -48.0f, 48.0f);
    if (stream == VocalRiderGraph::Target) return std::isnan(value) ? value : std::clamp(value, -96.0f, 0.0f);
    if (std::isnan(value)) return float(VocalRiderGraph::kFloorDb);
    return std::clamp(value, float(VocalRiderGraph::kFloorDb), 0.0f);
}

}  // namespace

VocalRiderGraph::VocalRiderGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kMinimumWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    ends_.fill(-1);
}

double VocalRiderGraph::levelsBottom() const { return std::floor(height() * (1.0 - kLaneShare)) - 2.0; }

double VocalRiderGraph::yOf(double db) const {
    const double fraction = (std::clamp(db, kFloorDb, 0.0) - kFloorDb) / -kFloorDb;
    return levelsBottom() - fraction * (levelsBottom() - 1.0);
}

double VocalRiderGraph::laneRange() const { return std::max({6.0, -minGain_, maxGain_}); }

double VocalRiderGraph::laneYOf(double db) const {
    const double top = levelsBottom() + 3.0, bottom = height() - 2.0;
    const double middle = 0.5 * (top + bottom);
    return middle - std::clamp(db / laneRange(), -1.0, 1.0) * 0.5 * (bottom - top - 2.0);
}

void VocalRiderGraph::sync() {
    target_ = value(QStringLiteral("target"));
    minGain_ = value(QStringLiteral("min_gain"));
    maxGain_ = value(QStringLiteral("max_gain"));
    if (!started_) liveTarget_ = target_;
    update();
}

// --- The history ---------------------------------------------------------------------------

void VocalRiderGraph::fitRings(double valuesPerSecond) {
    rate_ = std::max(1.0, valuesPerSecond);
    const double values = (kHistorySeconds * 1.1 + kMaxLagSeconds) * rate_ + 64.0;
    qint64 capacity = 64;
    while (double(capacity) < values) capacity *= 2;
    if (capacity == capacity_) return;
    capacity_ = capacity;
    for (int s = 0; s < kStreams; ++s) rings_[std::size_t(s)].assign(std::size_t(capacity_), clearValue(s));
    started_ = false;
}

qint64 VocalRiderGraph::historyBegin() const {
    qint64 latest = newest_;
    for (const qint64 end : ends_) latest = std::max(latest, end);
    return std::max(begin_, latest - capacity_);
}

int VocalRiderGraph::bucketSize() const {
    const double px = width() / std::max(1.0, kHistorySeconds * rate_);
    return px >= 1.0 ? 1 : int(std::ceil(1.0 / px - 1e-9));
}

bool VocalRiderGraph::readStreams(bool& restarted) {
    std::array<std::pair<qint64, std::vector<float>>, kStreams> reads;
    bool came = false;
    for (int s = 0; s < kStreams; ++s) {
        reads[std::size_t(s)] = readDisplayAt(QString::fromLatin1(kStreamIds[s]));
        came = came || !reads[std::size_t(s)].second.empty();
    }
    if (!came) return false;
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
        if (ends_[std::size_t(s)] < 0) begin_ = std::max(begin_, first);
        std::vector<float>& ring = rings_[std::size_t(s)];
        if (ends_[std::size_t(s)] >= 0) {
            for (qint64 i = std::max(ends_[std::size_t(s)], first - capacity_); i < first; ++i)
                ring[std::size_t(i & mask)] = clearValue(s);
        }
        for (qint64 i = std::max(first, end - capacity_); i < end; ++i)
            ring[std::size_t(i & mask)] = clean(s, values[std::size_t(i - first)]);
        ends_[std::size_t(s)] = end;
        const float latest = clean(s, values.back());
        if (s == Gain) gain_ = latest;
        if (s == Level) level_ = latest;
        if (s == Target && !std::isnan(latest)) liveTarget_ = latest;
    }
    newest_ = std::numeric_limits<qint64>::max();
    for (const qint64 end : ends_)
        if (end >= 0) newest_ = std::min(newest_, end);
    newest_ = std::max(newest_, begin_);
    return true;
}

void VocalRiderGraph::refreshDisplays() {
    const double dt = tickSeconds();
    fitRings(sampleRate() / kDisplaySamples);
    bool restarted = false;
    const bool came = readStreams(restarted);
    const double before = scroll_;
    if (restarted) {
        scroll_ = std::max(double(historyBegin()), double(newest_) - kLagSeconds * rate_);
    } else if (started_) {
        scroll_ += rate_ * dt;
        const double lag = double(newest_) - scroll_;
        if (lag > kMaxLagSeconds * rate_)
            scroll_ = double(newest_) - kMaxLagSeconds * rate_;
        else if (lag < 0.0)
            scroll_ = double(newest_);
        else
            scroll_ += easeFraction(dt, kLagEaseSeconds) * (lag - kLagSeconds * rate_);
    }
    scroll_ = std::min(scroll_, double(newest_));
    if (came || scroll_ != before) update();
    if (came) Q_EMIT levelsChanged();
}

// --- The mouse -----------------------------------------------------------------------------

bool VocalRiderGraph::onLine(const QPointF& pos) const { return std::abs(pos.y() - targetY()) <= kLineGrab; }

void VocalRiderGraph::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    if (event->button() == Qt::RightButton) {
        if (!onLine(pos) || device() == nullptr) {
            event->ignore();
            return;
        }
        Q_EMIT paramMenuRequested(QStringLiteral("target"));
        return;
    }
    if (event->button() != Qt::LeftButton || pos.y() > levelsBottom()) {  // (the ride's lane: nothing to drag)
        event->ignore();
        return;
    }
    if (secondPressOfDoubleClick(event) || device() == nullptr) return;
    dragging_ = true;
    hovered_ = true;
    gesture_ = newGestureKey();
    dragValue_ = target_;
    lastY_ = pos.y();
    touch(QStringLiteral("target"));
    setCursor(Qt::SizeVerCursor);
    Q_EMIT draggingChanged();
    update();
}

void VocalRiderGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const double y = event->position().y();
    const double fine = event->modifiers() & Qt::ShiftModifier ? 0.1 : 1.0;
    dragValue_ += (lastY_ - y) / std::max(1.0, levelsBottom() - 1.0) * -kFloorDb * fine;
    lastY_ = y;
    setParams({{QStringLiteral("target"), std::clamp(dragValue_, kTargetMinDb, kTargetMaxDb)}}, gesture_,
              QStringLiteral("Change Vocal Rider Target"));
}

void VocalRiderGraph::endDrag() {
    if (!dragging_) return;
    dragging_ = false;
    gesture_.clear();
    Q_EMIT draggingChanged();
    update();
}

void VocalRiderGraph::mouseReleaseEvent(QMouseEvent* event) {
    endDrag();
    hovered_ = onLine(event->position());
    setCursor(hovered_ ? Qt::SizeVerCursor : Qt::ArrowCursor);
    update();
}

void VocalRiderGraph::mouseUngrabEvent() {
    endDrag();
    hovered_ = false;
}

void VocalRiderGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || event->position().y() > levelsBottom()) return;
    endDrag();
    setParams({{QStringLiteral("target"), defaultValue(QStringLiteral("target"))}}, QString(),
              QStringLiteral("Change Vocal Rider Target"));
}

void VocalRiderGraph::hoverMoveEvent(QHoverEvent* event) {
    if (dragging_) return;
    const bool on = onLine(event->position());
    setCursor(on ? Qt::SizeVerCursor : Qt::ArrowCursor);
    if (on != hovered_) {
        hovered_ = on;
        update();
    }
}

void VocalRiderGraph::hoverLeaveEvent(QHoverEvent*) {
    if (dragging_ || !hovered_) return;
    hovered_ = false;
    update();
}

// --- Painting ------------------------------------------------------------------------------

void VocalRiderGraph::paint(SgPainter& p) {
    const double w = width(), h = height();
    const double split = levelsBottom();
    p.fillRect(QRectF(0, 0, w, h), Theme::meterBg());
    for (double db = -12.0; db > kFloorDb; db -= 12.0)
        p.drawLine(QPointF(0, yOf(db)), QPointF(w, yOf(db)), withAlpha(Theme::gridBeat(), 70));
    // The ride's lane: its own ground, 0 dB across it, the range's ends.
    const QRectF lane(0, split + 2.0, w, h - split - 2.0);
    p.fillRect(lane, withAlpha(Theme::text(), 8));
    p.drawLine(QPointF(0, split + 1.5), QPointF(w, split + 1.5), Theme::border());
    p.drawLine(QPointF(0, laneYOf(0.0)), QPointF(w, laneYOf(0.0)), withAlpha(Theme::gridBeat(), 140));
    for (const double end : {minGain_, maxGain_})
        if (end != 0.0)
            drawDashedPolyline(p, {QPointF(0, laneYOf(end)), QPointF(w, laneYOf(end))}, withAlpha(Theme::textDim(), 110),
                               1.0);
    p.setAntialiasing(true);

    int count = 0;
    double x0 = 0.0, dx = 1.0;
    bool haveTargets = false;
    if (started_ && capacity_ > 0) {
        const int k = bucketSize();
        const double px = w / std::max(1.0, kHistorySeconds * rate_);
        qint64 first = floorDiv(qint64(std::floor(scroll_ - kHistorySeconds * rate_)), k) - 1;
        first = std::max(first, floorDiv(historyBegin() + k - 1, k));
        const qint64 last = std::min(floorDiv(newest_, k), floorDiv(qint64(std::ceil(scroll_)), k) + 1);
        count = int(std::max<qint64>(0, last - first));
        if (count >= 2) {
            inTops_.resize(std::size_t(count));
            bottoms_.assign(std::size_t(count), float(split));
            outline_.resize(std::size_t(count));
            music_.resize(std::size_t(count));
            ride_.resize(std::size_t(count));
            targets_.clear();
            dx = k * px;
            x0 = w - (scroll_ - double(first * k)) * px;
            const qint64 mask = capacity_ - 1;
            for (int i = 0; i < count; ++i) {
                const qint64 from = (first + i) * k;
                float in = float(kFloorDb), music = float(kFloorDb), gain = 0.f, target = std::numeric_limits<float>::quiet_NaN();
                float out = float(kFloorDb);
                for (qint64 v = from; v < from + k; ++v) {
                    const std::size_t slot = std::size_t(v & mask);
                    in = std::max(in, rings_[Level][slot]);
                    music = std::max(music, rings_[Music][slot]);
                    gain = rings_[Gain][slot];
                    out = std::max(out, rings_[Level][slot] + rings_[Gain][slot]);
                    if (!std::isnan(rings_[Target][slot])) target = rings_[Target][slot];
                }
                const double x = x0 + (i + 0.5) * dx;
                inTops_[std::size_t(i)] = float(yOf(in));
                outline_[std::size_t(i)] = QPointF(x, yOf(out));
                music_[std::size_t(i)] = QPointF(x, yOf(music));
                ride_[std::size_t(i)] = QPointF(x, laneYOf(gain));
                if (!std::isnan(target)) {
                    targets_.emplace_back(x, yOf(target));
                    haveTargets = true;
                }
            }
        }
    }
    p.save();
    p.setClipRect(QRectF(0, 0, w, split));
    if (count >= 2) {
        p.fillBand(x0, dx, inTops_.data(), bottoms_.data(), count, withAlpha(Theme::textDim(), 110));
        p.drawPolyline(music_.data(), count, withAlpha(Theme::meterMid(), 120), 1.0);
        p.drawPolyline(outline_.data(), count, withAlpha(Theme::text(), 210), 1.0);
    }
    // Where the target was (Music Sens. moving it), faintly; the parameter's line, to drag.
    if (haveTargets && targets_.size() >= 2 && liveTarget_ != target_)
        p.drawPolyline(targets_.data(), int(targets_.size()), withAlpha(Theme::soloOn(), 110), 1.0);
    const double ty = std::round(targetY()) + 0.5;
    const QColor blue = Theme::soloOn();
    if (hovered_ || dragging_)
        drawGlowPolyline(p, {QPointF(0, ty), QPointF(w, ty)}, blue, 1.5);
    else
        p.drawLine(QPointF(0, ty), QPointF(w, ty), blue, 1.5);
    p.restore();

    p.save();
    p.setClipRect(lane);
    if (count >= 2) p.drawPolyline(ride_.data(), count, Theme::accent(), 1.5);
    p.restore();
}

}  // namespace sub::ui
