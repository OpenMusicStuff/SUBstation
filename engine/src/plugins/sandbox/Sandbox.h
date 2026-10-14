#pragma once
// One plug-in sandbox, as the engine sees it: a plug-in host process
// (substation-plugin-host), its channel, and the plug-ins in it (each a
// SandboxClient: SandboxedProcessor). See docs/engine/sandboxes.md.
//
// A thread of its own reads what the host says: replies go to whoever waits
// for them (call()), or else to the plug-in they are about, like everything
// about a plug-in (SandboxClient::received()); its editors' keys and the like
// are kept for the main thread. When the host ends (it crashed, or was killed),
// every plug-in in it hears so (sandboxEnded()), the rendering thread waiting on
// one too: nothing waits for a sandbox that is gone.
//
// Messages go out from the main thread (any thread may send: they take turns).
// Blocks to process go on its doorbell (SandboxProtocol.h), from the rendering
// threads (ring()): each plug-in in it has a slot there (takeSlot()).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "platform/Ipc.h"
#include "plugins/sandbox/SandboxProtocol.h"

namespace sub::sandbox {

// A plug-in in a sandbox, as the sandbox's reader thread hands it what comes.
class SandboxClient {
public:
    virtual ~SandboxClient() = default;
    // Reader thread: a message about it (a reply nobody waits for too). Quick: keep it for the main thread.
    virtual void received(Message message) = 0;
    // Reader thread: its sandbox ended.
    virtual void sandboxEnded() = 0;
};

struct Reply {
    bool ok = false;
    std::string data;  // the text or state asked for, or why not
};

// A key pressed in an editor that the application takes (Kind::Key).
struct SandboxKey {
    uint32_t key = 0;
    int modifiers = 0;
    bool repeat = false;
    bool textField = false;
};

class Sandbox {
public:
    // Starts the host `program`. Throws std::runtime_error if it can't be started.
    Sandbox(std::string key, std::string label, const std::string& program);
    // Kills it if it is still running (retire() first to let it go by itself).
    ~Sandbox();
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    const std::string& key() const { return key_; }      // what the hosting mode groups by
    const std::string& label() const { return label_; }  // for the user: "All plug-ins", a vendor, a plug-in
    int pid() const;
    // Whether its process still runs (or said it can't serve us: another version).
    bool alive() const { return alive_.load(std::memory_order_acquire); }
    // Why it ended, after its label ("stopped"; "" while it runs).
    std::string ending() const;
    // Whether its main thread is loading a plug-in (it answers once that is done).
    bool busy() const { return busy_.load(std::memory_order_acquire); }

    // Ids unique among every sandbox's (a plug-in loading again elsewhere is never taken for what it was).
    static uint32_t newInstance();
    void attach(uint32_t instance, SandboxClient* client);
    void detach(uint32_t instance);  // (after which the client hears nothing more)
    size_t clients() const;

    // A plug-in's slot on the doorbell, while it is in this sandbox. Throws
    // std::runtime_error if every one is taken (kMaxSlots plug-ins).
    uint32_t takeSlot();
    void releaseSlot(uint32_t slot);
    // Real-time: the block of the plug-in in `slot` is to be processed (its
    // `request` moved). False if it couldn't be queued (the memory trampled).
    bool ring(uint32_t slot) noexcept;

    // A request id for a message whose reply goes to the plug-in it is about.
    static uint32_t newRequest();
    // Sends a message. False if the sandbox is gone.
    bool send(const Message& message);
    // Sends a request and waits at most `timeoutMs` for the reply; none if it didn't come
    // (the sandbox is gone, or busy). A reply that comes later goes to the plug-in it is about.
    std::optional<Reply> call(Message message, int timeoutMs);

    // Editors: the keys taken from them since the last call, and when one last came to the front
    // (a count that goes up across every sandbox: the highest is the foremost's).
    std::vector<SandboxKey> takeKeys();
    uint64_t raised() const { return raised_.load(std::memory_order_acquire); }
    void allowForeground();

    // Asks it to quit, and lets it go: once it has ended (finished()) it can be destroyed
    // without waiting. Every plug-in should have left it.
    void retire();
    bool finished();  // it ended (by itself, or killed)

    // Shared by every sandbox of an engine: the count raised() takes from.
    static std::atomic<uint64_t>& raiseCounter();

private:
    void readLoop();
    void ended(const std::string& why);

    const std::string key_;
    const std::string label_;
    std::unique_ptr<platform::SharedMemory> doorbellMemory_;
    Doorbell* doorbell_ = nullptr;
    std::unique_ptr<platform::IpcSignal> ringSignal_;
    std::unique_ptr<platform::ChildProcess> child_;
    std::thread reader_;
    std::atomic<bool> alive_{true};
    std::atomic<bool> busy_{false};
    std::atomic<uint64_t> raised_{0};

    std::mutex sendMutex_;

    mutable std::mutex mutex_;  // what follows
    std::condition_variable replied_;
    std::map<uint32_t, SandboxClient*> clients_;
    struct Waiter {
        bool done = false;
        Reply reply;
    };
    std::map<uint32_t, std::shared_ptr<Waiter>> waiting_;  // request -> who waits for it
    std::vector<SandboxKey> keys_;
    std::string ending_;
    std::vector<uint32_t> freeSlots_;  // (the lowest last)
    uint32_t nextSlot_ = 0;            // slots never taken start here
};

}  // namespace sub::sandbox
