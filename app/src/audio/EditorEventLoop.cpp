#include "audio/EditorEventLoop.h"

#include <algorithm>
#include <utility>

namespace sub::app {

EditorEventLoop::~EditorEventLoop() {
    for (const auto& notifier : std::as_const(notifiers_)) delete notifier.data();
    for (const auto& timer : std::as_const(timers_)) delete timer.data();
}

EditorEventLoop::Id EditorEventLoop::watch(int fd, std::function<void()> ready) {
    auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read);
    QObject::connect(notifier, &QSocketNotifier::activated, notifier, [ready = std::move(ready)] {
        const auto call = ready;  // (a copy: it may unwatch itself, and its notifier goes later)
        call();
    });
    const Id id = ++next_;
    notifiers_.insert(id, notifier);
    return id;
}

void EditorEventLoop::unwatch(Id id) {
    const QPointer<QSocketNotifier> notifier = notifiers_.take(id);
    if (!notifier) return;
    notifier->setEnabled(false);
    notifier->deleteLater();
}

EditorEventLoop::Id EditorEventLoop::startTimer(int milliseconds, std::function<void()> tick) {
    auto* timer = new QTimer;
    timer->setInterval(std::max(1, milliseconds));
    QObject::connect(timer, &QTimer::timeout, timer, [tick = std::move(tick)] {
        const auto call = tick;
        call();
    });
    timer->start();
    const Id id = ++next_;
    timers_.insert(id, timer);
    return id;
}

void EditorEventLoop::stopTimer(Id id) {
    const QPointer<QTimer> timer = timers_.take(id);
    if (!timer) return;
    timer->stop();
    timer->deleteLater();
}

}  // namespace sub::app
