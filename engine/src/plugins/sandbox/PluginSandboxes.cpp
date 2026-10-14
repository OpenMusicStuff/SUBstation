#include "plugins/sandbox/PluginSandboxes.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace sub::sandbox {

namespace {

constexpr int64_t kKillRetiredAfterMs = 3000;  // a sandbox asked to quit that is still there then is killed
constexpr int kQuitWaitMs = 2000;              // what the engine gives its sandboxes to quit when it goes

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string lowered(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

Message keysMessage(const std::vector<std::pair<uint32_t, int>>& keys) {
    platform::ByteWriter out;
    out.u32(static_cast<uint32_t>(keys.size()));
    for (const auto& [key, modifiers] : keys) {
        out.u32(key);
        out.u32(static_cast<uint32_t>(modifiers));
    }
    return {Kind::SetKeys, 0, 0, std::move(out.bytes)};
}

Message holdMessage(bool held) {
    platform::ByteWriter out;
    out.u8(held ? 1 : 0);
    return {Kind::HoldIdle, 0, 0, std::move(out.bytes)};
}

}  // namespace

PluginSandboxes::~PluginSandboxes() {
    std::vector<std::shared_ptr<Sandbox>> all;
    {
        std::lock_guard lock(mutex_);
        for (const Held& held : all_) all.push_back(held.sandbox);
        all_.clear();
        current_.clear();
    }
    for (const auto& sandbox : all) sandbox->retire();
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(kQuitWaitMs);
    for (const auto& sandbox : all) {
        while (!sandbox->finished() && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    // (Those still running are killed as they go.)
}

void PluginSandboxes::setProgram(std::string program) {
    std::lock_guard lock(mutex_);
    program_ = std::move(program);
}

void PluginSandboxes::setHosting(PluginHosting hosting) {
    std::lock_guard lock(mutex_);
    hosting_ = hosting;
}

PluginHosting PluginSandboxes::hosting() const {
    std::lock_guard lock(mutex_);
    return hosting_;
}

void PluginSandboxes::setRetireDelay(int milliseconds) {
    std::lock_guard lock(mutex_);
    retireDelayMs_ = std::max(0, milliseconds);
}

std::string PluginSandboxes::keyFor(const PluginOrigin& origin, std::string& label) {
    const std::string name = origin.name.empty() ? origin.uid : origin.name;
    switch (hosting_) {
        case PluginHosting::ByManufacturer:
            label = origin.vendor.empty() ? "Unknown maker" : origin.vendor;
            return "vendor:" + lowered(label);
        case PluginHosting::ByPlugin:
            label = name;
            return "plugin:" + lowered(origin.uid);
        case PluginHosting::Individually:
            label = name;
            return "instance:" + std::to_string(++nextInstanceKey_);
        case PluginHosting::InProcess:
        case PluginHosting::Together: break;
    }
    label = "All plug-ins";
    return "all";
}

std::shared_ptr<Sandbox> PluginSandboxes::sandboxFor(const PluginOrigin& origin) {
    std::lock_guard lock(mutex_);
    if (program_.empty()) throw std::runtime_error("There is no plug-in host to run it in.");
    std::string label;
    const std::string key = keyFor(origin, label);
    if (const auto it = current_.find(key); it != current_.end() && it->second->alive()) return it->second;
    auto sandbox = std::make_shared<Sandbox>(key, label, program_);
    configure(*sandbox);
    current_[key] = sandbox;
    all_.push_back({sandbox});
    return sandbox;
}

void PluginSandboxes::configure(Sandbox& sandbox) {
    if (!keys_.empty()) sandbox.send(keysMessage(keys_));
    if (idleHeld_) sandbox.send(holdMessage(true));
}

void PluginSandboxes::idle() {
    std::vector<std::shared_ptr<Sandbox>> gone;  // (destroyed outside the lock)
    {
        std::lock_guard lock(mutex_);
        const int64_t now = nowMs();
        for (Held& held : all_) {
            if (held.retiredAt >= 0) continue;
            const auto current = current_.find(held.sandbox->key());
            const bool isCurrent = current != current_.end() && current->second == held.sandbox;
            const bool used = held.sandbox.use_count() > (isCurrent ? 2 : 1);
            if (used) {
                held.unusedSince = -1;
                continue;
            }
            if (held.unusedSince < 0) held.unusedSince = now;
            if (held.sandbox->alive() && now - held.unusedSince < retireDelayMs_) continue;
            if (isCurrent) current_.erase(current);
            held.sandbox->retire();
            held.retiredAt = now;
        }
        for (auto it = all_.begin(); it != all_.end();) {
            const bool done = it->retiredAt >= 0 && it->sandbox.use_count() == 1 &&
                              (it->sandbox->finished() || now - it->retiredAt >= kKillRetiredAfterMs);
            if (!done) {
                ++it;
                continue;
            }
            gone.push_back(std::move(it->sandbox));
            it = all_.erase(it);
        }
    }
}

std::vector<SandboxInfo> PluginSandboxes::sandboxes() const {
    std::lock_guard lock(mutex_);
    std::vector<SandboxInfo> list;
    for (const Held& held : all_) {
        if (held.retiredAt >= 0) continue;
        list.push_back({held.sandbox->label(), held.sandbox->pid(), held.sandbox->alive(), held.sandbox->clients()});
    }
    return list;
}

void PluginSandboxes::setEditorKeys(std::vector<std::pair<uint32_t, int>> keys) {
    std::lock_guard lock(mutex_);
    keys_ = std::move(keys);
    const Message message = keysMessage(keys_);
    for (const Held& held : all_) {
        if (held.retiredAt < 0) held.sandbox->send(message);
    }
}

std::vector<SandboxKey> PluginSandboxes::takeEditorKeys() {
    std::lock_guard lock(mutex_);
    std::vector<SandboxKey> keys;
    for (const Held& held : all_) {
        for (const SandboxKey& key : held.sandbox->takeKeys()) keys.push_back(key);
    }
    return keys;
}

bool PluginSandboxes::closeForemostEditor() {
    std::vector<std::shared_ptr<Sandbox>> raised;
    {
        std::lock_guard lock(mutex_);
        for (const Held& held : all_) {
            if (held.retiredAt < 0 && held.sandbox->alive() && held.sandbox->raised() > 0) raised.push_back(held.sandbox);
        }
    }
    std::sort(raised.begin(), raised.end(), [](const auto& a, const auto& b) { return a->raised() > b->raised(); });
    for (const auto& sandbox : raised) {
        const auto reply = sandbox->call({Kind::CloseForemostEditor}, 1000);
        if (reply && reply->ok) return true;
    }
    return false;
}

void PluginSandboxes::holdIdle(bool held) {
    std::lock_guard lock(mutex_);
    if (idleHeld_ == held) return;
    idleHeld_ = held;
    const Message message = holdMessage(held);
    for (const Held& entry : all_) {
        if (entry.retiredAt < 0) entry.sandbox->send(message);
    }
}

}  // namespace sub::sandbox
