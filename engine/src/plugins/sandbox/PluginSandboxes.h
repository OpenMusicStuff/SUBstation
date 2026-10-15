#pragma once
// An engine's plug-in sandboxes: which one a plug-in goes to, as the hosting
// mode says (PluginHosting.h), started when first needed and let go when
// nothing has used them for a while. One a key: every plug-in (Together), its
// maker (ByManufacturer), its class (ByPlugin), or the instance itself
// (Individually). A sandbox that ended (it crashed) stays with the plug-ins that
// were in it, which say so, until they are loaded again: in a new one.
//
// Main thread (Engine::idle() runs it); thread-safe all the same.

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "PluginHosting.h"
#include "plugins/sandbox/Sandbox.h"

namespace sub::sandbox {

// Which plug-in, and who made it: what decides its sandbox.
struct PluginOrigin {
    std::string path;
    std::string uid;     // its class id (VST3: 32 hex digits; VST2: its unique id, 8)
    std::string name;    // as the scan found it (what it is called until it has loaded)
    std::string vendor;  // its maker, as the scan found it
    std::string format = "VST3";  // or "VST2"
};

struct SandboxInfo {
    std::string label;   // what it holds, for the user: "All plug-ins", a maker, a plug-in
    int pid = 0;
    bool running = false;
    size_t plugins = 0;  // in it now
};

class PluginSandboxes {
public:
    PluginSandboxes() = default;
    // Asks every sandbox to quit, waits a moment for them, and kills the rest.
    ~PluginSandboxes();
    PluginSandboxes(const PluginSandboxes&) = delete;
    PluginSandboxes& operator=(const PluginSandboxes&) = delete;

    // The host program (substation-plugin-host). Without one, no sandbox can start.
    void setProgram(std::string program);
    void setHosting(PluginHosting hosting);
    PluginHosting hosting() const;

    // The sandbox a plug-in goes to now, started if need be. Throws
    // std::runtime_error if none can be started.
    std::shared_ptr<Sandbox> sandboxFor(const PluginOrigin& origin);

    // Lets go of sandboxes nothing has used for a while (they quit), and of those that ended.
    void idle();
    // How long an unused sandbox stays (a plug-in deleted and its deletion undone doesn't start one again).
    void setRetireDelay(int milliseconds);
    // How long an offline render waits for a plug-in's block (a minute; for plug-ins loaded from then on).
    void setOfflineWait(int milliseconds);
    int offlineWaitMs() const;

    std::vector<SandboxInfo> sandboxes() const;

    // The keys (key, EditorKey modifiers) the application takes from editors: every sandbox's
    // editors hand them over (takeEditorKeys()) instead of to their plug-in.
    void setEditorKeys(std::vector<std::pair<uint32_t, int>> keys);
    std::vector<SandboxKey> takeEditorKeys();
    // Closes the foremost editor of all the sandboxes' (the one last raised); false if none is shown.
    bool closeForemostEditor();
    // No idle() for the plug-ins in sandboxes while a render runs in the background.
    void holdIdle(bool held);

private:
    std::string keyFor(const PluginOrigin& origin, std::string& label);
    void configure(Sandbox& sandbox);  // keys and holds, as they are now

    mutable std::mutex mutex_;
    std::string program_;
    PluginHosting hosting_ = PluginHosting::Together;
    std::map<std::string, std::shared_ptr<Sandbox>> current_;  // key -> the sandbox plug-ins go to now
    struct Held {
        std::shared_ptr<Sandbox> sandbox;
        int64_t unusedSince = -1;  // ms (steady clock) since nothing used it; -1: in use
        int64_t retiredAt = -1;    // ms when it was asked to quit; -1: it wasn't
    };
    std::vector<Held> all_;
    std::vector<std::pair<uint32_t, int>> keys_;
    bool idleHeld_ = false;
    uint64_t nextInstanceKey_ = 0;
    int retireDelayMs_ = 5000;
    int offlineWaitMs_ = 60000;
};

}  // namespace sub::sandbox
