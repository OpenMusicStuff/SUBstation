#include "plugins/vst2/Vst2Processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "platform/Paths.h"
#include "plugins/PresetName.h"
#include "plugins/vst2/Vst2Format.h"
#include "plugins/vst2/Vst2Preset.h"
#include "pluginterfaces/gui/iplugview.h"
#include "rt/RtUtils.h"

namespace sub::vst2 {

namespace {

constexpr int kMaxChannels = 64;

// A switch's values (VST_PARAMETER_FLAG_SWITCH): Off below half, On from it.
float toPlain(float normalized, int steps) {
    normalized = std::clamp(normalized, 0.f, 1.f);
    return steps > 0 ? (normalized >= 0.5f ? 1.f : 0.f) : normalized;
}

}  // namespace

// ---------------------------------------------------------------------------
// The editor: a VST 3 view standing in for the plug-in's editor, so an
// EditorWindow holds it as it holds a VST3 plug-in's. Attached, it opens the
// editor in the window (an HWND on Windows, an X11 window id on Linux);
// removed, it closes it. VST2 editors aren't resized by the user: the plug-in
// asks (VST_HOST_OPCODE_EDITOR_RESIZE), and the window follows.

class Vst2Processor::EditorView final : public Steinberg::IPlugView {
public:
    explicit EditorView(Vst2Processor& owner) : owner_(&owner) {}
    ~EditorView() {
        if (owner_ && owner_->view_ == this) owner_->view_ = nullptr;
    }

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override {
#if SMTG_OS_WINDOWS
        return std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0 ? Steinberg::kResultTrue : Steinberg::kResultFalse;
#elif SMTG_OS_LINUX
        return std::strcmp(type, Steinberg::kPlatformTypeX11EmbedWindowID) == 0 ? Steinberg::kResultTrue
                                                                              : Steinberg::kResultFalse;
#else
        (void)type;
        return Steinberg::kResultFalse;
#endif
    }
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override {
        if (!owner_ || isPlatformTypeSupported(type) != Steinberg::kResultTrue) return Steinberg::kResultFalse;
        owner_->dispatch(VST_EFFECT_OPCODE_EDITOR_OPEN, 0, 0, parent);
        open_ = true;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API removed() override {
        if (owner_ && open_) owner_->dispatch(VST_EFFECT_OPCODE_EDITOR_CLOSE);
        open_ = false;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API onWheel(float) override { return Steinberg::kResultFalse; }
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16, Steinberg::int16, Steinberg::int16) override {
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16, Steinberg::int16, Steinberg::int16) override {
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override {
        if (!size) return Steinberg::kInvalidArgument;
        if (owner_) {
            vst_rect_t* rect = nullptr;
            owner_->dispatch(VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect);
            if (rect && rect->right > rect->left && rect->bottom > rect->top) {
                size_ = Steinberg::ViewRect(0, 0, rect->right - rect->left, rect->bottom - rect->top);
            }
        }
        if (size_.getWidth() <= 0 || size_.getHeight() <= 0) return Steinberg::kResultFalse;
        *size = size_;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override {
        if (newSize) size_ = *newSize;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool) override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override {
        frame_ = frame;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API canResize() override { return Steinberg::kResultFalse; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override {
        if (rect) *rect = size_;
        return Steinberg::kResultTrue;
    }

    // The plug-in asks for another size.
    bool resize(int width, int height) {
        if (width <= 0 || height <= 0) return false;
        size_ = Steinberg::ViewRect(0, 0, width, height);
        Steinberg::ViewRect rect = size_;
        return frame_ && frame_->resizeView(this, &rect) == Steinberg::kResultTrue;
    }
    // The processor goes: the window may outlive it a moment.
    void forget() { owner_ = nullptr; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID queried, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(queried, Steinberg::IPlugView::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(queried, Steinberg::FUnknown::iid)) {
            addRef();
            *obj = static_cast<Steinberg::IPlugView*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++refs_; }
    Steinberg::uint32 PLUGIN_API release() override {
        const Steinberg::uint32 left = --refs_;
        if (left == 0) delete this;
        return left;
    }

private:
    Vst2Processor* owner_;
    Steinberg::IPlugFrame* frame_ = nullptr;
    Steinberg::ViewRect size_{};
    bool open_ = false;
    std::atomic<Steinberg::uint32> refs_{1};
};

// ---------------------------------------------------------------------------
// Life cycle

Vst2Processor::Vst2Processor(std::shared_ptr<Module> module, vst_effect_t* effect)
    : module_(std::move(module)), effect_(effect), uid_(vst2Uid(effect->unique_id)) {
    effect_->host_internal = this;
    dispatch(VST_EFFECT_OPCODE_CREATE);
    const auto path = platform::toPath(module_->path());
    directory_ = platform::fromPath(path.parent_path());
    name_ = dispatchText(effect_, VST_EFFECT_OPCODE_EFFECT_NAME);
    if (name_.empty()) name_ = dispatchText(effect_, VST_EFFECT_OPCODE_PRODUCT_NAME);
    if (name_.empty()) name_ = platform::fromPath(path.stem());
    replacing_ = (effect_->flags & VST_EFFECT_FLAG_SUPPORTS_FLOAT) != 0 && effect_->process_float;
    if (!replacing_ && !effect_->process) {
        dispatch(VST_EFFECT_OPCODE_DESTROY);
        throw std::runtime_error(name_ + " does not process audio.");
    }
    const auto supports = [this](const char* feature) {
        return dispatch(VST_EFFECT_OPCODE_SUPPORTS, 0, 0, const_cast<char*>(feature)) > 0;
    };
    acceptsMidi_ = (effect_->flags & VST_EFFECT_FLAG_INSTRUMENT) != 0 || supports("receiveVstEvents") ||
                   supports("receiveVstMidiEvent");
    midi_.assign(kMaxEvents, vst_event_midi_t{});
    midiOffsets_.assign(kMaxEvents, 0);
    buildParams();
}

Vst2Processor::~Vst2Processor() {
    editor_.reset();             // (its view closes the plug-in's editor as the window lets it go)
    if (view_) view_->forget();  // (a view something else still holds mustn't call us back)
    deactivate();
    dispatch(VST_EFFECT_OPCODE_DESTROY);  // the plug-in frees the instance
}

intptr_t Vst2Processor::dispatch(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt) const {
    return effect_->control(effect_, opcode, index, value, ptr, opt);
}

void Vst2Processor::activate() {
    dispatch(VST_EFFECT_OPCODE_SUSPEND_RESUME, 0, 1);
    dispatch(VST_EFFECT_OPCODE_PROCESS_BEGIN);
    active_ = true;
    latency_.store(std::clamp(effect_->delay, 0, 1 << 22));
    const intptr_t tail = dispatch(VST_EFFECT_OPCODE_TAIL_SAMPLES);  // (0: unknown, 1: none)
    tail_ = tail > 1 ? static_cast<int>(std::min<double>(static_cast<double>(tail), sampleRate_.load() * 60)) : 0;
}

void Vst2Processor::deactivate() {
    if (!active_) return;
    dispatch(VST_EFFECT_OPCODE_PROCESS_END);
    dispatch(VST_EFFECT_OPCODE_SUSPEND_RESUME, 0, 0);
    active_ = false;
}

void Vst2Processor::prepare(double sampleRate, int maxBlockSize) {
    vst3::ScopedSuspend suspend(guard_);
    deactivate();
    sampleRate_.store(sampleRate);
    maxBlock_.store(maxBlockSize);
    dispatch(VST_EFFECT_OPCODE_SET_SAMPLE_RATE, 0, 0, nullptr, static_cast<float>(sampleRate));
    dispatch(VST_EFFECT_OPCODE_SET_BLOCK_SIZE, 0, maxBlockSize);
    allocateBuffers();
    activate();
}

void Vst2Processor::allocateBuffers() {
    numInputs_ = std::clamp(effect_->num_inputs, 0, kMaxChannels);
    numOutputs_ = std::clamp(effect_->num_outputs, 0, kMaxChannels);
    sidechainInputs_ = std::clamp(numInputs_ - 2, 0, 2);
    const auto frames = static_cast<size_t>(std::max(1, maxBlock_.load()));
    const auto allocate = [frames](int count, std::vector<std::vector<float>>& buffers, std::vector<float*>& pointers) {
        buffers.assign(static_cast<size_t>(count), std::vector<float>(frames, 0.f));
        pointers.assign(static_cast<size_t>(std::max(count, 1)), nullptr);
        for (int c = 0; c < count; ++c) pointers[c] = buffers[c].data();
    };
    allocate(numInputs_, inputs_, inputPointers_);
    allocate(numOutputs_, outputs_, outputPointers_);
    std::lock_guard lock(mutex_);
    numAutomated_ = params_.size();
    automated_ = std::make_unique<std::atomic<float>[]>(std::max<size_t>(numAutomated_, 1));
    for (size_t i = 0; i < numAutomated_; ++i) automated_[i].store(kNotAutomated);
    automationPending_.store(false);
}

void Vst2Processor::resetOffline() {
    vst3::ScopedSuspend suspend(guard_);
    deactivate();
    activate();
    releaseAll_.store(true);  // (suspended and resumed, not every plug-in lets its notes go: note-offs for them)
}

void Vst2Processor::reset() { releaseAll_.store(true, std::memory_order_relaxed); }

// ---------------------------------------------------------------------------
// Processing

void Vst2Processor::process(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) {
    if (numFrames <= 0 || numFrames > maxBlock_.load(std::memory_order_relaxed) || numChannels < 1) return;
    if (!guard_.tryEnter()) return;  // the main thread has the plug-in: the audio passes through
    setInAudioThread(true);

    vst3::ParamChange change;
    while (toAudio_.pop(change)) {
        if (change.id < numAutomated_) effect_->set_parameter(effect_, change.id, static_cast<float>(change.value));
    }
    fillTime(ctx);
    buildEvents(ctx);

    float* left = channels[0];
    float* right = numChannels > 1 ? channels[1] : channels[0];
    // The track into the first two inputs, the sidechain into the next two.
    if (numInputs_ == 1) {
        float* mono = inputs_[0].data();
        for (int i = 0; i < numFrames; ++i) mono[i] = 0.5f * (left[i] + right[i]);
    } else if (numInputs_ >= 2) {
        std::copy_n(left, numFrames, inputs_[0].data());
        std::copy_n(right, numFrames, inputs_[1].data());
    }
    for (int c = 0; c < sidechainInputs_; ++c) {
        const float* key = sidechain(c) ? sidechain(c) : sidechain(0);
        if (key) std::copy_n(key, numFrames, inputs_[2 + c].data());
        else std::fill_n(inputs_[2 + c].data(), numFrames, 0.f);
    }
    for (int c = 2 + sidechainInputs_; c < numInputs_; ++c) std::fill_n(inputs_[c].data(), numFrames, 0.f);

    // Automation: the block split where values change (at least kMinSegment frames a call).
    ParamAutomation* changes = automation();
    const size_t numChanges = numAutomation();
    std::sort(changes, changes + numChanges, [](const ParamAutomation& a, const ParamAutomation& b) {
        return a.sampleOffset != b.sampleOffset ? a.sampleOffset < b.sampleOffset : a.order < b.order;
    });
    size_t next = 0;
    int start = 0;
    while (start < numFrames) {
        for (; next < numChanges && changes[next].sampleOffset <= start; ++next) {
            const auto index = static_cast<size_t>(changes[next].index);
            if (index >= numAutomated_) continue;
            effect_->set_parameter(effect_, static_cast<uint32_t>(index), changes[next].value);
            automated_[index].store(changes[next].value, std::memory_order_relaxed);
        }
        int end = numFrames;
        if (next < numChanges) end = std::clamp(changes[next].sampleOffset, start + kMinSegment, numFrames);
        runSegment(start, end - start, ctx);
        start = end;
    }
    if (numChanges > 0) automationPending_.store(true, std::memory_order_release);

    if (numOutputs_ >= 1) {
        std::copy_n(outputs_[0].data(), numFrames, left);
        if (right != left) std::copy_n(outputs_[numOutputs_ > 1 ? 1 : 0].data(), numFrames, right);
    }
    setInAudioThread(false);
    guard_.leave();
}

void Vst2Processor::runSegment(int start, int frames, const ProcessContext& ctx) {
    // Where this call is (the plug-in asks with kGetTime).
    time_ = blockTime_;
    if (start > 0) {
        time_.samplePos += start;
        if (const double perBeat = ctx.samplesPerBeat(); perBeat > 0.0) time_.ppqPos += start / perBeat;
        time_.flags &= ~kTransportChanged;
    }
    eventList_.count = 0;
    for (; nextMidi_ < numMidi_ && midiOffsets_[nextMidi_] < start + frames; ++nextMidi_) {
        vst_event_midi_t& event = midi_[nextMidi_];
        event.event.offset = std::max(0, midiOffsets_[nextMidi_] - start);
        eventList_.events[eventList_.count++] = &event.event;
    }
    if (eventList_.count > 0) dispatch(VST_EFFECT_OPCODE_EVENT, 0, 0, &eventList_);

    for (int c = 0; c < numInputs_; ++c) inputPointers_[c] = inputs_[c].data() + start;
    for (int c = 0; c < numOutputs_; ++c) outputPointers_[c] = outputs_[c].data() + start;
    if (replacing_) {
        effect_->process_float(effect_, inputPointers_.data(), outputPointers_.data(), frames);
    } else {  // (VST 1's: it adds to its outputs)
        for (int c = 0; c < numOutputs_; ++c) std::fill_n(outputPointers_[c], frames, 0.f);
        effect_->process(effect_, inputPointers_.data(), outputPointers_.data(), frames);
    }
}

bool Vst2Processor::addMidi(int32_t offset, uint8_t status, uint8_t data1, uint8_t data2) noexcept {
    if (numMidi_ >= midi_.size()) return false;
    vst_event_midi_t& event = midi_[numMidi_];
    event = vst_event_midi_t{};
    event.event.type = VST_EVENT_TYPE_MIDI;
    event.event.size = static_cast<int32_t>(sizeof(vst_event_midi_t));  // (as hosts give it)
    event.midi.data[0] = static_cast<char>(status);
    event.midi.data[1] = static_cast<char>(data1 & 0x7F);
    event.midi.data[2] = static_cast<char>(data2 & 0x7F);
    midiOffsets_[numMidi_++] = std::max(0, offset);
    return true;
}

void Vst2Processor::buildEvents(const ProcessContext& ctx) {
    numMidi_ = 0;
    nextMidi_ = 0;
    if (releaseAll_.exchange(false, std::memory_order_relaxed)) {
        for (uint8_t channel = 0; channel < 16; ++channel) {
            for (uint8_t key = 0; key < 128; ++key) {
                if (!held_[channel][key]) continue;
                held_[channel][key] = 0;
                addMidi(0, 0x80 | channel, key, 0);
            }
        }
    }
    for (size_t e = 0; e < ctx.inEvents.count; ++e) {
        const ProcessEvent& in = ctx.inEvents.events[e];
        const auto channel = static_cast<uint8_t>(in.channel() & 0x0F);
        const auto key = static_cast<uint8_t>(in.key() & 0x7F);
        switch (in.type) {
            case ProcessEvent::Type::NoteOn:
                if (in.velocity() > 0) {
                    if (addMidi(in.sampleOffset, 0x90 | channel, key, in.velocity()) && held_[channel][key] < 255) {
                        ++held_[channel][key];
                    }
                    break;
                }
                [[fallthrough]];  // velocity 0 releases
            case ProcessEvent::Type::NoteOff:
                if (addMidi(in.sampleOffset, 0x80 | channel, key, 0) && held_[channel][key] > 0) --held_[channel][key];
                break;
            case ProcessEvent::Type::NoteBend:
                break;  // (VST 2 has no per-note pitch)
            case ProcessEvent::Type::Midi: {
                const uint8_t status = in.data[0];
                if (status < 0x80 || status >= 0xF0) break;  // channel messages only
                addMidi(in.sampleOffset, status, in.data[1], in.data[2]);
                break;
            }
        }
    }
}

void Vst2Processor::fillTime(const ProcessContext& ctx) {
    TimeInfo& t = blockTime_;
    t = {};
    t.samplePos = static_cast<double>(ctx.samplePos);
    t.sampleRate = ctx.sampleRate;
    t.nanoSeconds = static_cast<double>(hostTimeNs());
    t.ppqPos = ctx.beatPos;
    t.tempo = ctx.tempo;
    const double beatsPerBar = ctx.beatsPerBar();
    t.barStartPos = std::floor(ctx.beatPos / beatsPerBar + 1e-9) * beatsPerBar;
    t.timeSigNumerator = ctx.timeSigNum;
    t.timeSigDenominator = ctx.timeSigDen;
    t.flags = kNanosValid | kPpqPosValid | kTempoValid | kBarsValid | kTimeSigValid;
    if (ctx.playing) t.flags |= kTransportPlaying;
    if (ctx.playing != lastPlaying_) t.flags |= kTransportChanged;
    lastPlaying_ = ctx.playing;
    if (ctx.looping) {
        t.flags |= kTransportCycleActive | kCyclePosValid;
        t.cycleStartPos = ctx.loopStartBeat;
        t.cycleEndPos = ctx.loopEndBeat;
    }
    time_ = t;
}

// ---------------------------------------------------------------------------
// Parameters

void Vst2Processor::buildParams() {
    const int count = std::clamp(effect_->num_params, 0, 1 << 16);
    std::vector<ParamInfo> params(static_cast<size_t>(count));
    auto values = std::make_unique<std::atomic<float>[]>(std::max(count, 1));
    for (int i = 0; i < count; ++i) {
        ParamInfo& param = params[i];
        param.id = std::to_string(i);
        param.name = dispatchText(effect_, VST_EFFECT_OPCODE_PARAM_NAME, i);
        vst_parameter_properties_t properties{};
        if (dispatch(VST_EFFECT_OPCODE_PARAM_PROPERTIES, i, 0, &properties) == 1) {
            properties.name[sizeof(properties.name) - 1] = 0;
            if (properties.name[0]) param.name = properties.name;  // (its long name)
            if (properties.flags & VST_PARAMETER_FLAG_SWITCH) {
                param.steps = 1;
                param.valueLabels = {"Off", "On"};
            }
        }
        if (param.name.empty()) param.name = "Parameter " + std::to_string(i + 1);
        param.minValue = 0.f;
        param.maxValue = 1.f;
        const float value = toPlain(effect_->get_parameter(effect_, static_cast<uint32_t>(i)), param.steps);
        param.defaultValue = value;  // (VST 2 says none: as it starts)
        values[i].store(value);
    }
    std::lock_guard lock(mutex_);
    if (params_.size() == params.size()) {  // the defaults stay what they were when it loaded
        for (size_t i = 0; i < params.size(); ++i) params[i].defaultValue = params_[i].defaultValue;
    }
    params_ = std::move(params);
    values_ = std::move(values);
    gestures_.clear();
}

bool Vst2Processor::refreshValues() {
    size_t count = 0;
    std::vector<int> steps;
    {
        std::lock_guard lock(mutex_);
        count = params_.size();
        for (const ParamInfo& param : params_) steps.push_back(param.steps);
    }
    std::vector<float> values(count);
    for (size_t i = 0; i < count; ++i) {
        values[i] = toPlain(effect_->get_parameter(effect_, static_cast<uint32_t>(i)), steps[i]);
    }
    bool changed = false;
    std::lock_guard lock(mutex_);
    if (params_.size() != count) return false;
    for (size_t i = 0; i < count; ++i) changed |= values_[i].exchange(values[i]) != values[i];
    return changed;
}

float Vst2Processor::getParam(int index) const {
    std::lock_guard lock(mutex_);
    return index >= 0 && index < static_cast<int>(params_.size()) ? values_[index].load() : 0.f;
}

void Vst2Processor::setParam(int index, float value) { setControllerParam(index, value); }

std::optional<std::pair<uint32_t, double>> Vst2Processor::setControllerParam(int index, float value) {
    float normalized = 0.f;
    {
        std::lock_guard lock(mutex_);
        if (index < 0 || index >= static_cast<int>(params_.size())) return std::nullopt;
        normalized = params_[index].steps > 0 ? (value >= 0.5f ? 1.f : 0.f) : std::clamp(value, 0.f, 1.f);
        values_[index].store(normalized);
    }
    // At once, as VST2 hosts set them (the plug-in's editor shows it, with or without audio running).
    ++settingParams_;
    effect_->set_parameter(effect_, static_cast<uint32_t>(index), normalized);
    --settingParams_;
    return std::pair{static_cast<uint32_t>(index), static_cast<double>(normalized)};
}

void Vst2Processor::queueParamChange(uint32_t id, double normalized) noexcept { toAudio_.push(id, normalized); }

std::string Vst2Processor::paramText(int index, float value) const {
    // VST 2 says only what the current value shows as.
    if (index < 0 || index >= effect_->num_params || std::abs(getParam(index) - value) > 1e-6f) return {};
    std::string text = dispatchText(effect_, VST_EFFECT_OPCODE_PARAM_VALUE, index);
    const std::string label = dispatchText(effect_, VST_EFFECT_OPCODE_PARAM_LABEL, index);
    if (!text.empty() && !label.empty()) text += ' ' + label;
    return text;
}

// The plug-in's editor (or the plug-in itself) changed a parameter.
void Vst2Processor::edited(int index, float value) {
    if (settingParams_.load() > 0) return;  // an echo of a value we set
    std::lock_guard lock(mutex_);
    if (index < 0 || index >= static_cast<int>(params_.size())) return;
    const float plain = toPlain(value, params_[index].steps);
    const float old = values_[index].exchange(plain);
    ProcessorEvent event;
    event.type = ProcessorEvent::Type::ParamEdited;
    event.paramIndex = index;
    event.value = plain;
    event.oldValue = old;
    if (const auto gesture = gestures_.find(index); gesture != gestures_.end()) {
        event.gesture = gesture->second.serial;
        event.oldValue = gesture->second.startValue;
    }
    pending_.push_back(event);
}

void Vst2Processor::beginEdit(int index) {
    std::lock_guard lock(mutex_);
    if (index < 0 || index >= static_cast<int>(params_.size())) return;
    gestures_[index] = {++gestureCounter_, values_[index].load()};
    ProcessorEvent event;
    event.type = ProcessorEvent::Type::ParamTouched;
    event.paramIndex = index;
    pending_.push_back(event);
}

void Vst2Processor::endEdit(int index) {
    std::lock_guard lock(mutex_);
    gestures_.erase(index);
}

void Vst2Processor::pushEvent(const ProcessorEvent& event) {
    std::lock_guard lock(mutex_);
    pending_.push_back(event);
}

void Vst2Processor::takeEvents(std::vector<ProcessorEvent>& out) {
    std::lock_guard lock(mutex_);
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

// ---------------------------------------------------------------------------
// The plug-in's calls

intptr_t Vst2Processor::hostCall(int32_t opcode, int32_t index, int64_t value, void* ptr, float opt) {
    switch (opcode) {
        case VST_HOST_OPCODE_AUTOMATE:
            if (inAudioThread()) {
                fromAudio_.push({static_cast<uint32_t>(index), opt});  // (it set it itself: shown in idle())
            } else {
                presetStateChanged_.store(true);
                edited(index, opt);
            }
            return 0;
        case kGetTime: return reinterpret_cast<intptr_t>(&time_);
        case VST_HOST_OPCODE_IO_MODIFIED: ioChanged_.store(true); return 1;
        case VST_HOST_OPCODE_EDITOR_RESIZE:
            return view_ && view_->resize(index, static_cast<int>(value)) ? 1 : 0;
        case VST_HOST_OPCODE_GET_SAMPLE_RATE: return static_cast<intptr_t>(sampleRate_.load());
        case VST_HOST_OPCODE_GET_BLOCK_SIZE: return maxBlock_.load();
        case VST_HOST_OPCODE_GET_EFFECT_DIRECTORY: return reinterpret_cast<intptr_t>(directory_.c_str());
        case VST_HOST_OPCODE_REFRESH: displayChanged_.store(true); return 1;
        case kBeginEdit: beginEdit(index); return 1;
        case kEndEdit: endEdit(index); return 1;
        default: return defaultHostCall(opcode, index, value, ptr, opt);
    }
}

// ---------------------------------------------------------------------------
// Main-thread housekeeping

bool Vst2Processor::idle() {
    if (editor_ && editor_->wasClosed()) {
        dropEditor();
        pushEvent({ProcessorEvent::Type::EditorClosed});
    }
    if (editor_ && editor_->isOpen()) dispatch(VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE);  // (its timers, on some)

    bool changed = false;
    if (automationPending_.exchange(false, std::memory_order_acquire)) {
        std::lock_guard lock(mutex_);
        for (size_t i = 0; i < std::min(numAutomated_, params_.size()); ++i) {
            const float value = automated_[i].exchange(kNotAutomated, std::memory_order_relaxed);
            if (value < 0.f) continue;
            values_[i].store(toPlain(value, params_[i].steps));
            changed = true;
        }
    }
    vst3::ParamChange change;
    while (fromAudio_.pop(change)) {
        std::lock_guard lock(mutex_);
        if (change.id < params_.size()) {
            values_[change.id].store(toPlain(static_cast<float>(change.value), params_[change.id].steps));
            changed = true;
        }
    }

    bool latencyChanged = false;
    if (ioChanged_.exchange(false)) {  // its channels, latency or parameters changed
        const int before = latency_.load();
        {
            vst3::ScopedSuspend suspend(guard_);
            deactivate();
            if (static_cast<size_t>(std::max(0, effect_->num_params)) != params_.size()) {
                buildParams();
                pushEvent({ProcessorEvent::Type::ParamInfoChanged});
            }
            allocateBuffers();
            activate();
        }
        if (latency_.load() != before) {
            latencyChanged = true;
            pushEvent({ProcessorEvent::Type::LatencyChanged});
        }
    }
    if (displayChanged_.exchange(false)) {  // its names or values (a program picked)
        std::vector<std::string> names;
        {
            std::lock_guard lock(mutex_);
            for (const ParamInfo& param : params_) names.push_back(param.name);
        }
        bool renamed = static_cast<size_t>(std::max(0, effect_->num_params)) != names.size();
        for (size_t i = 0; i < names.size() && !renamed; ++i) {
            const std::string name = dispatchText(effect_, VST_EFFECT_OPCODE_PARAM_NAME, static_cast<int32_t>(i));
            renamed = !name.empty() && names[i].rfind(name, 0) != 0;  // (a long name starts as the short one)
        }
        if (renamed) {
            buildParams();
            vst3::ScopedSuspend suspend(guard_);
            allocateBuffers();
            pushEvent({ProcessorEvent::Type::ParamInfoChanged});
        }
        changed |= refreshValues();
        presetStateChanged_.store(true);
    }
    if (changed) pushEvent({ProcessorEvent::Type::ParamsChanged});

    // Its program's name: when the state may have changed (at most every 250 ms),
    // and every 2 s while the editor is open, where programs are picked.
    constexpr auto kPresetCheckInterval = std::chrono::milliseconds(250);
    constexpr auto kPresetPollInterval = std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    const auto since = now - lastPresetCheck_;
    if (since >= kPresetCheckInterval && presetStateChanged_.exchange(false)) {
        lastPresetCheck_ = now;
        refreshPresetName(true);
    } else if (since >= kPresetPollInterval && isEditorOpen()) {
        lastPresetCheck_ = now;
        refreshPresetName(false);
    }
    return latencyChanged;
}

// ---------------------------------------------------------------------------
// State: an .fxb of its bank's chunk, or an .fxp of its program's values.

std::vector<uint8_t> Vst2Processor::getState() {
    Preset preset;
    preset.pluginId = effect_->unique_id;
    preset.pluginVersion = effect_->version;
    if (effect_->flags & VST_EFFECT_FLAG_CHUNKS) {
        void* data = nullptr;
        const intptr_t size = dispatch(VST_EFFECT_OPCODE_GET_CHUNK_DATA, 0, 0, &data);
        if (data && size > 0) {
            preset.kind = Preset::Kind::BankChunk;
            preset.numPrograms = std::max(0, effect_->num_programs);
            preset.currentProgram = static_cast<int32_t>(dispatch(VST_EFFECT_OPCODE_PROGRAM_GET));
            const auto* bytes = static_cast<const uint8_t*>(data);
            preset.chunk.assign(bytes, bytes + size);
            return writePreset(preset);
        }
    }
    preset.kind = Preset::Kind::Program;
    preset.name = dispatchText(effect_, VST_EFFECT_OPCODE_PROGRAM_GET_NAME);
    const int count = std::max(0, effect_->num_params);
    for (int i = 0; i < count; ++i) preset.values.push_back(effect_->get_parameter(effect_, static_cast<uint32_t>(i)));
    return writePreset(preset);
}

void Vst2Processor::setState(const std::vector<uint8_t>& state) {
    const auto preset = readPreset(state);
    if (!preset || preset->pluginId != effect_->unique_id) {
        throw std::runtime_error("These settings are not for " + name_ + ".");
    }
    if (preset->hasChunk() && !(effect_->flags & VST_EFFECT_FLAG_CHUNKS)) {
        throw std::runtime_error("These settings are not for " + name_ + ".");
    }
    const auto setValues = [this](const Preset& program) {
        const int count = std::min<int>(static_cast<int>(program.values.size()), effect_->num_params);
        for (int i = 0; i < count; ++i) effect_->set_parameter(effect_, static_cast<uint32_t>(i), program.values[i]);
        if (!program.name.empty()) {
            char name[64] = {};
            std::memcpy(name, program.name.data(), std::min(program.name.size(), sizeof(name) - 1));
            dispatch(VST_EFFECT_OPCODE_PROGRAM_SET_NAME, 0, 0, name);
        }
    };
    {
        vst3::ScopedSuspend suspend(guard_);
        toAudio_.clear();  // changes from before must not undo the state
        ++settingParams_;
        std::vector<uint8_t> chunk = preset->chunk;  // (the plug-in may keep the pointer during the call only)
        switch (preset->kind) {
            case Preset::Kind::BankChunk:
            case Preset::Kind::ProgramChunk:
                dispatch(VST_EFFECT_OPCODE_SET_CHUNK_DATA, preset->kind == Preset::Kind::ProgramChunk ? 1 : 0,
                         static_cast<intptr_t>(chunk.size()), chunk.data());
                break;
            case Preset::Kind::Program:
                setValues(*preset);
                break;
            case Preset::Kind::Bank:
                for (size_t p = 0; p < preset->programs.size() && static_cast<int>(p) < effect_->num_programs; ++p) {
                    dispatch(VST_EFFECT_OPCODE_PROGRAM_SET_BEGIN);
                    dispatch(VST_EFFECT_OPCODE_PROGRAM_SET, 0, static_cast<intptr_t>(p));
                    dispatch(VST_EFFECT_OPCODE_PROGRAM_SET_END);
                    setValues(preset->programs[p]);
                }
                if (effect_->num_programs > 0) {
                    dispatch(VST_EFFECT_OPCODE_PROGRAM_SET, 0,
                             std::clamp<intptr_t>(preset->currentProgram, 0, effect_->num_programs - 1));
                }
                break;
        }
        --settingParams_;
    }
    if (static_cast<size_t>(std::max(0, effect_->num_params)) != params_.size()) {
        buildParams();
        vst3::ScopedSuspend suspend(guard_);
        allocateBuffers();
        pushEvent({ProcessorEvent::Type::ParamInfoChanged});
    }
    refreshValues();
    presetStateChanged_.store(true);
    chunkHasName_ = true;
    lastPresetCheck_ = {};
    pushEvent({ProcessorEvent::Type::ParamsChanged});
}

// ---------------------------------------------------------------------------
// Preset name

std::string Vst2Processor::presetName() const {
    std::lock_guard lock(mutex_);
    return presetName_;
}

void Vst2Processor::refreshPresetName(bool stateChanged) {
    std::string name = dispatchText(effect_, VST_EFFECT_OPCODE_PROGRAM_GET_NAME);
    if (vst3::isPlaceholderPresetName(name)) name.clear();
    if (stateChanged) chunkHasName_ = true;
    if (name.empty() && chunkHasName_ && (effect_->flags & VST_EFFECT_FLAG_CHUNKS)) {
        void* data = nullptr;
        const intptr_t size = dispatch(VST_EFFECT_OPCODE_GET_CHUNK_DATA, 0, 0, &data);
        if (data && size > 0) name = vst3::presetNameFromState(static_cast<const uint8_t*>(data), static_cast<size_t>(size));
        if (name.empty()) chunkHasName_ = false;  // none there: not read again until the state changes
    }
    {
        std::lock_guard lock(mutex_);
        if (name == presetName_) return;
        presetName_ = std::move(name);
    }
    pushEvent({ProcessorEvent::Type::PresetChanged});
}

// ---------------------------------------------------------------------------
// Editor

bool Vst2Processor::openEditor(void* ownerWindow, const std::string& title) {
    if (editor_ && editor_->isOpen()) {
        editor_->setTitle(title);
        editor_->bringToFront();
        return true;
    }
    dropEditor();
    if (!hasEditor()) return false;
    auto view = Steinberg::owned(new EditorView(*this));
    if (!vst3::EditorWindow::canHold(*view)) return false;
    view_ = view.get();
    editor_ = std::make_unique<vst3::EditorWindow>(Steinberg::IPtr<Steinberg::IPlugView>(view.get()), ownerWindow,
                                                    title, editorPosition_ ? &*editorPosition_ : nullptr);
    if (!editor_->isOpen()) {
        editor_.reset();
        view_ = nullptr;
        return false;
    }
    return true;
}

void Vst2Processor::closeEditor() { dropEditor(); }

void Vst2Processor::dropEditor() {
    if (!editor_) return;
    editorPosition_ = editor_->position();
    editor_.reset();
    view_ = nullptr;
}

bool Vst2Processor::isEditorOpen() const { return editor_ && editor_->isOpen() && editor_->isVisible(); }

bool Vst2Processor::setEditorVisible(bool visible) {
    if (!editor_ || !editor_->isOpen()) return false;
    editor_->setVisible(visible);
    return true;
}

void Vst2Processor::setEditorTitle(const std::string& title) {
    if (editor_) editor_->setTitle(title);
}

}  // namespace sub::vst2
