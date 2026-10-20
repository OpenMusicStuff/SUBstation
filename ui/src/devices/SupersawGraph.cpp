#include "devices/SupersawGraph.h"

#include "devices/EditorPaint.h"
#include "input/GestureKey.h"
#include "model/Device.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kAxisSeconds = 0.06;   // the axis' easing as the stacks widen or narrow
constexpr double kGlowSeconds = 0.05;   // the glow's
constexpr double kMaxShimmerHz = 8.0;   // a shimmer no faster than this (the display ticks at 60 Hz)
constexpr double kStackTicks[] = {10.0, 25.0, 50.0, 100.0};  // cents marked either side, while the axis reaches them

QColor scaledAlpha(const QColor& color, double alpha) {
    return withAlpha(color, int(std::lround(color.alpha() * std::clamp(alpha, 0.0, 1.0))));
}

// "7 saws · −12 st": a lane's caption.
QString stackCaption(int saws, double transpose, double fine) {
    QString text = saws == 1 ? QStringLiteral("1 saw") : QStringLiteral("%1 saws").arg(saws);
    const long semitones = std::lround(transpose);
    const long cents = std::lround(fine);
    if (semitones != 0)
        text += QStringLiteral(" · %1%2 st").arg(semitones > 0 ? QStringLiteral("+") : QString()).arg(semitones);
    if (cents != 0)
        text += QStringLiteral(" · %1%2 ct").arg(cents > 0 ? QStringLiteral("+") : QString()).arg(cents);
    return text;
}

}  // namespace

SupersawGraph::SupersawGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kMinimumWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::SizeAllCursor);
    axis_.snap(kMinAxisCents);
    meter_.reset();
}

void SupersawGraph::setStack(int stack) {
    stack = std::clamp(stack, 0, kStacks - 1);
    if (stack == stack_)
        return;
    stack_ = stack;
    Q_EMIT stackChanged();
    update();
}

QColor SupersawGraph::stackColour(int stack) {
    switch (stack) {
    case 0: return Theme::accent();
    case 1: return QColor(0x6f, 0xc8, 0xff);  // a cool blue
    default: return QColor(0xd2, 0x8c, 0xff);  // violet
    }
}

QString SupersawGraph::paramOf(int stack, const char* name) const {
    return QStringLiteral("s%1_%2").arg(stack + 1).arg(QLatin1String(name));
}

QRectF SupersawGraph::laneRect(int stack) const {
    const double lane = std::max(1.0, (height() - (kStacks - 1) * kLaneGap) / kStacks);
    return QRectF(0.0, std::clamp(stack, 0, kStacks - 1) * (lane + kLaneGap), width(), lane);
}

QRectF SupersawGraph::plotRect(int stack) const {
    return laneRect(stack).adjusted(kLabelWidth, kCaptionHeight + 1.0, -6.0, -2.0);
}

int SupersawGraph::sawCount(int stack) const { return int(saws(stack).size()); }

bool SupersawGraph::stackOn(int stack) const { return stacks_[std::size_t(std::clamp(stack, 0, kStacks - 1))].on; }

const std::vector<sub::app::SupersawSaw>& SupersawGraph::saws(int stack) const {
    return stacks_[std::size_t(std::clamp(stack, 0, kStacks - 1))].layout;
}

double SupersawGraph::xOfCents(int stack, double cents) const {
    const QRectF r = plotRect(stack);
    return r.center().x() + cents / std::max(axis_.value, 1e-6) * r.width() / 2.0;
}

QPointF SupersawGraph::sawPoint(int stack, int saw) const {
    const auto& all = saws(stack);
    if (saw < 0 || saw >= int(all.size()))
        return {};
    const QRectF r = plotRect(stack);
    const auto& s = all[std::size_t(saw)];
    const double reach = std::max(0.0, r.height() / 2.0 - sawRadius(stack, saw));
    return QPointF(xOfCents(stack, s.cents), r.center().y() + s.pan * reach);  // left at the top
}

double SupersawGraph::sawRadius(int stack, int saw) const {
    const auto& all = saws(stack);
    if (saw < 0 || saw >= int(all.size()))
        return 0.0;
    return 1.5 + 3.0 * std::sqrt(std::max(0.0, all[std::size_t(saw)].gain));
}

int SupersawGraph::laneAt(const QPointF& at) const {
    for (int s = 0; s < kStacks; ++s) {
        if (laneRect(s).contains(at))
            return s;
    }
    return -1;
}

// --- The parameters, and the displays ---------------------------------------------------

void SupersawGraph::sync() {
    const sub::app::Device* found = device();
    enabled_ = found && found->enabled;
    if (!found) {
        update();
        return;
    }
    double widest = 0.0;
    for (int s = 0; s < kStacks; ++s) {
        StackShape& shape = stacks_[std::size_t(s)];
        shape.on = value(paramOf(s, "on")) >= 0.5;
        shape.saws = std::clamp(int(std::lround(value(paramOf(s, "saws")))), 1, sub::app::supersawMaxSaws());
        shape.detune = value(paramOf(s, "detune"));
        shape.curve = value(paramOf(s, "curve"));
        shape.blend = value(paramOf(s, "blend"));
        shape.width = value(paramOf(s, "width"));
        shape.transpose = value(paramOf(s, "transpose"));
        shape.fine = value(paramOf(s, "fine"));
        shape.layout = sub::app::supersawLayout(shape.saws, shape.detune, shape.curve, shape.blend, shape.width);
        shape.beats.clear();
        for (const auto& saw : shape.layout)
            shape.beats.push_back(kReferenceHz * std::abs(std::exp2(saw.cents / 1200.0) - 1.0));
        if (shape.on || s == stack_)
            widest = std::max(widest, sub::app::supersawDetuneCents(shape.detune));
    }
    axis_.target = std::max(kMinAxisCents, widest * kAxisMargin);
    if (!synced_) {  // an editor opens settled
        axis_.snap(axis_.target);
        synced_ = true;
    }
    update();
}

void SupersawGraph::refreshDisplays() {
    const double dt = tickSeconds();
    // The output's loudest over the last tick, through the meter's ballistics (falling without any).
    double loudest = -120.0;
    for (const float sample : readRecent(QStringLiteral("output"), dt)) {
        if (std::isfinite(sample) && sample != 0.f)
            loudest = std::max(loudest, 20.0 * std::log10(std::abs(double(sample))));
    }
    meter_.update(enabled_ ? loudest : -120.0, dt, 24.0, 0.5);
    resting_ = meter_.level <= kSilentDb;
    if (!resting_)
        time_ += dt;
    glow_.target = resting_ ? 0.0 : std::clamp((meter_.level - kGlowFloorDb) / kGlowRangeDb, 0.0, 1.0);
    bool moving = glow_.step(easeFraction(dt, kGlowSeconds), 1e-3);
    moving |= axis_.step(easeFraction(dt, kAxisSeconds), 1e-3);
    if (moving || !resting_) {
        update();
        Q_EMIT animated();
    }
}

// --- Dragging -------------------------------------------------------------------------

void SupersawGraph::mousePressEvent(QMouseEvent* event) {
    const int lane = event->button() == Qt::LeftButton ? laneAt(event->position()) : -1;
    if (lane < 0) {
        event->ignore();
        return;
    }
    Q_EMIT stackPressed(lane);
    gesture_ = newGestureKey();
    dragStack_ = lane;
    lastAt_ = event->position();
    lastDetune_ = stacks_[std::size_t(lane)].detune;
    lastWidth_ = stacks_[std::size_t(lane)].width;
    startDrag(lastAt_, event->modifiers().testFlag(Qt::ShiftModifier));
    touch(paramOf(lane, "detune"));
    Q_EMIT draggingChanged();
    update();
}

void SupersawGraph::startDrag(const QPointF& at, bool fine) {
    pressedAt_ = at;
    pressedDetune_ = lastDetune_;
    pressedWidth_ = lastWidth_;
    fine_ = fine;
}

void SupersawGraph::mouseMoveEvent(QMouseEvent* event) {
    if (gesture_.isEmpty())
        return;
    const QPointF at = event->position();
    const bool fine = event->modifiers().testFlag(Qt::ShiftModifier);
    if (fine != fine_)  // Shift pressed or let go: on from the last move, without a jump
        startDrag(lastAt_, fine);
    const double scale = fine ? 4.0 : 1.0;
    lastAt_ = at;
    lastDetune_ = std::clamp(pressedDetune_ + (at.x() - pressedAt_.x()) * 100.0 / kDetunePixels / scale, 0.0, 100.0);
    lastWidth_ = std::clamp(pressedWidth_ + (pressedAt_.y() - at.y()) * 100.0 / kWidthPixels / scale, 0.0, 100.0);
    setParams({{paramOf(dragStack_, "detune"), std::round(lastDetune_ * 10.0) / 10.0},
               {paramOf(dragStack_, "width"), std::round(lastWidth_ * 10.0) / 10.0}},
              gesture_);
}

void SupersawGraph::mouseReleaseEvent(QMouseEvent*) {
    gesture_.clear();
    dragStack_ = -1;
    Q_EMIT draggingChanged();
    update();
}

void SupersawGraph::mouseUngrabEvent() {
    gesture_.clear();
    dragStack_ = -1;
    Q_EMIT draggingChanged();
    update();
}

void SupersawGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    const int lane = event->button() == Qt::LeftButton ? laneAt(event->position()) : -1;
    if (lane < 0)
        return;
    gesture_.clear();
    Q_EMIT draggingChanged();
    const QString detune = paramOf(lane, "detune"), width = paramOf(lane, "width");
    setParams({{detune, defaultValue(detune)}, {width, defaultValue(width)}}, newGestureKey());
}

// --- Painting ---------------------------------------------------------------------------

void SupersawGraph::paint(SgPainter& p) {
    p.setAntialiasing(true);
    const QFont number = uiFont(8, true);
    const QFont caption = uiFont(7);
    const double shimmerTime = time_;
    const double dim = enabled_ ? 1.0 : 0.6;
    for (int s = 0; s < kStacks; ++s) {
        const StackShape& shape = stacks_[std::size_t(s)];
        const QRectF lane = laneRect(s);
        const QRectF r = plotRect(s);
        const QColor colour = stackColour(s);
        const bool shown = s == stack_;
        p.fillRoundedRect(lane, 2, 2, shown ? mixColor(Theme::meterBg(), colour, 0.12) : Theme::meterBg());
        if (shown)
            p.drawRoundedRect(lane.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2, scaledAlpha(colour, 0.55), 1.0);
        p.drawText(QRectF(lane.left(), lane.top(), kLabelWidth, lane.height()), Qt::AlignCenter,
                   QString::number(s + 1), scaledAlpha(shape.on ? colour : Theme::textDisabled(), dim), number);

        // The axis: the stack's pitch in the middle, the cents marked either side; the stereo field's middle dashed.
        p.drawLine(r.center().x(), r.top(), r.center().x(), r.bottom(), withAlpha(Theme::scopeAxis(), 150), 1.0);
        for (const double cents : kStackTicks) {
            if (cents >= axis_.value)
                break;
            for (const double side : {-1.0, 1.0}) {
                const double x = xOfCents(s, side * cents);
                p.drawLine(x, r.top(), x, r.bottom(), withAlpha(Theme::scopeAxis(), 60), 1.0);
            }
        }
        drawDashedPolyline(p, {QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y())},
                           withAlpha(Theme::scopeAxis(), 110), 1.0);

        // The saws: a stem from the middle and a dot, glowing and shimmering at their beat while sound comes out.
        const double alpha = (shape.on ? 1.0 : 0.3) * dim;
        for (int i = 0; i < int(shape.layout.size()); ++i) {
            const QPointF at = sawPoint(s, i);
            const double radius = sawRadius(s, i);
            const double beat = std::min(shape.beats[std::size_t(i)], kMaxShimmerHz);
            const double shimmer = 0.5 + 0.5 * std::cos(2.0 * kPi * beat * shimmerTime);
            const double lit = shape.on ? glow_.value * (0.35 + 0.65 * shimmer) : 0.0;
            p.drawLine(at.x(), r.center().y(), at.x(), at.y(), scaledAlpha(colour, (0.35 + 0.45 * lit) * alpha), 1.0);
            if (lit > 0.02)
                p.fillRadialGlow(at, radius * 4.0, scaledAlpha(colour, 0.8 * lit * alpha), 5);
            p.fillEllipse(at, radius, radius,
                          scaledAlpha(mixColor(colour, Qt::white, 0.55 * lit), (0.7 + 0.3 * lit) * alpha));
        }

        // What the stack is, over its plot: how far its outermost saws reach; its saws and transpose (or off).
        const QRectF strip(r.left() + 2, lane.top() + 1, r.width() - 2, kCaptionHeight);
        const QColor ink = scaledAlpha(Theme::textDim(), dim);
        if (shape.saws > 1) {
            p.drawText(strip, Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("±%1 ct").arg(std::lround(sub::app::supersawDetuneCents(shape.detune))), ink,
                       caption);
        }
        p.drawText(strip, Qt::AlignRight | Qt::AlignVCenter,
                   shape.on ? stackCaption(shape.saws, shape.transpose, shape.fine) : tr("off"), ink, caption);
    }
}

}  // namespace sub::ui
