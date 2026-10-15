#include "plugins/sandbox/SandboxedProcessor.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <new>
#include <stdexcept>

namespace sub::sandbox {

namespace {

constexpr int kTextWaitMs = 250;                // a value's text, from a sandbox that isn't loading
constexpr int kStateWaitMs = 15000;             // a state, to save it
constexpr int kSetStateWaitMs = 10000;          // a state set, to hear whether it was taken
constexpr std::chrono::seconds kResetWait{10};  // an offline reset, before a render
constexpr size_t kMaxTexts = 4096;              // values' texts kept

// VST3's plain values, as Vst3Processor has them: 0..1, or the step for stepped parameters.
double toNormalized(float plain, int steps) {
    if (steps <= 0) return std::clamp<double>(plain, 0.0, 1.0);
    return std::clamp<double>(std::round(plain), 0.0, steps) / steps;
}

float toPlain(double normalized, int steps) {
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (steps <= 0) return static_cast<float>(normalized);
    return static_cast<float>(std::min<int>(steps, static_cast<int>(normalized * (steps + 1))));
}

}  // namespace

SandboxedProcessor::SandboxedProcessor(PluginSandboxes& sandboxes, PluginOrigin origin, std::vector<uint8_t> state,
                                       double sampleRate, int maxBlockSize)
    : origin_(std::move(origin)),
      sampleRate_(sampleRate),
      maxBlock_(std::min(maxBlockSize, kMaxFrames)),
      name_(origin_.name.empty() ? origin_.uid : origin_.name),
      lastState_(state) {
    link_.sandbox = sandboxes.sandboxFor(origin_);
    offlineWaitUs_ = static_cast<int64_t>(sandboxes.offlineWaitMs()) * 1000;
    link_.instance = Sandbox::newInstance();
    link_.slot = link_.sandbox->takeSlot();
    const std::string memory = platform::uniqueIpcName("p");
    try {
        link_.memory = platform::SharedMemory::create(memory, sizeof(Block));
        link_.block = new (link_.memory->data()) Block;
        link_.done = platform::IpcSignal::create(doneSignalName(memory), link_.block->done);
    } catch (...) {
        link_.sandbox->releaseSlot(link_.slot);
        throw;
    }

    platform::ByteWriter out;
    out.str(origin_.path);
    out.str(origin_.uid);
    writeF64(out, sampleRate_);
    out.u32(static_cast<uint32_t>(maxBlock_));
    out.str(memory);
    writeBytes(out, state);
    out.str(name_);
    out.u32(link_.slot);
    out.str(origin_.format);
    link_.sandbox->attach(link_.instance, this);  // (a sandbox already gone says so at once)
    link_.sandbox->send({Kind::Create, link_.instance, 0, std::move(out.bytes)});
}

SandboxedProcessor::~SandboxedProcessor() {
    link_.sandbox->detach(link_.instance);  // (nothing more comes for it)
    link_.sandbox->send({Kind::Destroy, link_.instance});
    link_.sandbox->releaseSlot(link_.slot);  // (the host has let it go before it reads a Create that takes it again)
}

std::string SandboxedProcessor::name() const { return name_; }

void SandboxedProcessor::send(Kind kind, std::string body) const {
    if (ended()) return;
    link_.sandbox->send({kind, link_.instance, 0, std::move(body)});
}

uint32_t SandboxedProcessor::ask(Kind kind, std::string body, Asked asked) const {
    if (ended()) return 0;
    const uint32_t request = Sandbox::newRequest();
    asked_[request] = std::move(asked);
    link_.sandbox->send({kind, link_.instance, request, std::move(body)});
    return request;
}

// --- The reader thread's -------------------------------------------------------------------

void SandboxedProcessor::received(Message message) {
    if (message.kind == Kind::Reply && message.request != 0 &&
        message.request == resetRequest_.load(std::memory_order_acquire)) {
        {
            std::lock_guard lock(resetMutex_);
            resetAnswer_ = message.request;
        }
        resetAnswered_.notify_all();  // (an offline render waiting for it goes on)
        return;
    }
    std::lock_guard lock(inboxMutex_);
    inbox_.push_back(std::move(message));
}

void SandboxedProcessor::sandboxEnded() {
    link_.ended.store(true, std::memory_order_release);
    link_.done->notify();  // (a rendering thread waiting on it goes on)
    { std::lock_guard lock(resetMutex_); }  // (not between a waiter's look at ended() and its wait)
    resetAnswered_.notify_all();
    std::lock_guard lock(inboxMutex_);
    endedSeen_ = true;
}

// --- Audio ---------------------------------------------------------------------------------

void SandboxedProcessor::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlock_ = std::min(maxBlockSize, kMaxFrames);
    platform::ByteWriter out;
    writeF64(out, sampleRate_);
    out.u32(static_cast<uint32_t>(maxBlock_));
    send(Kind::Prepare, std::move(out.bytes));
}

void SandboxedProcessor::reset() { resetPending_.store(true, std::memory_order_release); }

void SandboxedProcessor::resetOffline() {
    if (!loaded() || ended()) return;
    // Asked for, not waited for: the sandbox's main thread may be loading another plug-in, or stuck in one.
    const uint32_t request = Sandbox::newRequest();
    const auto deadline = std::chrono::steady_clock::now() + kResetWait;
    resetDeadline_.store(deadline.time_since_epoch().count(), std::memory_order_relaxed);
    resetRequest_.store(request, std::memory_order_release);
    link_.sandbox->send({Kind::ResetOffline, link_.instance, request});
}

// An offline render's first block comes after the reset asked for before it: the sandbox's main
// thread does that, its audio thread the blocks. Waited for until the deadline set when it was asked
// (all of a stuck sandbox's plug-ins were asked at once); past it the render goes on without.
void SandboxedProcessor::waitForOfflineReset() {
    uint32_t request = resetRequest_.load(std::memory_order_acquire);
    if (request == 0) return;
    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline(Clock::duration(resetDeadline_.load(std::memory_order_relaxed)));
    {
        std::unique_lock lock(resetMutex_);
        resetAnswered_.wait_until(lock, deadline, [&] { return resetAnswer_ == request || ended(); });
    }
    resetRequest_.compare_exchange_strong(request, 0, std::memory_order_acq_rel);
}

bool SandboxedProcessor::waitDone(uint32_t sent, int64_t timeoutMicroseconds) noexcept {
    Block& block = *link_.block;
    for (int spin = 0; spin < 256; ++spin) {  // (a plug-in that is quick is back before a wake-up would be)
        if (block.done.load(std::memory_order_acquire) == sent) return true;
        cpuRelax();
    }
    using Clock = std::chrono::steady_clock;
    const auto until = Clock::now() + std::chrono::microseconds(timeoutMicroseconds);
    for (;;) {
        const uint32_t done = block.done.load(std::memory_order_acquire);
        if (done == sent) return true;
        if (ended()) return false;
        const auto left = std::chrono::duration_cast<std::chrono::microseconds>(until - Clock::now()).count();
        if (left <= 0) return false;
        link_.done->wait(done, std::min<int64_t>(left, 100000));
    }
}

void SandboxedProcessor::process(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) {
    if (!ready_.load(std::memory_order_acquire) || ended() || numFrames <= 0 || numFrames > kMaxFrames ||
        numChannels < 1) {
        return;
    }
    if (ctx.offline) waitForOfflineReset();
    Block& block = *link_.block;
    if (block.done.load(std::memory_order_acquire) != sent_) {
        // The sandbox still has an earlier block (it didn't make it in time): this one passes by.
        // Offline it is waited for, once: a plug-in that hangs costs a render one wait, not one a block.
        if (!ctx.offline || offlineLate_ || !waitDone(sent_, offlineWaitUs_)) {
            if (ctx.offline) offlineLate_ = true;
            lateBlocks_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    offlineLate_ = false;  // (caught up)

    uint32_t flags = 0;
    if (resetPending_.exchange(false, std::memory_order_acq_rel)) flags |= kBlockReset;
    block.frames = numFrames;
    block.context = BlockContext::from(ctx);
    const size_t events = std::min<size_t>(ctx.inEvents.count, kMaxEvents);
    if (events > 0) std::copy_n(ctx.inEvents.events, events, block.events);
    block.numEvents = static_cast<uint32_t>(events);
    const size_t automation = std::min(numAutomation(), Processor::kMaxAutomation);
    if (automation > 0) std::copy_n(this->automation(), automation, block.automation);
    block.numAutomation = static_cast<uint32_t>(automation);
    uint32_t changes = 0;
    while (changes < static_cast<uint32_t>(kMaxChanges) && changes_.pop(block.changes[changes])) ++changes;
    block.numChanges = changes;
    std::copy_n(channels[0], numFrames, block.audio[0]);
    std::copy_n(channels[numChannels > 1 ? 1 : 0], numFrames, block.audio[1]);
    if (const float* keyLeft = sidechain(0)) {
        const float* keyRight = sidechain(1) ? sidechain(1) : keyLeft;
        std::copy_n(keyLeft, numFrames, block.sidechain[0]);
        std::copy_n(keyRight, numFrames, block.sidechain[1]);
        flags |= kBlockSidechain;
    }
    if (sidechainConnected()) flags |= kBlockSidechainConnected;
    block.flags = flags;

    block.request.store(++sent_, std::memory_order_release);
    if (!link_.sandbox->ring(link_.slot)) {  // (only trampled memory)
        lateBlocks_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Live, a little longer than the block lasts (a plug-in slower than that is late anyway).
    const int64_t lasts = static_cast<int64_t>(numFrames * 1e6 / std::max(ctx.sampleRate, 1.0));
    const int64_t timeout = ctx.offline ? offlineWaitUs_ : std::clamp<int64_t>(lasts * 2, 3000, 50000);
    if (!waitDone(sent_, timeout)) {
        if (ctx.offline) offlineLate_ = true;
        lateBlocks_.fetch_add(1, std::memory_order_relaxed);
        return;  // (it passes through unprocessed)
    }
    std::copy_n(block.audio[0], numFrames, channels[0]);
    if (numChannels > 1) std::copy_n(block.audio[1], numFrames, channels[1]);
}

// --- Parameters ----------------------------------------------------------------------------

void SandboxedProcessor::setParams(std::vector<ParamInfo> params, const std::vector<float>& values) {
    std::vector<uint32_t> ids;
    for (const ParamInfo& p : params) ids.push_back(static_cast<uint32_t>(std::strtoul(p.id.c_str(), nullptr, 10)));
    auto atomics = std::make_unique<std::atomic<float>[]>(params.size());
    for (size_t i = 0; i < params.size(); ++i) atomics[i].store(i < values.size() ? values[i] : params[i].defaultValue);
    {
        std::lock_guard lock(valuesMutex_);
        values_ = std::move(atomics);
        valueCount_ = params.size();
    }
    params_ = std::move(params);
    paramIds_ = std::move(ids);
    texts_.clear();
    asking_.clear();
}

float SandboxedProcessor::getParam(int index) const {
    std::lock_guard lock(valuesMutex_);
    return index >= 0 && static_cast<size_t>(index) < valueCount_ ? values_[static_cast<size_t>(index)].load() : 0.f;
}

void SandboxedProcessor::setParam(int index, float value) {
    if (index < 0 || static_cast<size_t>(index) >= params_.size() || params_[static_cast<size_t>(index)].readOnly) return;
    const int steps = params_[static_cast<size_t>(index)].steps;
    const double normalized = toNormalized(value, steps);
    {
        std::lock_guard lock(valuesMutex_);
        if (static_cast<size_t>(index) < valueCount_) values_[static_cast<size_t>(index)].store(toPlain(normalized, steps));
    }
    changes_.push({paramIds_[static_cast<size_t>(index)], 0, normalized});  // (its audio hears it with the next block)
    platform::ByteWriter out;
    out.u32(static_cast<uint32_t>(index));
    out.f32(value);
    send(Kind::SetParam, std::move(out.bytes));  // (and its controller, when the sandbox gets to it)
}

std::string SandboxedProcessor::paramText(int index, float value) const {
    if (!loaded() || ended() || index < 0 || static_cast<size_t>(index) >= params_.size()) return {};
    const std::pair<int, uint32_t> key{index, std::bit_cast<uint32_t>(value)};
    if (const auto it = texts_.find(key); it != texts_.end()) return it->second;
    platform::ByteWriter out;
    out.u32(static_cast<uint32_t>(index));
    out.f32(value);
    if (texts_.size() >= kMaxTexts) texts_.clear();
    if (!link_.sandbox->busy()) {
        if (const auto reply = link_.sandbox->call({Kind::ParamText, link_.instance, 0, out.bytes}, kTextWaitMs)) {
            return texts_[key] = reply->ok ? reply->data : std::string();
        }
        return {};  // (it didn't answer in time: asked again when shown again)
    }
    // Loading another plug-in: the text comes when it is done with that (ParamsChanged then).
    if (asking_.insert(key).second) {
        Asked asked;
        asked.what = Asked::What::Text;
        asked.text = key;
        ask(Kind::ParamText, std::move(out.bytes), std::move(asked));
    }
    return {};
}

std::string SandboxedProcessor::presetName() const {
    std::lock_guard lock(valuesMutex_);
    return presetName_;
}

// --- State ---------------------------------------------------------------------------------

std::vector<uint8_t> SandboxedProcessor::getState() {
    if (!loaded() || ended()) return lastState_;  // (as it loads, or as it was last)
    const auto reply = link_.sandbox->call({Kind::GetState, link_.instance}, kStateWaitMs);
    if (!reply) {
        if (!lastState_.empty()) return lastState_;
        throw std::runtime_error(name_ + " didn't answer.");
    }
    if (!reply->ok) throw std::runtime_error(reply->data);
    lastState_.assign(reply->data.begin(), reply->data.end());
    return lastState_;
}

void SandboxedProcessor::setState(const std::vector<uint8_t>& state) {
    if (ended() || status_ == Status::Failed || status_ == Status::Crashed) {
        lastState_ = state;  // (it loads again with it)
        return;
    }
    platform::ByteWriter out;
    writeBytes(out, state);
    if (loaded() && !link_.sandbox->busy()) {
        const auto reply = link_.sandbox->call({Kind::SetState, link_.instance, 0, out.bytes}, kSetStateWaitMs);
        if (reply && !reply->ok) throw std::runtime_error(reply->data);
        lastState_ = state;  // (taken, or it will be: it has it)
        return;
    }
    // Loading (it takes it once loaded, before it says so), or busy: whether it was taken comes later.
    // Loading, it isn't Ready until then (if it loaded before this came, it says so first).
    Asked asked;
    asked.what = Asked::What::SetState;
    asked.state = state;
    const uint32_t request = ask(Kind::SetState, std::move(out.bytes), std::move(asked));
    if (status_ == Status::Loading) stateAsLoading_ = request;
}

// --- Main-thread housekeeping ----------------------------------------------------------------

void SandboxedProcessor::pushEvent(ProcessorEvent::Type type) {
    ProcessorEvent event;
    event.type = type;
    events_.push_back(event);
}

void SandboxedProcessor::applyLoaded(const LoadedInfo& info) {
    name_ = info.name.empty() ? name_ : info.name;
    hasEditor_ = info.hasEditor;
    hasSidechain_ = info.hasSidechain;
    acceptsMidi_ = info.acceptsMidi;
    latency_.store(info.latency, std::memory_order_relaxed);
    tail_.store(info.tail, std::memory_order_relaxed);
    {
        std::lock_guard lock(valuesMutex_);
        presetName_ = info.presetName;
    }
    setParams(info.params, info.values);
    if (!info.problem.empty()) {
        problem_ = info.problem;
        pushEvent(ProcessorEvent::Type::Problem);
    }
    if (stateAsLoading_ != 0) {  // (a state set as it loaded, which it took after it said so: its answer is next)
        loadedWaiting_ = true;
        return;
    }
    becomeReady();
}

void SandboxedProcessor::becomeReady() {
    loadedWaiting_ = false;
    status_ = Status::Ready;
    ready_.store(true, std::memory_order_release);
    pushEvent(ProcessorEvent::Type::Loaded);
}

void SandboxedProcessor::apply(const Message& message, bool& latencyChanged) {
    auto in = message.reader();
    switch (message.kind) {
        case Kind::Attached: link_.memory->forgetName(); break;
        case Kind::Loaded: {
            applyLoaded(readLoaded(in));
            latencyChanged = latencyChanged || latency_.load() != 0;
            break;
        }
        case Kind::LoadFailed:
            status_ = Status::Failed;
            problem_ = in.str();
            ready_.store(false, std::memory_order_release);
            pushEvent(ProcessorEvent::Type::LoadFailed);
            break;
        case Kind::Report: {
            const uint32_t flags = in.u32();
            if (flags & kReportParams) {
                std::vector<ParamInfo> params = readParams(in);
                const std::vector<float> values = readValues(in);
                setParams(std::move(params), values);
            }
            if (flags & kReportValues) {
                const uint32_t count = in.u32();
                std::lock_guard lock(valuesMutex_);
                for (uint32_t i = 0; i < count && in.ok(); ++i) {
                    const uint32_t index = in.u32();
                    const float value = in.f32();
                    if (index < valueCount_) values_[index].store(value);
                }
            }
            if (flags & kReportLatency) {
                const auto latency = static_cast<int>(in.u32());
                tail_.store(static_cast<int>(in.u32()), std::memory_order_relaxed);
                if (latency_.exchange(latency) != latency) latencyChanged = true;
            }
            if (flags & kReportPreset) {
                std::string preset = in.str();
                std::lock_guard lock(valuesMutex_);
                presetName_ = std::move(preset);
            }
            const uint32_t count = in.u32();
            for (uint32_t i = 0; i < count && in.ok(); ++i) {
                const ProcessorEvent event = readEvent(in);
                if (!in.ok()) break;
                if (event.type == ProcessorEvent::Type::EditorClosed) editorOpen_ = false;
                events_.push_back(event);
            }
            break;
        }
        case Kind::Reply: {
            const auto found = asked_.find(message.request);
            if (found == asked_.end()) break;
            const Asked asked = std::move(found->second);
            asked_.erase(found);
            const bool ok = in.u8() != 0;
            std::string data = in.str();
            switch (asked.what) {
                case Asked::What::Text:
                    asking_.erase(asked.text);
                    if (texts_.size() >= kMaxTexts) texts_.clear();
                    texts_[asked.text] = ok ? std::move(data) : std::string();
                    pushEvent(ProcessorEvent::Type::ParamsChanged);  // (its text shows now)
                    break;
                case Asked::What::OpenEditor:
                    if (asked.editor != editorRequests_) break;  // (closed since)
                    editorOpen_ = ok;
                    editorVisible_ = true;
                    pushEvent(ok ? ProcessorEvent::Type::EditorOpened : ProcessorEvent::Type::NoEditor);
                    break;
                case Asked::What::SetState:
                    if (ok) {
                        lastState_ = asked.state;
                    } else {
                        problem_ = std::move(data);
                        pushEvent(ProcessorEvent::Type::Problem);
                    }
                    if (message.request == stateAsLoading_) {
                        stateAsLoading_ = 0;
                        if (loadedWaiting_) becomeReady();  // (with that state's values: they came just before)
                    }
                    break;
            }
            break;
        }
        default: break;
    }
}

bool SandboxedProcessor::idle() {
    std::vector<Message> messages;
    bool ended = false;
    {
        std::lock_guard lock(inboxMutex_);
        messages.swap(inbox_);
        ended = std::exchange(endedSeen_, false);
    }
    bool latencyChanged = false;
    for (const Message& message : messages) apply(message, latencyChanged);
    if (ended && status_ != Status::Failed && status_ != Status::Crashed) {
        status_ = Status::Crashed;
        ready_.store(false, std::memory_order_release);
        problem_ = "its sandbox (" + link_.sandbox->label() + ") " + link_.sandbox->ending();
        editorOpen_ = false;
        asked_.clear();
        asking_.clear();
        pushEvent(ProcessorEvent::Type::Crashed);
    }
    return latencyChanged;
}

void SandboxedProcessor::takeEvents(std::vector<ProcessorEvent>& out) {
    out.insert(out.end(), events_.begin(), events_.end());
    events_.clear();
}

// --- Editor --------------------------------------------------------------------------------

bool SandboxedProcessor::hasEditor() const { return status_ == Status::Loading || (loaded() && hasEditor_); }

bool SandboxedProcessor::openEditor(void* ownerWindow, const std::string& title) {
    if (ended() || !hasEditor()) return false;
    link_.sandbox->allowForeground();  // (its window may come to the front)
    platform::ByteWriter out;
    out.u64(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ownerWindow)));
    out.str(title);
    Asked asked;
    asked.what = Asked::What::OpenEditor;
    asked.editor = editorRequests_;
    ask(Kind::OpenEditor, std::move(out.bytes), std::move(asked));
    return true;  // (it says whether it did: EditorOpened, or NoEditor)
}

void SandboxedProcessor::closeEditor() {
    ++editorRequests_;
    editorOpen_ = false;
    send(Kind::CloseEditor);
}

bool SandboxedProcessor::setEditorVisible(bool visible) {
    if (!editorOpen_) return false;
    if (visible) link_.sandbox->allowForeground();
    platform::ByteWriter out;
    out.u8(visible ? 1 : 0);
    send(Kind::ShowEditor, std::move(out.bytes));
    editorVisible_ = visible;
    return true;
}

void SandboxedProcessor::setEditorTitle(const std::string& title) {
    platform::ByteWriter out;
    out.str(title);
    send(Kind::SetEditorTitle, std::move(out.bytes));
}

}  // namespace sub::sandbox
