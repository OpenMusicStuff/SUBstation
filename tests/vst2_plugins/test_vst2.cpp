// The test VST2 plug-ins, built with the same SDK the engine hosts them with
// (engine/src/plugins/vst2/Vst2Sdk.h). One source, three libraries (SUB_TEST_VST2_KIND):
//
//  * SUB VST2 Gain ('SuG2', an effect): its output is (input + sidechain) times
//    2 * Gain, inverted by Invert (a switch), Latency late (0..1 is 0..1000
//    samples), which it reports (VST_HOST_OPCODE_IO_MODIFIED) when it changes. Four inputs (the last two its sidechain), two outputs. Its state is
//    a chunk, text: its values and, when it has one, presetName="...". No programs.
//    Where the engine has X11 editors (SUB_TEST_X11), an editor: an X11 window of
//    its own connection inside the host's, run by the host's editor idle calls,
//    which client messages ask to edit Gain (one gesture: 0.25, then 0.3) and to
//    ask the host for another size (SUB_TEST_EDIT_GAIN, SUB_TEST_RESIZE w h; from
//    a thread of its own, as plug-ins drawing on their own threads may:
//    SUB_TEST_RESIZE_ELSEWHERE w h).
//  * SUB VST2 Synth ('SuS2', an instrument): its output is DC, each held note's
//    velocity / 127 times 2 * Level, sample-exactly at its events' offsets; it
//    reports the host's transport as parameters it sets itself from process()
//    (Tempo / 1000, Playing, Beat / 1000), through VST_HOST_OPCODE_AUTOMATE. Its
//    state is its values (no chunk); two programs, "Init" (Level 0.5) and
//    "Bright" (Level 1), which tell the host to refresh when picked.
//  * SUB VST2 Shell ('SuSh'): a shell holding both, made by the id the host
//    answers VST_HOST_OPCODE_CURRENT_EFFECT_ID with.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "plugins/vst2/Vst2Sdk.h"

#if defined(SUB_TEST_X11)
#include <xcb/xcb.h>

#include <cstdlib>
#include <thread>
#endif

#if defined(_WIN32)
#define SUB_EXPORT extern "C" __declspec(dllexport)
#else
#define SUB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

using sub::vst2::TimeInfo;

constexpr int32_t kGainId = static_cast<int32_t>(VST_FOURCC('S', 'u', 'G', '2'));
constexpr int32_t kSynthId = static_cast<int32_t>(VST_FOURCC('S', 'u', 'S', '2'));
constexpr int32_t kShellId = static_cast<int32_t>(VST_FOURCC('S', 'u', 'S', 'h'));

void copyText(void* to, const std::string& text, size_t size) {
    if (!to) return;
    const size_t length = std::min(text.size(), size - 1);
    std::memcpy(to, text.data(), length);
    static_cast<char*>(to)[length] = 0;
}

// What every test plug-in is: the instance the host sees, and its answers.
class Plugin {
public:
    Plugin(vst_host_callback_t host, int32_t id, int32_t params, int32_t programs, int32_t inputs, int32_t outputs,
           int32_t flags)
        : host_(host) {
        effect_.magic_number = static_cast<int32_t>(VST_MAGICNUMBER);
        effect_.control = &Plugin::control;
        effect_.process = &Plugin::processAdding;
        effect_.process_float = &Plugin::processReplacing;
        effect_.set_parameter = &Plugin::setParameter;
        effect_.get_parameter = &Plugin::getParameter;
        effect_.num_params = params;
        effect_.num_programs = programs;
        effect_.num_inputs = inputs;
        effect_.num_outputs = outputs;
        effect_.flags = flags | VST_EFFECT_FLAG_SUPPORTS_FLOAT;
        effect_.input_output_ratio = 1.f;
        effect_.effect_internal = this;
        effect_.unique_id = id;
        effect_.version = 0x010203;  // 1.2.3
        values_.assign(static_cast<size_t>(params), 0.f);
    }
    virtual ~Plugin() = default;

    vst_effect_t* effect() { return &effect_; }

protected:
    intptr_t host(int32_t opcode, int32_t index = 0, int64_t value = 0, void* ptr = nullptr, float opt = 0.f) {
        return host_ ? host_(&effect_, opcode, index, value, static_cast<const char*>(ptr), opt) : 0;
    }

    virtual std::string name() const = 0;
    virtual int32_t category() const = 0;
    virtual std::string paramName(int index) const = 0;
    virtual void set(int index, float value) { values_[static_cast<size_t>(index)] = value; }
    virtual bool isSwitch(int) const { return false; }
    virtual void process(const float* const* inputs, float** outputs, int frames) = 0;
    virtual void events(const vst_events_t*) {}
    virtual intptr_t other(int32_t, int32_t, intptr_t, void*, float) { return 0; }
    virtual intptr_t supports(const char*) { return 0; }

    vst_host_callback_t host_;
    vst_effect_t effect_{};
    std::vector<float> values_;
    double sampleRate_ = 44100.0;

private:
    static Plugin* self(vst_effect_t* effect) { return static_cast<Plugin*>(effect->effect_internal); }

    static intptr_t VST_FUNCTION_INTERFACE control(vst_effect_t* effect, int32_t opcode, int32_t index, intptr_t value,
                                                   void* ptr, float opt) {
        Plugin* p = self(effect);
        switch (opcode) {
            case VST_EFFECT_OPCODE_DESTROY: delete p; return 0;
            case VST_EFFECT_OPCODE_SET_SAMPLE_RATE: p->sampleRate_ = opt; return 0;
            case VST_EFFECT_OPCODE_EFFECT_NAME:
            case VST_EFFECT_OPCODE_PRODUCT_NAME: copyText(ptr, p->name(), VST_BUFFER_SIZE_EFFECT_NAME); return 1;
            case VST_EFFECT_OPCODE_VENDOR_NAME: copyText(ptr, "SUBstation Tests", VST_BUFFER_SIZE_VENDOR_NAME); return 1;
            case VST_EFFECT_OPCODE_VENDOR_VERSION: return 0x010203;
            case VST_EFFECT_OPCODE_CATEGORY: return p->category();
            case VST_EFFECT_OPCODE_PARAM_NAME: copyText(ptr, p->paramName(index), VST_BUFFER_SIZE_PARAM_NAME); return 0;
            case VST_EFFECT_OPCODE_PARAM_LABEL: copyText(ptr, index == 0 ? "x" : "", VST_BUFFER_SIZE_PARAM_LABEL); return 0;
            case VST_EFFECT_OPCODE_PARAM_VALUE: {
                char text[16];
                std::snprintf(text, sizeof(text), "%.2f", p->values_[static_cast<size_t>(index)] * 2.0);
                copyText(ptr, text, VST_BUFFER_SIZE_PARAM_VALUE);
                return 0;
            }
            case VST_EFFECT_OPCODE_PARAM_PROPERTIES: {
                if (!ptr || !p->isSwitch(index)) return 0;
                auto* properties = static_cast<vst_parameter_properties_t*>(ptr);
                properties->flags = VST_PARAMETER_FLAG_SWITCH;
                copyText(properties->name, p->paramName(index), sizeof(properties->name));
                return 1;
            }
            case VST_EFFECT_OPCODE_EVENT: p->events(static_cast<const vst_events_t*>(ptr)); return 1;
            case VST_EFFECT_OPCODE_SUPPORTS: return p->supports(static_cast<const char*>(ptr));
            default: return p->other(opcode, index, value, ptr, opt);
        }
    }
    static void VST_FUNCTION_INTERFACE processReplacing(vst_effect_t* effect, const float* const* inputs,
                                                        float** outputs, int32_t frames) {
        self(effect)->process(inputs, outputs, frames);
    }
    static void VST_FUNCTION_INTERFACE processAdding(vst_effect_t* effect, const float* const* inputs, float** outputs,
                                                     int32_t frames) {
        std::vector<std::vector<float>> out(static_cast<size_t>(effect->num_outputs), std::vector<float>(frames));
        std::vector<float*> pointers;
        for (auto& channel : out) pointers.push_back(channel.data());
        self(effect)->process(inputs, pointers.data(), frames);
        for (int c = 0; c < effect->num_outputs; ++c)
            for (int i = 0; i < frames; ++i) outputs[c][i] += out[static_cast<size_t>(c)][static_cast<size_t>(i)];
    }
    static void VST_FUNCTION_INTERFACE setParameter(vst_effect_t* effect, uint32_t index, float value) {
        Plugin* p = self(effect);
        if (index < p->values_.size()) p->set(static_cast<int>(index), value);
    }
    static float VST_FUNCTION_INTERFACE getParameter(vst_effect_t* effect, uint32_t index) {
        Plugin* p = self(effect);
        return index < p->values_.size() ? p->values_[index] : 0.f;
    }
};

// ---------------------------------------------------------------------------

#if defined(SUB_TEST_X11)
// SUB VST2 Gain's editor on Linux: what it is asked to do (by client messages) it
// hands to its owner.
class X11Editor {
public:
    enum class Request { None, EditGain, Resize, ResizeElsewhere };

    bool open(void* parent, int width, int height) {
        c_ = xcb_connect(nullptr, nullptr);
        if (xcb_connection_has_error(c_)) return false;
        window_ = xcb_generate_id(c_);
        const uint32_t events = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        xcb_create_window(c_, XCB_COPY_FROM_PARENT, window_,
                          static_cast<xcb_window_t>(reinterpret_cast<uintptr_t>(parent)), 0, 0, uint16_t(width),
                          uint16_t(height), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK,
                          &events);
        editGain_ = atom("SUB_TEST_EDIT_GAIN");
        resize_ = atom("SUB_TEST_RESIZE");
        resizeElsewhere_ = atom("SUB_TEST_RESIZE_ELSEWHERE");
        xcb_map_window(c_, window_);
        xcb_flush(c_);
        return true;
    }
    void close() {
        if (c_) {
            if (window_) xcb_destroy_window(c_, window_);
            xcb_disconnect(c_);
        }
        c_ = nullptr;
        window_ = 0;
    }
    ~X11Editor() { close(); }

    // The next thing asked of it (its size for a resize).
    Request poll(int& width, int& height) {
        if (!c_) return Request::None;
        while (xcb_generic_event_t* event = xcb_poll_for_event(c_)) {
            Request request = Request::None;
            if ((event->response_type & 0x7F) == XCB_CLIENT_MESSAGE) {
                const auto* message = reinterpret_cast<const xcb_client_message_event_t*>(event);
                if (message->type == editGain_) request = Request::EditGain;
                if (message->type == resize_ || message->type == resizeElsewhere_) {
                    width = int(message->data.data32[0]);
                    height = int(message->data.data32[1]);
                    request = message->type == resize_ ? Request::Resize : Request::ResizeElsewhere;
                }
            }
            std::free(event);
            if (request != Request::None) return request;
        }
        return Request::None;
    }
    void resize(int width, int height) {
        const uint32_t size[] = {uint32_t(width), uint32_t(height)};
        xcb_configure_window(c_, window_, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
        xcb_flush(c_);
    }

private:
    xcb_atom_t atom(const char* name) {
        xcb_intern_atom_reply_t* reply =
            xcb_intern_atom_reply(c_, xcb_intern_atom(c_, 0, uint16_t(strlen(name)), name), nullptr);
        const xcb_atom_t result = reply ? reply->atom : xcb_atom_t(XCB_ATOM_NONE);
        std::free(reply);
        return result;
    }

    xcb_connection_t* c_ = nullptr;
    xcb_window_t window_ = 0;
    xcb_atom_t editGain_ = 0, resize_ = 0, resizeElsewhere_ = 0;
};
constexpr int32_t kEditorFlag = VST_EFFECT_FLAG_EDITOR;
#else
constexpr int32_t kEditorFlag = 0;
#endif

class Gain final : public Plugin {
public:
    enum { kGain, kLatency, kInvert, kCount };

    explicit Gain(vst_host_callback_t host)
        : Plugin(host, kGainId, kCount, 0, 4, 2, VST_EFFECT_FLAG_CHUNKS | kEditorFlag) {
        values_[kGain] = 0.5f;
    }

protected:
    std::string name() const override { return "SUB VST2 Gain"; }
    int32_t category() const override { return VST_EFFECT_CATEGORY_DELAY_OR_ECHO; }
    std::string paramName(int index) const override {
        static const char* names[] = {"Gain", "Latency", "Invert"};
        return names[index];
    }
    bool isSwitch(int index) const override { return index == kInvert; }
    void set(int index, float value) override {
        Plugin::set(index, value);
        if (index == kLatency) {
            const int latency = static_cast<int>(std::lround(value * 1000.f));
            if (latency != effect_.delay) {
                effect_.delay = latency;
                host(VST_HOST_OPCODE_IO_MODIFIED);
            }
        }
    }
    void process(const float* const* inputs, float** outputs, int frames) override {
        const float gain = 2.f * values_[kGain] * (values_[kInvert] >= 0.5f ? -1.f : 1.f);
        const auto latency = static_cast<size_t>(effect_.delay);
        for (int c = 0; c < 2; ++c) {
            std::vector<float>& line = lines_[c];  // (what is still to come out, latency late)
            for (int i = 0; i < frames; ++i) {
                line.push_back((inputs[c][i] + inputs[2 + c][i]) * gain);
                outputs[c][i] = line.size() > latency ? line[line.size() - 1 - latency] : 0.f;
            }
            if (line.size() > latency + 1) {
                line.erase(line.begin(), line.end() - static_cast<std::ptrdiff_t>(latency + 1));
            }
        }
    }
    intptr_t other(int32_t opcode, int32_t, intptr_t value, void* ptr, float) override {
#if defined(SUB_TEST_X11)
        switch (opcode) {
            case VST_EFFECT_OPCODE_EDITOR_GET_RECT:
                *static_cast<vst_rect_t**>(ptr) = &rect_;
                return 1;
            case VST_EFFECT_OPCODE_EDITOR_OPEN: return editor_.open(ptr, rect_.right, rect_.bottom) ? 1 : 0;
            case VST_EFFECT_OPCODE_EDITOR_CLOSE: editor_.close(); return 0;
            case VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE: idleEditor(); return 0;
            default: break;
        }
#endif
        if (opcode == VST_EFFECT_OPCODE_GET_CHUNK_DATA) {
            char text[256];
            std::snprintf(text, sizeof(text), "gain=%.9g latency=%.9g invert=%.9g", values_[kGain], values_[kLatency],
                          values_[kInvert]);
            chunk_ = text;
            if (!presetName_.empty()) chunk_ += " presetName=\"" + presetName_ + "\"";
            *static_cast<void**>(ptr) = chunk_.data();
            return static_cast<intptr_t>(chunk_.size());
        }
        if (opcode == VST_EFFECT_OPCODE_SET_CHUNK_DATA) {
            const std::string text(static_cast<const char*>(ptr), static_cast<size_t>(value));
            float gain = 0.5f, latency = 0.f, invert = 0.f;
            std::sscanf(text.c_str(), "gain=%f latency=%f invert=%f", &gain, &latency, &invert);
            set(kGain, gain);
            set(kLatency, latency);
            set(kInvert, invert);
            presetName_.clear();
            if (const auto at = text.find("presetName=\""); at != std::string::npos) {
                const auto start = at + 12;
                presetName_ = text.substr(start, text.find('"', start) - start);
            }
            return 1;
        }
        return 0;
    }

private:
#if defined(SUB_TEST_X11)
    void idleEditor() {
        int width = 0, height = 0;
        for (;;) {
            const X11Editor::Request request = editor_.poll(width, height);
            switch (request) {
                case X11Editor::Request::None: return;
                case X11Editor::Request::EditGain:  // a knob dragged: one gesture
                    host(sub::vst2::kBeginEdit, kGain);
                    for (const float value : {0.25f, 0.3f}) {
                        set(kGain, value);
                        host(VST_HOST_OPCODE_AUTOMATE, kGain, 0, nullptr, value);
                    }
                    host(sub::vst2::kEndEdit, kGain);
                    break;
                case X11Editor::Request::Resize:
                case X11Editor::Request::ResizeElsewhere: {
                    rect_.right = int16_t(width);
                    rect_.bottom = int16_t(height);
                    editor_.resize(width, height);
                    if (request == X11Editor::Request::Resize) {
                        host(VST_HOST_OPCODE_EDITOR_RESIZE, width, height);
                    } else {
                        std::thread([&] { host(VST_HOST_OPCODE_EDITOR_RESIZE, width, height); }).join();
                    }
                    break;
                }
            }
        }
    }

    X11Editor editor_;
    vst_rect_t rect_{0, 0, 200, 300};  // (top, left, bottom, right)
#endif
    std::string chunk_;
    std::string presetName_;
    std::vector<float> lines_[2];
};

// ---------------------------------------------------------------------------

class Synth final : public Plugin {
public:
    enum { kLevel, kTempo, kPlaying, kBeat, kCount };

    explicit Synth(vst_host_callback_t host)
        : Plugin(host, kSynthId, kCount, 2, 0, 2, VST_EFFECT_FLAG_INSTRUMENT) {
        values_[kLevel] = 0.5f;
    }

protected:
    std::string name() const override { return "SUB VST2 Synth"; }
    int32_t category() const override { return VST_EFFECT_CATEGORY_INSTRUMENT; }
    std::string paramName(int index) const override {
        static const char* names[] = {"Level", "Tempo", "Playing", "Beat"};
        return names[index];
    }
    intptr_t supports(const char* feature) override {
        return feature && (std::strcmp(feature, "receiveVstEvents") == 0 ||
                           std::strcmp(feature, "receiveVstMidiEvent") == 0 ||
                           std::strcmp(feature, "receiveVstTimeInfo") == 0)
                   ? 1
                   : -1;
    }
    void events(const vst_events_t* list) override {
        // (As hosts lay the list out: the pointers inline, after the count and a reserved field.)
        const auto* events = reinterpret_cast<const sub::vst2::EventList<1>*>(list);
        for (int32_t e = 0; e < events->count && pending_.size() < 1024; ++e) {
            const vst_event_t* event = events->events[e];
            if (event->type != VST_EVENT_TYPE_MIDI) continue;
            pending_.push_back(*reinterpret_cast<const vst_event_midi_t*>(event));
        }
    }
    void process(const float* const*, float** outputs, int frames) override {
        report();
        std::stable_sort(pending_.begin(), pending_.end(),
                         [](const auto& a, const auto& b) { return a.event.offset < b.event.offset; });
        size_t next = 0;
        for (int i = 0; i < frames; ++i) {
            for (; next < pending_.size() && pending_[next].event.offset <= i; ++next) play(pending_[next]);
            float level = 0.f;
            for (int key = 0; key < 128; ++key) level += velocity_[key] / 127.f;
            outputs[0][i] = outputs[1][i] = level * 2.f * values_[kLevel];
        }
        for (; next < pending_.size(); ++next) play(pending_[next]);
        pending_.clear();
    }
    intptr_t other(int32_t opcode, int32_t, intptr_t value, void* ptr, float) override {
        switch (opcode) {
            case VST_EFFECT_OPCODE_PROGRAM_SET:
                program_ = std::clamp<int>(static_cast<int>(value), 0, 1);
                set(kLevel, program_ == 0 ? 0.5f : 1.f);
                host(VST_HOST_OPCODE_REFRESH);
                return 0;
            case VST_EFFECT_OPCODE_PROGRAM_GET: return program_;
            case VST_EFFECT_OPCODE_PROGRAM_GET_NAME: copyText(ptr, names_[program_], VST_BUFFER_SIZE_PROGRAM_NAME); return 0;
            case VST_EFFECT_OPCODE_PROGRAM_SET_NAME: names_[program_] = static_cast<const char*>(ptr); return 0;
            default: return 0;
        }
    }

private:
    void play(const vst_event_midi_t& event) {
        const auto status = static_cast<uint8_t>(event.midi.data[0]) & 0xF0;
        const int key = event.midi.data[1] & 0x7F;
        const int velocity = event.midi.data[2] & 0x7F;
        if (status == 0x90 && velocity > 0) velocity_[key] = velocity;
        else if (status == 0x80 || status == 0x90) velocity_[key] = 0;
    }
    // The transport, as the host says it, as parameters it sets itself.
    void report() {
        const auto* time = reinterpret_cast<const TimeInfo*>(host(sub::vst2::kGetTime, 0, sub::vst2::kTempoValid));
        if (!time) return;
        const float tempo = (time->flags & sub::vst2::kTempoValid) ? static_cast<float>(time->tempo / 1000.0) : 0.f;
        const float playing = (time->flags & sub::vst2::kTransportPlaying) ? 1.f : 0.f;
        const float beat = (time->flags & sub::vst2::kPpqPosValid) ? static_cast<float>(time->ppqPos / 1000.0) : 0.f;
        for (const auto& [index, value] : {std::pair{kTempo, tempo}, {kPlaying, playing}, {kBeat, beat}}) {
            if (values_[index] == value) continue;
            values_[index] = value;
            host(VST_HOST_OPCODE_AUTOMATE, index, 0, nullptr, value);
        }
    }

    std::vector<vst_event_midi_t> pending_;
    int velocity_[128] = {};
    int program_ = 0;
    std::string names_[2] = {"Init", "Bright"};
};

// ---------------------------------------------------------------------------

class Shell final : public Plugin {
public:
    explicit Shell(vst_host_callback_t host) : Plugin(host, kShellId, 0, 0, 2, 2, 0) {}

protected:
    std::string name() const override { return "SUB VST2 Shell"; }
    int32_t category() const override { return sub::vst2::kCategoryShell; }
    std::string paramName(int) const override { return {}; }
    void process(const float* const* inputs, float** outputs, int frames) override {
        for (int c = 0; c < 2; ++c) std::copy_n(inputs[c], frames, outputs[c]);
    }
    intptr_t other(int32_t opcode, int32_t, intptr_t, void* ptr, float) override {
        if (opcode != VST_EFFECT_OPCODE_CONTAINER_NEXT_EFFECT_ID) return 0;
        static const std::pair<int32_t, const char*> members[] = {{kGainId, "SUB VST2 Gain"},
                                                                  {kSynthId, "SUB VST2 Synth"}};
        if (next_ >= 2) return 0;
        copyText(ptr, members[next_].second, VST_BUFFER_SIZE_EFFECT_NAME);
        return members[next_++].first;
    }

private:
    int next_ = 0;
};

}  // namespace

SUB_EXPORT vst_effect_t* VSTPluginMain(vst_host_callback_t host) {
    if (!host || host(nullptr, VST_HOST_OPCODE_VST_VERSION, 0, 0, nullptr, 0.f) == 0) return nullptr;
#if SUB_TEST_VST2_KIND == 1
    return (new Gain(host))->effect();
#elif SUB_TEST_VST2_KIND == 2
    return (new Synth(host))->effect();
#else
    const auto id = static_cast<int32_t>(host(nullptr, VST_HOST_OPCODE_CURRENT_EFFECT_ID, 0, 0, nullptr, 0.f));
    if (id == kGainId) return (new Gain(host))->effect();
    if (id == kSynthId) return (new Synth(host))->effect();
    return (new Shell(host))->effect();
#endif
}
