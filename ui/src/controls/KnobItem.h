#pragma once

// The rotary knob, drawn on the scene graph; Knob.qml wraps it with its tooltip
// and the small text field typing opens.
//
// Drag vertically (the cursor hides meanwhile): kDragPixels for the whole
// range, kFineDragPixels with Shift; pressing or letting go of Shift mid-drag
// changes the rate from there on. The wheel moves 1/50 of the range a notch
// (unless `wheel` is false: the event goes on to what is under it, a scrolling
// list). Double-click resets to `defaultValue` (if never set: the value the knob
// had when it was made). `logScale` (only when `from` > 0) moves evenly in
// log(value), for frequencies and times; `step` only takes multiples of it
// counted from `from`; `bipolar` draws the arc from the middle. A click gives
// it the focus (as Ableton's knobs: the one clicked is the one keys go to):
// typing a digit then opens a small text field (editRequested), applyTyped()
// parsing and setting what was typed, by the `parser` if it has one, else as the
// number its text shows (typedValue(): "-6" on a knob reading "-3.0 dB" goes to
// where it would read -6, "2k" on one reading in Hz to 2000); and the Up and Down
// arrows move it by `keyStep` (Shift: `largeKeyStep`), each press a gesture of
// its own (held: one).
// A dot in its corner marks it automated ("on", red) or its automation
// overridden ("off", grey). A modulated knob (a modulator moves its parameter)
// shows the range it moves over (`modulationFrom`..`modulationTo`, 0..1 of the
// knob's travel) as an arc of the theme's modulation colour and where it is now
// (`modulationValue`, < 0: not known yet) as a dot riding that arc; its pointer
// stays the knob's own value (where the user set it);
// a ring round it while a modulator's Map waits for it (`mappable`).
//
// A modulated knob also has a small handle at its top left for the modulation's
// amount (`modulationAmount`, -1..1; `modulationBipolar`: either side of the
// knob's value), as Serum's and Ableton's knobs do: dragging it up or down
// changes the amount (modulationAmountMoved, one gesture key a drag; Shift
// fine), Alt+Shift-click switches it between unipolar and bipolar
// (modulationPolarityToggled). The knob's own value is where it starts from.
//
// `moved(value, gestureKey)` is emitted for user changes only, never for
// `value` set from outside: every value of one drag shares a gesture key (a new
// one for each wheel notch, reset or typed value), for the editor's undo merge
// key. `touched()` is emitted on every left press or double-click (pressing a
// control shows its automation). `relative` says whether the last user change
// was a drag or wheel (rather than typed or reset).

#include "controls/DragCursor.h"
#include "input/GestureKey.h"
#include "sg/SgCanvas.h"
#include "theme/Theme.h"

#include <QColor>
#include <QJSValue>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace sub::ui {

class SgPainter;

// The automation dot both knobs and value boxes draw: "on" red, "off" grey, else nothing.
void drawAutomationDot(SgPainter& painter, const QString& state, const QPointF& at);

class KnobItem : public SgCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(qreal from READ from WRITE setFrom NOTIFY rangeChanged)
    Q_PROPERTY(qreal to READ to WRITE setTo NOTIFY rangeChanged)
    Q_PROPERTY(qreal defaultValue READ defaultValue WRITE setDefaultValue RESET resetDefaultValue NOTIFY
                   defaultValueChanged)
    Q_PROPERTY(bool bipolar READ bipolar WRITE setBipolar NOTIFY lookChanged)
    Q_PROPERTY(bool logScale READ logScale WRITE setLogScale NOTIFY rangeChanged)
    Q_PROPERTY(qreal step READ step WRITE setStep NOTIFY rangeChanged)
    // The arrows' steps (0: `step`, else a hundredth of the travel; and ten of those with Shift).
    Q_PROPERTY(qreal keyStep READ keyStep WRITE setKeyStep NOTIFY rangeChanged)
    Q_PROPERTY(qreal largeKeyStep READ largeKeyStep WRITE setLargeKeyStep NOTIFY rangeChanged)
    Q_PROPERTY(bool wheel READ wheel WRITE setWheel NOTIFY wheelChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY lookChanged)
    Q_PROPERTY(QString automation READ automation WRITE setAutomation NOTIFY lookChanged)
    Q_PROPERTY(bool modulated READ modulated WRITE setModulated NOTIFY lookChanged)
    Q_PROPERTY(qreal modulationFrom READ modulationFrom WRITE setModulationFrom NOTIFY lookChanged)
    Q_PROPERTY(qreal modulationTo READ modulationTo WRITE setModulationTo NOTIFY lookChanged)
    Q_PROPERTY(qreal modulationValue READ modulationValue WRITE setModulationValue NOTIFY lookChanged)
    Q_PROPERTY(qreal modulationAmount READ modulationAmount WRITE setModulationAmount NOTIFY lookChanged)
    Q_PROPERTY(bool modulationBipolar READ modulationBipolar WRITE setModulationBipolar NOTIFY lookChanged)
    Q_PROPERTY(bool draggingAmount READ draggingAmount NOTIFY draggingChanged)
    Q_PROPERTY(bool mappable READ mappable WRITE setMappable NOTIFY lookChanged)
    Q_PROPERTY(QJSValue formatter READ formatter WRITE setFormatter NOTIFY textChanged)
    Q_PROPERTY(QJSValue parser READ parser WRITE setParser NOTIFY typeableChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)  // the value as the formatter has it (the tooltip)
    Q_PROPERTY(bool relative READ relative NOTIFY relativeChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(bool typeable READ typeable NOTIFY typeableChanged)  // (always: by its parser, or its text's number)

public:
    static constexpr double kStartAngle = 225.0;  // degrees, Qt's convention (0 = 3 o'clock, counter-clockwise)
    static constexpr double kSpan = 270.0;
    static constexpr double kDragPixels = 600.0;      // dragged this far, a knob turns through its whole range
    static constexpr double kFineDragPixels = 6000.0;  // with Shift
    static constexpr double kWheelNotches = 50.0;     // notches for the whole range
    static constexpr double kAmountDragPixels = 200.0;      // the amount's handle: this far from 0 to 100 %
    static constexpr double kFineAmountDragPixels = 2000.0;  // with Shift
    static constexpr double kHandleRadius = 3.5, kHandleHitRadius = 7.0;

    explicit KnobItem(QQuickItem* parent = nullptr);

    qreal value() const { return value_; }
    void setValue(qreal value);
    qreal from() const { return from_; }
    void setFrom(qreal from);
    qreal to() const { return to_; }
    void setTo(qreal to);
    qreal defaultValue() const { return default_.value_or(value_); }
    void setDefaultValue(qreal value);
    void resetDefaultValue();
    bool bipolar() const { return bipolar_; }
    void setBipolar(bool bipolar);
    bool logScale() const { return logScale_; }
    void setLogScale(bool logScale);
    qreal step() const { return step_; }
    void setStep(qreal step);
    qreal keyStep() const { return keyStep_; }
    void setKeyStep(qreal step);
    qreal largeKeyStep() const { return largeKeyStep_; }
    void setLargeKeyStep(qreal step);
    bool wheel() const { return wheel_; }
    void setWheel(bool wheel);
    // The value arc's colour: the theme's knob() unless set.
    QColor color() const { return color_.isValid() ? color_ : Theme::knob(); }
    void setColor(const QColor& color);
    QString automation() const { return automation_; }
    void setAutomation(const QString& state);  // "", "on" or "off"
    bool modulated() const { return modulated_; }
    void setModulated(bool modulated);
    qreal modulationFrom() const { return modulationFrom_; }
    void setModulationFrom(qreal fraction);
    qreal modulationTo() const { return modulationTo_; }
    void setModulationTo(qreal fraction);
    qreal modulationValue() const { return modulationValue_; }
    void setModulationValue(qreal fraction);
    qreal modulationAmount() const { return modulationAmount_; }
    void setModulationAmount(qreal amount);
    bool modulationBipolar() const { return modulationBipolar_; }
    void setModulationBipolar(bool bipolar);
    bool draggingAmount() const { return amountDrag_.has_value(); }
    // The amount's handle: its centre, and whether `point` is on it (only while modulated).
    QPointF modulationHandleCenter() const;
    Q_INVOKABLE bool isOnModulationHandle(const QPointF& point) const;
    bool contains(const QPointF& point) const override;  // (the handle reaches a little past the edge)
    bool mappable() const { return mappable_; }
    void setMappable(bool mappable);
    QJSValue formatter() const { return formatter_; }
    void setFormatter(const QJSValue& formatter);
    QJSValue parser() const { return parser_; }
    void setParser(const QJSValue& parser);
    QString text() const { return text_; }
    bool relative() const { return relative_; }
    bool dragging() const { return drag_.has_value(); }
    bool typeable() const;

    // C++ alternatives to the JavaScript formatter and parser.
    void setFormatFunction(std::function<QString(double)> format);
    void setParseFunction(std::function<std::optional<double>(const QString&)> parse);

    // Where `value` sits in the range, 0 to 1 (evenly in log(value) with logScale), and back.
    double fraction(double value) const;
    double fromFraction(double fraction) const;

    // The text field's text: parsed, and set as a user change (a gesture of its
    // own, not relative). False if it didn't parse.
    Q_INVOKABLE bool applyTyped(const QString& text);
    // Without a parser: the value whose text shows the number typed (its first; "k" a thousand),
    // found along the travel (held to the range); the number itself while its text shows none.
    std::optional<double> typedValue(const QString& text) const;
    // The first number in `text` ("k" after it: a thousand times it), if any.
    static std::optional<double> numberIn(const QString& text);

Q_SIGNALS:
    void valueChanged();
    void rangeChanged();
    void defaultValueChanged();
    void lookChanged();
    void wheelChanged();
    void textChanged();
    void relativeChanged();
    void draggingChanged();
    void typeableChanged();
    void moved(qreal value, const QString& gestureKey);
    void modulationAmountMoved(qreal amount, const QString& gestureKey);
    void modulationPolarityToggled();
    void touched();
    void editRequested(const QString& initialText);

protected:
    void paint(SgPainter& painter) override;
    void componentComplete() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    // Whether a key typed opens the text field with it (a digit, "-", "+" or
    // "."; it doesn't go to the window's shortcuts then, such as 0 or -).
    bool typesInto(const QKeyEvent* event) const;
    // Up (1) or Down (-1) without Ctrl, Alt or Meta (Shift may be held); else 0.
    static int arrowSign(const QKeyEvent* event);

    struct Drag {
        qreal lastY;
        double fraction;  // where the drag has the knob, unrounded
        QString gesture;
    };

    struct AmountDrag {
        qreal lastY;
        double amount;  // where the drag has it, unrounded
        QString gesture;
    };

    bool effectiveLog() const { return logScale_ && from_ > 0; }
    double clamped(double value) const;
    void setFromUser(double value, const QString& gesture);
    void setRelative(bool relative);
    void updateText();
    QString textFor(double value) const;
    void endDrag();

    double value_ = 0.0;
    double from_ = 0.0;
    double to_ = 1.0;
    std::optional<double> default_;
    bool bipolar_ = false;
    bool logScale_ = false;
    double step_ = 0.0;
    double keyStep_ = 0.0;
    double largeKeyStep_ = 0.0;
    QString keyGesture_;  // the arrows' (held: one)
    bool wheel_ = true;
    QColor color_;
    QString automation_;
    bool modulated_ = false;
    double modulationFrom_ = 0.0, modulationTo_ = 1.0, modulationValue_ = -1.0;
    double modulationAmount_ = 1.0;
    bool modulationBipolar_ = false;
    std::optional<AmountDrag> amountDrag_;
    bool mappable_ = false;
    QJSValue formatter_;
    QJSValue parser_;
    std::function<QString(double)> format_;
    std::function<std::optional<double>(const QString&)> parse_;
    QString text_;
    bool relative_ = true;
    std::optional<Drag> drag_;
    DragCursor cursor_;
};

}  // namespace sub::ui
