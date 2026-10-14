#include "controls/KnobItem.h"

#include "input/Modifiers.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
const QString kTypingKeys = QStringLiteral("0123456789-+.");

// A JavaScript parser's answer: a finite number, or nothing.
std::optional<double> parsedNumber(const QJSValue& result) {
    if (!result.isNumber())
        return std::nullopt;
    const double value = result.toNumber();
    if (!std::isfinite(value))
        return std::nullopt;
    return value;
}

}  // namespace

void drawAutomationDot(SgPainter& p, const QString& state, const QPointF& at) {
    const QColor color = Theme::automationDotColor(state);
    if (!color.isValid())
        return;
    p.save();
    p.setAntialiasing(true);
    p.fillEllipse(at, 2.5, 2.5, color);
    p.restore();
}

KnobItem::KnobItem(QQuickItem* parent) : SgCanvas(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setImplicitSize(28, 28);  // at least 22 x 22, as the Python UI's knob
    updateText();
}

void KnobItem::componentComplete() {
    SgCanvas::componentComplete();
    // Made from QML: the range is known only now.
    value_ = clamped(value_);
    if (!default_)
        default_ = value_;  // without a default, double-click goes back to where it started
    updateText();
    update();
}

double KnobItem::clamped(double value) const { return std::clamp(value, std::min(from_, to_), std::max(from_, to_)); }

void KnobItem::setValue(qreal value) {
    if (isComponentComplete())
        value = clamped(value);
    if (value == value_)
        return;
    value_ = value;
    updateText();
    update();
    Q_EMIT valueChanged();
}

void KnobItem::setFrom(qreal from) {
    if (from == from_)
        return;
    from_ = from;
    Q_EMIT rangeChanged();
    if (isComponentComplete())
        setValue(value_);
    update();
}

void KnobItem::setTo(qreal to) {
    if (to == to_)
        return;
    to_ = to;
    Q_EMIT rangeChanged();
    if (isComponentComplete())
        setValue(value_);
    update();
}

void KnobItem::setDefaultValue(qreal value) {
    if (default_ && *default_ == value)
        return;
    default_ = value;
    Q_EMIT defaultValueChanged();
}

void KnobItem::resetDefaultValue() {
    default_.reset();
    Q_EMIT defaultValueChanged();
}

void KnobItem::setBipolar(bool bipolar) {
    if (bipolar == bipolar_)
        return;
    bipolar_ = bipolar;
    update();
    Q_EMIT lookChanged();
}

void KnobItem::setLogScale(bool logScale) {
    if (logScale == logScale_)
        return;
    logScale_ = logScale;
    update();
    Q_EMIT rangeChanged();
}

void KnobItem::setStep(qreal step) {
    if (step == step_)
        return;
    step_ = step;
    Q_EMIT rangeChanged();
}

void KnobItem::setWheel(bool wheel) {
    if (wheel == wheel_)
        return;
    wheel_ = wheel;
    Q_EMIT wheelChanged();
}

void KnobItem::setColor(const QColor& color) {
    if (color == color_)
        return;
    color_ = color;
    update();
    Q_EMIT lookChanged();
}

void KnobItem::setAutomation(const QString& state) {
    if (state == automation_)
        return;
    automation_ = state;
    update();
    Q_EMIT lookChanged();
}

namespace {

// Sets a look property, repainting if it changed.
template <typename T>
bool changeLook(T& member, const T& value) {
    if (member == value)
        return false;
    member = value;
    return true;
}

}  // namespace

void KnobItem::setModulated(bool modulated) {
    if (changeLook(modulated_, modulated)) {
        update();
        Q_EMIT lookChanged();
    }
}

void KnobItem::setModulationFrom(qreal fraction) {
    if (changeLook(modulationFrom_, std::clamp<double>(fraction, 0.0, 1.0))) {
        update();
        Q_EMIT lookChanged();
    }
}

void KnobItem::setModulationTo(qreal fraction) {
    if (changeLook(modulationTo_, std::clamp<double>(fraction, 0.0, 1.0))) {
        update();
        Q_EMIT lookChanged();
    }
}

void KnobItem::setModulationValue(qreal fraction) {
    if (changeLook(modulationValue_, fraction < 0 ? -1.0 : std::min<double>(fraction, 1.0))) {
        update();
        Q_EMIT lookChanged();
    }
}

void KnobItem::setModulationAmount(qreal amount) {
    if (changeLook(modulationAmount_, std::clamp<double>(amount, -1.0, 1.0))) {
        update();
        Q_EMIT lookChanged();
    }
}

void KnobItem::setModulationBipolar(bool bipolar) {
    if (changeLook(modulationBipolar_, bipolar)) {
        update();
        Q_EMIT lookChanged();
    }
}

// (Out at the corner, clear of the arc: a little past the item's edge, which still takes its clicks: contains().)
QPointF KnobItem::modulationHandleCenter() const { return QPointF(2.0, 2.0); }

bool KnobItem::contains(const QPointF& point) const {
    return SgCanvas::contains(point) || isOnModulationHandle(point);
}

bool KnobItem::isOnModulationHandle(const QPointF& point) const {
    const QPointF d = point - modulationHandleCenter();
    return modulated_ && d.x() * d.x() + d.y() * d.y() <= kHandleHitRadius * kHandleHitRadius;
}

void KnobItem::setMappable(bool mappable) {
    if (changeLook(mappable_, mappable)) {
        update();
        Q_EMIT lookChanged();
    }
}

void KnobItem::setFormatter(const QJSValue& formatter) {
    formatter_ = formatter;
    updateText();
}

void KnobItem::setParser(const QJSValue& parser) {
    parser_ = parser;
    Q_EMIT typeableChanged();
}

void KnobItem::setFormatFunction(std::function<QString(double)> format) {
    format_ = std::move(format);
    updateText();
}

void KnobItem::setParseFunction(std::function<std::optional<double>(const QString&)> parse) {
    parse_ = std::move(parse);
    Q_EMIT typeableChanged();
}

bool KnobItem::typeable() const { return bool(parse_) || parser_.isCallable(); }

void KnobItem::updateText() {
    QString text;
    if (format_)
        text = format_(value_);
    else if (formatter_.isCallable())
        text = formatter_.call({QJSValue(value_)}).toString();
    else
        text = QString::number(value_, 'f', 2);
    if (text != text_) {
        text_ = text;
        Q_EMIT textChanged();
    }
}

double KnobItem::fraction(double value) const {
    if (effectiveLog())
        return std::log(value / from_) / std::log(to_ / from_);
    if (to_ == from_)
        return 0.0;
    return (value - from_) / (to_ - from_);
}

double KnobItem::fromFraction(double fraction) const {
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (effectiveLog())
        return from_ * std::pow(to_ / from_, fraction);
    return from_ + fraction * (to_ - from_);
}

void KnobItem::setRelative(bool relative) {
    if (relative == relative_)
        return;
    relative_ = relative;
    Q_EMIT relativeChanged();
}

void KnobItem::setFromUser(double value, const QString& gesture) {
    if (step_ > 0)
        value = from_ + std::nearbyint((value - from_) / step_) * step_;
    value = clamped(value);
    if (value == value_)
        return;
    value_ = value;
    updateText();
    update();
    Q_EMIT valueChanged();
    Q_EMIT moved(value, gesture);
}

bool KnobItem::applyTyped(const QString& text) {
    std::optional<double> parsed;
    if (parse_)
        parsed = parse_(text);
    else if (parser_.isCallable())
        parsed = parsedNumber(parser_.call({QJSValue(text)}));
    if (!parsed)
        return false;
    setRelative(false);
    setFromUser(*parsed, newGestureKey());
    return true;
}

void KnobItem::paint(SgPainter& p) {
    p.setAntialiasing(true);
    const double side = std::min(width(), height()) - 4;
    if (side <= 0)
        return;
    const QRectF rect((width() - side) / 2, (height() - side) / 2, side, side);
    if (mappable_) {  // a modulator's Map waits for it
        const QColor ring = Theme::modulation();
        p.drawEllipse(rect.adjusted(-1.5, -1.5, 1.5, 1.5), QColor(ring.red(), ring.green(), ring.blue(), 190), 1.5);
    }
    p.drawArc(rect, kStartAngle - kSpan, kSpan, Theme::knobTrack(), 3, Qt::FlatCap);

    const QPointF center = rect.center();
    const double radius = side / 2 - 2;
    const auto pointer = [&](double frac, const QColor& colour, double width) {
        const double angle = (kStartAngle - frac * kSpan) * kPi / 180.0;
        const QPointF tip(center.x() + std::cos(angle) * radius, center.y() - std::sin(angle) * radius);
        const QPointF inner(center.x() + std::cos(angle) * radius * 0.3, center.y() - std::sin(angle) * radius * 0.3);
        p.drawLine(inner, tip, colour, width, Qt::RoundCap);
        return tip;
    };
    const double frac = fraction(value_);
    if (modulated_) {
        // The range it moves over, a dot riding it where the modulator has it now; the pointer is the knob's own
        // value (where the user set it).
        const QColor mod = Theme::modulation();
        const double sweep = -(modulationTo_ - modulationFrom_) * kSpan;
        if (std::abs(sweep) > 0.5) {
            p.drawArc(rect, kStartAngle - modulationFrom_ * kSpan, sweep,
                      QColor(mod.red(), mod.green(), mod.blue(), 150), 3, Qt::FlatCap);
        }
        pointer(frac, Theme::text(), 2);
        if (modulationValue_ >= 0.0) {
            const double angle = (kStartAngle - modulationValue_ * kSpan) * kPi / 180.0;
            const double onArc = side / 2;  // (the arc's line: the rect's edge)
            const QPointF dot(center.x() + std::cos(angle) * onArc, center.y() - std::sin(angle) * onArc);
            p.fillRadialGlow(dot, 4.5, QColor(mod.red(), mod.green(), mod.blue(), 120), 2);
            p.fillEllipse(dot, 2.0, 2.0, QColor(255, 255, 255, 235));
        }
        // The amount's handle: a dot with the amount round it, from the top (clockwise: up; bipolar both ways).
        const QPointF at = modulationHandleCenter();
        const QRectF ring(at.x() - kHandleRadius, at.y() - kHandleRadius, 2 * kHandleRadius, 2 * kHandleRadius);
        p.fillEllipse(at, kHandleRadius, kHandleRadius, Theme::knobTrack());
        const double degrees = modulationAmount_ * (modulationBipolar_ ? 180.0 : 360.0);
        if (std::abs(degrees) > 0.5) {
            if (modulationBipolar_)
                p.drawArc(ring, 90.0 + std::abs(degrees), -2 * std::abs(degrees), mod, 1.5, Qt::FlatCap);
            else
                p.drawArc(ring, 90.0, -degrees, mod, 1.5, Qt::FlatCap);
        }
        p.fillEllipse(at, modulationBipolar_ ? 0.9 : 1.3, modulationBipolar_ ? 0.9 : 1.3,
                      amountDrag_ ? Theme::text() : mod);
    } else {
        const double origin = bipolar_ ? 0.5 : 0.0;
        const double start = kStartAngle - origin * kSpan;
        const double sweep = -(frac - origin) * kSpan;
        if (std::abs(sweep) > 0.5)
            p.drawArc(rect, start, sweep, color(), 3, Qt::FlatCap);
        pointer(frac, Theme::text(), 2);
    }
    drawAutomationDot(p, automation_, QPointF(width() - 3.5, 3.5));
}

void KnobItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (isOnModulationHandle(event->position())) {
        const Qt::KeyboardModifiers both = Qt::AltModifier | Qt::ShiftModifier;
        if ((event->modifiers() & both) == both) {
            Q_EMIT modulationPolarityToggled();
        } else {
            amountDrag_ = AmountDrag{event->position().y(), modulationAmount_, newGestureKey()};
            cursor_.press(event->globalPosition());
            Q_EMIT draggingChanged();
            update();
        }
        event->accept();
        return;
    }
    Q_EMIT touched();
    if (typeable())
        forceActiveFocus(Qt::MouseFocusReason);  // so typing a digit edits
    const bool wasDragging = dragging();
    drag_ = Drag{event->position().y(), fraction(value_), newGestureKey()};
    cursor_.press(event->globalPosition());
    if (!wasDragging)
        Q_EMIT draggingChanged();
    event->accept();
}

void KnobItem::mouseMoveEvent(QMouseEvent* event) {
    if (amountDrag_ && cursor_.dragging()) {
        const qreal y = event->position().y();
        const double pixels = (event->modifiers() & Qt::ShiftModifier) ? kFineAmountDragPixels : kAmountDragPixels;
        amountDrag_->amount = std::clamp(amountDrag_->amount + (amountDrag_->lastY - y) / pixels, -1.0, 1.0);
        amountDrag_->lastY = cursor_.moved(this, event->position(), event->globalPosition());
        // (The amount shown comes back from what it sets: held where the range ends.)
        Q_EMIT modulationAmountMoved(amountDrag_->amount, amountDrag_->gesture);
        return;
    }
    if (!drag_ || !cursor_.dragging())
        return;
    // Moved on from where the mouse was last, so pressing or letting go of
    // Shift mid-drag changes the rate from here on (not the whole drag).
    const qreal y = event->position().y();
    const double pixels = (event->modifiers() & Qt::ShiftModifier) ? kFineDragPixels : kDragPixels;
    drag_->fraction = std::clamp(drag_->fraction + (drag_->lastY - y) / pixels, 0.0, 1.0);
    drag_->lastY = cursor_.moved(this, event->position(), event->globalPosition());
    setRelative(true);
    setFromUser(fromFraction(drag_->fraction), drag_->gesture);
}

void KnobItem::endDrag() {
    cursor_.release();
    if (amountDrag_) {
        amountDrag_.reset();
        update();
        Q_EMIT draggingChanged();
    }
    if (drag_) {
        drag_.reset();
        Q_EMIT draggingChanged();
    }
}

void KnobItem::mouseReleaseEvent(QMouseEvent*) { endDrag(); }

void KnobItem::mouseUngrabEvent() { endDrag(); }

void KnobItem::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (isOnModulationHandle(event->position())) {  // (a second click on the handle: as the first)
        mousePressEvent(event);
        return;
    }
    Q_EMIT touched();
    setRelative(false);
    setFromUser(defaultValue(), newGestureKey());
}

void KnobItem::wheelEvent(QWheelEvent* event) {
    if (!wheel_) {
        event->ignore();
        return;
    }
    setRelative(true);
    const double notches = event->angleDelta().y() / 120.0;
    setFromUser(fromFraction(fraction(value_) + notches / kWheelNotches), newGestureKey());
    event->accept();
}

bool KnobItem::typesInto(const QKeyEvent* event) const {
    return typeable() && !event->text().isEmpty() && kTypingKeys.contains(event->text()) &&
           !hasShortcutModifier(event->modifiers());
}

void KnobItem::keyPressEvent(QKeyEvent* event) {
    if (typesInto(event)) {
        Q_EMIT editRequested(event->text());
        event->accept();
        return;
    }
    event->ignore();
}

bool KnobItem::event(QEvent* event) {
    // What it types into its text field comes to it as a key press, not to the window's shortcuts.
    if (event->type() == QEvent::ShortcutOverride && typesInto(static_cast<QKeyEvent*>(event))) {
        event->accept();
        return true;
    }
    return SgCanvas::event(event);
}

}  // namespace sub::ui
