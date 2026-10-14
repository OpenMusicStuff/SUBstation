#include "HostServer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include "EditorHost.h"
#include "platform/Threads.h"
#include "plugins/Vst3Format.h"
#include "plugins/Vst3Processor.h"
#include "rt/RtUtils.h"

namespace sub::pluginhost {

using sandbox::Kind;
using sandbox::Message;

namespace {

constexpr auto kIdleInterval = std::chrono::milliseconds(30);  // as the engine bridge idles plug-ins (33 ms)

std::vector<float> valuesOf(const Processor& processor) {
    std::vector<float> values(processor.params().size());
    for (size_t i = 0; i < values.size(); ++i) values[i] = processor.getParam(static_cast<int>(i));
    return values;
}

}  // namespace

HostServer::HostServer(std::unique_ptr<platform::Channel> channel, HostLoop& loop)
    : channel_(std::move(channel)), loop_(loop) {}

HostServer::~HostServer() = default;

int HostServer::run() {
    {
        platform::ByteWriter out;
        out.u32(sandbox::kVersion);
        out.u32(static_cast<uint32_t>(sizeof(sandbox::Block)));
        out.u32(static_cast<uint32_t>(platform::currentProcessId()));
        send({Kind::Ready, 0, 0, std::move(out.bytes)});
    }
    std::thread(&HostServer::readLoop, this).detach();  // (it ends the process when the engine goes)
    loop_.setKeyFilter([this](const KeyPress& key) { return keyTaken(key); });
    setEditorRaisedHook([this] { send({Kind::EditorRaised}); });
    nextIdle_ = std::chrono::steady_clock::now();

    while (!quitting_) {
        std::deque<Message> batch;
        {
            std::lock_guard lock(incomingMutex_);
            batch.swap(incoming_);
        }
        for (Message& message : batch) {
            handle(std::move(message));
            if (quitting_) break;
        }
        if (quitting_) break;
        bool more;
        {
            std::lock_guard lock(incomingMutex_);
            more = !incoming_.empty();
        }
        // A load waits while anything else is there to do: then one, and the loop goes round again.
        if (!more && !loads_.empty()) loadNext();
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextIdle_) {
            if (!idleHeld_) idleAll();
            nextIdle_ = now + kIdleInterval;
        }
        {
            std::lock_guard lock(incomingMutex_);
            more = !incoming_.empty();
        }
        const auto untilIdle =
            std::chrono::duration_cast<std::chrono::milliseconds>(nextIdle_ - std::chrono::steady_clock::now()).count();
        loop_.wait(more || !loads_.empty() ? 0 : static_cast<int>(std::clamp<long long>(untilIdle, 0, 30)));
    }

    loop_.setKeyFilter(nullptr);
    setEditorRaisedHook(nullptr);
    std::vector<uint32_t> ids;
    for (const auto& [id, instance] : instances_) ids.push_back(id);
    for (const uint32_t id : ids) destroy(id);
    return 0;
}

void HostServer::readLoop() {
    Message message;
    bool quit = false;
    while (readMessage(*channel_, message)) {
        quit = quit || message.kind == Kind::Quit;
        {
            std::lock_guard lock(incomingMutex_);
            incoming_.push_back(std::move(message));
        }
        loop_.wake();
    }
    // The engine went (or closed the channel after Quit): nothing is left to do here, even
    // if the main thread is stuck in a plug-in. After Quit the main thread gets a moment to
    // let its plug-ins go.
    if (quit) std::this_thread::sleep_for(std::chrono::seconds(5));
    std::fflush(nullptr);
    std::_Exit(0);
}

void HostServer::send(Message message) {
    std::lock_guard lock(sendMutex_);
    writeMessage(*channel_, message);  // (a failure: the engine went, and the reader ends the process)
}

void HostServer::reply(const Message& request, bool ok, std::string data) {
    platform::ByteWriter out;
    out.u8(ok ? 1 : 0);
    out.str(data);
    send({Kind::Reply, request.instance, request.request, std::move(out.bytes)});
}

void HostServer::handle(Message message) {
    switch (message.kind) {
        case Kind::Create: create(message); return;
        case Kind::Destroy: destroy(message.instance); return;
        case Kind::Prioritize: {
            auto in = message.reader();
            const uint32_t count = in.u32();
            std::vector<uint32_t> first;
            for (uint32_t i = 0; i < count && in.ok(); ++i) first.push_back(in.u32());
            std::deque<uint32_t> order;
            for (const uint32_t id : first)
                if (std::find(loads_.begin(), loads_.end(), id) != loads_.end()) order.push_back(id);
            for (const uint32_t id : loads_)
                if (std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
            loads_ = std::move(order);
            return;
        }
        case Kind::SetKeys: {
            auto in = message.reader();
            const uint32_t count = in.u32();
            keys_.clear();
            for (uint32_t i = 0; i < count && in.ok(); ++i) {
                const uint32_t key = in.u32();
                const auto modifiers = static_cast<int>(in.u32());
                keys_.insert({key, modifiers});
            }
            return;
        }
        case Kind::HoldIdle: idleHeld_ = message.reader().u8() != 0; return;
        case Kind::CloseForemostEditor: reply(message, loop_.closeForemostEditor()); return;
        case Kind::Quit: quitting_ = true; return;
        default: break;
    }
    const auto found = instances_.find(message.instance);
    if (found == instances_.end()) {
        if (message.request) reply(message, false, "The plug-in is gone.");
        return;
    }
    Instance& instance = *found->second;
    if (!instance.failure.empty()) {
        if (message.request) reply(message, false, instance.failure);
        return;
    }
    if (!instance.loaded) {
        if (message.kind == Kind::Prepare) {  // (it loads at the latest rate)
            auto in = message.reader();
            instance.sampleRate = sandbox::readF64(in);
            instance.maxBlock = static_cast<int>(in.u32());
        } else {
            instance.waiting.push_back(std::move(message));
        }
        return;
    }
    try {
        handleForInstance(instance, message);
    } catch (const std::exception& error) {
        if (message.request) reply(message, false, error.what());
    } catch (...) {
        if (message.request) reply(message, false, "The plug-in failed.");
    }
}

void HostServer::handleForInstance(Instance& instance, const Message& message) {
    Processor& processor = *instance.processor;
    auto in = message.reader();
    switch (message.kind) {
        case Kind::Prepare: {
            instance.sampleRate = sandbox::readF64(in);
            instance.maxBlock = static_cast<int>(in.u32());
            processor.prepare(instance.sampleRate, instance.maxBlock);
            break;
        }
        case Kind::ResetOffline:
            processor.resetOffline();
            reply(message, true);
            break;
        case Kind::SetParam: {
            const auto index = static_cast<int>(in.u32());
            const float value = in.f32();
            if (instance.vst3) instance.vst3->setControllerParam(index, value);  // (its audio has it from the block)
            else processor.setParam(index, value);
            // The engine knows this value: only a change from it is news.
            if (index >= 0 && static_cast<size_t>(index) < instance.sentValues.size()) {
                instance.sentValues[static_cast<size_t>(index)] = processor.getParam(index);
            }
            break;
        }
        case Kind::ParamText: {
            const auto index = static_cast<int>(in.u32());
            const float value = in.f32();
            reply(message, true, processor.paramText(index, value));
            break;
        }
        case Kind::GetState: {
            const std::vector<uint8_t> state = processor.getState();
            reply(message, true, std::string(state.begin(), state.end()));
            break;
        }
        case Kind::SetState:
            processor.setState(sandbox::readBytes(in));
            // Its values before the answer: one set as it loaded is ready with them (SandboxedProcessor).
            if (instance.announced) report(instance);
            reply(message, true);
            break;
        case Kind::OpenEditor: {
            const uint64_t owner = in.u64();
            const std::string title = in.str();
            reply(message, processor.openEditor(reinterpret_cast<void*>(static_cast<uintptr_t>(owner)), title));
            break;
        }
        case Kind::CloseEditor: processor.closeEditor(); break;
        case Kind::ShowEditor: processor.setEditorVisible(in.u8() != 0); break;
        case Kind::SetEditorTitle: processor.setEditorTitle(in.str()); break;
        default:
            if (message.request) reply(message, false, "Not understood.");
            break;
    }
}

void HostServer::create(const Message& message) {
    auto instance = std::make_unique<Instance>();
    instance->id = message.instance;
    auto in = message.reader();
    instance->path = in.str();
    instance->uid = in.str();
    instance->sampleRate = sandbox::readF64(in);
    instance->maxBlock = static_cast<int>(in.u32());
    const std::string memory = in.str();
    instance->state = sandbox::readBytes(in);
    instance->name = in.str();
    try {
        if (!in.ok()) throw std::runtime_error("The engine's request was garbled.");
        instance->memory = platform::SharedMemory::open(memory, sizeof(sandbox::Block));
        instance->block = static_cast<sandbox::Block*>(instance->memory->data());
        if (instance->block->magic != sandbox::kBlockMagic || instance->block->size != sizeof(sandbox::Block)) {
            throw std::runtime_error("The engine's audio block isn't this host's.");
        }
        instance->requestSignal =
            platform::IpcSignal::open(sandbox::requestSignalName(memory), instance->block->request);
        instance->doneSignal = platform::IpcSignal::open(sandbox::doneSignalName(memory), instance->block->done);
        send({Kind::Attached, instance->id});
        loads_.push_back(instance->id);
    } catch (const std::exception& error) {
        instance->failure = error.what();
        platform::ByteWriter out;
        out.str(instance->failure);
        send({Kind::LoadFailed, instance->id, 0, std::move(out.bytes)});
    }
    instances_[message.instance] = std::move(instance);
}

void HostServer::destroy(uint32_t id) {
    loads_.erase(std::remove(loads_.begin(), loads_.end(), id), loads_.end());
    const auto found = instances_.find(id);
    if (found == instances_.end()) return;
    Instance& instance = *found->second;
    instance.stop = true;
    if (instance.requestSignal) instance.requestSignal->notify();
    if (instance.audio.joinable()) {
        // A plug-in stuck in process() keeps its thread: it is left as it is (destroying it
        // under that call would be worse), and the sandbox's other plug-ins go on.
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!instance.stopped && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!instance.stopped) {
            instance.audio.detach();
            static_cast<void>(found->second.release());
            instances_.erase(found);
            return;
        }
        instance.audio.join();
    }
    try {
        instance.processor.reset();  // (its editor goes with it)
    } catch (...) {
    }
    instances_.erase(found);
}

void HostServer::loadNext() {
    const uint32_t id = loads_.front();
    loads_.pop_front();
    const auto found = instances_.find(id);
    if (found == instances_.end()) return;
    Instance& instance = *found->second;
    {
        platform::ByteWriter out;
        out.u8(1);
        send({Kind::Busy, 0, 0, std::move(out.bytes)});
    }
    std::deque<Message> waiting;
    try {
        instance.processor =
            vst3::Vst3Format::instance().instantiate(instance.path, instance.uid, instance.sampleRate, instance.maxBlock);
        instance.vst3 = dynamic_cast<vst3::Vst3Processor*>(instance.processor.get());
        sandbox::LoadedInfo info;
        if (!instance.state.empty()) {
            try {
                instance.processor->setState(instance.state);
            } catch (const std::exception& error) {
                info.problem = error.what();
            }
        }
        instance.state = {};
        instance.loaded = true;
        // What came for it meanwhile, in order, before it says it has loaded: a state set
        // right after it was asked for is its state from its first block. That is what came
        // before it began loading, then what came while it loaded.
        waiting.swap(instance.waiting);
        for (Message& message : waiting) handle(std::move(message));
        waiting.clear();
        for (Message& message : takeIncomingFor(id)) handle(std::move(message));
        Processor& processor = *instance.processor;
        info.name = processor.name();
        info.hasEditor = processor.hasEditor();
        info.hasSidechain = processor.hasSidechain();
        info.acceptsMidi = processor.acceptsMidi();
        info.latency = processor.latencySamples();
        info.tail = processor.tailSamples();
        info.presetName = processor.presetName();
        info.params = processor.params();
        info.values = valuesOf(processor);
        instance.sentValues = info.values;
        instance.audio = std::thread(&HostServer::audioLoop, &instance);
        platform::ByteWriter out;
        sandbox::writeLoaded(out, info);
        send({Kind::Loaded, id, 0, std::move(out.bytes)});
        instance.announced = true;
    } catch (const std::exception& error) {
        instance.failure = *error.what() ? error.what() : "It could not be loaded.";
    } catch (...) {
        instance.failure = "It could not be loaded.";
    }
    if (!instance.failure.empty()) {
        instance.loaded = false;
        instance.processor.reset();
        instance.vst3 = nullptr;
        platform::ByteWriter out;
        out.str(instance.failure);
        send({Kind::LoadFailed, id, 0, std::move(out.bytes)});
        // (What came for it is answered: it isn't there.)
        if (waiting.empty()) waiting.swap(instance.waiting);
        for (Message& message : waiting) handle(std::move(message));
    }
    {
        platform::ByteWriter out;
        out.u8(0);
        send({Kind::Busy, 0, 0, std::move(out.bytes)});
    }
}

// The messages for instance `id` read while it loaded, in order, taken from among the others'
// (up to its Destroy, which stays where it is: it comes once the instance has said it loaded).
std::deque<Message> HostServer::takeIncomingFor(uint32_t id) {
    std::deque<Message> taken;
    std::lock_guard lock(incomingMutex_);
    for (auto it = incoming_.begin(); it != incoming_.end();) {
        if (it->instance != id) {
            ++it;
            continue;
        }
        if (it->kind == Kind::Destroy) break;
        taken.push_back(std::move(*it));
        it = incoming_.erase(it);
    }
    return taken;
}

void HostServer::idleAll() {
    for (auto& [id, instance] : instances_) {
        if (!instance->loaded) continue;
        try {
            instance->processor->idle();
            report(*instance);
        } catch (...) {
        }
    }
}

void HostServer::report(Instance& instance) {
    Processor& processor = *instance.processor;
    std::vector<ProcessorEvent> events;
    processor.takeEvents(events);
    using Type = ProcessorEvent::Type;
    const auto any = [&](std::initializer_list<Type> types) {
        return std::any_of(events.begin(), events.end(), [&](const ProcessorEvent& e) {
            return std::find(types.begin(), types.end(), e.type) != types.end();
        });
    };
    uint32_t flags = 0;
    platform::ByteWriter parts;
    if (any({Type::ParamInfoChanged})) {
        flags |= sandbox::kReportParams;
        sandbox::writeParams(parts, processor.params());
        instance.sentValues = valuesOf(processor);
        sandbox::writeValues(parts, instance.sentValues);
    } else if (any({Type::ParamsChanged, Type::ParamEdited})) {
        const std::vector<float> values = valuesOf(processor);
        std::vector<std::pair<uint32_t, float>> changed;
        for (size_t i = 0; i < values.size(); ++i) {
            if (i >= instance.sentValues.size() || values[i] != instance.sentValues[i]) {
                changed.emplace_back(static_cast<uint32_t>(i), values[i]);
            }
        }
        instance.sentValues = values;
        if (!changed.empty()) {
            flags |= sandbox::kReportValues;
            parts.u32(static_cast<uint32_t>(changed.size()));
            for (const auto& [index, value] : changed) {
                parts.u32(index);
                parts.f32(value);
            }
        }
    }
    if (any({Type::LatencyChanged})) {
        flags |= sandbox::kReportLatency;
        parts.u32(static_cast<uint32_t>(processor.latencySamples()));
        parts.u32(static_cast<uint32_t>(processor.tailSamples()));
    }
    if (any({Type::PresetChanged})) {
        flags |= sandbox::kReportPreset;
        parts.str(processor.presetName());
    }
    if (flags == 0 && events.empty()) return;
    platform::ByteWriter out;
    out.u32(flags);
    out.bytes += parts.bytes;
    out.u32(static_cast<uint32_t>(events.size()));
    for (const ProcessorEvent& event : events) sandbox::writeEvent(out, event);
    send({Kind::Report, instance.id, 0, std::move(out.bytes)});
}

bool HostServer::keyTaken(const KeyPress& key) {
    const int modifiers = key.modifiers & (EditorKey::kShift | EditorKey::kControl | EditorKey::kAlt);
    // Without Ctrl or Alt, a key typed into a text field the system knows is the plug-in's.
    if (!(modifiers & (EditorKey::kControl | EditorKey::kAlt)) && key.textField) return false;
    if (keys_.count({key.key, modifiers}) == 0) return false;
    platform::ByteWriter out;
    out.u32(key.key);
    out.u32(static_cast<uint32_t>(modifiers));
    out.u8(key.repeat ? 1 : 0);
    out.u8(key.textField ? 1 : 0);
    send({Kind::Key, 0, 0, std::move(out.bytes)});
    return true;
}

void HostServer::audioLoop(Instance* instance) {
    const platform::ScopedRealtimePriority realtime;
    const ScopedNoDenormals noDenormals;
    sandbox::Block& block = *instance->block;
    uint32_t seen = block.done.load(std::memory_order_acquire);
    while (!instance->stop.load(std::memory_order_acquire)) {
        instance->requestSignal->wait(seen, 200000);
        if (instance->stop.load(std::memory_order_acquire)) break;
        const uint32_t request = block.request.load(std::memory_order_acquire);
        if (request == seen) continue;
        processBlock(*instance);
        seen = request;
        block.done.store(request, std::memory_order_release);
        instance->doneSignal->notify();
    }
    instance->stopped.store(true, std::memory_order_release);
}

void HostServer::processBlock(Instance& instance) {
    sandbox::Block& block = *instance.block;
    Processor& processor = *instance.processor;
    const int frames = std::clamp(block.frames, 0, sandbox::kMaxFrames);
    if (block.flags & sandbox::kBlockReset) processor.reset();
    ProcessContext context = block.context.context();
    context.inEvents = {block.events, std::min<size_t>(block.numEvents, sandbox::kMaxEvents)};
    const size_t automation = std::min<size_t>(block.numAutomation, Processor::kMaxAutomation);
    for (size_t i = 0; i < automation; ++i) {
        const ParamAutomation& a = block.automation[i];
        processor.automate(a.index, a.value, a.sampleOffset);
    }
    if (instance.vst3) {
        const size_t changes = std::min<size_t>(block.numChanges, sandbox::kMaxChanges);
        for (size_t i = 0; i < changes; ++i) instance.vst3->queueParamChange(block.changes[i].id, block.changes[i].normalized);
    }
    processor.setSidechainConnected((block.flags & sandbox::kBlockSidechainConnected) != 0);
    if (block.flags & sandbox::kBlockSidechain) processor.setSidechain(block.sidechain[0], block.sidechain[1]);
    float* channels[2] = {block.audio[0], block.audio[1]};
    if (frames > 0) processor.process(context, channels, 2, frames);
    processor.clearAutomation();
    processor.setSidechain(nullptr, nullptr);
}

}  // namespace sub::pluginhost
