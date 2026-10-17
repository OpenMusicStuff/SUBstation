// The child process the platform tests start (Ipc.h): `ipc_child <mode> ...`.
//
//   echo                   sends back everything it reads, until the channel closes
//   wait                   reads until the channel closes, sends nothing
//   ping <memory> <count>  opens the shared memory <memory> (two counters, then a
//                          payload) and answers <count> pings: each time the
//                          first counter moves it doubles the payload, then moves
//                          the second counter to match
//
// Exits 0 when done, 2 on bad arguments.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "platform/Ipc.h"

using namespace sub::platform;

namespace {

struct Pings {
    std::atomic<uint32_t> request{0};
    std::atomic<uint32_t> answer{0};
    uint32_t payload = 0;
};

}  // namespace

int main(int argc, char** argv) {
    std::unique_ptr<Channel> channel;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (isChannelArgument(arg)) channel = parentChannel(arg);
        else args.push_back(arg);
    }
    if (!channel || args.empty()) return 2;
    if (args[0] == "echo") {
        char buffer[4096];
        while (const size_t n = channel->read(buffer, sizeof buffer)) {
            if (!channel->write(buffer, n)) break;
        }
        return 0;
    }
    if (args[0] == "wait") {
        char buffer[256];
        while (channel->read(buffer, sizeof buffer) > 0) {
        }
        return 0;
    }
    if (args[0] == "ping" && args.size() == 3) {
        auto memory = SharedMemory::open(args[1], sizeof(Pings));
        auto* pings = static_cast<Pings*>(memory->data());
        auto request = IpcSignal::open(args[1] + "q", pings->request);
        auto answer = IpcSignal::open(args[1] + "a", pings->answer);
        const auto count = static_cast<uint32_t>(std::strtoul(args[2].c_str(), nullptr, 10));
        const char ready = 'r';
        channel->write(&ready, 1);
        for (uint32_t seen = 0; seen < count;) {
            // (A wait may end for nothing, a late wake-up: loop until it moved, or 5 s: the parent is gone.)
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (pings->request.load(std::memory_order_acquire) == seen) {
                if (std::chrono::steady_clock::now() >= deadline) return 3;
                request->wait(seen, 100000);
            }
            seen = pings->request.load(std::memory_order_acquire);
            pings->payload *= 2;
            pings->answer.store(seen, std::memory_order_release);
            answer->notify();
        }
        return 0;
    }
    return 2;
}
