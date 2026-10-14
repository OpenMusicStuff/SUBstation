#pragma once
// The plug-in host's work (substation-plugin-host: a plug-in sandbox, see
// docs/engine/sandboxes.md and engine/src/plugins/sandbox/SandboxProtocol.h).
//
// Threads:
// - The main thread is the plug-ins' UI thread, as VST3 requires: it loads
//   them, and makes every call to them but process(), runs their editors, and
//   idles them (restarts, parameter changes, their reports to the engine). It
//   works through the engine's messages in order, but loads, which wait while
//   anything else is there to do: a slow plug-in loading holds up only the
//   plug-ins of its own sandbox, and only between messages.
// - The reader thread reads the engine's messages into a queue for the main
//   thread, so the engine never waits to write one, however long a load
//   takes. When the engine goes (the channel ends) it ends the process.
// - Each loaded plug-in has an audio thread of its own: it waits on the plug-in's
//   block, processes it, and says so.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "HostLoop.h"
#include "Processor.h"
#include "platform/Ipc.h"
#include "plugins/sandbox/SandboxProtocol.h"

namespace sub::vst3 {
class Vst3Processor;
}

namespace sub::pluginhost {

class HostServer {
public:
    HostServer(std::unique_ptr<platform::Channel> channel, HostLoop& loop);
    ~HostServer();
    HostServer(const HostServer&) = delete;
    HostServer& operator=(const HostServer&) = delete;

    // Until the engine says Quit (or goes). The exit code.
    int run();

private:
    struct Instance {
        uint32_t id = 0;
        std::string path;
        std::string uid;
        std::string name;
        double sampleRate = 48000.0;
        int maxBlock = sandbox::kMaxFrames;
        std::vector<uint8_t> state;  // to restore as it loads
        std::unique_ptr<platform::SharedMemory> memory;
        sandbox::Block* block = nullptr;
        std::unique_ptr<platform::IpcSignal> requestSignal;
        std::unique_ptr<platform::IpcSignal> doneSignal;
        std::shared_ptr<Processor> processor;
        vst3::Vst3Processor* vst3 = nullptr;
        std::thread audio;
        std::atomic<bool> stop{false};
        std::atomic<bool> stopped{false};  // its audio thread has ended
        bool loaded = false;
        bool announced = false;              // it said it has loaded (Loaded)
        std::string failure;                 // why it didn't load ("" : it did, or hasn't yet)
        std::deque<sandbox::Message> waiting;  // what came for it before it began loading
        std::vector<float> sentValues;         // its values as the engine last heard them
    };

    void readLoop();
    void send(sandbox::Message message);
    void reply(const sandbox::Message& request, bool ok, std::string data = {});

    void handle(sandbox::Message message);
    void handleForInstance(Instance& instance, const sandbox::Message& message);
    void create(const sandbox::Message& message);
    void destroy(uint32_t id);
    void loadNext();
    std::deque<sandbox::Message> takeIncomingFor(uint32_t id);
    void idleAll();
    void report(Instance& instance);
    bool keyTaken(const KeyPress& key);

    static void audioLoop(Instance* instance);
    static void processBlock(Instance& instance);

    std::unique_ptr<platform::Channel> channel_;
    HostLoop& loop_;
    std::mutex sendMutex_;

    std::mutex incomingMutex_;
    std::deque<sandbox::Message> incoming_;  // read, for the main thread
    std::atomic<bool> quitting_{false};

    std::map<uint32_t, std::unique_ptr<Instance>> instances_;
    std::deque<uint32_t> loads_;  // instances waiting to load, in the order they load
    std::set<std::pair<uint32_t, int>> keys_;  // (key, modifiers) the application takes from editors
    bool idleHeld_ = false;
    std::chrono::steady_clock::time_point nextIdle_;
};

}  // namespace sub::pluginhost
