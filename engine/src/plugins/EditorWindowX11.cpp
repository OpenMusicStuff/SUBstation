// Plug-in editor windows on Linux (see EditorWindow.h): an X11 top-level
// window holding the view (kPlatformTypeX11EmbedWindowID: the plug-in makes
// its own window inside it, on its own connection). On Wayland it is an
// XWayland window, as Qt's are when they run on X11.
//
// Each window has a connection of its own to the X server (DISPLAY), watched
// by the editor event loop (EditorHost.h), and a VST 3 run loop for the
// plug-in. The window is the owner's transient (it floats above the main
// window), a dialog to the window manager, closed through WM_DELETE_WINDOW.
// Sizes are in pixels (VST 3 on Linux); the content scale is the X
// resources' Xft.dpi over 96, as desktops set it and Qt reads it.
//
// Keys: with a key filter set, the window grabs the keys a shortcut can be
// (any key with Ctrl or Alt; Space and S alone), synchronously, so the X
// server holds each until the filter has said: taken, or replayed to the
// plug-in as if nothing had grabbed it. (While the main thread is busy, such
// keys wait for it, in every application.) Events xcb reads while waiting for
// a reply stay in its queue with nothing left to read on the connection, so
// the window also looks at its queue every 50 ms.

#include "plugins/EditorWindow.h"

#include <unistd.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "EditorHost.h"
#include "plugins/Vst3RunLoop.h"

namespace sub::vst3 {

using Steinberg::kResultTrue;
using Steinberg::ViewRect;

namespace {

constexpr uint32_t kXK_space = 0x0020;
constexpr uint32_t kXK_s = 0x0073;

int width(const ViewRect& rect) { return rect.right - rect.left; }
int height(const ViewRect& rect) { return rect.bottom - rect.top; }

xcb_atom_t atom(xcb_connection_t* c, const char* name) {
    xcb_intern_atom_reply_t* reply =
        xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, static_cast<uint16_t>(std::strlen(name)), name), nullptr);
    const xcb_atom_t result = reply ? reply->atom : xcb_atom_t{XCB_ATOM_NONE};
    std::free(reply);
    return result;
}

// The scale the desktop draws at: Xft.dpi (the root window's X resources) over 96.
float resourceScale(xcb_connection_t* c, xcb_window_t root) {
    xcb_get_property_reply_t* reply = xcb_get_property_reply(
        c, xcb_get_property(c, 0, root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 1 << 16), nullptr);
    float scale = 1.f;
    if (reply) {
        const std::string resources(static_cast<const char*>(xcb_get_property_value(reply)),
                                    static_cast<size_t>(xcb_get_property_value_length(reply)));
        size_t at = 0;
        while (at < resources.size()) {
            const size_t end = std::min(resources.find('\n', at), resources.size());
            const std::string line = resources.substr(at, end - at);
            at = end + 1;
            if (line.rfind("Xft.dpi:", 0) != 0) continue;
            const double dpi = std::atof(line.c_str() + 8);
            if (dpi >= 48 && dpi <= 960) scale = static_cast<float>(dpi / 96.0);
        }
        std::free(reply);
    }
    return scale;
}

// WM_NORMAL_HINTS (ICCCM 4.1.2.3), as the CARD32s X sends.
struct SizeHints {
    enum : uint32_t { kUSPosition = 1, kPMinSize = 16, kPMaxSize = 32, kPWinGravity = 512 };
    uint32_t flags = 0;
    int32_t x = 0, y = 0, width = 0, height = 0;
    int32_t minWidth = 0, minHeight = 0, maxWidth = 0, maxHeight = 0;
    int32_t widthInc = 0, heightInc = 0;
    int32_t minAspect[2] = {0, 0}, maxAspect[2] = {0, 0};
    int32_t baseWidth = 0, baseHeight = 0;
    uint32_t winGravity = 0;
};
static_assert(sizeof(SizeHints) == 18 * 4);
constexpr uint32_t kStaticGravity = 10;  // the window's own position, not its frame's: no drift when placed again

// The windows open, the foremost first.
std::vector<EditorWindow*>& windows() {
    static std::vector<EditorWindow*> open;
    return open;
}

void toFront(EditorWindow* window) {
    auto& open = windows();
    open.erase(std::remove(open.begin(), open.end(), window), open.end());
    open.insert(open.begin(), window);
    editorRaised();
}

}  // namespace

struct EditorWindow::Native {
    xcb_connection_t* c = nullptr;
    xcb_screen_t* screen = nullptr;
    xcb_window_t window = XCB_WINDOW_NONE;
    std::shared_ptr<EventLoop> loop;  // the one watching it (unwatched through it)
    EventLoop::Id watch = 0;
    EventLoop::Id drain = 0;
    bool visible = false;
    int sizeW = 0, sizeH = 0;  // as last configured
    struct {
        xcb_atom_t protocols, deleteWindow, netName, utf8, type, dialog, activeWindow, pid, userTime;
    } atoms{};
    // The keyboard: keysyms by keycode, and the keys the filter has taken.
    xcb_keycode_t minKeycode = 0;
    int keysymsPerKeycode = 0;
    std::vector<xcb_keysym_t> keysyms;
    xcb_keycode_t takenKey = 0;  // a key taken and still held: the grab stays active until it is released
    xcb_keycode_t releasedKey = 0;
    xcb_timestamp_t releasedAt = 0;
    xcb_keycode_t heldKey = 0;

    void flush() const { xcb_flush(c); }

    void setTitle(const std::string& title) const {
        const auto length = static_cast<uint32_t>(title.size());
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, length, title.data());
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, atoms.netName, atoms.utf8, 8, length, title.data());
    }

    void setSizeHints(bool resizable, int w, int h, const EditorWindow::Position* at) const {
        SizeHints hints;
        hints.flags = SizeHints::kPWinGravity;
        hints.winGravity = kStaticGravity;
        if (at) {
            hints.flags |= SizeHints::kUSPosition;
            hints.x = at->x;
            hints.y = at->y;
        }
        if (!resizable) {  // the view's size, and no other
            hints.flags |= SizeHints::kPMinSize | SizeHints::kPMaxSize;
            hints.minWidth = hints.maxWidth = w;
            hints.minHeight = hints.maxHeight = h;
        }
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32,
                            18, &hints);
    }

    void resize(int w, int h) const {
        const uint32_t size[] = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        xcb_configure_window(c, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
    }

    void move(int x, int y) const {
        const uint32_t at[] = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)};
        xcb_configure_window(c, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, at);
    }

    // Where the window is on the screen (its own top-left corner, inside the window manager's frame).
    bool where(EditorWindow::Position& at) const {
        xcb_translate_coordinates_reply_t* reply =
            xcb_translate_coordinates_reply(c, xcb_translate_coordinates(c, window, screen->root, 0, 0), nullptr);
        if (!reply) return false;
        at = {reply->dst_x, reply->dst_y};
        std::free(reply);
        return true;
    }

    // `x`, `y` kept so that the window (of `w` x `h`) is on the screen.
    void placeInside(int x, int y, int w, int h) const {
        x = std::max(0, std::min(x, screen->width_in_pixels - w));
        y = std::max(0, std::min(y, screen->height_in_pixels - h));
        move(x, y);
    }

    void placeOverOwner(xcb_window_t owner, int w, int h) const {
        static int cascade = 0;  // successive editors don't hide each other
        const int offset = (cascade++ % 6) * 28;
        int left = 0, top = 0, ownerWidth = screen->width_in_pixels, ownerHeight = screen->height_in_pixels;
        if (owner != XCB_WINDOW_NONE) {
            xcb_get_geometry_reply_t* geometry = xcb_get_geometry_reply(c, xcb_get_geometry(c, owner), nullptr);
            xcb_translate_coordinates_reply_t* origin =
                xcb_translate_coordinates_reply(c, xcb_translate_coordinates(c, owner, screen->root, 0, 0), nullptr);
            if (geometry && origin) {
                left = origin->dst_x;
                top = origin->dst_y;
                ownerWidth = geometry->width;
                ownerHeight = geometry->height;
            }
            std::free(geometry);
            std::free(origin);
        }
        placeInside(left + (ownerWidth - w) / 2 + offset, top + (ownerHeight - h) / 3 + offset, w, h);
    }

    bool readKeyboard() {
        const xcb_setup_t* setup = xcb_get_setup(c);
        minKeycode = setup->min_keycode;
        const int count = setup->max_keycode - setup->min_keycode + 1;
        xcb_get_keyboard_mapping_reply_t* reply = xcb_get_keyboard_mapping_reply(
            c, xcb_get_keyboard_mapping(c, setup->min_keycode, static_cast<uint8_t>(count)), nullptr);
        if (!reply) return false;
        keysymsPerKeycode = reply->keysyms_per_keycode;
        const xcb_keysym_t* syms = xcb_get_keyboard_mapping_keysyms(reply);
        keysyms.assign(syms, syms + xcb_get_keyboard_mapping_keysyms_length(reply));
        std::free(reply);
        return true;
    }

    // The keycode's unshifted keysym.
    uint32_t keysym(xcb_keycode_t keycode) const {
        const size_t at = static_cast<size_t>(keycode - minKeycode) * static_cast<size_t>(keysymsPerKeycode);
        if (keycode < minKeycode || keysymsPerKeycode == 0 || at >= keysyms.size()) return 0;
        uint32_t sym = keysyms[at];
        if (sym >= 'A' && sym <= 'Z') sym += 'a' - 'A';  // (a keyboard mapping with capitals first)
        return sym;
    }

    std::vector<xcb_keycode_t> keycodes(uint32_t sym) const {
        std::vector<xcb_keycode_t> found;
        for (size_t i = 0; keysymsPerKeycode > 0 && i < keysyms.size(); i += static_cast<size_t>(keysymsPerKeycode))
            if (keysyms[i] == sym) found.push_back(static_cast<xcb_keycode_t>(minKeycode + i / keysymsPerKeycode));
        return found;
    }

    // The keys a shortcut can be, held until the filter has said (keyboard mode synchronous).
    void grabKeys() {
        if (!readKeyboard()) return;
        constexpr uint16_t kNumLock = XCB_MOD_MASK_2;
        const uint16_t locks[] = {0, XCB_MOD_MASK_LOCK, kNumLock, XCB_MOD_MASK_LOCK | kNumLock};
        const uint16_t withCtrlOrAlt[] = {
            XCB_MOD_MASK_CONTROL, XCB_MOD_MASK_1, XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1,
            XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_SHIFT, XCB_MOD_MASK_1 | XCB_MOD_MASK_SHIFT,
            XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_SHIFT};
        for (const uint16_t lock : locks) {
            for (const uint16_t modifiers : withCtrlOrAlt) {
                xcb_grab_key(c, 1, window, static_cast<uint16_t>(modifiers | lock), XCB_GRAB_ANY,
                             XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC);
            }
            for (const uint32_t sym : {kXK_space, kXK_s}) {
                for (const xcb_keycode_t keycode : keycodes(sym))
                    xcb_grab_key(c, 1, window, lock, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC);
            }
        }
    }

    // The window and the plug-in's inside it, with the focus.
    void focusView() const {
        xcb_query_tree_reply_t* tree = xcb_query_tree_reply(c, xcb_query_tree(c, window), nullptr);
        if (!tree) return;
        if (xcb_query_tree_children_length(tree) > 0) {
            const xcb_window_t child = xcb_query_tree_children(tree)[0];
            xcb_set_input_focus(c, XCB_INPUT_FOCUS_PARENT, child, XCB_CURRENT_TIME);
        }
        std::free(tree);
    }

    // A key press the window grabbed: the filter takes it, or the plug-in has it.
    void keyPressed(const xcb_key_press_event_t& event) {
        const bool frozen = takenKey == 0;  // a grab's first key (the X server holds the keyboard): else one pressed
                                            // while a taken key is held, which isn't held for us
        EditorKey key;
        key.keysym = keysym(event.detail);
        if (event.state & XCB_MOD_MASK_SHIFT) key.modifiers |= EditorKey::kShift;
        if (event.state & XCB_MOD_MASK_CONTROL) key.modifiers |= EditorKey::kControl;
        if (event.state & XCB_MOD_MASK_1) key.modifiers |= EditorKey::kAlt;
        // Held down, X repeats the press, with a release at the same time before it.
        key.repeat = event.detail == heldKey || (event.detail == releasedKey && event.time == releasedAt);
        heldKey = event.detail;
        const auto& filter = editorKeyFilter();
        const bool taken = key.keysym != 0 && filter && filter(key);
        if (!c) return;  // (closed meanwhile after all: its grab went with its connection)
        if (frozen) {
            if (taken) takenKey = event.detail;
            xcb_allow_events(c, taken ? XCB_ALLOW_ASYNC_KEYBOARD : XCB_ALLOW_REPLAY_KEYBOARD, event.time);
        } else if (!taken) {
            forward(event);
        }
        flush();
    }

    void keyReleased(const xcb_key_release_event_t& event) {
        if (event.detail == takenKey) takenKey = 0;  // (the grab ended with it)
        if (event.detail == heldKey) heldKey = 0;
        releasedKey = event.detail;
        releasedAt = event.time;
    }

    // A key that came to the window while a taken one was held, to the window with the focus.
    void forward(const xcb_key_press_event_t& event) const {
        xcb_get_input_focus_reply_t* focus = xcb_get_input_focus_reply(c, xcb_get_input_focus(c), nullptr);
        if (!focus) return;
        xcb_key_press_event_t copy = event;
        copy.event = focus->focus;
        copy.child = XCB_WINDOW_NONE;
        xcb_send_event(c, 1, focus->focus, XCB_EVENT_MASK_KEY_PRESS, reinterpret_cast<const char*>(&copy));
        std::free(focus);
    }

    // What came in on the connection (the editor event loop calls it when there is something).
    static void events(EditorWindow& w);
    // The window gone, and the connection with it.
    static void destroy(EditorWindow& w);
};

bool EditorWindow::canHold(Steinberg::IPlugView& view) {
    return view.isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) == kResultTrue;
}

EditorWindow::EditorWindow(Steinberg::IPtr<Steinberg::IPlugView> view, void* ownerWindow, const std::string& title,
                           const Position* position)
    : view_(std::move(view)), native_(std::make_unique<Native>()), runLoop_(new RunLoop) {
    Native& n = *native_;
    int screenNumber = 0;
    n.c = xcb_connect(nullptr, &screenNumber);
    if (xcb_connection_has_error(n.c)) {  // no X server (DISPLAY)
        xcb_disconnect(n.c);
        n.c = nullptr;
        view_ = nullptr;
        return;
    }
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(n.c));
    for (int i = 0; i < screenNumber && screens.rem > 0; ++i) xcb_screen_next(&screens);
    n.screen = screens.data;
    n.atoms = {atom(n.c, "WM_PROTOCOLS"),
               atom(n.c, "WM_DELETE_WINDOW"),
               atom(n.c, "_NET_WM_NAME"),
               atom(n.c, "UTF8_STRING"),
               atom(n.c, "_NET_WM_WINDOW_TYPE"),
               atom(n.c, "_NET_WM_WINDOW_TYPE_DIALOG"),
               atom(n.c, "_NET_ACTIVE_WINDOW"),
               atom(n.c, "_NET_WM_PID"),
               atom(n.c, "_NET_WM_USER_TIME")};

    // A window that isn't one (another platform's handle, a window gone) isn't the owner.
    auto owner = static_cast<xcb_window_t>(reinterpret_cast<uintptr_t>(ownerWindow));
    if (owner != XCB_WINDOW_NONE) {
        xcb_get_geometry_reply_t* geometry = xcb_get_geometry_reply(n.c, xcb_get_geometry(n.c, owner), nullptr);
        if (!geometry) owner = XCB_WINDOW_NONE;
        std::free(geometry);
    }

    resizable_ = view_->canResize() == kResultTrue;
    setContentScale(resourceScale(n.c, n.screen->root));  // before attaching: the plug-in may size itself for it
    view_->setFrame(this);
    ViewRect rect{};
    int w = 400, h = 300;
    if (view_->getSize(&rect) == kResultTrue && width(rect) > 0 && height(rect) > 0) {
        w = width(rect);
        h = height(rect);
    }

    n.window = xcb_generate_id(n.c);
    const uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    const uint32_t values[] = {n.screen->black_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE |
                                                          XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE};
    xcb_create_window(n.c, XCB_COPY_FROM_PARENT, n.window, n.screen->root, static_cast<int16_t>(position ? position->x : 0),
                      static_cast<int16_t>(position ? position->y : 0), static_cast<uint16_t>(w),
                      static_cast<uint16_t>(h), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, n.screen->root_visual, mask, values);
    n.sizeW = w;
    n.sizeH = h;
    n.setTitle(title);
    static const char kClass[] = "plugin-editor\0SUBstation";  // WM_CLASS: instance, then class
    xcb_change_property(n.c, XCB_PROP_MODE_REPLACE, n.window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof kClass,
                        kClass);
    xcb_change_property(n.c, XCB_PROP_MODE_REPLACE, n.window, n.atoms.protocols, XCB_ATOM_ATOM, 32, 1,
                        &n.atoms.deleteWindow);
    xcb_change_property(n.c, XCB_PROP_MODE_REPLACE, n.window, n.atoms.type, XCB_ATOM_ATOM, 32, 1, &n.atoms.dialog);
    const auto pid = static_cast<uint32_t>(getpid());
    xcb_change_property(n.c, XCB_PROP_MODE_REPLACE, n.window, n.atoms.pid, XCB_ATOM_CARDINAL, 32, 1, &pid);
    if (owner != XCB_WINDOW_NONE) {
        xcb_change_property(n.c, XCB_PROP_MODE_REPLACE, n.window, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1,
                            &owner);
    }
    n.setSizeHints(resizable_, w, h, position);
    if (position) {
        n.placeInside(position->x, position->y, w, h);
    } else {
        n.placeOverOwner(owner, w, h);
    }
    n.flush();

    if (view_->attached(reinterpret_cast<void*>(static_cast<uintptr_t>(n.window)),
                        Steinberg::kPlatformTypeX11EmbedWindowID) != kResultTrue) {
        view_->setFrame(nullptr);
        view_ = nullptr;
        xcb_destroy_window(n.c, n.window);
        xcb_disconnect(n.c);
        n.c = nullptr;
        n.window = XCB_WINDOW_NONE;
        return;
    }
    // Some plug-ins only know their size once attached.
    if (view_->getSize(&rect) == kResultTrue && width(rect) > 0 && height(rect) > 0 &&
        (width(rect) != w || height(rect) != h)) {
        n.sizeW = width(rect);
        n.sizeH = height(rect);
        n.setSizeHints(resizable_, n.sizeW, n.sizeH, position);
        n.resize(n.sizeW, n.sizeH);
    }
    if (editorKeyFilter()) n.grabKeys();
    xcb_map_window(n.c, n.window);
    n.visible = true;
    n.flush();
    n.loop = editorEventLoop();
    n.watch = n.loop->watch(xcb_get_file_descriptor(n.c), [this] { Native::events(*this); });
    // Events xcb read while waiting for a reply wait in its queue, with nothing more to read on the
    // connection: looked for now and then too (a key held for the filter mustn't wait for the next event).
    n.drain = n.loop->startTimer(50, [this] { Native::events(*this); });
    toFront(this);
    position_ = this->position();
}

EditorWindow::~EditorWindow() {
    detachView();
    Native::destroy(*this);
}

void EditorWindow::closeAsUser() {
    if (!isOpen()) return;
    closed_ = true;
    detachView();
    Native::destroy(*this);
}

void EditorWindow::Native::destroy(EditorWindow& w) {
    Native& n = *w.native_;
    auto& open = windows();
    open.erase(std::remove(open.begin(), open.end(), &w), open.end());
    if (!n.c) return;
    if (n.loop) {
        n.loop->unwatch(n.watch);
        n.loop->stopTimer(n.drain);
        n.loop = nullptr;
    }
    if (n.window != XCB_WINDOW_NONE) {
        w.position_ = w.position();
        xcb_destroy_window(n.c, n.window);
        n.window = XCB_WINDOW_NONE;
    }
    xcb_disconnect(n.c);
    n.c = nullptr;
}

void EditorWindow::Native::events(EditorWindow& w) {
    Native& n = *w.native_;
    while (n.c) {
        xcb_generic_event_t* event = xcb_poll_for_event(n.c);
        if (!event) {
            if (xcb_connection_has_error(n.c)) w.closeAsUser();  // the X server went
            return;
        }
        const uint8_t type = event->response_type & 0x7F;
        switch (type) {
            case XCB_CONFIGURE_NOTIFY: {
                const auto* configure = reinterpret_cast<const xcb_configure_notify_event_t*>(event);
                if (configure->window != n.window) break;
                const int cw = configure->width, ch = configure->height;
                if (cw == n.sizeW && ch == n.sizeH) break;
                n.sizeW = cw;
                n.sizeH = ch;
                if (w.resizing_ || !w.view_) break;
                // The user resized the window: the view takes the size it allows of it, and the window follows.
                ViewRect wanted(0, 0, cw, ch);
                if (w.resizable_ && w.view_->checkSizeConstraint(&wanted) == kResultTrue &&
                    (vst3::width(wanted) != cw || vst3::height(wanted) != ch)) {
                    n.sizeW = vst3::width(wanted);
                    n.sizeH = vst3::height(wanted);
                    n.resize(n.sizeW, n.sizeH);
                    n.flush();
                }
                w.sizeView(wanted);
                break;
            }
            case XCB_CLIENT_MESSAGE: {
                const auto* message = reinterpret_cast<const xcb_client_message_event_t*>(event);
                if (message->type == n.atoms.protocols && message->data.data32[0] == n.atoms.deleteWindow) {
                    w.closeAsUser();
                }
                break;
            }
            case XCB_FOCUS_IN: {
                const auto* focus = reinterpret_cast<const xcb_focus_in_event_t*>(event);
                if (focus->event != n.window || focus->mode != XCB_NOTIFY_MODE_NORMAL) break;
                toFront(&w);
                if (focus->detail != XCB_NOTIFY_DETAIL_INFERIOR) n.focusView();  // keys go to the plug-in
                n.flush();
                break;
            }
            case XCB_KEY_PRESS: n.keyPressed(*reinterpret_cast<const xcb_key_press_event_t*>(event)); break;
            case XCB_KEY_RELEASE: n.keyReleased(*reinterpret_cast<const xcb_key_release_event_t*>(event)); break;
            case XCB_MAPPING_NOTIFY:  // the keyboard changed: its keys grabbed anew
                if (editorKeyFilter()) {
                    xcb_ungrab_key(n.c, XCB_GRAB_ANY, n.window, XCB_MOD_MASK_ANY);
                    n.grabKeys();
                    n.flush();
                }
                break;
            default: break;
        }
        std::free(event);
    }
}

bool EditorWindow::isOpen() const noexcept { return native_->c && native_->window != XCB_WINDOW_NONE; }

void EditorWindow::setTitle(const std::string& title) {
    if (!isOpen()) return;
    native_->setTitle(title);
    native_->flush();
}

void EditorWindow::bringToFront() {
    if (!isOpen()) return;
    Native& n = *native_;
    xcb_map_window(n.c, n.window);
    n.visible = true;
    const uint32_t above = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(n.c, n.window, XCB_CONFIG_WINDOW_STACK_MODE, &above);
    // Asked of the window manager (EWMH: an application's request), as it may keep the focus from a plain raise.
    xcb_client_message_event_t activate{};
    activate.response_type = XCB_CLIENT_MESSAGE;
    activate.format = 32;
    activate.window = n.window;
    activate.type = n.atoms.activeWindow;
    activate.data.data32[0] = 1;
    xcb_send_event(n.c, 0, n.screen->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char*>(&activate));
    toFront(this);
    n.flush();
}

void EditorWindow::setVisible(bool visible) {
    if (!isOpen() || visible == native_->visible) return;
    Native& n = *native_;
    if (visible) {
        const uint32_t never = 0;  // shown again without taking the focus (EWMH: _NET_WM_USER_TIME 0)
        xcb_change_property(n.c, XCB_PROP_MODE_REPLACE, n.window, n.atoms.userTime, XCB_ATOM_CARDINAL, 32, 1, &never);
        n.setSizeHints(resizable_, n.sizeW, n.sizeH, &position_);  // where it was
        xcb_map_window(n.c, n.window);
    } else {
        position_ = position();
        xcb_unmap_window(n.c, n.window);
        // ICCCM 4.1.4: withdrawn, the window manager is told with a synthetic UnmapNotify.
        xcb_unmap_notify_event_t unmap{};
        unmap.response_type = XCB_UNMAP_NOTIFY;
        unmap.event = n.screen->root;
        unmap.window = n.window;
        xcb_send_event(n.c, 0, n.screen->root,
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                       reinterpret_cast<const char*>(&unmap));
    }
    n.visible = visible;
    n.flush();
}

bool EditorWindow::isVisible() const { return isOpen() && native_->visible; }

EditorWindow::Position EditorWindow::position() const {
    Position at;
    if (isOpen() && native_->visible && native_->where(at)) return at;
    return position_;
}

tresult PLUGIN_API EditorWindow::resizeView(Steinberg::IPlugView* view, ViewRect* newSize) {
    if (!view_ || view != view_.get() || !newSize || width(*newSize) <= 0 || height(*newSize) <= 0 || !isOpen()) {
        return Steinberg::kInvalidArgument;
    }
    if (resizing_) return Steinberg::kResultFalse;
    resizing_ = true;
    Native& n = *native_;
    n.sizeW = width(*newSize);
    n.sizeH = height(*newSize);
    if (!resizable_) n.setSizeHints(false, n.sizeW, n.sizeH, nullptr);
    n.resize(n.sizeW, n.sizeH);
    n.flush();
    resizing_ = false;
    sizeView(*newSize);
    return kResultTrue;
}

}  // namespace sub::vst3

namespace sub {

bool editorsUseHost() { return true; }

bool closeForemostEditor() {
    for (vst3::EditorWindow* window : vst3::windows()) {
        if (!window->isVisible()) continue;
        window->closeAsUser();
        return true;
    }
    return false;
}

}  // namespace sub
