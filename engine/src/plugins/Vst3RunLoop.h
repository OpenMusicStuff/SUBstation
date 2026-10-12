#pragma once
// VST 3's run loop for plug-ins on Linux (Steinberg::Linux::IRunLoop): their
// file descriptors and timers, run on the main thread by the editor event
// loop (EditorHost.h). One per editor window, handed to the view through its
// IPlugFrame; what a plug-in leaves registered goes with the window.

#include <map>
#include <memory>

#include "EditorHost.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "plugins/Vst3Support.h"

namespace sub::vst3 {

class RunLoop final : public Steinberg::Linux::IRunLoop {
public:
    RunLoop() = default;
    ~RunLoop();
    RunLoop(const RunLoop&) = delete;
    RunLoop& operator=(const RunLoop&) = delete;

    // Everything a plug-in left registered, unregistered.
    void clear();

    tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler* handler,
                                            Steinberg::Linux::FileDescriptor fd) override;
    tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler* handler) override;
    tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler* handler,
                                     Steinberg::Linux::TimerInterval milliseconds) override;
    tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler* handler) override;
    SUB_HOST_OWNED_FUNKNOWN(Steinberg::Linux::IRunLoop)

private:
    // Each with the loop it is registered on, which unregisters it.
    struct Watch {
        Steinberg::IPtr<Steinberg::Linux::IEventHandler> handler;
        std::shared_ptr<EventLoop> loop;
        EventLoop::Id id;
    };
    struct Timer {
        Steinberg::IPtr<Steinberg::Linux::ITimerHandler> handler;
        std::shared_ptr<EventLoop> loop;
        EventLoop::Id id;
    };
    std::multimap<int, Watch> watches_;  // by file descriptor
    std::multimap<Steinberg::Linux::ITimerHandler*, Timer> timers_;
};

}  // namespace sub::vst3
