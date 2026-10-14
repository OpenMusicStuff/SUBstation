// HostLoop.h elsewhere than Windows: poll() on a wake-up pipe and on the file
// descriptors the editors' X11 connections and the plug-ins watch, with their
// timers. It is the engine's editor event loop in this process (EditorHost.h),
// so the editor windows and the plug-ins' run loop (Vst3RunLoop) register here.

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <vector>

#include "EditorHost.h"
#include "HostLoop.h"

namespace sub::pluginhost {

namespace {

using Clock = std::chrono::steady_clock;

class PollLoop final : public EventLoop {
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

    // Waits at most `milliseconds` for `wakeFd` or a watched descriptor, or the next timer; runs what came.
    void run(int wakeFd, int milliseconds) {
        const auto now = Clock::now();
        auto timeout = std::chrono::milliseconds(std::max(0, milliseconds));
        for (const auto& [id, timer] : timers_) {
            const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(timer.next - now);
            timeout = std::min(timeout, std::max(std::chrono::milliseconds(0), until));
        }
        std::vector<pollfd> fds{{wakeFd, POLLIN, 0}};
        std::vector<Id> ids{0};
        for (const auto& [id, watch] : watches_) {
            fds.push_back({watch.fd, POLLIN, 0});
            ids.push_back(id);
        }
        if (::poll(fds.data(), fds.size(), static_cast<int>(timeout.count())) > 0) {
            if (fds[0].revents & POLLIN) {
                char drain[64];
                while (::read(wakeFd, drain, sizeof drain) > 0) {
                }
            }
            // (Each looked up again before it is called: one may unwatch others.)
            for (size_t i = 1; i < fds.size(); ++i) {
                if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                const auto it = watches_.find(ids[i]);
                if (it == watches_.end()) continue;
                auto ready = it->second.ready;
                ready();
            }
        }
        const auto later = Clock::now();
        std::vector<Id> due;
        for (const auto& [id, timer] : timers_)
            if (timer.next <= later) due.push_back(id);
        for (const Id id : due) {
            const auto it = timers_.find(id);
            if (it == timers_.end()) continue;
            it->second.next = later + it->second.interval;
            auto tick = it->second.tick;
            tick();
        }
    }

private:
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

class PosixHostLoop final : public HostLoop {
public:
    PosixHostLoop() : loop_(std::make_shared<PollLoop>()) {
        if (::pipe(wake_) == 0) {
            for (const int fd : wake_) {
                ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
                ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
            }
        }
        setEditorEventLoop(loop_);
    }
    ~PosixHostLoop() override {
        setEditorKeyFilter(nullptr);
        setEditorEventLoop(nullptr);
        ::close(wake_[0]);
        ::close(wake_[1]);
    }

    void wake() noexcept override {
        const char c = 1;
        [[maybe_unused]] const ssize_t n = ::write(wake_[1], &c, 1);  // (a full pipe wakes it already)
    }

    void wait(int milliseconds) override { loop_->run(wake_[0], milliseconds); }

    void setKeyFilter(KeyFilter filter) override {
        if (!filter) {
            setEditorKeyFilter(nullptr);
            return;
        }
        setEditorKeyFilter([filter = std::move(filter)](const EditorKey& key) {
            return filter({key.keysym, key.modifiers, key.repeat, false});
        });
    }

    bool closeForemostEditor() override { return sub::closeForemostEditor(); }

private:
    std::shared_ptr<PollLoop> loop_;
    int wake_[2] = {-1, -1};
};

}  // namespace

std::unique_ptr<HostLoop> HostLoop::create() { return std::make_unique<PosixHostLoop>(); }

}  // namespace sub::pluginhost
