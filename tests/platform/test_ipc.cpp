// Ipc.h: a child process and its channel, shared memory, and the wake-up
// between processes, with a child of the tests' own (child/ipc_child.cpp).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <thread>

#include "harness/Test.h"
#include "platform/Ipc.h"

using namespace sub::platform;

namespace {

const std::string kChild = SUBSTATION_IPC_CHILD;

// Reads exactly `size` bytes (false if the channel closed first).
bool readAll(Channel& channel, std::string& out, size_t size) {
    out.clear();
    char buffer[256];
    while (out.size() < size) {
        const size_t n = channel.read(buffer, std::min(sizeof buffer, size - out.size()));
        if (n == 0) return false;
        out.append(buffer, n);
    }
    return true;
}

struct Pings {
    std::atomic<uint32_t> request{0};
    std::atomic<uint32_t> answer{0};
    uint32_t payload = 0;
};

}  // namespace

TEST_CASE("a child process's channel carries bytes both ways, and ends with it") {
    auto child = ChildProcess::start(kChild, {"echo"});
    REQUIRE(child != nullptr);
    CHECK(child->pid() > 0 && child->pid() != currentProcessId());
    CHECK(child->running());
    const std::string big(100000, 'x');  // (more than a pipe holds at once: the child reads as we write)
    std::thread writer([&] { CHECK(child->channel().write(big.data(), big.size())); });
    std::string back;
    CHECK(readAll(child->channel(), back, big.size()));
    writer.join();
    CHECK(back == big);
    // Closed: the child reads its end and goes.
    child->channel().close();
    CHECK(child->wait(5000));
    CHECK(!child->running());
}

TEST_CASE("a killed child's channel ends: a blocked read returns, writes fail") {
    auto child = ChildProcess::start(kChild, {"wait"});
    std::atomic<bool> ended{false};
    std::thread reader([&] {
        char c;
        CHECK_EQ(child->channel().read(&c, 1), size_t(0));
        ended = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!ended);
    child->kill();
    CHECK(child->wait(5000));
    reader.join();
    CHECK(ended);
    const char c = 'x';
    bool failed = false;
    for (int i = 0; i < 100 && !failed; ++i) failed = !child->channel().write(&c, 1);  // (until the system has noticed)
    CHECK(failed);
}

TEST_CASE("a program that isn't there doesn't start") {
    CHECK_THROWS_MATCHING(ChildProcess::start(kChild + ".missing", {}), std::runtime_error, "Could not start");
}

TEST_CASE("the channel argument is the child's only") {
    CHECK(isChannelArgument("--channel=3"));
    CHECK(!isChannelArgument("--chan"));
    CHECK(parentChannel("echo") == nullptr);
}

TEST_CASE("shared memory and wake-ups between processes") {
    const std::string name = uniqueIpcName("t");
    auto memory = SharedMemory::create(name, sizeof(Pings));
    auto* pings = new (memory->data()) Pings;
    auto request = IpcSignal::create(name + "q", pings->request);
    auto answer = IpcSignal::create(name + "a", pings->answer);
    constexpr uint32_t kCount = 2000;
    auto child = ChildProcess::start(kChild, {"ping", name, std::to_string(kCount)});
    std::string ready;
    REQUIRE(readAll(child->channel(), ready, 1));
    memory->forgetName();  // (it has it: nothing is left behind should the test die)
    const auto started = std::chrono::steady_clock::now();
    for (uint32_t i = 1; i <= kCount; ++i) {
        pings->payload = i;
        pings->request.store(i, std::memory_order_release);
        request->notify();
        while (pings->answer.load(std::memory_order_acquire) != i) {
            REQUIRE(answer->wait(i - 1, 5000000));
        }
        REQUIRE(pings->payload == i * 2);
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    std::printf("    %.1f us a round trip\n", us / kCount);
    CHECK(child->wait(5000));
}

TEST_CASE("a wake-up wait ends when its time is up, or at once if the counter moved") {
    const std::string name = uniqueIpcName("w");
    auto memory = SharedMemory::create(name, sizeof(Pings));
    auto* pings = new (memory->data()) Pings;
    auto signal = IpcSignal::create(name + "q", pings->request);
    const auto started = std::chrono::steady_clock::now();
    CHECK(!signal->wait(0, 20000));
    const auto waited = std::chrono::steady_clock::now() - started;
    CHECK(waited >= std::chrono::milliseconds(15));
    CHECK(waited < std::chrono::seconds(2));
    pings->request.store(1);
    CHECK(signal->wait(0, 20000));
    // Another thread's change wakes a wait without limit.
    std::thread later([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pings->request.store(2, std::memory_order_release);
        signal->notify();
    });
    while (pings->request.load() != 2) signal->wait(1, -1);
    later.join();
    CHECK_EQ(pings->request.load(), uint32_t(2));
}

TEST_CASE("shared memory: opened by name as made, zeroed; gone once forgotten") {
    const std::string name = uniqueIpcName("m");
    auto made = SharedMemory::create(name, 4096);
    auto* bytes = static_cast<unsigned char*>(made->data());
    bool zero = true;
    for (size_t i = 0; i < 4096; ++i) zero = zero && bytes[i] == 0;
    CHECK(zero);
    bytes[100] = 42;
    auto opened = SharedMemory::open(name, 4096);
    CHECK_EQ(int(static_cast<unsigned char*>(opened->data())[100]), 42);
    made->forgetName();
#ifndef _WIN32  // (on Windows a mapping's name goes with its last handle: the open one keeps it)
    CHECK_THROWS_MATCHING(SharedMemory::open(name, 4096), std::runtime_error, "shm_open");
#endif
    CHECK_EQ(int(static_cast<unsigned char*>(opened->data())[100]), 42);
}
