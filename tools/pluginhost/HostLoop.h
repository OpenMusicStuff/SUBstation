#pragma once
// The plug-in host's main thread, the one part of the host that is the
// system's: it waits for work (a message the reader thread queued, the
// system's events for the plug-ins and their editors) and runs the system's
// events, and it holds the keys pressed in editors that the application takes.
// A file per system: HostLoopPosix.cpp (poll() on a wake-up pipe and on what
// the editors and plug-ins watch: it is the engine's editor event loop,
// EditorHost.h), HostLoopWin32.cpp (window messages and an event). A system
// whose plug-ins need its own run loop (macOS's) has a file of its own here,
// and nothing else of the host changes.

#include <cstdint>
#include <functional>
#include <memory>

namespace sub::pluginhost {

// A key pressed in a plug-in's editor: the system's key code (an X11 keysym,
// unshifted; a Windows virtual-key code), EditorKey's modifiers (EditorHost.h),
// held down, and whether the focus is in a text field the system knows (Windows).
struct KeyPress {
    uint32_t key = 0;
    int modifiers = 0;
    bool repeat = false;
    bool textField = false;
};

// Whether the application takes a key (then the plug-in doesn't get it).
using KeyFilter = std::function<bool(const KeyPress&)>;

class HostLoop {
public:
    static std::unique_ptr<HostLoop> create();
    virtual ~HostLoop() = default;

    // Any thread: ends the main thread's wait().
    virtual void wake() noexcept = 0;
    // Main thread: waits at most `milliseconds` (0: not at all) for a wake() or
    // the system's events, and runs those that came.
    virtual void wait(int milliseconds) = 0;
    // Main thread: keys pressed in editors go through `filter` (none: all the plug-ins').
    virtual void setKeyFilter(KeyFilter filter) = 0;
    // Main thread: closes the foremost of this process's editors, as its close
    // button would; false if none is shown.
    virtual bool closeForemostEditor() = 0;
};

}  // namespace sub::pluginhost
