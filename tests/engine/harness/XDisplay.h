#pragma once
// An X server of the test's own (Linux, engines built with X11 editors): Xvfb
// on a display number it picks itself (-displayfd), which DISPLAY points at
// while it runs, so plug-in editors open there. No window manager: what one
// would do, tests do by hand (the focus, closing through WM_DELETE_WINDOW).
// Where Xvfb isn't installed it doesn't run (running(), and why not: tests
// skip). And a client of it for the tests (XClient): windows found by name,
// their sizes and properties, client messages, the focus, keys typed (XTEST).
// Needs nothing of a test harness: the engine's tests and the application's
// use it.

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern char** environ;

namespace subtest {

class XDisplay {
public:
    XDisplay() {
        const char* previous = std::getenv("DISPLAY");
        if (previous) previous_ = previous;
        int fds[2];
        if (pipe(fds) != 0) {
            whyNot_ = "no pipe";
            return;
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addclose(&actions, fds[0]);
        std::vector<std::string> args{"Xvfb", "-displayfd", std::to_string(fds[1]), "-screen", "0", "1280x800x24",
                                      "-nolisten", "tcp"};
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        const int error = posix_spawnp(&pid_, "Xvfb", &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(fds[1]);
        if (error != 0) {
            pid_ = 0;
            close(fds[0]);
            whyNot_ = "Xvfb isn't installed";
            return;
        }
        // It writes its display number when it is ready.
        std::string number;
        char ch = 0;
        while (read(fds[0], &ch, 1) == 1 && ch != '\n') number += ch;
        close(fds[0]);
        if (number.empty()) {
            stop();
            whyNot_ = "Xvfb didn't start";
            return;
        }
        display_ = ":" + number;
        ::setenv("DISPLAY", display_.c_str(), 1);
    }

    ~XDisplay() {
        stop();
        if (previous_) {
            ::setenv("DISPLAY", previous_->c_str(), 1);
        } else {
            ::unsetenv("DISPLAY");
        }
    }

    XDisplay(const XDisplay&) = delete;
    XDisplay& operator=(const XDisplay&) = delete;

    bool running() const { return pid_ != 0; }
    const std::string& whyNot() const { return whyNot_; }
    const std::string& name() const { return display_; }

private:
    void stop() {
        if (pid_ == 0) return;
        ::kill(pid_, SIGTERM);
        int status = 0;
        ::waitpid(pid_, &status, 0);
        pid_ = 0;
    }

    pid_t pid_ = 0;
    std::string display_;
    std::string whyNot_;
    std::optional<std::string> previous_;
};

// A client of the display DISPLAY names.
class XClient {
public:
    XClient() {
        c_ = xcb_connect(nullptr, nullptr);
        screen_ = xcb_setup_roots_iterator(xcb_get_setup(c_)).data;
    }
    ~XClient() { xcb_disconnect(c_); }
    XClient(const XClient&) = delete;
    XClient& operator=(const XClient&) = delete;

    bool ok() const { return !xcb_connection_has_error(c_); }
    xcb_window_t root() const { return screen_->root; }

    xcb_atom_t atom(const std::string& name) {
        auto* reply = xcb_intern_atom_reply(c_, xcb_intern_atom(c_, 0, uint16_t(name.size()), name.c_str()), nullptr);
        const xcb_atom_t result = reply ? reply->atom : xcb_atom_t(XCB_ATOM_NONE);
        std::free(reply);
        return result;
    }

    std::vector<xcb_window_t> children(xcb_window_t window) {
        std::vector<xcb_window_t> found;
        if (auto* tree = xcb_query_tree_reply(c_, xcb_query_tree(c_, window), nullptr)) {
            const xcb_window_t* list = xcb_query_tree_children(tree);
            found.assign(list, list + xcb_query_tree_children_length(tree));
            std::free(tree);
        }
        return found;
    }

    // A property's bytes ("" if it has none).
    std::string property(xcb_window_t window, xcb_atom_t property) {
        auto* reply =
            xcb_get_property_reply(c_, xcb_get_property(c_, 0, window, property, XCB_GET_PROPERTY_TYPE_ANY, 0, 1 << 16),
                                   nullptr);
        std::string value;
        if (reply) {
            // (The length is in bytes already: value_len items of format / 8 bytes.)
            value.assign(static_cast<const char*>(xcb_get_property_value(reply)),
                         size_t(xcb_get_property_value_length(reply)));
            std::free(reply);
        }
        return value;
    }
    uint32_t cardinal(xcb_window_t window, const std::string& name) {
        const std::string value = property(window, atom(name));
        uint32_t result = 0;
        if (value.size() >= 4) std::memcpy(&result, value.data(), 4);
        return result;
    }
    std::string title(xcb_window_t window) { return property(window, XCB_ATOM_WM_NAME); }

    // The top-level window of that title (none: 0).
    xcb_window_t topLevel(const std::string& title) {
        for (const xcb_window_t window : children(root()))
            if (this->title(window) == title) return window;
        return 0;
    }

    std::pair<int, int> size(xcb_window_t window) {
        auto* geometry = xcb_get_geometry_reply(c_, xcb_get_geometry(c_, window), nullptr);
        std::pair<int, int> result{0, 0};
        if (geometry) result = {geometry->width, geometry->height};
        std::free(geometry);
        return result;
    }
    bool mapped(xcb_window_t window) {
        auto* attributes = xcb_get_window_attributes_reply(c_, xcb_get_window_attributes(c_, window), nullptr);
        const bool result = attributes && attributes->map_state == XCB_MAP_STATE_VIEWABLE;
        std::free(attributes);
        return result;
    }

    void resize(xcb_window_t window, int width, int height) {
        const uint32_t size[] = {uint32_t(width), uint32_t(height)};
        xcb_configure_window(c_, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
        xcb_flush(c_);
    }

    void send(xcb_window_t window, xcb_atom_t type, uint32_t a = 0, uint32_t b = 0) {
        xcb_client_message_event_t message{};
        message.response_type = XCB_CLIENT_MESSAGE;
        message.format = 32;
        message.window = window;
        message.type = type;
        message.data.data32[0] = a;
        message.data.data32[1] = b;
        xcb_send_event(c_, 0, window, 0, reinterpret_cast<const char*>(&message));
        xcb_flush(c_);
    }
    // As a window manager does when the window's close button is clicked.
    void close(xcb_window_t window) { send(window, atom("WM_PROTOCOLS"), atom("WM_DELETE_WINDOW")); }

    void focus(xcb_window_t window) {
        xcb_set_input_focus(c_, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME);
        xcb_flush(c_);
    }

    // The keycode of a keysym (0: none).
    xcb_keycode_t keycode(uint32_t keysym) {
        const xcb_setup_t* setup = xcb_get_setup(c_);
        const int count = setup->max_keycode - setup->min_keycode + 1;
        auto* reply = xcb_get_keyboard_mapping_reply(
            c_, xcb_get_keyboard_mapping(c_, setup->min_keycode, uint8_t(count)), nullptr);
        xcb_keycode_t found = 0;
        if (reply) {
            const xcb_keysym_t* syms = xcb_get_keyboard_mapping_keysyms(reply);
            const int per = reply->keysyms_per_keycode;
            for (int i = 0; i < count && !found; ++i)
                if (syms[i * per] == keysym) found = xcb_keycode_t(setup->min_keycode + i);
            std::free(reply);
        }
        return found;
    }
    // Typed as on a keyboard: the modifiers' keys (keysyms) held around the key.
    void type(uint32_t keysym, const std::vector<uint32_t>& modifiers = {}) {
        for (const uint32_t modifier : modifiers) fake(XCB_KEY_PRESS, keycode(modifier));
        fake(XCB_KEY_PRESS, keycode(keysym));
        fake(XCB_KEY_RELEASE, keycode(keysym));
        for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) fake(XCB_KEY_RELEASE, keycode(*it));
        xcb_flush(c_);
    }

    void setRootProperty(xcb_atom_t property, xcb_atom_t type, const std::string& value) {
        xcb_change_property(c_, XCB_PROP_MODE_REPLACE, root(), property, type, 8, uint32_t(value.size()), value.data());
        xcb_flush(c_);
    }

    void sync() { std::free(xcb_get_input_focus_reply(c_, xcb_get_input_focus(c_), nullptr)); }

private:
    void fake(uint8_t type, xcb_keycode_t keycode) {
        xcb_test_fake_input(c_, type, keycode, XCB_CURRENT_TIME, XCB_WINDOW_NONE, 0, 0, 0);
    }

    xcb_connection_t* c_ = nullptr;
    xcb_screen_t* screen_ = nullptr;
};

}  // namespace subtest
