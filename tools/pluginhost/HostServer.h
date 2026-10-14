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
// - Audio workers, a few for all the plug-ins (as many as are loaded, up to
//   half the cores): each takes the next slot off the doorbell (the engine's
//   queue of blocks to process, SandboxProtocol.h), processes that plug-in's
//   block, and says so. One of the idle ones spins briefly for the next block
//   of the same callback (where there is more than one plug-in: one alone has
//   its next block a callback later), the others sleep until rung: so a block rarely
//   waits for a thread to wake, and the system's real-time scheduling (MMCSS on
//   Windows, which takes a few dozen threads a process) covers every one. A
//   plug-in stuck in process() keeps its worker: the main thread starts
//   another when one has been in a block for long.

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
    // `doorbell`: the name of the sandbox's doorbell (its memory, and its wake-up beside it).
    // Throws std::runtime_error if it can't be opened.
    HostServer(std::unique_ptr<platform::Channel> channel, HostLoop& loop, const std::string& doorbell);
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
        uint32_t slot = 0;  // on the doorbell
        std::unique_ptr<platform::SharedMemory> memory;
        sandbox::Block* block = nullptr;
        std::unique_ptr<platform::IpcSignal> doneSignal;
        std::shared_ptr<Processor> processor;
        vst3::Vst3Processor* vst3 = nullptr;
        std::atomic<bool> processing{false};  // a worker is in its block (one at a time)
        bool inSlot = false;                  // the workers find it (slots_)
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

    // An audio worker: busySince is when it began its current block (steady clock, ns; 0: none).
    struct Worker {
        std::thread thread;
        std::atomic<int64_t> busySince{0};
    };
    void workerLoop(Worker& worker);
    void serve(uint32_t slot, Worker& worker);
    void wakeWorker();
    // As many workers as the plug-ins loaded need (up to the most), and one more
    // for each that is stuck in a block.
    void checkWorkers();
    static void processBlock(Instance& instance);

    std::unique_ptr<platform::Channel> channel_;
    HostLoop& loop_;
    std::mutex sendMutex_;

    std::mutex incomingMutex_;
    std::deque<sandbox::Message> incoming_;  // read, for the main thread
    std::atomic<bool> quitting_{false};

    std::unique_ptr<platform::SharedMemory> doorbellMemory_;
    sandbox::Doorbell* doorbell_ = nullptr;
    std::unique_ptr<platform::IpcSignal> ringSignal_;
    // The workers' way to the plug-ins: each slot's instance (null: none), and the
    // workers looking at it (a slot is emptied, then waited for until none is).
    std::unique_ptr<std::atomic<Instance*>[]> slots_;
    std::unique_ptr<std::atomic<uint32_t>[]> slotUsers_;
    std::vector<std::unique_ptr<Worker>> workers_;  // (the main thread's; never fewer)
    std::atomic<int> spinning_{0};                  // workers spinning for the next block
    std::atomic<bool> stopping_{false};
    std::atomic<int> inSlots_{0};  // instances in their slots (the main thread's to change)

    std::map<uint32_t, std::unique_ptr<Instance>> instances_;
    std::deque<uint32_t> loads_;  // instances waiting to load, in the order they load
    std::set<std::pair<uint32_t, int>> keys_;  // (key, modifiers) the application takes from editors
    bool idleHeld_ = false;
    std::chrono::steady_clock::time_point nextIdle_;
};

}  // namespace sub::pluginhost
