#pragma once
// The plug-in editors' event loop on Qt's (Linux: see the engine's
// EditorHost.h): file descriptors watched with QSocketNotifier, timers with
// QTimer, on the main thread. The engine bridge installs one while it lives.
// What is unwatched or stopped from inside its own call goes once the call
// returns (deleteLater).

#include <QHash>
#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>

#include "EditorHost.h"

namespace sub::app {

class EditorEventLoop final : public sub::EventLoop {
public:
    EditorEventLoop() = default;
    ~EditorEventLoop() override;
    EditorEventLoop(const EditorEventLoop&) = delete;
    EditorEventLoop& operator=(const EditorEventLoop&) = delete;

    Id watch(int fd, std::function<void()> ready) override;
    void unwatch(Id id) override;
    Id startTimer(int milliseconds, std::function<void()> tick) override;
    void stopTimer(Id id) override;

private:
    Id next_ = 0;
    QHash<Id, QPointer<QSocketNotifier>> notifiers_;
    QHash<Id, QPointer<QTimer>> timers_;
};

}  // namespace sub::app
