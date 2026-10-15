// Plug-in editors on Linux (plugins/editor/EditorWindowX11.cpp), on an X server of
// the test's own (harness/XDisplay.h): SUB Test Effect's editor (an X11
// window of the plug-in's own connection, run by the host's IRunLoop) asked
// to edit, resize and mark its state dirty with client messages, as a user's
// clicks would; the window following the view and the view the window; the
// content scale; the keys a shortcut can be going to the key filter, the rest
// to the plug-in; hiding, closing, the foremost editor. With no window manager,
// the test does what one would (the focus, the close button). Skipped where
// the engine was built without X11 editors, or Xvfb isn't installed.

#if SUBSTATION_HAS_X11_EDITORS && !SUBSTATION_NO_EDITOR_TESTS

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <thread>

#include "EditorHost.h"
#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/XDisplay.h"

using namespace subtest;

namespace {

using EventType = sub::ProcessorEvent::Type;

constexpr uint32_t XK_space = 0x0020, XK_a = 0x0061, XK_c = 0x0063, XK_f = 0x0066, XK_s = 0x0073;
constexpr uint32_t XK_Control_L = 0xffe3, XK_Shift_L = 0xffe1;

// The X server, then the engine (its editors close before the server goes).
struct EditorTest {
    XDisplay display;
    sub::Engine engine;
    EditorTest() {
        if (!display.running()) SKIP(display.whyNot());
        engine.setClipFadeMs(0);
    }
    ~EditorTest() { sub::setEditorKeyFilter(nullptr); }
};

// Runs the editors (Engine::idle()) until `done`.
bool pumpUntil(sub::Engine& engine, const std::function<bool()>& done, double timeout = 5.0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        engine.idle();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

std::vector<sub::ProcessorEventRecord> eventsOf(std::vector<sub::ProcessorEventRecord>& all, EventType type) {
    std::vector<sub::ProcessorEventRecord> found;
    for (const auto& e : all)
        if (e.type == type) found.push_back(e);
    return found;
}

// The editor's window, and the plug-in's inside it, once both show.
struct Editor {
    xcb_window_t frame = 0;
    xcb_window_t view = 0;
};

Editor findEditor(sub::Engine& engine, XClient& x, const std::string& title) {
    Editor editor;
    pumpUntil(engine, [&] {
        editor.frame = x.topLevel(title);
        const auto children = editor.frame ? x.children(editor.frame) : std::vector<xcb_window_t>{};
        editor.view = children.empty() ? 0 : children.front();
        return editor.view != 0 && x.mapped(editor.view);  // (viewable: its window and the editor's shown)
    });
    return editor;
}

// The keys the plug-in got: "keycode:state;" each.
std::string keysOf(XClient& x, const Editor& editor) { return x.property(editor.view, x.atom("SUB_TEST_KEYS")); }

}  // namespace

TEST_CASE("x11 editor: edits, resizes and closes") {
    EditorTest t;
    auto& engine = t.engine;
    const uint32_t track = clipTrack(engine, dcWav());
    const uint32_t effect = addTestPlugin(engine, engine.trackChain(track), "SUB Test Effect");
    XClient x;
    REQUIRE(x.ok());
    REQUIRE(engine.openEditor(effect, 0, "SUB Test Effect - Audio"));
    CHECK(engine.isEditorOpen(effect));
    const Editor editor = findEditor(engine, x, "SUB Test Effect - Audio");
    REQUIRE(editor.frame && editor.view);
    CHECK(x.size(editor.frame) == std::make_pair(400, 300));
    CHECK(engine.openEditor(effect, 0, "Renamed"));  // already open: raised, retitled
    // (Each connection's requests in order, but not across connections: waited for.)
    CHECK(pumpUntil(engine, [&] { return x.title(editor.frame) == "Renamed"; }));

    // The plug-in's timer runs (on the host's run loop).
    CHECK(pumpUntil(engine, [&] { return x.cardinal(editor.view, "SUB_TEST_TICKS") > 2; }));

    // A knob drag in the plug-in's editor: one gesture, heard at once, reported for undo.
    engine.takeProcessorEvents();
    x.send(editor.view, x.atom("SUB_TEST_EDIT_GAIN"));
    std::vector<sub::ProcessorEventRecord> all;
    REQUIRE(pumpUntil(engine, [&] {
        for (auto& e : engine.takeProcessorEvents()) all.push_back(e);
        return eventsOf(all, EventType::ParamEdited).size() >= 2;
    }));
    const auto edits = eventsOf(all, EventType::ParamEdited);
    REQUIRE(edits.size() == 2);
    CHECK_EQ(edits[0].paramIndex, FX_GAIN);
    CHECK_EQ(std::round(edits[0].value * 1e6) / 1e6, 0.25);
    CHECK_EQ(edits[1].paramIndex, FX_GAIN);
    CHECK_EQ(std::round(edits[1].value * 1e6) / 1e6, 0.3);
    CHECK(edits[0].gesture == edits[1].gesture && edits[1].gesture != 0);
    CHECK_APPROX(engine.processorParam(effect, FX_GAIN), 0.3);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 100), 0.3, 1e-7, 1e-6);  // 0.5 * 2 * 0.3

    // The plug-in resizes its editor; the window follows.
    x.send(editor.view, x.atom("SUB_TEST_RESIZE"), 500, 350);
    CHECK(pumpUntil(engine, [&] {
        return x.size(editor.frame) == std::make_pair(500, 350) && x.size(editor.view) == std::make_pair(500, 350);
    }));

    // The user makes the window smaller than the plug-in allows (200 x 150): it stops there,
    // and the view follows the window.
    x.resize(editor.frame, 50, 50);
    CHECK(pumpUntil(engine, [&] {
        return x.size(editor.frame) == std::make_pair(200, 150) && x.size(editor.view) == std::make_pair(200, 150);
    }));
    x.resize(editor.frame, 260, 180);
    CHECK(pumpUntil(engine, [&] { return x.size(editor.view) == std::make_pair(260, 180); }));

    x.send(editor.view, x.atom("SUB_TEST_DIRTY"));
    CHECK(pumpUntil(engine, [&] {
        auto events = engine.takeProcessorEvents();
        return !eventsOf(events, EventType::StateDirty).empty();
    }));

    // Hidden and shown again.
    CHECK(engine.setEditorVisible(effect, false));
    CHECK(!engine.isEditorOpen(effect));  // (shown: not)
    CHECK(pumpUntil(engine, [&] { return !x.mapped(editor.frame); }));
    CHECK(engine.setEditorVisible(effect, true));
    CHECK(pumpUntil(engine, [&] { return x.mapped(editor.frame); }));

    // Closing the window (its close button) closes the editor.
    x.close(editor.frame);
    CHECK(pumpUntil(engine, [&] { return !engine.isEditorOpen(effect); }));
    auto events = engine.takeProcessorEvents();
    CHECK(!eventsOf(events, EventType::EditorClosed).empty());
    CHECK(pumpUntil(engine, [&] { return x.topLevel("Renamed") == 0; }));

    // Removing the device closes its editor too.
    CHECK(engine.openEditor(effect, 0, "Again"));
    CHECK(findEditor(engine, x, "Again").frame != 0);
    engine.removeProcessor(effect);
    CHECK(pumpUntil(engine, [&] { return x.topLevel("Again") == 0; }));
}

// A VST2 plug-in's editor (SUB VST2 Gain, tests/vst2_plugins): held by the same
// window, through a view standing in for it; opened into the window, run by the
// engine's idle calls (effEditIdle); its edits one gesture; its resize requests
// followed; closed with the window.
TEST_CASE("x11 editor: a VST2 plug-in's") {
    EditorTest t;
    auto& engine = t.engine;
    requireTestVst2();
    const uint32_t track = clipTrack(engine, dcWav());
    const uint32_t gain = addTestVst2(engine, engine.trackChain(track), "Gain");
    CHECK(engine.processorInfo(gain).hasEditor);
    XClient x;
    REQUIRE(x.ok());
    REQUIRE(engine.openEditor(gain, 0, "SUB VST2 Gain - Audio"));
    const Editor editor = findEditor(engine, x, "SUB VST2 Gain - Audio");
    REQUIRE(editor.frame && editor.view);
    CHECK(x.size(editor.frame) == std::make_pair(300, 200));  // the size it says (effEditGetRect)

    engine.takeProcessorEvents();
    x.send(editor.view, x.atom("SUB_TEST_EDIT_GAIN"));
    std::vector<sub::ProcessorEventRecord> all;
    REQUIRE(pumpUntil(engine, [&] {
        for (auto& e : engine.takeProcessorEvents()) all.push_back(e);
        return eventsOf(all, EventType::ParamEdited).size() >= 2;
    }));
    const auto edits = eventsOf(all, EventType::ParamEdited);
    REQUIRE(edits.size() == 2);
    CHECK_EQ(edits[0].paramIndex, 0);
    CHECK_EQ(std::round(edits[1].value * 1e6) / 1e6, 0.3);
    CHECK_EQ(std::round(edits[1].oldValue * 1e6) / 1e6, 0.5);  // (the value before the gesture)
    CHECK(edits[0].gesture == edits[1].gesture && edits[1].gesture != 0);
    CHECK(!eventsOf(all, EventType::ParamTouched).empty());
    CHECK_APPROX(engine.processorParam(gain, 0), 0.3);
    CHECK_ALLCLOSE(engine.renderOffline(0.0, 100), 0.3, 1e-7, 1e-6);  // 0.5 * 2 * 0.3

    x.send(editor.view, x.atom("SUB_TEST_RESIZE"), 420, 260);
    CHECK(pumpUntil(engine, [&] { return x.size(editor.frame) == std::make_pair(420, 260); }));

    x.close(editor.frame);
    CHECK(pumpUntil(engine, [&] { return !engine.isEditorOpen(gain); }));
    auto events = engine.takeProcessorEvents();
    CHECK(!eventsOf(events, EventType::EditorClosed).empty());
    CHECK(engine.openEditor(gain, 0, "Again"));  // (it opens again)
    CHECK(findEditor(engine, x, "Again").frame != 0);
    engine.removeProcessor(gain);
    CHECK(pumpUntil(engine, [&] { return x.topLevel("Again") == 0; }));
}

TEST_CASE("x11 editor: the content scale, from Xft.dpi") {
    EditorTest t;
    auto& engine = t.engine;
    const uint32_t track = engine.addTrack();
    const uint32_t effect = addTestPlugin(engine, engine.trackChain(track), "SUB Test Effect");
    XClient x;
    REQUIRE(x.ok());
    x.setRootProperty(XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, "Xft.antialias:\t1\nXft.dpi:\t144\n");
    REQUIRE(engine.openEditor(effect, 0, "Scaled"));
    const Editor editor = findEditor(engine, x, "Scaled");
    REQUIRE(editor.view);
    CHECK_EQ(x.cardinal(editor.view, "SUB_TEST_SCALE"), 150u);
}

TEST_CASE("x11 editor: the keys a shortcut can be go to the key filter") {
    EditorTest t;
    auto& engine = t.engine;
    std::vector<sub::EditorKey> asked;
    sub::setEditorKeyFilter([&](const sub::EditorKey& key) {
        asked.push_back(key);
        // The application takes Space and Ctrl+F; Ctrl+C is the plug-in's.
        return (key.keysym == XK_space && key.modifiers == 0) ||
               (key.keysym == XK_f && key.modifiers == sub::EditorKey::kControl);
    });
    const uint32_t track = engine.addTrack();
    const uint32_t effect = addTestPlugin(engine, engine.trackChain(track), "SUB Test Effect");
    XClient x;
    REQUIRE(x.ok());
    REQUIRE(engine.openEditor(effect, 0, "Keys"));
    const Editor editor = findEditor(engine, x, "Keys");
    REQUIRE(editor.view);
    x.focus(editor.view);  // (the plug-in's window has the focus, as after a click in it)
    const auto pluginGot = [&](uint32_t keysym, int state) {
        return keysOf(x, editor).find(std::to_string(x.keycode(keysym)) + ":" + std::to_string(state) + ";") !=
               std::string::npos;
    };

    // A letter alone: the plug-in's, not asked about.
    x.type(XK_a);
    CHECK(pumpUntil(engine, [&] { return pluginGot(XK_a, 0); }));
    CHECK(asked.empty());

    // Space: asked, taken.
    x.type(XK_space);
    CHECK(pumpUntil(engine, [&] { return asked.size() == 1; }));
    REQUIRE(asked.size() == 1);
    CHECK_EQ(asked[0].keysym, XK_space);
    CHECK_EQ(asked[0].modifiers, 0);
    CHECK(!asked[0].repeat);

    // Ctrl+C: asked, left to the plug-in (it gets it as if nothing had held it).
    x.type(XK_c, {XK_Control_L});
    CHECK(pumpUntil(engine, [&] { return pluginGot(XK_c, XCB_MOD_MASK_CONTROL); }));
    REQUIRE(asked.size() == 2);
    CHECK_EQ(asked[1].keysym, XK_c);
    CHECK_EQ(asked[1].modifiers, int(sub::EditorKey::kControl));

    // Ctrl+F: taken.
    x.type(XK_f, {XK_Control_L});
    CHECK(pumpUntil(engine, [&] { return asked.size() == 3; }));
    CHECK(!pluginGot(XK_f, XCB_MOD_MASK_CONTROL));

    // Shift+S: not a shortcut's (S alone is): the plug-in's.
    x.type(XK_s, {XK_Shift_L});
    CHECK(pumpUntil(engine, [&] { return pluginGot(XK_s, XCB_MOD_MASK_SHIFT); }));
    CHECK_EQ(asked.size(), size_t{3});

    // Space held down (past the X server's autorepeat delay): its repeats are said to be.
    const xcb_keycode_t space = x.keycode(XK_space);
    REQUIRE(space != 0);
    asked.clear();
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    xcb_test_fake_input(c, XCB_KEY_PRESS, space, XCB_CURRENT_TIME, XCB_WINDOW_NONE, 0, 0, 0);
    xcb_flush(c);
    pumpUntil(engine, [] { return false; }, 1.2);
    xcb_test_fake_input(c, XCB_KEY_RELEASE, space, XCB_CURRENT_TIME, XCB_WINDOW_NONE, 0, 0, 0);
    xcb_flush(c);
    pumpUntil(engine, [] { return false; }, 0.1);
    xcb_disconnect(c);
    REQUIRE(asked.size() >= 2);
    CHECK(!asked[0].repeat);
    CHECK(std::all_of(asked.begin() + 1, asked.end(), [](const sub::EditorKey& k) { return k.repeat; }));
    CHECK(keysOf(x, editor).find(std::to_string(space) + ":") == std::string::npos);  // none reached the plug-in

    // Nothing is held for the filter once the editor is gone.
    engine.closeEditor(effect);
    x.sync();
}

TEST_CASE("x11 editor: the keyboard changing, Space's grab goes where Space is") {
    EditorTest t;
    auto& engine = t.engine;
    std::vector<sub::EditorKey> asked;
    sub::setEditorKeyFilter([&](const sub::EditorKey& key) {
        asked.push_back(key);
        return key.keysym == XK_space && key.modifiers == 0;
    });
    const uint32_t effect = addTestPlugin(engine, engine.trackChain(engine.addTrack()), "SUB Test Effect");
    XClient x;
    REQUIRE(x.ok());
    REQUIRE(engine.openEditor(effect, 0, "Keys"));
    const Editor editor = findEditor(engine, x, "Keys");
    REQUIRE(editor.view);
    x.focus(editor.view);
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    REQUIRE(!xcb_connection_has_error(c));
    // A key typed, again and again until `done` (the window may not have read the keyboard yet).
    const auto typeUntil = [&](xcb_keycode_t keycode, const std::function<bool()>& done) {
        return pumpUntil(engine, [&] {
            xcb_test_fake_input(c, XCB_KEY_PRESS, keycode, XCB_CURRENT_TIME, XCB_WINDOW_NONE, 0, 0, 0);
            xcb_test_fake_input(c, XCB_KEY_RELEASE, keycode, XCB_CURRENT_TIME, XCB_WINDOW_NONE, 0, 0, 0);
            xcb_flush(c);
            pumpUntil(engine, [] { return false; }, 0.05);
            return done();
        });
    };

    // Another layout: Space on a key that had nothing. Its grab follows (the window reads the keyboard again).
    const xcb_setup_t* setup = xcb_get_setup(c);
    const int count = setup->max_keycode - setup->min_keycode + 1;
    auto* mapping = xcb_get_keyboard_mapping_reply(
        c, xcb_get_keyboard_mapping(c, setup->min_keycode, uint8_t(count)), nullptr);
    REQUIRE(mapping);
    const int per = mapping->keysyms_per_keycode;
    const xcb_keysym_t* syms = xcb_get_keyboard_mapping_keysyms(mapping);
    xcb_keycode_t spare = 0;
    for (int i = count - 1; i >= 0 && !spare; --i)
        if (std::all_of(syms + i * per, syms + (i + 1) * per, [](xcb_keysym_t sym) { return sym == 0; }))
            spare = xcb_keycode_t(setup->min_keycode + i);
    std::free(mapping);
    REQUIRE(spare != 0);
    std::vector<xcb_keysym_t> spaceThere(size_t(per), 0);
    spaceThere[0] = XK_space;
    xcb_change_keyboard_mapping(c, 1, spare, uint8_t(per), spaceThere.data());
    xcb_flush(c);
    CHECK(typeUntil(spare, [&] { return !asked.empty(); }));
    REQUIRE(!asked.empty());
    CHECK_EQ(asked[0].keysym, XK_space);
    CHECK_EQ(asked[0].modifiers, 0);

    // Taken away again: so is its grab (the plug-in gets the key), and Space's own key stays grabbed.
    const std::vector<xcb_keysym_t> nothing(size_t(per), 0);
    xcb_change_keyboard_mapping(c, 1, spare, uint8_t(per), nothing.data());
    xcb_flush(c);
    CHECK(typeUntil(spare, [&] {
        x.sync();
        return keysOf(x, editor).find(std::to_string(spare) + ":") != std::string::npos;
    }));
    asked.clear();
    x.type(XK_space);
    CHECK(pumpUntil(engine, [&] { return asked.size() == 1; }));
    xcb_disconnect(c);
    engine.closeEditor(effect);
}

TEST_CASE("x11 editor: the foremost editor closed") {
    EditorTest t;
    auto& engine = t.engine;
    const uint32_t track = engine.addTrack();
    const uint32_t first = addTestPlugin(engine, engine.trackChain(track), "SUB Test Effect");
    const uint32_t second = addTestPlugin(engine, engine.trackChain(track), "SUB Test Effect");
    XClient x;
    REQUIRE(x.ok());
    CHECK(!sub::closeForemostEditor());
    REQUIRE(engine.openEditor(first, 0, "One"));
    REQUIRE(engine.openEditor(second, 0, "Two"));
    const Editor one = findEditor(engine, x, "One");
    REQUIRE(one.frame);
    CHECK(sub::editorsUseHost());
    // The last opened, then the one focused since.
    CHECK(sub::closeForemostEditor());
    CHECK(pumpUntil(engine, [&] { return !engine.isEditorOpen(second); }));
    CHECK(engine.isEditorOpen(first));
    REQUIRE(engine.openEditor(second, 0, "Two"));
    findEditor(engine, x, "Two");
    x.focus(one.frame);
    CHECK(pumpUntil(engine, [&] { return keysOf(x, one).empty(); }));  // (the focus event read)
    engine.idle();
    CHECK(sub::closeForemostEditor());
    CHECK(pumpUntil(engine, [&] { return !engine.isEditorOpen(first); }));
    CHECK(engine.isEditorOpen(second));
    // A hidden editor isn't the foremost.
    CHECK(engine.setEditorVisible(second, false));
    CHECK(!sub::closeForemostEditor());
}

TEST_CASE("x11 editor: unregistered through the loop it registered on") {
    // An application's loop that keeps what is registered on it (and runs none of it).
    struct Kept final : sub::EventLoop {
        std::set<Id> watches, timers;
        Id next = 1000;
        Id watch(int, std::function<void()>) override { return *watches.insert(++next).first; }
        void unwatch(Id id) override { watches.erase(id); }
        Id startTimer(int, std::function<void()>) override { return *timers.insert(++next).first; }
        void stopTimer(Id id) override { timers.erase(id); }
    };
    EditorTest t;
    auto& engine = t.engine;
    const uint32_t track = engine.addTrack();
    const uint32_t effect = addTestPlugin(engine, engine.trackChain(track), "SUB Test Effect");
    auto kept = std::make_shared<Kept>();
    sub::setEditorEventLoop(kept);
    REQUIRE(engine.openEditor(effect, 0, "Kept"));
    CHECK(!kept->watches.empty());  // the window's connection, the plug-in's
    CHECK(!kept->timers.empty());
    sub::setEditorEventLoop(nullptr);  // (the application's goes: the bridge did)
    engine.closeEditor(effect);
    CHECK(kept->watches.empty());
    CHECK(kept->timers.empty());
}

#endif  // SUBSTATION_HAS_X11_EDITORS
