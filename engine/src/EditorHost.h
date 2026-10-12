#pragma once
// What plug-in editors need from the application where the system doesn't give
// it (Linux): the main thread's event loop, and a say in the keys pressed in
// them. On Windows editors are Win32 windows: their messages come through the
// application's event loop, and its native event filter sees their keys, so
// none of this is used there.
//
// - The event loop: an X11 editor window's connection, and the plug-ins' own
//   file descriptors and timers (VST 3's Steinberg::Linux::IRunLoop, which
//   the host hands to the view), are run on the main thread by an EventLoop.
//   The application gives its own (Qt's: QSocketNotifier, QTimer); until it
//   does, the engine's runs them, without waiting, in Engine::idle().
// - The keys: the main window's shortcuts while an editor has the focus. The
//   editor window holds the keys a shortcut can be (with Ctrl or Alt, and
//   Space and S without), asks the key filter whether the application takes
//   each, and lets the plug-in have it if not.
//
// Main thread only.

#include <cstdint>
#include <functional>
#include <memory>

namespace sub {

class EventLoop {
public:
    using Id = uint64_t;
    virtual ~EventLoop() = default;
    // Calls `ready` whenever `fd` can be read, until unwatched.
    virtual Id watch(int fd, std::function<void()> ready) = 0;
    virtual void unwatch(Id id) = 0;
    // Calls `tick` every `milliseconds`, until stopped.
    virtual Id startTimer(int milliseconds, std::function<void()> tick) = 0;
    virtual void stopTimer(Id id) = 0;
};

// The application's event loop (null: the engine's own, run by runEditorEvents()).
void setEditorEventLoop(std::shared_ptr<EventLoop> loop);
// The loop to register on now. What is registered is unregistered through the
// same loop (kept: the application's may be another by then).
std::shared_ptr<EventLoop> editorEventLoop();
// The engine's own loop's work, without waiting: the file descriptors that are
// ready, the timers that are due (Engine::idle() calls it). Only what was
// registered on it runs here: none while the application's loop is the one.
void runEditorEvents();

// A key pressed in a plug-in's editor.
struct EditorKey {
    enum Modifier : int { kShift = 1, kControl = 2, kAlt = 4 };
    uint32_t keysym = 0;  // X11's (X11/keysymdef.h), unshifted: XK_s for S and Shift+S
    int modifiers = 0;
    bool repeat = false;  // the key is held down
};

// Whether the application takes a key pressed in an editor (it does what the
// key does then); if not, the plug-in has it. None: the plug-ins have every key.
// It is called from inside the editor window's own events: what the key does
// (which may close that editor, or every one) must be done after it returns.
void setEditorKeyFilter(std::function<bool(const EditorKey&)> filter);
const std::function<bool(const EditorKey&)>& editorKeyFilter();

// Whether plug-in editors are windows of the engine's own on this system that
// the above serves (Linux, built with X11's xcb).
bool editorsUseHost();

// Closes the foremost editor (the one last focused, opened or brought to
// front, of those shown), as its close button would: false if none is shown.
// Only for the engine's own windows (editorsUseHost()).
bool closeForemostEditor();

}  // namespace sub
