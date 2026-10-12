#include "EditorHost.h"

#ifndef _WIN32
#include <poll.h>
#endif

#include <algorithm>
#include <chrono>
#include <map>
#include <vector>

namespace sub {

namespace {

// The engine's own loop: what is ready or due is run when runEditorEvents() is called.
class PolledEventLoop final : public EventLoop {
public:
    Id watch(int fd, std::function<void()> ready) override {
        const Id id = ++next_;
        watches_[id] = {fd, std::move(ready)};
        return id;
    }
    void unwatch(Id id) override { watches_.erase(id); }

    Id startTimer(int milliseconds, std::function<void()> tick) override {
        const Id id = ++next_;
        const auto interval = std::chrono::milliseconds(std::max(1, milliseconds));
        timers_[id] = {interval, Clock::now() + interval, std::move(tick)};
        return id;
    }
    void stopTimer(Id id) override { timers_.erase(id); }

    void run() {
        // (Each looked up again before it is called: one may remove others.)
        std::vector<std::pair<Id, int>> fds;
        for (const auto& [id, watch] : watches_) fds.emplace_back(id, watch.fd);
#ifndef _WIN32  // (nothing is watched on Windows)
        std::vector<pollfd> polled;
        for (const auto& [id, fd] : fds) polled.push_back({fd, POLLIN, 0});
        if (!polled.empty() && poll(polled.data(), polled.size(), 0) > 0) {
            for (size_t i = 0; i < polled.size(); ++i) {
                if (!(polled[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                const auto it = watches_.find(fds[i].first);
                if (it == watches_.end()) continue;
                auto ready = it->second.ready;  // (a copy: it may unwatch itself)
                ready();
            }
        }
#endif
        const auto now = Clock::now();
        std::vector<Id> due;
        for (const auto& [id, timer] : timers_)
            if (timer.next <= now) due.push_back(id);
        for (const Id id : due) {
            const auto it = timers_.find(id);
            if (it == timers_.end()) continue;
            it->second.next = now + it->second.interval;
            auto tick = it->second.tick;
            tick();
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Watch {
        int fd;
        std::function<void()> ready;
    };
    struct Timer {
        Clock::duration interval;
        Clock::time_point next;
        std::function<void()> tick;
    };
    Id next_ = 0;
    std::map<Id, Watch> watches_;
    std::map<Id, Timer> timers_;
};

PolledEventLoop& ownLoop() {
    static PolledEventLoop loop;
    return loop;
}

std::shared_ptr<EventLoop>& applicationLoop() {
    static std::shared_ptr<EventLoop> loop;
    return loop;
}

std::function<bool(const EditorKey&)>& keyFilter() {
    static std::function<bool(const EditorKey&)> filter;
    return filter;
}

}  // namespace

void setEditorEventLoop(std::shared_ptr<EventLoop> loop) { applicationLoop() = std::move(loop); }

std::shared_ptr<EventLoop> editorEventLoop() {
    if (const auto& loop = applicationLoop()) return loop;
    return std::shared_ptr<EventLoop>(std::shared_ptr<void>(), &ownLoop());  // (static: owned by no one)
}

void runEditorEvents() { ownLoop().run(); }

void setEditorKeyFilter(std::function<bool(const EditorKey&)> filter) { keyFilter() = std::move(filter); }

const std::function<bool(const EditorKey&)>& editorKeyFilter() { return keyFilter(); }

}  // namespace sub
