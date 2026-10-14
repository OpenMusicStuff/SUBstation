// substation-plugin-host: a plug-in sandbox. The engine starts one (or several,
// as the plug-in hosting preference says: docs/engine/sandboxes.md) and runs
// plug-ins in it instead of in the application, so a plug-in that takes its
// time to load never holds up the application's window, and one that crashes
// takes down only its sandbox.
//
// It is started by the engine only: `--parent=<pid>`, `--doorbell=<name>` (its
// blocks to process: SandboxProtocol.h) and the channel to it (platform/Ipc.h). It ends when the engine says Quit, or goes.
//
// Links the engine's VST3 host (sub_engine) and nothing of Qt.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include "HostLoop.h"
#include "HostServer.h"
#include "platform/Ipc.h"

int main(int argc, char** argv) {
    int parent = 0;
    std::string doorbell;
    std::unique_ptr<sub::platform::Channel> channel;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--parent=", 0) == 0) parent = std::atoi(arg.c_str() + 9);
        else if (arg.rfind("--doorbell=", 0) == 0) doorbell = arg.substr(11);
        else if (sub::platform::isChannelArgument(arg)) channel = sub::platform::parentChannel(arg);
    }
    if (!channel || doorbell.empty()) {
        std::fprintf(stderr, "substation-plugin-host is started by SUBstation, to run plug-ins in.\n");
        return 2;
    }
    sub::platform::childProcessStarted(parent);
    auto loop = sub::pluginhost::HostLoop::create();
    int code = 0;
    try {
        // Never destroyed: its audio workers are let go, not joined (one may be stuck in a
        // plug-in), and may still look at it until the process ends.
        auto* server = new sub::pluginhost::HostServer(std::move(channel), *loop, doorbell);
        code = server->run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "substation-plugin-host: %s\n", error.what());
        code = 2;
    }
    loop.reset();
    std::fflush(nullptr);
    std::_Exit(code);  // (the reader thread may still be waiting on the channel)
}
