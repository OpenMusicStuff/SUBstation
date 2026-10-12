#include "plugins/Vst3RunLoop.h"

namespace sub::vst3 {

using Steinberg::kInvalidArgument;
using Steinberg::kResultFalse;
using Steinberg::kResultTrue;
namespace Linux = Steinberg::Linux;

RunLoop::~RunLoop() { clear(); }

void RunLoop::clear() {
    for (const auto& [fd, watch] : watches_) editorEventLoop().unwatch(watch.id);
    for (const auto& [handler, timer] : timers_) editorEventLoop().stopTimer(timer.id);
    watches_.clear();
    timers_.clear();
}

tresult PLUGIN_API RunLoop::registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) {
    if (!handler || fd < 0) return kInvalidArgument;
    // (The handler is held by the loop's call: unregistering itself from onFDIsSet() can't free it under the call.)
    Steinberg::IPtr<Linux::IEventHandler> held(handler);
    const EventLoop::Id id = editorEventLoop().watch(fd, [held, fd] {
        Steinberg::IPtr<Linux::IEventHandler> calling = held;
        calling->onFDIsSet(fd);
    });
    watches_.emplace(fd, Watch{held, id});
    return kResultTrue;
}

tresult PLUGIN_API RunLoop::unregisterEventHandler(Linux::IEventHandler* handler) {
    bool found = false;
    for (auto it = watches_.begin(); it != watches_.end();) {
        if (it->second.handler.get() != handler) {
            ++it;
            continue;
        }
        editorEventLoop().unwatch(it->second.id);
        it = watches_.erase(it);
        found = true;
    }
    return found ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API RunLoop::registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval milliseconds) {
    if (!handler || milliseconds == 0) return kInvalidArgument;
    Steinberg::IPtr<Linux::ITimerHandler> held(handler);
    const EventLoop::Id id = editorEventLoop().startTimer(static_cast<int>(milliseconds), [held] {
        Steinberg::IPtr<Linux::ITimerHandler> calling = held;
        calling->onTimer();
    });
    timers_.emplace(handler, Timer{held, id});
    return kResultTrue;
}

tresult PLUGIN_API RunLoop::unregisterTimer(Linux::ITimerHandler* handler) {
    const auto [first, last] = timers_.equal_range(handler);
    if (first == last) return kResultFalse;
    for (auto it = first; it != last; ++it) editorEventLoop().stopTimer(it->second.id);
    timers_.erase(first, last);
    return kResultTrue;
}

}  // namespace sub::vst3
