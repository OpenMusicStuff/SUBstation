#include "session/DisplayPreferences.h"

#include <QScreen>
#include <QSettings>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "audio/EngineBridge.h"

namespace sub::app {

namespace {

// The nearest of kRates to `hz`.
int nearestRate(int hz) {
    int nearest = DisplayPreferences::kDefaultRate;
    for (const int rate : DisplayPreferences::kRates) {
        if (std::abs(rate - hz) < std::abs(nearest - hz)) nearest = rate;
    }
    return nearest;
}

// "60 Hz", "59.94 Hz".
QString hertz(double hz) {
    const bool whole = std::abs(hz - std::round(hz)) < 0.005;
    return QStringLiteral("%1 Hz").arg(QString::number(hz, 'f', whole ? 0 : 2));
}

}  // namespace

DisplayPreferences::DisplayPreferences(EngineBridge* bridge, QObject* parent) : QObject(parent), bridge_(bridge) {
    bool ok = false;
    const int saved = QSettings().value(kKey).toInt(&ok);
    maxRate_ = ok && saved > 0 ? nearestRate(saved) : kDefaultRate;
    apply();
}

int DisplayPreferences::intervalFor(double hz) {
    // (Leaning short: 60 Hz is 16 ms, as it always was, and 144 Hz 7 ms rather than 6, 167 a second.)
    return hz > 0.0 ? std::max(1, int(std::lround(1000.0 / hz - 0.4))) : intervalFor(kDefaultRate);
}

void DisplayPreferences::setMaxRate(int hz) {
    const int rate = nearestRate(hz);
    QSettings().setValue(kKey, rate);
    if (rate == maxRate_) return;
    maxRate_ = rate;
    apply();
    Q_EMIT changed();
}

QVariantList DisplayPreferences::rateChoices() const {
    QVariantList choices;
    for (const int rate : kRates)
        choices << QVariantMap{{QStringLiteral("label"), hertz(rate)}, {QStringLiteral("value"), rate}};
    return choices;
}

int DisplayPreferences::rateIndex() const {
    const auto found = std::find(kRates.begin(), kRates.end(), maxRate_);
    return found == kRates.end() ? -1 : int(found - kRates.begin());
}

void DisplayPreferences::chooseRate(int index) {
    if (index >= 0 && index < int(kRates.size())) setMaxRate(kRates[std::size_t(index)]);
}

double DisplayPreferences::rate() const {
    return screenRate_ > 0.0 ? std::min(double(maxRate_), screenRate_) : double(maxRate_);
}

void DisplayPreferences::setScreenRate(double hz) {
    hz = std::isfinite(hz) && hz >= 1.0 ? hz : 0.0;
    if (hz == screenRate_) return;
    screenRate_ = hz;
    apply();
    Q_EMIT changed();
}

void DisplayPreferences::followWindow(QWindow* window) {
    disconnect(windowScreenChanged_);
    if (window) windowScreenChanged_ = connect(window, &QWindow::screenChanged, this, &DisplayPreferences::followScreen);
    followScreen(window ? window->screen() : nullptr);
}

void DisplayPreferences::followScreen(QScreen* screen) {
    disconnect(screenRateChanged_);
    screen_ = screen;
    if (screen) screenRateChanged_ = connect(screen, &QScreen::refreshRateChanged, this, &DisplayPreferences::setScreenRate);
    setScreenRate(screen ? screen->refreshRate() : 0.0);
}

QString DisplayPreferences::status() const {
    if (screenRate_ > 0.0 && screenRate_ < maxRate_)
        return QStringLiteral("Displays refresh at %1, the screen's rate.").arg(hertz(rate()));
    if (screenRate_ > 0.0)
        return QStringLiteral("Displays refresh at %1 (the screen: %2).").arg(hertz(rate()), hertz(screenRate_));
    return QStringLiteral("Displays refresh at %1.").arg(hertz(rate()));
}

void DisplayPreferences::apply() {
    if (bridge_) bridge_->setDisplayInterval(intervalMs());
}

}  // namespace sub::app
