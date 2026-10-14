#include "plugins/sandbox/Sandbox.h"

#include <chrono>
#include <new>
#include <stdexcept>
#include <utility>

namespace sub::sandbox {

uint32_t Sandbox::newInstance() {
    static std::atomic<uint32_t> next{0};
    return ++next;
}

uint32_t Sandbox::newRequest() {
    static std::atomic<uint32_t> next{0};
    return ++next;
}

std::atomic<uint64_t>& Sandbox::raiseCounter() {
    static std::atomic<uint64_t> counter{0};
    return counter;
}

Sandbox::Sandbox(std::string key, std::string label, const std::string& program)
    : key_(std::move(key)), label_(std::move(label)) {
    const std::string doorbell = platform::uniqueIpcName("b");
    doorbellMemory_ = platform::SharedMemory::create(doorbell, sizeof(Doorbell));
    doorbell_ = new (doorbellMemory_->data()) Doorbell;
    ringSignal_ = platform::IpcSignal::create(ringSignalName(doorbell), doorbell_->rung);
    child_ = platform::ChildProcess::start(
        program, {"--parent=" + std::to_string(platform::currentProcessId()), "--doorbell=" + doorbell});
    reader_ = std::thread(&Sandbox::readLoop, this);
}

Sandbox::~Sandbox() {
    if (!finished()) {
        child_->kill();
        child_->wait(-1);
    }
    child_->channel().close();
    if (reader_.joinable()) reader_.join();
}

int Sandbox::pid() const { return child_->pid(); }

std::string Sandbox::ending() const {
    std::lock_guard lock(mutex_);
    return ending_;
}

void Sandbox::attach(uint32_t instance, SandboxClient* client) {
    bool gone = false;
    {
        std::lock_guard lock(mutex_);
        clients_[instance] = client;
        gone = !alive();
    }
    if (gone) client->sandboxEnded();
}

void Sandbox::detach(uint32_t instance) {
    std::lock_guard lock(mutex_);
    clients_.erase(instance);
}

size_t Sandbox::clients() const {
    std::lock_guard lock(mutex_);
    return clients_.size();
}

uint32_t Sandbox::takeSlot() {
    std::lock_guard lock(mutex_);
    if (!freeSlots_.empty()) {
        const uint32_t slot = freeSlots_.back();
        freeSlots_.pop_back();
        return slot;
    }
    if (nextSlot_ >= kMaxSlots) throw std::runtime_error("Its sandbox (" + label_ + ") holds as many plug-ins as it can.");
    return nextSlot_++;
}

void Sandbox::releaseSlot(uint32_t slot) {
    std::lock_guard lock(mutex_);
    freeSlots_.push_back(slot);
}

bool Sandbox::ring(uint32_t slot) noexcept {
    if (!doorbell_->push(slot)) return false;
    if (doorbell_->anyAsleep()) {
        doorbell_->rung.fetch_add(1, std::memory_order_seq_cst);
        ringSignal_->notify();
    }
    return true;
}

bool Sandbox::send(const Message& message) {
    if (!alive()) return false;
    std::lock_guard lock(sendMutex_);
    return writeMessage(child_->channel(), message);
}

std::optional<Reply> Sandbox::call(Message message, int timeoutMs) {
    message.request = newRequest();
    auto waiter = std::make_shared<Waiter>();
    {
        std::lock_guard lock(mutex_);
        if (!alive()) return std::nullopt;
        waiting_[message.request] = waiter;
    }
    if (!send(message)) {
        std::lock_guard lock(mutex_);
        waiting_.erase(message.request);
        return std::nullopt;
    }
    std::unique_lock lock(mutex_);
    replied_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return waiter->done || !alive(); });
    waiting_.erase(message.request);  // (a reply after this goes to the plug-in it is about)
    if (!waiter->done) return std::nullopt;
    return waiter->reply;
}

std::vector<SandboxKey> Sandbox::takeKeys() {
    std::lock_guard lock(mutex_);
    return std::exchange(keys_, {});
}

void Sandbox::allowForeground() { child_->allowForeground(); }

void Sandbox::retire() {
    send({Kind::Quit});
    // (Its channel stays open until it has gone: closing it would end the host before its plug-ins have.)
}

bool Sandbox::finished() { return !child_->running(); }

void Sandbox::readLoop() {
    Message message;
    while (readMessage(child_->channel(), message)) {
        switch (message.kind) {
            case Kind::Ready: {
                auto in = message.reader();
                const uint32_t version = in.u32();
                const uint32_t blockSize = in.u32();
                if (version != kVersion || blockSize != sizeof(Block)) {
                    ended("is another version of SUBstation's plug-in host");
                    child_->kill();
                    return;
                }
                doorbellMemory_->forgetName();  // (it has the doorbell: it opens it before it says it is ready)
                break;
            }
            case Kind::Busy: busy_.store(message.reader().u8() != 0, std::memory_order_release); break;
            case Kind::Key: {
                auto in = message.reader();
                SandboxKey key;
                key.key = in.u32();
                key.modifiers = static_cast<int>(in.u32());
                key.repeat = in.u8() != 0;
                key.textField = in.u8() != 0;
                std::lock_guard lock(mutex_);
                keys_.push_back(key);
                break;
            }
            case Kind::EditorRaised: raised_.store(++raiseCounter(), std::memory_order_release); break;
            case Kind::Reply: {
                std::lock_guard lock(mutex_);
                if (const auto it = waiting_.find(message.request); it != waiting_.end()) {
                    auto in = message.reader();
                    it->second->reply.ok = in.u8() != 0;
                    it->second->reply.data = in.str();
                    it->second->done = true;
                    replied_.notify_all();
                    break;
                }
                if (const auto it = clients_.find(message.instance); it != clients_.end()) {
                    it->second->received(std::move(message));
                }
                break;
            }
            default: {
                std::lock_guard lock(mutex_);
                if (const auto it = clients_.find(message.instance); it != clients_.end()) {
                    it->second->received(std::move(message));
                }
                break;
            }
        }
    }
    ended("stopped");
}

void Sandbox::ended(const std::string& why) {
    std::lock_guard lock(mutex_);
    if (!alive()) return;
    alive_.store(false, std::memory_order_release);
    busy_.store(false, std::memory_order_release);
    ending_ = why;
    replied_.notify_all();
    // (Under the lock: no client is detached meanwhile, and none is told twice.)
    for (const auto& [instance, client] : clients_) client->sandboxEnded();
}

}  // namespace sub::sandbox
