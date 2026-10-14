#include "devices/VitalEnvelopeGraph.h"

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
constexpr double kGrab = 8.0;          // px from a handle that picks it
constexpr double kPowerPerPixel = 0.15;  // a curve's power, dragged up and down
constexpr double kFineRatio = 0.2;     // Shift
constexpr double kGrowSeconds = 0.05;
constexpr double kDotSeconds = 0.08;   // the live dot fades in and out
constexpr double kStaleSeconds = 0.12;  // without a voice this long: none plays
constexpr std::size_t kTrail = 14;

// vital::futils::powerScale.
double powerScale(double value, double power) {
    if (std::abs(power) < 0.01)
        return value;
    return (std::exp(power * value) - 1.0) / (std::exp(power) - 1.0);
}

QString nameOf(int handle) {
    switch (handle) {
    case VitalEnvelopeGraph::Delay: return QStringLiteral("Delay");
    case VitalEnvelopeGraph::Attack: return QStringLiteral("Attack");
    case VitalEnvelopeGraph::Hold: return QStringLiteral("Hold");
    case VitalEnvelopeGraph::Decay: return QStringLiteral("Decay · Sustain");
    case VitalEnvelopeGraph::Release: return QStringLiteral("Release");
    case VitalEnvelopeGraph::AttackCurve: return QStringLiteral("Attack curve");
    case VitalEnvelopeGraph::DecayCurve: return QStringLiteral("Decay curve");
    case VitalEnvelopeGraph::ReleaseCurve: return QStringLiteral("Release curve");
    default: return {};
    }
}
}  // namespace

VitalEnvelopeGraph::VitalEnvelopeGraph(QQuickItem* parent) : VitalGraph(parent) {
    setImplicitSize(240, 100);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

double VitalEnvelopeGraph::attackValue(double t, double power) { return powerScale(std::clamp(t, 0.0, 1.0), -power); }

double VitalEnvelopeGraph::decayValue(double t, double power, double sustain) {
    return 1.0 - (1.0 - sustain) * powerScale(std::clamp(t, 0.0, 1.0), power);
}

double VitalEnvelopeGraph::releaseValue(double t, double power, double from) {
    return from * (1.0 - powerScale(std::clamp(t, 0.0, 1.0), power));
}

QRectF VitalEnvelopeGraph::area() const { return plot().adjusted(7, 15, -8, -6); }

double VitalEnvelopeGraph::span() const {
    return std::max(kMinSpan, s_.delay + s_.attack + s_.hold + s_.decay + kSustainWidth + s_.release);
}

double VitalEnvelopeGraph::scale() const {
    // While a stage's end is dragged the scale stays as it was, so the handle follows the mouse.
    if (pressed_ >= Delay && pressed_ <= Release)
        return startScale_;
    return area().width() / span();
}

double VitalEnvelopeGraph::xOf(double units) const { return area().left() + units * scale(); }

double VitalEnvelopeGraph::yOf(double level) const {
    const QRectF a = area();
    return a.bottom() - std::clamp(level, 0.0, 1.0) * a.height();
}

QPointF VitalEnvelopeGraph::handlePosition(int handle) const {
    const double b1 = s_.delay, b2 = b1 + s_.attack, b3 = b2 + s_.hold, b4 = b3 + s_.decay, b5 = b4 + kSustainWidth,
                 b6 = b5 + s_.release;
    switch (handle) {
    case Delay: return {xOf(b1), yOf(0)};
    case Attack: return {xOf(b2), yOf(1)};
    case Hold: return {xOf(b3), yOf(1)};
    case Decay: return {xOf(b4), yOf(s_.sustain)};
    case Release: return {xOf(b6), yOf(0)};
    case AttackCurve: return {xOf((b1 + b2) / 2), yOf(attackValue(0.5, s_.attackPower))};
    case DecayCurve: return {xOf((b3 + b4) / 2), yOf(decayValue(0.5, s_.decayPower, s_.sustain))};
    case ReleaseCurve: return {xOf((b5 + b6) / 2), yOf(releaseValue(0.5, s_.releasePower, s_.sustain))};
    default: return {};
    }
}

double VitalEnvelopeGraph::xOfPhase(double phase) const {
    const int stage = int(std::floor(phase));
    const double t = std::clamp(phase - stage, 0.0, 1.0);
    const double b1 = s_.delay, b2 = b1 + s_.attack, b3 = b2 + s_.hold, b4 = b3 + s_.decay, b5 = b4 + kSustainWidth,
                 b6 = b5 + s_.release;
    switch (stage) {
    case 1: return xOf(t * b1);                // delaying
    case 2: return xOf(b1 + t * s_.attack);    // attacking
    case 3: return xOf(b2 + t * s_.hold);      // holding
    case 4: return xOf(t < 1.0 ? b3 + t * s_.decay : b4 + kSustainWidth / 2);  // decaying, then sustaining
    case 5: return xOf(b5 + t * s_.release);   // releasing
    default: return xOf(b6);
    }
}

void VitalEnvelopeGraph::readSection() {
    s_.delay = param(QStringLiteral("delay"));
    s_.attack = param(QStringLiteral("attack"));
    s_.hold = param(QStringLiteral("hold"));
    s_.decay = param(QStringLiteral("decay"));
    s_.sustain = std::clamp(param(QStringLiteral("sustain")), 0.0, 1.0);
    s_.release = param(QStringLiteral("release"));
    s_.attackPower = param(QStringLiteral("attack_power"));
    s_.decayPower = param(QStringLiteral("decay_power"));
    s_.releasePower = param(QStringLiteral("release_power"));
}

std::vector<QPointF> VitalEnvelopeGraph::curve() const {
    std::vector<QPointF> points;
    const auto stage = [&](double from, double width, auto level) {
        const int steps = std::clamp(int(width * scale() / 2), 1, 64);
        for (int i = 0; i <= steps; ++i) {
            const double t = double(i) / steps;
            points.emplace_back(xOf(from + t * width), yOf(level(t)));
        }
    };
    const double b1 = s_.delay, b2 = b1 + s_.attack, b3 = b2 + s_.hold, b4 = b3 + s_.decay, b5 = b4 + kSustainWidth;
    points.emplace_back(xOf(0), yOf(0));
    points.emplace_back(xOf(b1), yOf(0));
    stage(b1, s_.attack, [&](double t) { return attackValue(t, s_.attackPower); });
    points.emplace_back(xOf(b3), yOf(1));
    stage(b3, s_.decay, [&](double t) { return decayValue(t, s_.decayPower, s_.sustain); });
    points.emplace_back(xOf(b5), yOf(s_.sustain));
    stage(b5, s_.release, [&](double t) { return releaseValue(t, s_.releasePower, s_.sustain); });
    return points;
}

// --- Live ---------------------------------------------------------------------------------------

void VitalEnvelopeGraph::refreshDisplays() {
    const double seconds = tickSeconds();
    const QString id = QStringLiteral("env_%1").arg(index() + 1);
    const std::vector<float> phases = readDisplay(id);
    const std::vector<float> levels = readDisplay(id + QStringLiteral("_value"));
    const std::size_t count = std::min(phases.size(), levels.size());
    bool moved = false;
    for (std::size_t i = count > kTrail ? count - kTrail : 0; i < count; ++i) {
        if (phases[i] < 0.0f)
            continue;
        trail_.emplace_back(phases[i], levels[i]);
        if (trail_.size() > kTrail)
            trail_.pop_front();
        moved = true;
    }
    if (count > 0 && phases[count - 1] >= 0.0f) {
        phase_ = phases[count - 1];
        level_ = levels[count - 1];
        stale_ = 0.0;
    } else if (count > 0 || (stale_ += seconds) > kStaleSeconds) {
        stale_ = kStaleSeconds;
        if (!trail_.empty()) {
            trail_.pop_front();
            moved = true;
        }
    }
    const bool wasPlaying = playing();
    dot_.target = stale_ < kStaleSeconds && phase_ >= 0.0 ? 1.0 : 0.0;
    moved |= dot_.step(easeFraction(seconds, kDotSeconds), 1e-3);
    for (int h = 0; h < HandleCount; ++h) {
        grow_[std::size_t(h)].target = (h == hovered_ || h == pressed_) ? 1.0 : 0.0;
        moved |= grow_[std::size_t(h)].step(easeFraction(seconds, kGrowSeconds), 1e-3);
    }
    moved |= advanceDim(seconds);
    if (playing() != wasPlaying)
        Q_EMIT playingChanged();
    if (moved)
        update();
}

// --- Painting -----------------------------------------------------------------------------------

void VitalEnvelopeGraph::paint(SgPainter& p) {
    paintBackground(p);
    p.setAntialiasing(true);
    const QRectF a = area();
    const QColor color = Theme::modulation();

    // The grid: the stages' ends, the level's quarters.
    for (int i = 0; i <= 4; ++i) {
        const double y = a.bottom() - i / 4.0 * a.height();
        p.drawLine(QPointF(a.left(), y), QPointF(a.right(), y), i % 2 == 0 ? gridMajor() : gridMinor());
    }
    for (const int h : {Delay, Attack, Hold, Decay}) {
        const double x = handlePosition(h).x();
        p.drawLine(QPointF(x, a.top()), QPointF(x, a.bottom()), gridMinor());
    }
    const double sustainEnd = handlePosition(Decay).x() + kSustainWidth * scale();
    p.fillRect(QRectF(handlePosition(Decay).x(), a.top(), sustainEnd - handlePosition(Decay).x(), a.height()),
               QColor(255, 255, 255, 6));

    // The curve.
    const std::vector<QPointF> line = curve();
    QLinearGradient fill(a.topLeft(), a.bottomLeft());
    fill.setColorAt(0, withAlpha(color, 90));
    fill.setColorAt(1, withAlpha(color, 6));
    p.fillToBaseline(line.data(), int(line.size()), a.bottom(), fill);
    drawGlowPolyline(p, line, color, 1.7);

    // The live dot: a trail, a glowing head on a faint line down.
    if (dot_.value > 0.0) {
        p.save();
        p.setOpacity(dot_.value);
        for (std::size_t i = 1; i < trail_.size(); ++i) {
            const QPointF from(xOfPhase(trail_[i - 1].x()), yOf(trail_[i - 1].y()));
            const QPointF to(xOfPhase(trail_[i].x()), yOf(trail_[i].y()));
            if (to.x() < from.x())
                continue;  // (a new note: not across)
            const double fade = double(i) / double(trail_.size());
            p.drawLine(from, to, withAlpha(Qt::white, int(120 * fade * fade)), 1.0 + 1.2 * fade, Qt::RoundCap);
        }
        const QPointF head(xOfPhase(phase_), yOf(level_));
        p.drawLine(QPointF(head.x(), a.top()), QPointF(head.x(), a.bottom()), withAlpha(color, 50));
        p.fillRadialGlow(head, 8, withAlpha(color, 90), 3);
        p.fillEllipse(head, 3.0, 3.0, Qt::white);
        p.restore();
    }

    // The handles: the stages' ends, the curves' (smaller, hollow).
    for (int h = 0; h < HandleCount; ++h) {
        const QPointF at = handlePosition(h);
        const double grow = grow_[std::size_t(h)].value;
        if (h >= AttackCurve) {
            const double r = 2.4 + 1.4 * grow;
            p.fillEllipse(at, r, r, backgroundBottom());
            p.drawEllipse(QRectF(at.x() - r, at.y() - r, 2 * r, 2 * r), withAlpha(color, int(150 + 100 * grow)), 1.2);
        } else {
            const double r = 3.4 + 1.6 * grow;
            if (grow > 0.0)
                p.fillRadialGlow(at, r + 6, withAlpha(color, int(80 * grow)), 3);
            p.fillEllipse(at, r, r, mixColor(color, Qt::white, 0.35 + 0.5 * grow));
        }
    }

    const int named = pressed_ != None ? pressed_ : hovered_;
    if (named != None) {
        const QRectF r = plot();
        p.drawText(QRectF(r.left() + 5, r.top() + 1, r.width() - 10, 12), Qt::AlignRight | Qt::AlignVCenter,
                   nameOf(named), labelColor(), labelFont());
    }
}

// --- The mouse ----------------------------------------------------------------------------------

VitalEnvelopeGraph::Handle VitalEnvelopeGraph::handleNear(const QPointF& pos) const {
    // The stages' ends first (the attack's before the hold's, which it covers while there is no hold; the
    // later ends before the earlier, which they cover while a stage takes no time), then the curves'.
    Handle best = None;
    double nearest = kGrab;
    for (const Handle h : {Release, Decay, Attack, Hold, Delay, AttackCurve, DecayCurve, ReleaseCurve}) {
        const QPointF d = handlePosition(h) - pos;
        const double distance = std::hypot(d.x(), d.y());
        if (distance < nearest - 0.5) {
            nearest = distance;
            best = h;
        }
    }
    return best;
}

QStringList VitalEnvelopeGraph::paramsOf(Handle handle) {
    switch (handle) {
    case Delay: return {QStringLiteral("delay")};
    case Attack: return {QStringLiteral("attack")};
    case Hold: return {QStringLiteral("hold")};
    case Decay: return {QStringLiteral("decay"), QStringLiteral("sustain")};
    case Release: return {QStringLiteral("release")};
    case AttackCurve: return {QStringLiteral("attack_power")};
    case DecayCurve: return {QStringLiteral("decay_power")};
    case ReleaseCurve: return {QStringLiteral("release_power")};
    default: return {};
    }
}

void VitalEnvelopeGraph::mousePressEvent(QMouseEvent* event) {
    if (secondPressOfDoubleClick(event))
        return;
    const Handle handle = handleNear(event->position());
    if (handle == None) {
        event->ignore();
        return;
    }
    startScale_ = area().width() / span();
    pressed_ = handle;
    anchor_ = event->position();
    start_ = s_;
    beginGesture(paramsOf(handle).front());
    event->accept();
}

void VitalEnvelopeGraph::mouseMoveEvent(QMouseEvent* event) {
    if (pressed_ == None || !dragging())
        return;
    const double fine = event->modifiers() & Qt::ShiftModifier ? kFineRatio : 1.0;
    const QPointF moved = (event->position() - anchor_) * fine;
    const double units = moved.x() / startScale_;
    const QString p = prefix();
    const auto time = [&](double from, double max) { return std::clamp(from + units, 0.0, max); };
    const auto power = [&](double from) { return std::clamp(from - moved.y() * kPowerPerPixel, -kMaxPower, kMaxPower); };
    switch (pressed_) {
    case Delay: setDragged({{p + QStringLiteral("delay"), time(start_.delay, kMaxDelay)}}); break;
    case Attack: setDragged({{p + QStringLiteral("attack"), time(start_.attack, kMaxTime)}}); break;
    case Hold: setDragged({{p + QStringLiteral("hold"), time(start_.hold, kMaxDelay)}}); break;
    case Decay:
        setDragged({{p + QStringLiteral("decay"), time(start_.decay, kMaxTime)},
                    {p + QStringLiteral("sustain"),
                     std::clamp(start_.sustain - moved.y() / std::max(1.0, area().height()), 0.0, 1.0)}});
        break;
    case Release: setDragged({{p + QStringLiteral("release"), time(start_.release, kMaxTime)}}); break;
    case AttackCurve: setDragged({{p + QStringLiteral("attack_power"), power(start_.attackPower)}}); break;
    case DecayCurve: setDragged({{p + QStringLiteral("decay_power"), power(start_.decayPower)}}); break;
    case ReleaseCurve: setDragged({{p + QStringLiteral("release_power"), power(start_.releasePower)}}); break;
    default: break;
    }
}

void VitalEnvelopeGraph::mouseReleaseEvent(QMouseEvent*) {
    pressed_ = None;
    endGesture();
    update();
}

void VitalEnvelopeGraph::mouseUngrabEvent() {
    pressed_ = None;
    endGesture();
}

void VitalEnvelopeGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    const Handle handle = handleNear(event->position());
    if (handle == None) {
        event->ignore();
        return;
    }
    sub::app::OrderedMap<QString, double> values;
    for (const QString& suffix : paramsOf(handle)) values.insert(prefix() + suffix, defaultValue(prefix() + suffix));
    setParams(values, QString(), QStringLiteral("Reset Envelope"));
    event->accept();
}

void VitalEnvelopeGraph::hoverMoveEvent(QHoverEvent* event) {
    const Handle handle = handleNear(event->position());
    if (handle != hovered_) {
        hovered_ = handle;
        setCursor(handle == None ? Qt::ArrowCursor
                  : handle == Decay ? Qt::SizeAllCursor
                  : handle >= AttackCurve ? Qt::SizeVerCursor
                                          : Qt::SizeHorCursor);
        update();
    }
}

void VitalEnvelopeGraph::hoverLeaveEvent(QHoverEvent*) {
    hovered_ = None;
    update();
}

}  // namespace sub::ui
