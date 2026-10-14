#pragma once
// Where plug-ins run (Engine::setPluginHosting): in the application's own
// process, or in sandboxes, processes of their own (plugins/sandbox), grouped
// one of four ways. A plug-in in a sandbox never holds up the application's
// window while it loads, and one that crashes takes down only its sandbox:
// the plug-ins there stop (they pass their input on) and can be loaded again.
// See docs/engine/sandboxes.md.

#include <cstdint>

namespace sub {

enum class PluginHosting : uint8_t {
    InProcess,       // in the application: the least memory and CPU, but a plug-in that crashes takes it down
    Together,        // every plug-in in one sandbox
    ByManufacturer,  // a sandbox for each plug-in maker (vendor)
    ByPlugin,        // a sandbox for each plug-in (its instances share it)
    Individually,    // a sandbox for each instance: the safest, and the most memory and CPU
};

}  // namespace sub
