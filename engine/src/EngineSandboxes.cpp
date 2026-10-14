// Where plug-ins run (PluginHosting.h): in this process, or in sandboxes
// (plugins/sandbox), and loading one again in its place. See
// docs/engine/sandboxes.md.

#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>
#include <thread>

#include "plugins/Vst3Format.h"
#include "plugins/sandbox/PluginSandboxes.h"
#include "plugins/sandbox/SandboxedProcessor.h"

namespace sub {

namespace {

ProcessorEventRecord engineEvent(ProcessorEvent::Type type, uint32_t processorId) {
    ProcessorEventRecord record;
    record.type = type;
    record.processorId = processorId;
    return record;
}

}  // namespace

std::shared_ptr<Processor> Engine::makePlugin(const ProcessorEntry::Plugin& plugin, const std::vector<uint8_t>& state,
                                              double sampleRate, std::string& problem) {
    if (pluginHosting() == PluginHosting::InProcess) {
        auto processor = vst3::Vst3Format::instance().instantiate(plugin.path, plugin.uid, sampleRate, Renderer::kMaxBlock);
        if (!state.empty()) {
            try {
                processor->setState(state);
            } catch (const std::exception& error) {
                problem = error.what();
            }
        }
        return processor;
    }
    return std::make_shared<sandbox::SandboxedProcessor>(
        *sandboxes_, sandbox::PluginOrigin{plugin.path, plugin.uid, plugin.identity.name, plugin.identity.vendor},
        state, sampleRate, Renderer::kMaxBlock);
}

void Engine::setPluginHosting(PluginHosting hosting) {
    std::lock_guard lock(mutex_);
    hosting_ = hosting;
    sandboxes_->setHosting(hosting);
}

PluginHosting Engine::pluginHosting() {
    std::lock_guard lock(mutex_);
    return hosting_;
}

void Engine::setPluginHostProgram(const std::string& path) { sandboxes_->setProgram(path); }

void Engine::reloadProcessor(uint32_t processorId) {
    std::shared_ptr<Processor> old;
    ProcessorEntry::Plugin plugin;
    double rate = 0.0;
    {
        std::lock_guard lock(mutex_);
        old = processorLocked(processorId);
        const auto& entry = processors_.at(processorId);
        if (!entry.plugin) throw std::invalid_argument("Device " + std::to_string(processorId) + " is not a plug-in");
        plugin = *entry.plugin;
        rate = sampleRate_;
    }
    // As it is now (one in a sandbox that ended, or still loading: as it was last known).
    std::vector<uint8_t> state;
    try {
        state = old->getState();
    } catch (const std::exception&) {
    }
    old->closeEditor();
    plugin.problem.clear();
    auto fresh = makePlugin(plugin, state, rate, plugin.problem);  // (throws: the old one stays)
    std::lock_guard lock(mutex_);
    const auto it = processors_.find(processorId);
    if (it == processors_.end() || it->second.processor != old) {  // it went meanwhile (a dialog's message loop)
        retireProcessorLocked(std::move(fresh));
        throw std::invalid_argument("Device " + std::to_string(processorId) + " went while it loaded again");
    }
    if (sampleRate_ != rate) fresh->prepare(sampleRate_, Renderer::kMaxBlock);
    fresh->setEnabled(old->isEnabled());
    auto& inserts = chainLocked(it->second.chainId).inserts;
    std::replace(inserts.begin(), inserts.end(), old, fresh);
    it->second.processor = fresh;
    it->second.plugin = plugin;
    retireProcessorLocked(std::move(old));
    rebuildSnapshotLocked();
    // Its parameters (and latency) are another processor's now: one in this process has them
    // already; one in a sandbox says Loaded when it has.
    engineEvents_.push_back(engineEvent(ProcessorEvent::Type::ParamInfoChanged, processorId));
    engineEvents_.push_back(engineEvent(ProcessorEvent::Type::LatencyChanged, processorId));
    if (!plugin.problem.empty()) engineEvents_.push_back(engineEvent(ProcessorEvent::Type::Problem, processorId));
}

void Engine::prioritizeProcessors(const std::vector<uint32_t>& processorIds) {
    // By sandbox, in the order given.
    std::vector<std::pair<std::shared_ptr<sandbox::Sandbox>, std::vector<uint32_t>>> bySandbox;
    {
        std::lock_guard lock(mutex_);
        for (const uint32_t id : processorIds) {
            const auto it = processors_.find(id);
            if (it == processors_.end()) continue;
            const auto* sandboxed = dynamic_cast<const sandbox::SandboxedProcessor*>(it->second.processor.get());
            if (!sandboxed || sandboxed->status() != sandbox::SandboxedProcessor::Status::Loading) continue;
            auto group = std::find_if(bySandbox.begin(), bySandbox.end(),
                                      [&](const auto& g) { return g.first == sandboxed->sandbox(); });
            if (group == bySandbox.end()) group = bySandbox.insert(bySandbox.end(), {sandboxed->sandbox(), {}});
            group->second.push_back(sandboxed->instance());
        }
    }
    for (const auto& [box, instances] : bySandbox) {
        platform::ByteWriter out;
        out.u32(static_cast<uint32_t>(instances.size()));
        for (const uint32_t instance : instances) out.u32(instance);
        box->send({sandbox::Kind::Prioritize, 0, 0, std::move(out.bytes)});
    }
}

size_t Engine::pluginsLoading() {
    std::lock_guard lock(mutex_);
    size_t loading = 0;
    for (const auto& [id, entry] : processors_) {
        const auto* sandboxed = dynamic_cast<const sandbox::SandboxedProcessor*>(entry.processor.get());
        if (sandboxed && sandboxed->status() == sandbox::SandboxedProcessor::Status::Loading) ++loading;
    }
    return loading;
}

bool Engine::waitForPlugins(int timeoutMs) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        idle();
        if (pluginsLoading() == 0) return true;
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

std::vector<PluginSandboxInfo> Engine::pluginSandboxes() {
    std::vector<PluginSandboxInfo> list;
    for (const sandbox::SandboxInfo& info : sandboxes_->sandboxes()) {
        list.push_back({info.label, info.pid, info.running, info.plugins});
    }
    return list;
}

void Engine::setSandboxRetireDelay(int milliseconds) { sandboxes_->setRetireDelay(milliseconds); }

void Engine::setSandboxOfflineWait(int milliseconds) { sandboxes_->setOfflineWait(milliseconds); }

void Engine::setSandboxEditorKeys(const std::vector<std::pair<uint32_t, int>>& keys) { sandboxes_->setEditorKeys(keys); }

std::vector<SandboxEditorKey> Engine::takeSandboxEditorKeys() {
    std::vector<SandboxEditorKey> keys;
    for (const sandbox::SandboxKey& key : sandboxes_->takeEditorKeys()) {
        keys.push_back({key.key, key.modifiers, key.repeat, key.textField});
    }
    return keys;
}

bool Engine::closeForemostSandboxEditor() { return sandboxes_->closeForemostEditor(); }

std::vector<int> Engine::sandboxProcessIds() {
    std::vector<int> pids;
    for (const sandbox::SandboxInfo& info : sandboxes_->sandboxes()) {
        if (info.running) pids.push_back(info.pid);
    }
    return pids;
}

}  // namespace sub
