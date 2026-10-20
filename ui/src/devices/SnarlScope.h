#pragma once

// Snarl's mouth: the sound it makes, as an oscilloscope that stands still on any
// note. The device publishes its latest cycle at the note's pitch (the display
// `scope`, 64 values a cycle, every 512 samples); this draws two of them across,
// eased from one to the next so the trace breathes as the growl and the wobble
// move it, filled faintly down to its middle. Its colour runs with the growl as
// it plays (the display `growl`): the theme's accent when clean, acid green,
// then hot magenta at full growl. It scales to fill its height (an eased peak),
// and once nothing comes it fades and flattens, then goes still (nothing
// repaints while silent). punch() swells it for a moment: the editor's styles and
// its dice call it.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QColor>
#include <QtQml/qqmlregistration.h>

#include <array>

namespace sub::ui {

class SnarlScope : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double activity READ activity NOTIFY animated)  // 0..1, eased: how lit the trace is
    Q_PROPERTY(double heat READ heat NOTIFY animated)          // 0..1, eased: the growl as it plays
    Q_PROPERTY(QColor traceColor READ traceColor NOTIFY animated)
    Q_PROPERTY(double punchLeft READ punchLeft NOTIFY animated)  // 1 → 0 after punch()

public:
    static constexpr int kPoints = 64;            // a cycle's values (the device's)
    static constexpr int kCycles = 2;             // cycles drawn across
    static constexpr double kEaseSeconds = 0.05;  // a new cycle eased in over this
    static constexpr double kQuietSeconds = 0.15; // no cycle for this long: it fades
    static constexpr double kFadeSeconds = 0.2;
    static constexpr double kPunchSeconds = 0.4;

    explicit SnarlScope(QQuickItem* parent = nullptr);

    double activity() const { return activity_.value; }
    double heat() const { return heat_.value; }
    QColor traceColor() const { return colorFor(heat_.value); }
    double punchLeft() const { return punch_; }
    // The cycle drawn (eased), kPoints values.
    std::array<double, kPoints> shown() const;

    // The trace's colour for a growl of `heat` (0..1).
    static QColor colorFor(double heat);

    // A swell of the trace that settles over kPunchSeconds.
    Q_INVOKABLE void punch();

Q_SIGNALS:
    void animated();

protected:
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;

private:
    std::array<Eased, kPoints> points_;
    Eased activity_, heat_, scale_;
    double quiet_ = 1.0;  // seconds since a cycle came
    double punch_ = 0.0;
};

}  // namespace sub::ui
