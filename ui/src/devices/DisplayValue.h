#pragma once

// One display of a built-in device, as a number for QML: the newest value the
// device published (`value`), eased toward it (`smoothed`, by `smoothing`
// seconds: rising at once, falling over that time), and whether any came in the
// last kSilentSeconds (`active`). It draws nothing: an editor binds its own
// items' animations to it (the Arpeggiator's step lighting up, the Bitcrusher's
// wave moving with what comes in). Read on each DisplayClock tick, while visible.
//
//   DisplayValue { id: level; session: Session; trackId: t; deviceId: d; display: "level"; smoothing: 0.3 }

#include "devices/DeviceCanvas.h"

#include <QtQml/qqmlregistration.h>

namespace sub::ui {

class DisplayValue : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString display READ display WRITE setDisplay NOTIFY displayChanged)
    Q_PROPERTY(double smoothing READ smoothing WRITE setSmoothing NOTIFY displayChanged)
    Q_PROPERTY(double value READ value NOTIFY valueChanged)
    Q_PROPERTY(double smoothed READ smoothed NOTIFY valueChanged)
    Q_PROPERTY(bool active READ active NOTIFY valueChanged)

public:
    static constexpr double kSilentSeconds = 0.25;  // nothing came for this long: not active

    explicit DisplayValue(QQuickItem* parent = nullptr);

    QString display() const { return display_; }
    void setDisplay(const QString& display);
    double smoothing() const { return smoothing_; }
    void setSmoothing(double seconds);
    double value() const { return value_; }
    double smoothed() const { return smoothed_; }
    bool active() const { return active_; }

    // The newest value (if one came) and `dt` seconds on; what refreshDisplays() does with what it read.
    void advance(bool came, double newest, double dt);

Q_SIGNALS:
    void displayChanged();
    void valueChanged();

protected:
    void refreshDisplays() override;
    void paint(SgPainter&) override {}

private:
    QString display_;
    double smoothing_ = 0.0;
    double value_ = 0.0;
    double smoothed_ = 0.0;
    double silent_ = kSilentSeconds;  // seconds since a value last came
    bool active_ = false;
};

}  // namespace sub::ui
