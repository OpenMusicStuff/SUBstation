// Editors of plug-ins in sandboxes (plugins/sandbox), on Linux: X11 windows of
// the sandbox's process, on an X server of the test's own (harness/XDisplay.h).
// The editor opens there, and the engine hears of it (EditorOpened) and of the
// user closing it (EditorClosed); the keys the application takes (its key table)
// reach it instead of the plug-in; the foremost editor of all the sandboxes is
// the one Ctrl+W closes. Skipped where the engine was built without X11 editors,
// or Xvfb isn't installed.

#if SUBSTATION_HAS_X11_EDITORS && !SUBSTATION_NO_EDITOR_TESTS

#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>

#include "EditorHost.h"
#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/XDisplay.h"

using namespace subtest;

namespace {

using EventType = sub::ProcessorEvent::Type;

constexpr uint32_t XK_space = 0x0020, XK_a = 0x0061, XK_w = 0x0077;
constexpr uint32_t XK_Control_L = 0xffe3;

// The X server, then the engine, its plug-ins in sandboxes of their own (each editor a process's).
struct SandboxEditorTest {
    XDisplay display;
    sub::Engine engine;
    SandboxEditorTest() {
        if (!display.running()) SKIP(display.whyNot());
        requireTestPlugins();
        engine.setPluginHostProgram(SUBSTATION_PLUGIN_HOST);
        engine.setPluginHosting(sub::PluginHosting::Individually);
    }
};

uint32_t addEffect(sub::Engine& engine, uint32_t chain) {
    return engine.addPluginProcessor(chain, "VST3", testPluginsBundle(), testPluginUids().at("SUB Test Effect"), -1,
                                     {"SUB Test Effect", "SUBstation"});
}

bool pumpUntil(sub::Engine& engine, const std::function<bool()>& done, double seconds = 10.0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        engine.idle();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

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
        return editor.view != 0 && x.mapped(editor.view);
    });
    return editor;
}

std::string keysOf(XClient& x, const Editor& editor) { return x.property(editor.view, x.atom("SUB_TEST_KEYS")); }

}  // namespace

TEST_CASE("sandbox x11: an editor opens in its sandbox, and closes as the user closes it") {
    SandboxEditorTest t;
    auto& engine = t.engine;
    const uint32_t effect = addEffect(engine, engine.trackChain(engine.addTrack()));
    XClient x;
    REQUIRE(x.ok());
    REQUIRE(engine.openEditor(effect, 0, "Sandboxed"));  // (asked for as it loads: it opens once loaded)
    const Editor editor = findEditor(engine, x, "Sandboxed");
    REQUIRE(editor.view);
    std::vector<sub::ProcessorEventRecord> events;
    CHECK(pumpUntil(engine, [&] {
        for (const auto& e : engine.takeProcessorEvents()) events.push_back(e);
        return engine.isEditorOpen(effect);
    }));
    CHECK(std::any_of(events.begin(), events.end(), [](const auto& e) { return e.type == EventType::EditorOpened; }));
    CHECK(!sub::closeForemostEditor());  // (not one of this process's windows)
    engine.setEditorTitle(effect, "Renamed");
    CHECK(pumpUntil(engine, [&] { return x.title(editor.frame) == "Renamed"; }));
    // Hidden, and shown again.
    CHECK(engine.setEditorVisible(effect, false));
    CHECK(pumpUntil(engine, [&] { return !x.mapped(editor.frame); }));
    CHECK(!engine.isEditorOpen(effect));
    CHECK(engine.setEditorVisible(effect, true));
    CHECK(pumpUntil(engine, [&] { return x.mapped(editor.frame); }));
    // The user closes it.
    x.close(editor.frame);
    events.clear();
    CHECK(pumpUntil(engine, [&] {
        for (const auto& e : engine.takeProcessorEvents()) events.push_back(e);
        return std::any_of(events.begin(), events.end(), [](const auto& e) { return e.type == EventType::EditorClosed; });
    }));
    CHECK(!engine.isEditorOpen(effect));
}

TEST_CASE("sandbox x11: the application's keys go to it, the rest to the plug-in") {
    SandboxEditorTest t;
    auto& engine = t.engine;
    engine.setSandboxEditorKeys({{XK_space, 0}, {XK_w, sub::EditorKey::kControl}});
    const uint32_t effect = addEffect(engine, engine.trackChain(engine.addTrack()));
    XClient x;
    REQUIRE(x.ok());
    REQUIRE(engine.openEditor(effect, 0, "Keys"));
    const Editor editor = findEditor(engine, x, "Keys");
    REQUIRE(editor.view);
    x.focus(editor.view);
    const auto pluginGot = [&](uint32_t keysym) {
        return keysOf(x, editor).find(std::to_string(x.keycode(keysym)) + ":") != std::string::npos;
    };
    std::vector<sub::SandboxEditorKey> taken;
    const auto take = [&] {
        for (const auto& key : engine.takeSandboxEditorKeys()) taken.push_back(key);
    };
    x.type(XK_a);
    CHECK(pumpUntil(engine, [&] { return pluginGot(XK_a); }));
    x.type(XK_space);
    CHECK(pumpUntil(engine, [&] {
        take();
        return !taken.empty();
    }));
    REQUIRE(taken.size() == 1);
    CHECK_EQ(taken[0].key, XK_space);
    CHECK_EQ(taken[0].modifiers, 0);
    CHECK(!taken[0].repeat);
    x.type(XK_w, {XK_Control_L});
    CHECK(pumpUntil(engine, [&] {
        take();
        return taken.size() == 2;
    }));
    REQUIRE(taken.size() == 2);
    CHECK_EQ(taken[1].modifiers, int(sub::EditorKey::kControl));
    x.sync();
    CHECK(!pluginGot(XK_space));
    CHECK(!pluginGot(XK_w));
}

TEST_CASE("sandbox x11: the foremost editor of all the sandboxes' closes") {
    SandboxEditorTest t;
    auto& engine = t.engine;
    const uint32_t chain = engine.trackChain(engine.addTrack());
    const uint32_t first = addEffect(engine, chain);
    const uint32_t second = addEffect(engine, chain);
    XClient x;
    REQUIRE(x.ok());
    CHECK(!engine.closeForemostSandboxEditor());
    REQUIRE(engine.openEditor(first, 0, "One"));
    const Editor one = findEditor(engine, x, "One");
    REQUIRE(one.frame);
    // Open as the engine has it: its sandbox said so after it said the editor was raised (on the same channel),
    // so One was raised before Two is. (Its window can show before that message has gone.)
    CHECK(pumpUntil(engine, [&] { return engine.isEditorOpen(first); }));
    REQUIRE(engine.openEditor(second, 0, "Two"));
    REQUIRE(findEditor(engine, x, "Two").frame);
    CHECK(pumpUntil(engine, [&] { return engine.isEditorOpen(first) && engine.isEditorOpen(second); }));
    CHECK(engine.processorInfo(first).sandboxPid != engine.processorInfo(second).sandboxPid);
    // The last opened (in another process than the first)...
    CHECK(engine.closeForemostSandboxEditor());
    CHECK(pumpUntil(engine, [&] { return !engine.isEditorOpen(second); }));
    CHECK(engine.isEditorOpen(first));
    // ...then the one left.
    CHECK(engine.closeForemostSandboxEditor());
    CHECK(pumpUntil(engine, [&] { return !engine.isEditorOpen(first); }));
    CHECK(!engine.closeForemostSandboxEditor());
}

#endif
