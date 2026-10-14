// Working with processes of our own: a child process started with a private
// channel to it, memory both processes map, and a wake-up one process gives
// another through a counter in that memory. The engine's plug-in sandboxes
// (engine/src/plugins/sandbox) are made of these: a plug-in host process, its
// messages on the channel, each plug-in's audio in shared memory.
//
// Each system's code is a file of its own: IpcWin32.cpp (pipes, a job object,
// named file mappings and events), IpcPosix.cpp (a socket pair, posix_spawn,
// shm_open; on Linux futexes, elsewhere a polled wait).

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sub::platform {

int currentProcessId();

// A byte stream between two processes, both ways. Reads and writes block. When
// either side closes it (or its process ends), read() returns 0 and write()
// false, on both sides. Reading and writing may happen on two threads at once
// (one reader, and writers taking turns: the caller serialises them).
class Channel {
public:
    virtual ~Channel() = default;
    // Writes all `size` bytes; false if the channel is closed.
    virtual bool write(const void* data, size_t size) = 0;
    // Reads at least one byte and at most `size`; 0 once the channel is closed.
    virtual size_t read(void* data, size_t size) = 0;
    // Closes this side for writing: the other side reads its end. Where the
    // system allows it, a read blocked on another thread of this process ends
    // too (POSIX); on Windows it ends when the other process does.
    virtual void close() = 0;
};

// A process started from this one, with a Channel to it. It ends with this
// object (killed if it hasn't ended by itself), and with this process: on
// Windows it is in a job that is closed when we go; on Linux it asks to be
// killed when its parent dies (childProcessStarted()).
class ChildProcess {
public:
    // Starts `program` with `arguments`, and an argument of its own after them
    // ("--channel=..."), which the child hands to parentChannel(). Throws
    // std::runtime_error with the reason if it can't be started.
    static std::unique_ptr<ChildProcess> start(const std::string& program, const std::vector<std::string>& arguments);

    virtual ~ChildProcess() = default;
    virtual Channel& channel() = 0;
    virtual int pid() const = 0;
    // Whether it is still running (it may have ended, or crashed, by itself).
    virtual bool running() = 0;
    virtual void kill() = 0;
    // Waits at most `milliseconds` for it to end; true if it has.
    virtual bool wait(int milliseconds) = 0;
    // Lets it bring a window of its own to the front now (Windows only lets the
    // process the user is working in do that); nothing elsewhere.
    virtual void allowForeground() = 0;
};

// In a child process: its channel to the parent, from the argument the parent
// gave it ("--channel=..."); null if `argument` isn't one.
std::unique_ptr<Channel> parentChannel(const std::string& argument);
// Whether `argument` is the channel's.
bool isChannelArgument(const std::string& argument);

// In a child process, first thing: it ends when the parent `parentPid` does,
// even if it is stuck (Linux: the parent-death signal; Windows: the parent's
// job does it); and a crash ends it quietly, without the system's "stopped
// working" dialog (Windows), so the parent hears of it at once.
void childProcessStarted(int parentPid);

// Memory mapped by several processes, by name. The one that creates it removes
// the name when it goes (or earlier: forgetName()); those that opened it keep
// their mapping. Zeroed when created.
class SharedMemory {
public:
    // Throws std::runtime_error if it can't be made (a name in use: an old one
    // of a process that died is replaced).
    static std::unique_ptr<SharedMemory> create(const std::string& name, size_t size);
    // Throws std::runtime_error if there is none of that name and size.
    static std::unique_ptr<SharedMemory> open(const std::string& name, size_t size);

    virtual ~SharedMemory() = default;
    virtual void* data() const = 0;
    virtual size_t size() const = 0;
    // The creator: no other process will open it any more (they have): its name
    // goes now, and nothing is left behind should this process die.
    virtual void forgetName() = 0;
};

// A unique name for shared memory and signals: this process's id and a counter,
// then `what`, a few characters at most ("p"; macOS takes 31 in all).
std::string uniqueIpcName(const std::string& what);

// A wake-up between processes, on a 32-bit counter in shared memory: one side
// changes the counter, then notify(); the other waits while it holds the value
// it last saw. (Linux: a futex on the counter itself; Windows: a named
// auto-reset event beside it; elsewhere a polled wait.) notify() and wait()
// neither allocate nor lock: the audio thread uses them.
class IpcSignal {
public:
    // `word` lives in memory both processes map, as long as this object.
    // create() makes the system's object named `name` where it needs one; open()
    // finds it. Both throw std::runtime_error on failure.
    static std::unique_ptr<IpcSignal> create(const std::string& name, std::atomic<uint32_t>& word);
    static std::unique_ptr<IpcSignal> open(const std::string& name, std::atomic<uint32_t>& word);

    virtual ~IpcSignal() = default;
    // Wakes whoever waits (after the counter changed).
    virtual void notify() noexcept = 0;
    // Waits while the counter is `seen`, at most `timeoutMicroseconds` (< 0:
    // without limit). True if it changed. It may return early when woken for
    // nothing: callers loop.
    virtual bool wait(uint32_t seen, int64_t timeoutMicroseconds) noexcept = 0;
};

static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t) && std::atomic<uint32_t>::is_always_lock_free,
              "shared counters are plain 32-bit words, usable by both processes");

}  // namespace sub::platform
