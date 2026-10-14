// substation-plugin-host: a plug-in sandbox. The engine starts one (or several,
// as the plug-in hosting preference says: docs/engine/sandboxes.md) and runs
// plug-ins in it instead of in the application, so a plug-in that takes its
// time to load never holds up the application's window, and one that crashes
// takes down only its sandbox.
//
// It is started by the engine only: `--parent=<pid>` and the channel to it
// (platform/Ipc.h). It ends when the engine says Quit, or goes.
//
// Links the engine's VST3 host (sub_engine) and nothing of Qt.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "HostLoop.h"
#include "HostServer.h"
#include "platform/Ipc.h"

int main(int argc, char** argv) {
    int parent = 0;
    std::unique_ptr<sub::platform::Channel> channel;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--parent=", 0) == 0) parent = std::atoi(arg.c_str() + 9);
        else if (sub::platform::isChannelArgument(arg)) channel = sub::platform::parentChannel(arg);
    }
    if (!channel) {
        std::fprintf(stderr, "substation-plugin-host is started by SUBstation, to run plug-ins in.\n");
        return 2;
    }
    sub::platform::childProcessStarted(parent);
    auto loop = sub::pluginhost::HostLoop::create();
    int code = 0;
    {
        sub::pluginhost::HostServer server(std::move(channel), *loop);
        code = server.run();
    }
    loop.reset();
    std::fflush(nullptr);
    std::_Exit(code);  // (the reader thread may still be waiting on the channel)
}
