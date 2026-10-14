#include "devices/DisplayValue.h"

#include <cmath>

namespace sub::ui {

DisplayValue::DisplayValue(QQuickItem* parent) : DeviceCanvas(parent) {}

void DisplayValue::setDisplay(const QString& display) {
    if (display == display_) return;
    display_ = display;
    Q_EMIT displayChanged();
}

void DisplayValue::setSmoothing(double seconds) {
    if (seconds == smoothing_) return;
    smoothing_ = seconds;
    Q_EMIT displayChanged();
}

void DisplayValue::refreshDisplays() {
    if (display_.isEmpty()) return;
    const double dt = tickSeconds();
    const std::vector<float> values = readRecent(display_, 0.02);
    advance(!values.empty(), values.empty() ? 0.0 : double(values.back()), dt);
}

void DisplayValue::advance(bool came, double newest, double dt) {
    silent_ = came ? 0.0 : silent_ + dt;
    const double value = came ? newest : value_;
    const bool active = silent_ < kSilentSeconds;
    // Rising at once, falling over `smoothing` seconds (toward 0 once nothing comes).
    const double target = active ? value : 0.0;
    double smoothed = target;
    if (target < smoothed_ && smoothing_ > 0.0) smoothed = target + (smoothed_ - target) * std::exp(-dt / smoothing_);
    if (std::abs(smoothed - target) < 1e-4) smoothed = target;
    if (value == value_ && smoothed == smoothed_ && active == active_) return;
    value_ = value;
    smoothed_ = smoothed;
    active_ = active;
    Q_EMIT valueChanged();
}

}  // namespace sub::ui
