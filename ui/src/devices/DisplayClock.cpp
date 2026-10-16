#include "devices/DisplayClock.h"

#include <QCoreApplication>
#include <QMetaMethod>

#include <algorithm>

namespace sub::ui {

DisplayClock* DisplayClock::instance() {
    static DisplayClock* clock = new DisplayClock();  // (the application's child: it goes with it)
    return clock;
}

DisplayClock::DisplayClock() : QObject(QCoreApplication::instance()) {
    timer_.setInterval(kDisplayRefreshMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &DisplayClock::tick);
}

double DisplayClock::elapsed(QElapsedTimer& since) const {
    const double tick = tickSeconds();
    if (!since.isValid()) {
        since.start();
        return tick;
    }
    const double seconds = double(since.restart()) / 1000.0;
    return seconds > 1.0 ? tick : std::clamp(seconds, tick, std::max(tick, 0.1));
}

void DisplayClock::connectNotify(const QMetaMethod& signal) {
    if (signal == QMetaMethod::fromSignal(&DisplayClock::tick) && !timer_.isActive()) timer_.start();
}

}  // namespace sub::ui
