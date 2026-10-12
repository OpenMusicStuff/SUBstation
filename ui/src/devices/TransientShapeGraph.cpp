#include "devices/TransientShapeGraph.h"

#include "audio/TransientResponse.h"
#include "input/GestureKey.h"
#include "model/ParamSpec.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QHoverEvent>
#include <QLinearGradient>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {

constexpr double kMorphSeconds = 0.07;   // the curve easing to a new shape
constexpr double kHoverSeconds = 0.08;   // a handle growing under the mouse
constexpr double kOnsetDb = 6.0;         // a transient this strong starts a playhead,
constexpr double kRearmDb = 2.0;         // once it has fallen back under this
constexpr double kTrailMs = 70.0;        // a playhead's comet tail
constexpr double kPercentPerPixel = 1.0;  // an amount's drag
constexpr double kPixelsPerOctave = 40.0;  // a time's drag
constexpr double kDecidePixels = 4.0;     // a drag picks its direction after this far
constexpr double kShownDb = 0.3;          // less change than this isn't coloured

QColor attackColor() { return Theme::accent(); }
QColor sustainColor() { return Theme::soloOn(); }

}  // namespace

TransientShapeGraph::TransientShapeGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);  // (right-click off the handles: the frame's menu)
    setAcceptHoverEvents(true);
    // Evenly spaced across the square-root axis: denser where the onset is.
    const double window = sub::app::transientHitWindowMs();
    times_.resize(kColumns);
    for (int i = 0; i < kColumns; ++i) {
        const double f = double(i) / (kColumns - 1);
        times_[std::size_t(i)] = window * f * f;
    }
    sync();
}

QFont TransientShapeGraph::textFont() { return uiFont(7); }

QString TransientShapeGraph::hovered() const {
    const Handle h = drag_ != Handle::None ? drag_ : hover_;
    return h == Handle::Attack ? QStringLiteral("attack") : h == Handle::Sustain ? QStringLiteral("sustain") : QString();
}

// --- Geometry ------------------------------------------------------------------------------

QRectF TransientShapeGraph::plot() const {
    return QRectF(5, 5, std::max(1.0, width() - 10), std::max(1.0, height() - 10));
}

double TransientShapeGraph::xOfMs(double ms) const {
    const QRectF r = plot();
    const double f = std::sqrt(std::clamp(ms / sub::app::transientHitWindowMs(), 0.0, 1.0));
    return r.left() + f * r.width();
}

double TransientShapeGraph::msAt(double x) const {
    const QRectF r = plot();
    const double f = std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
    return f * f * sub::app::transientHitWindowMs();
}

double TransientShapeGraph::yOf(double db) const {
    const QRectF r = plot();
    if (!std::isfinite(db))
        db = db > 0 ? kCeilingDb : kFloorDb;
    return r.top() + (kCeilingDb - std::clamp(db, kFloorDb, kCeilingDb)) / (kCeilingDb - kFloorDb) * r.height();
}

int TransientShapeGraph::peakIndex() const {
    return int(std::max_element(target_.begin(), target_.end()) - target_.begin());
}

int TransientShapeGraph::sustainIndex() const {
    return int(std::lower_bound(times_.begin(), times_.end(), kSustainMs) - times_.begin());
}

double TransientShapeGraph::drawnAt(double ms) const {
    const auto it = std::lower_bound(times_.begin(), times_.end(), ms);
    if (it == times_.begin())
        return drawn_.front();
    if (it == times_.end())
        return drawn_.back();
    const std::size_t i = std::size_t(it - times_.begin());
    const double t = (ms - times_[i - 1]) / std::max(1e-9, times_[i] - times_[i - 1]);
    return drawn_[i - 1] + t * (drawn_[i] - drawn_[i - 1]);
}

QPointF TransientShapeGraph::attackHandle() const {
    const std::size_t i = std::size_t(peakIndex());
    return QPointF(xOfMs(times_[i]), yOf(drawn_[i]));
}

QPointF TransientShapeGraph::sustainHandle() const {
    const std::size_t i = std::size_t(sustainIndex());
    return QPointF(xOfMs(times_[i]), yOf(drawn_[i]));
}

std::vector<double> TransientShapeGraph::heads() const { return {heads_.begin(), heads_.begin() + headCount_}; }

// --- The parameters ------------------------------------------------------------------------

void TransientShapeGraph::sync() {
    attack_ = value(QStringLiteral("attack"));
    sustain_ = value(QStringLiteral("sustain"));
    attackTime_ = value(QStringLiteral("attack_time"));
    release_ = value(QStringLiteral("release"));
    threshold_ = value(QStringLiteral("threshold"));
    if (!alive()) {  // (no device yet: its defaults)
        attackTime_ = attackTime_ > 0 ? attackTime_ : 20.0;
        release_ = release_ > 0 ? release_ : 300.0;
    }
    const sub::app::TransientHit hit =
        sub::app::transientHit(attack_, sustain_, attackTime_, release_, threshold_, times_);
    in_ = hit.inDb;
    target_ = hit.outDb;
    attackTarget_ = hit.attackDb;
    sustainTarget_ = hit.sustainDb;
    // The curve starts as it is, and follows a drag at once; otherwise it morphs.
    if (!synced_ || drag_ != Handle::None || drawn_.size() != target_.size()) {
        drawn_ = target_;
        attackPart_ = attackTarget_;
        sustainPart_ = sustainTarget_;
    }
    synced_ = true;
    update();
}

// --- The ticks -----------------------------------------------------------------------------

void TransientShapeGraph::refreshDisplays() {
    const double dt = tickSeconds();
    bool redraw = false;

    // A transient heard: a playhead starts along the hit (the oldest makes way).
    for (const float detected : readRecent(QStringLiteral("detect"), 0.1)) {
        if (armed_ && detected >= kOnsetDb) {
            if (headCount_ == kMaxHeads) {
                std::rotate(heads_.begin(), heads_.begin() + 1, heads_.end());
                --headCount_;
            }
            heads_[std::size_t(headCount_++)] = 0.0;
            armed_ = false;
        } else if (detected < kRearmDb) {
            armed_ = true;
        }
    }
    const double window = sub::app::transientHitWindowMs();
    int kept = 0;
    for (int i = 0; i < headCount_; ++i) {
        const double ms = heads_[std::size_t(i)] + dt * 1000.0;
        if (ms <= window)
            heads_[std::size_t(kept++)] = ms;
        redraw = true;  // (moved, or just ended)
    }
    headCount_ = kept;

    // The curve morphing to its shape.
    const double f = easeFraction(dt, kMorphSeconds);
    const auto ease = [f](std::vector<float>& drawn, const std::vector<float>& target) {
        bool moved = false;
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            const float gap = target[i] - drawn[i];
            if (gap == 0.f)
                continue;
            drawn[i] = std::abs(gap) < 0.01f ? target[i] : drawn[i] + float(f) * gap;
            moved = true;
        }
        return moved;
    };
    redraw = ease(drawn_, target_) || redraw;
    redraw = ease(attackPart_, attackTarget_) || redraw;
    redraw = ease(sustainPart_, sustainTarget_) || redraw;

    const Handle shown = drag_ != Handle::None ? drag_ : hover_;
    hoverAttack_.target = shown == Handle::Attack ? 1.0 : 0.0;
    hoverSustain_.target = shown == Handle::Sustain ? 1.0 : 0.0;
    redraw = hoverAttack_.step(easeFraction(dt, kHoverSeconds), 1e-3) || redraw;
    redraw = hoverSustain_.step(easeFraction(dt, kHoverSeconds), 1e-3) || redraw;

    animating_ = redraw;
    if (redraw)
        update();
}

// --- The mouse -----------------------------------------------------------------------------

TransientShapeGraph::Handle TransientShapeGraph::handleAt(const QPointF& pos) const {
    const auto distance = [&](const QPointF& at) { return std::hypot(pos.x() - at.x(), pos.y() - at.y()); };
    const double toAttack = distance(attackHandle()), toSustain = distance(sustainHandle());
    if (std::min(toAttack, toSustain) > kGrab)
        return Handle::None;
    return toAttack <= toSustain ? Handle::Attack : Handle::Sustain;
}

void TransientShapeGraph::setHover(Handle handle) {
    if (handle == hover_)
        return;
    hover_ = handle;
    setCursor(handle != Handle::None ? Qt::SizeAllCursor : Qt::ArrowCursor);
    Q_EMIT hoveredChanged();
    update();
}

void TransientShapeGraph::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    const Handle handle = handleAt(pos);
    if (event->button() == Qt::RightButton) {
        // On a handle: its amount's menu (automation, a macro); elsewhere, the device's (the frame's).
        if (handle == Handle::None || device() == nullptr) {
            event->ignore();
            return;
        }
        Q_EMIT paramMenuRequested(handle == Handle::Attack ? QStringLiteral("attack") : QStringLiteral("sustain"));
        return;
    }
    if (event->button() != Qt::LeftButton || handle == Handle::None || device() == nullptr) {
        event->ignore();  // (off the handles: the frame's, selecting the device)
        return;
    }
    clicked_ = handle;
    if (secondPressOfDoubleClick(event))
        return;  // (the double-click follows)
    drag_ = handle;
    axis_ = Axis::Undecided;
    pressPos_ = lastPos_ = pos;
    const bool attack = handle == Handle::Attack;
    dragAmount_ = attack ? attack_ : sustain_;
    dragTime_ = attack ? attackTime_ : release_;
    gesture_ = newGestureKey();
    touch(attack ? QStringLiteral("attack") : QStringLiteral("sustain"));
    Q_EMIT draggingChanged();
    Q_EMIT hoveredChanged();
    update();
}

void TransientShapeGraph::mouseMoveEvent(QMouseEvent* event) {
    if (drag_ == Handle::None)
        return;
    const QPointF pos = event->position();
    const bool attack = drag_ == Handle::Attack;
    if (axis_ == Axis::Undecided) {
        const QPointF moved = pos - pressPos_;
        if (std::max(std::abs(moved.x()), std::abs(moved.y())) < kDecidePixels)
            return;
        axis_ = std::abs(moved.x()) > std::abs(moved.y()) ? Axis::Time : Axis::Amount;
        if (axis_ == Axis::Time)
            touch(attack ? QStringLiteral("attack_time") : QStringLiteral("release"));
        lastPos_ = pressPos_;  // (the pixels it took to decide count)
    }
    const double fine = event->modifiers() & Qt::ShiftModifier ? 0.1 : 1.0;
    const QPointF delta = pos - lastPos_;
    lastPos_ = pos;
    if (axis_ == Axis::Amount) {
        dragAmount_ -= delta.y() * kPercentPerPixel * fine;  // (up: more)
        dragAmount_ = std::clamp(dragAmount_, sub::app::transientAmountRange().min - 20.0,
                                 sub::app::transientAmountRange().max + 20.0);  // (past the ends a little, not far)
        const QString id = attack ? QStringLiteral("attack") : QStringLiteral("sustain");
        setParams({{id, sub::app::transientAmountRange().clamp(dragAmount_)}}, gesture_,
                  attack ? QStringLiteral("Change Attack") : QStringLiteral("Change Sustain"));
    } else {
        dragTime_ *= std::exp2(delta.x() / kPixelsPerOctave * fine);
        const sub::app::TransientRange range =
            attack ? sub::app::transientAttackTimeRange() : sub::app::transientReleaseRange();
        dragTime_ = std::clamp(dragTime_, range.min / 2, range.max * 2);
        setParams({{attack ? QStringLiteral("attack_time") : QStringLiteral("release"), range.clamp(dragTime_)}},
                  gesture_, attack ? QStringLiteral("Change Attack Time") : QStringLiteral("Change Release"));
    }
}

void TransientShapeGraph::endDrag() {
    if (drag_ == Handle::None)
        return;
    drag_ = Handle::None;
    gesture_.clear();
    Q_EMIT draggingChanged();
    Q_EMIT hoveredChanged();
    update();
}

void TransientShapeGraph::mouseReleaseEvent(QMouseEvent* event) {
    endDrag();
    setHover(handleAt(event->position()));
}

void TransientShapeGraph::mouseUngrabEvent() {
    endDrag();
    setHover(Handle::None);
}

void TransientShapeGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || clicked_ == Handle::None) {
        event->ignore();
        return;
    }
    // The handle its first press was on (where it was then: a reset moves it).
    const bool attack = clicked_ == Handle::Attack;
    endDrag();
    const QString amount = attack ? QStringLiteral("attack") : QStringLiteral("sustain");
    const QString time = attack ? QStringLiteral("attack_time") : QStringLiteral("release");
    setParams({{amount, defaultValue(amount)}, {time, defaultValue(time)}}, QString(),
              attack ? QStringLiteral("Reset Attack") : QStringLiteral("Reset Sustain"));
    clicked_ = Handle::None;
}

void TransientShapeGraph::hoverMoveEvent(QHoverEvent* event) {
    if (drag_ == Handle::None)
        setHover(handleAt(event->position()));
}

void TransientShapeGraph::hoverLeaveEvent(QHoverEvent*) {
    if (drag_ == Handle::None)
        setHover(Handle::None);
}

// --- Painting ------------------------------------------------------------------------------

QString TransientShapeGraph::tagText(Handle handle) const {
    const bool attack = handle == Handle::Attack;
    return QStringLiteral("%1 %2 · %3")
        .arg(attack ? tr("Attack") : tr("Sustain"),
             sub::app::formatValue(attack ? attack_ : sustain_, QStringLiteral("%")),
             sub::app::formatValue(attack ? attackTime_ : release_, QStringLiteral("ms")));
}

void TransientShapeGraph::paint(SgPainter& p) {
    p.setAntialiasing(true);
    const double w = width(), h = height();
    p.fillRoundedRect(QRectF(0, 0, w, h), 4, 4, Theme::meterBg());
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 4, 4, withAlpha(Theme::gridBeat(), 150), 1);
    const QRectF r = plot();
    const QFont small = textFont();
    if (drawn_.size() != times_.size())
        return;

    // The grid: a line every 12 dB (0 dB brighter), and the times 10, 100 and 500 ms.
    for (int db = 12; db > kFloorDb; db -= 12)
        p.drawLine(QPointF(r.left(), yOf(db)), QPointF(r.right(), yOf(db)),
                   withAlpha(Theme::gridBar(), db == 0 ? 130 : 55));
    for (const double ms : {10.0, 100.0, 500.0}) {
        const double x = xOfMs(ms);
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()), withAlpha(Theme::gridBar(), 45));
        // Each figure right of its line; the last, with the unit, left of it (the plot ends soon after).
        const bool last = ms == 500.0;
        const QRectF box = last ? QRectF(x - 2 - 40, r.bottom() - 12, 40, 11) : QRectF(x + 2, r.bottom() - 12, 40, 11);
        p.drawText(box, (last ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                   last ? QStringLiteral("500 ms") : QString::number(ms), Theme::textDisabled(), small);
    }

    // The hit's curves, a point per column, evenly across.
    const int n = int(times_.size());
    inLine_.resize(std::size_t(n));
    outLine_.resize(std::size_t(n));
    for (int i = 0; i < n; ++i) {
        const double x = xOfMs(times_[std::size_t(i)]);
        inLine_[std::size_t(i)] = QPointF(x, yOf(in_[std::size_t(i)]));
        outLine_[std::size_t(i)] = QPointF(x, yOf(drawn_[std::size_t(i)]));
    }
    p.save();
    p.setClipRect(r);
    // Before: a ghost.
    p.fillToBaseline(inLine_.data(), n, r.bottom(), withAlpha(Theme::textDim(), 30));
    // After: filled, fading downwards.
    QLinearGradient fill(QPointF(0, r.top()), QPointF(0, r.bottom()));
    fill.setColorAt(0, withAlpha(Theme::text(), 70));
    fill.setColorAt(1, withAlpha(Theme::text(), 6));
    p.fillToBaseline(outLine_.data(), n, r.bottom(), fill);

    // Between before and after, where they part: the colour of what did it (Attack or Sustain, whichever
    // gave more of the change there), bright where it turns the hit up, dimmer where down; a run of
    // columns of one kind at once.
    tops_.resize(std::size_t(n));
    bottoms_.resize(std::size_t(n));
    const double dx = r.width() / (n - 1);
    const auto kindOf = [&](int i) {
        const std::size_t at = std::size_t(i);
        const double change = drawn_[at] - in_[at];
        if (std::abs(change) < kShownDb || std::max(in_[at], drawn_[at]) <= kFloorDb)
            return 0;  // (no change, or none in sight)
        const bool byAttack = std::abs(attackPart_[at]) >= std::abs(sustainPart_[at]);
        return (byAttack ? 1 : 3) + (change > 0 ? 0 : 1);  // 1, 2: Attack up, down; 3, 4: Sustain up, down
    };
    for (int i = 0; i < n;) {
        const int kind = kindOf(i);
        int j = i + 1;
        while (j < n && kindOf(j) == kind)
            ++j;
        if (kind != 0) {
            for (int k = i; k < j; ++k) {
                const std::size_t at = std::size_t(k);
                tops_[at] = float(std::min(inLine_[at].y(), outLine_[at].y()));
                bottoms_[at] = float(std::max(inLine_[at].y(), outLine_[at].y()));
            }
            const QColor color = kind <= 2 ? attackColor() : sustainColor();
            p.fillBand(r.left() + (i - 0.5) * dx, dx, tops_.data() + i, bottoms_.data() + i, j - i,
                       withAlpha(color, kind % 2 == 1 ? 165 : 80));
        }
        i = j;
    }
    drawDashedPolyline(p, inLine_, withAlpha(Theme::textDim(), 150), 1.0);
    drawGlowPolyline(p, outLine_, Theme::text(), 1.4);

    // The playheads: a glowing head on the curve with a comet tail, orange through the onset turning blue
    // along the tail, fading out at the window's end.
    const double window = sub::app::transientHitWindowMs();
    for (int k = 0; k < headCount_; ++k) {
        const double ms = heads_[std::size_t(k)];
        const double fade = 1.0 - smoothstep((ms - 0.8 * window) / (0.2 * window));
        const QColor color = mixColor(attackColor(), sustainColor(), smoothstep(ms / std::max(1.0, 3.0 * attackTime_)));
        const double x = xOfMs(ms);
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()), withAlpha(color, int(std::lround(40 * fade))));
        constexpr int kTrailPoints = 14;
        QPointF previous;
        for (int s = 0; s <= kTrailPoints; ++s) {
            const double at = std::max(0.0, ms - kTrailMs * (1.0 - double(s) / kTrailPoints));
            const QPointF point(xOfMs(at), yOf(drawnAt(at)));
            if (s > 0)
                p.drawLine(previous, point, withAlpha(color, int(std::lround(255 * fade * s / kTrailPoints))),
                           1.0 + 1.6 * s / kTrailPoints, Qt::RoundCap);
            previous = point;
        }
        p.fillEllipse(previous, 7, 7, withAlpha(color, int(std::lround(70 * fade))));
        p.fillEllipse(previous, 2.6, 2.6, withAlpha(mixColor(color, Theme::text(), 0.6), int(std::lround(255 * fade))));
    }
    p.restore();

    // Attack Time and Release on the time axis: a bar from the start to Attack Time, a notch at Release
    // (an arrow at the edge when it is past the window).
    const double attackX = xOfMs(attackTime_);
    p.fillRect(QRectF(r.left(), r.bottom() - 2, attackX - r.left(), 2), withAlpha(attackColor(), 150));
    p.fillRect(QRectF(attackX - 0.75, r.bottom() - 5, 1.5, 5), attackColor());
    if (release_ <= window) {
        const double x = xOfMs(release_);
        p.fillRect(QRectF(x - 0.75, r.bottom() - 5, 1.5, 5), sustainColor());
    } else {
        const QPointF tip(r.right(), r.bottom() - 3);
        const QPointF arrow[3] = {tip, tip + QPointF(-4, -2.5), tip + QPointF(-4, 2.5)};
        p.fillPolygon(arrow, 3, sustainColor());
    }

    // The handles: a dot each on the curve, its halo growing under the mouse; while hovered or dragged, its
    // values in a tag beside it.
    const auto handle = [&](const QPointF& at, const QColor& color, double hover) {
        p.fillEllipse(at, 7 + 4 * hover, 7 + 4 * hover, withAlpha(color, int(std::lround(35 + 55 * hover))));
        p.fillEllipse(at, 4.6 + hover, 4.6 + hover, withAlpha(Theme::meterBg(), 230));
        p.fillEllipse(at, 3.4 + hover, 3.4 + hover, color);
    };
    handle(sustainHandle(), sustainColor(), hoverSustain_.value);
    handle(attackHandle(), attackColor(), hoverAttack_.value);
    const auto tag = [&](const QPointF& at, double opacity, const QString& text, const QColor& color) {
        if (opacity <= 0.01)
            return;
        const double tw = SgPainter::textWidth(text, small) + 8;
        double left = std::clamp(at.x() - tw / 2, r.left() + 1, r.right() - 1 - tw);
        const double top = at.y() - 22 >= r.top() ? at.y() - 22 : at.y() + 10;
        const QRectF box(left, top, tw, 12);
        p.save();
        p.setOpacity(opacity);
        p.fillRoundedRect(box, 2, 2, withAlpha(Theme::meterBg(), 225));
        p.drawRoundedRect(box, 2, 2, withAlpha(color, 120), 1);
        p.drawText(box, Qt::AlignCenter, text, color, small);
        p.restore();
    };
    tag(attackHandle(), hoverAttack_.value, tagText(Handle::Attack), attackColor());
    tag(sustainHandle(), hoverSustain_.value, tagText(Handle::Sustain), sustainColor());
}

}  // namespace sub::ui
