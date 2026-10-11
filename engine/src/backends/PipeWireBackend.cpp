#include "PipeWireBackend.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/filter.h>
#include <pipewire/pipewire.h>
#include <spa/utils/json.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "rt/RtUtils.h"

namespace sub {

namespace {

constexpr int64_t kSyncTimeoutNs = 2'000'000'000;      // a round trip to the daemon
constexpr auto kOpenTimeout = std::chrono::seconds(3);  // our ports appearing in the registry
constexpr auto kFirstCycleTimeout = std::chrono::seconds(1);

struct Node {
    std::string name;  // node.name: what the default metadata names
    std::string description;
    std::string mediaClass;
    std::string deviceId;  // the card it belongs to ("": none)
};

struct Port {
    uint32_t node = 0;
    bool output = false;
    bool monitor = false;
    uint32_t index = 0;
    std::string name;
    std::string channel;  // audio.channel: FL, FR, AUX0...
};

bool startsWith(std::string_view text, std::string_view prefix) { return text.substr(0, prefix.size()) == prefix; }

bool isOutputDevice(const Node& node) {
    return startsWith(node.mediaClass, "Audio/Sink") || startsWith(node.mediaClass, "Audio/Duplex");
}

bool isInputDevice(const Node& node) {
    return startsWith(node.mediaClass, "Audio/Source") || startsWith(node.mediaClass, "Audio/Duplex");
}

std::string text(const char* value) { return value ? value : ""; }

// The "name" of a default metadata value: {"name":"alsa_output.pci-0000_00_1f.3.analog-stereo"}.
std::string jsonName(const char* value) {
    if (!value) return {};
    spa_json it[2];
    spa_json_init(&it[0], value, std::strlen(value));
    if (spa_json_enter_object(&it[0], &it[1]) <= 0) return {};
    char key[64];
    while (spa_json_get_string(&it[1], key, sizeof(key)) > 0) {
        if (std::strcmp(key, "name") == 0) {
            char name[1024];
            return spa_json_get_string(&it[1], name, sizeof(name)) > 0 ? std::string(name) : std::string();
        }
        const char* skipped = nullptr;
        if (spa_json_next(&it[1], &skipped) <= 0) break;
    }
    return {};
}

// clock.allowed-rates: "[ 44100, 48000 ]".
std::vector<uint32_t> jsonRates(const char* value) {
    std::vector<uint32_t> rates;
    if (!value) return rates;
    spa_json it[2];
    spa_json_init(&it[0], value, std::strlen(value));
    if (spa_json_enter_array(&it[0], &it[1]) <= 0) return rates;
    int rate = 0;
    while (spa_json_get_int(&it[1], &rate) > 0) {
        if (rate > 0) rates.push_back(static_cast<uint32_t>(rate));
    }
    return rates;
}

uint32_t number(const char* value, uint32_t fallback) {
    if (!value) return fallback;
    char* end = nullptr;
    const unsigned long n = std::strtoul(value, &end, 10);
    return end != value && n > 0 ? static_cast<uint32_t>(n) : fallback;
}

// A channel as the user sees it: "Front Left", "Aux 3"; the port's name if it has no position.
std::string channelLabel(const Port& port) {
    static const std::map<std::string_view, const char*> names{
        {"MONO", "Mono"},        {"FL", "Front Left"},   {"FR", "Front Right"},  {"FC", "Front Center"},
        {"LFE", "LFE"},          {"SL", "Side Left"},    {"SR", "Side Right"},   {"RL", "Rear Left"},
        {"RR", "Rear Right"},    {"RC", "Rear Center"},  {"FLC", "Front Left Center"},
        {"FRC", "Front Right Center"}};
    if (const auto it = names.find(port.channel); it != names.end()) return it->second;
    if (startsWith(port.channel, "AUX")) {
        const uint32_t n = number(port.channel.c_str() + 3, 0);
        if (port.channel.size() > 3 && (n > 0 || port.channel[3] == '0')) return "Aux " + std::to_string(n + 1);
    }
    return port.channel.empty() ? port.name : port.channel;
}

std::once_flag g_pwInit;

}  // namespace

struct PipeWireBackend::Impl {
    // The connection, made on first use. Everything below the loop is touched
    // only under its lock (its own thread holds it while calling us).
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook coreListener{};
    spa_hook registryListener{};
    bool connected = false;  // false once the daemon went away
    int waitSeq = -1;
    bool synced = false;
    std::string coreError;  // the last error the daemon sent (a link refused)

    std::map<uint32_t, Node> nodes;
    std::map<uint32_t, Port> ports;
    struct Metadata {
        Impl* self = nullptr;
        uint32_t id = 0;
        bool defaults = false;  // "default", else "settings"
        pw_metadata* proxy = nullptr;
        spa_hook listener{};
    };
    std::vector<std::unique_ptr<Metadata>> metadata;
    std::string defaultSink;  // node.name
    std::string defaultSource;
    uint32_t clockRate = 48000;
    uint32_t clockQuantum = 1024;
    uint32_t minQuantum = 32;
    uint32_t maxQuantum = 2048;
    std::vector<uint32_t> allowedRates;

    // The open device.
    pw_filter* filter = nullptr;
    spa_hook filterListener{};
    pw_filter_state filterState = PW_FILTER_STATE_UNCONNECTED;
    std::string filterError;
    std::vector<void*> inPorts;  // ours, in the callback's order
    std::vector<void*> outPorts;
    std::vector<pw_proxy*> links;
    AudioCallback* callback = nullptr;
    uint32_t outputNode = SPA_ID_INVALID;
    uint32_t inputNode = SPA_ID_INVALID;
    std::string outputNodeName;
    std::string inputNodeName;
    bool followsDefault = false;  // opened without a name: the default output (and source)
    bool inputIsDefault = false;  // the inputs are the default source's
    DeviceState opened;           // its rate and buffer size are the atomics'
    std::atomic<bool> started{false};
    std::atomic<bool> closing{false};
    std::atomic<bool> cycled{false};  // the graph has run us once
    std::atomic<uint32_t> rate{0};
    std::atomic<uint32_t> quantum{0};
    std::atomic<uint32_t> newRate{0};  // the graph's rate changed to this: open again at it

    // The callback's buffers, made at open: what an unlinked port reads or writes.
    static constexpr uint32_t kChunk = 4096;  // frames per engine callback; longer cycles take several
    std::vector<float> silence;
    std::vector<float> discard;
    std::vector<const float*> inBase;
    std::vector<float*> outBase;
    std::vector<const float*> inputs;
    std::vector<float*> outputs;

    class Lock {
    public:
        explicit Lock(pw_thread_loop* loop) : loop_(loop) { pw_thread_loop_lock(loop_); }
        ~Lock() { pw_thread_loop_unlock(loop_); }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;

    private:
        pw_thread_loop* loop_;
    };

    ~Impl() {
        if (!loop) return;
        pw_thread_loop_stop(loop);
        disconnect();
        if (context) pw_context_destroy(context);
        pw_thread_loop_destroy(loop);
    }

    // Connects to the daemon (again, if it went away), and reads the graph.
    bool connect() {
        std::call_once(g_pwInit, [] { pw_init(nullptr, nullptr); });
        if (!loop) {
            loop = pw_thread_loop_new("SUBstation PipeWire", nullptr);
            if (!loop) return false;
            if (pw_thread_loop_start(loop) < 0) {
                pw_thread_loop_destroy(loop);
                loop = nullptr;
                return false;
            }
        }
        Lock lock(loop);
        if (core && connected) return true;
        if (filter) return false;  // the device of a lost connection: closed first
        disconnect();
        if (!context) {
            context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
            if (!context) return false;
        }
        core = pw_context_connect(context, pw_properties_new(PW_KEY_APP_NAME, "SUBstation", nullptr), 0);
        if (!core) return false;
        connected = true;
        static const pw_core_events coreEvents = [] {
            pw_core_events events{};
            events.version = PW_VERSION_CORE_EVENTS;
            events.done = &Impl::onCoreDone;
            events.error = &Impl::onCoreError;
            return events;
        }();
        pw_core_add_listener(core, &coreListener, &coreEvents, this);
        registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
        static const pw_registry_events registryEvents = [] {
            pw_registry_events events{};
            events.version = PW_VERSION_REGISTRY_EVENTS;
            events.global = &Impl::onGlobal;
            events.global_remove = &Impl::onGlobalRemove;
            return events;
        }();
        pw_registry_add_listener(registry, &registryListener, &registryEvents, this);
        // The globals, then the metadata bound on seeing them.
        return sync() && sync();
    }

    // Under the lock, with no device open.
    void disconnect() {
        for (auto& m : metadata) {
            spa_hook_remove(&m->listener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m->proxy));
        }
        metadata.clear();
        if (registry) {
            spa_hook_remove(&registryListener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
            registry = nullptr;
        }
        if (core) {
            spa_hook_remove(&coreListener);
            pw_core_disconnect(core);
            core = nullptr;
        }
        connected = false;
        nodes.clear();
        ports.clear();
        defaultSink.clear();
        defaultSource.clear();
        allowedRates.clear();
    }

    // A round trip: every event the daemon sent before it has arrived. Under the lock.
    bool sync() {
        if (!core || !connected) return false;
        synced = false;
        waitSeq = pw_core_sync(core, PW_ID_CORE, 0);
        timespec deadline{};
        pw_thread_loop_get_time(loop, &deadline, kSyncTimeoutNs);
        while (!synced && connected) {
            if (pw_thread_loop_timed_wait_full(loop, &deadline) != 0) break;
        }
        return synced && connected;
    }

    bool deviceOpen() const { return filter && !closing.load(); }
    void raise(DeviceEvent event) {
        if (deviceOpen() && callback) callback->deviceEvent(event);
    }

    // --- the daemon's events (the loop's thread, under the lock) ---

    static void onCoreDone(void* data, uint32_t id, int seq) {
        auto* self = static_cast<Impl*>(data);
        if (id == PW_ID_CORE && seq == self->waitSeq) {
            self->synced = true;
            pw_thread_loop_signal(self->loop, false);
        }
    }

    static void onCoreError(void* data, uint32_t id, int, int res, const char* message) {
        auto* self = static_cast<Impl*>(data);
        if (id == PW_ID_CORE && res == -EPIPE) {
            self->connected = false;
            self->raise(DeviceEvent::Stopped);
        } else {
            self->coreError = text(message);
        }
        pw_thread_loop_signal(self->loop, false);
    }

    static void onGlobal(void* data, uint32_t id, uint32_t, const char* type, uint32_t, const spa_dict* props) {
        auto* self = static_cast<Impl*>(data);
        if (!props) return;
        if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            Node node;
            node.name = text(spa_dict_lookup(props, PW_KEY_NODE_NAME));
            node.description = text(spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION));
            if (node.description.empty()) node.description = text(spa_dict_lookup(props, PW_KEY_NODE_NICK));
            if (node.description.empty()) node.description = node.name;
            node.mediaClass = text(spa_dict_lookup(props, PW_KEY_MEDIA_CLASS));
            node.deviceId = text(spa_dict_lookup(props, PW_KEY_DEVICE_ID));
            self->nodes[id] = std::move(node);
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
            const std::string dsp = text(spa_dict_lookup(props, PW_KEY_FORMAT_DSP));
            if (!dsp.empty() && dsp.find("audio") == std::string::npos) return;  // MIDI
            Port port;
            port.node = number(spa_dict_lookup(props, PW_KEY_NODE_ID), SPA_ID_INVALID);
            port.output = text(spa_dict_lookup(props, PW_KEY_PORT_DIRECTION)) == "out";
            port.monitor = text(spa_dict_lookup(props, PW_KEY_PORT_MONITOR)) == "true";
            const char* index = spa_dict_lookup(props, PW_KEY_PORT_ID);
            port.index = index ? static_cast<uint32_t>(std::strtoul(index, nullptr, 10)) : id;
            port.name = text(spa_dict_lookup(props, PW_KEY_PORT_NAME));
            port.channel = text(spa_dict_lookup(props, PW_KEY_AUDIO_CHANNEL));
            self->ports[id] = std::move(port);
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
            const std::string name = text(spa_dict_lookup(props, PW_KEY_METADATA_NAME));
            if (name != "default" && name != "settings") return;
            auto m = std::make_unique<Metadata>();
            m->self = self;
            m->id = id;
            m->defaults = name == "default";
            m->proxy = static_cast<pw_metadata*>(pw_registry_bind(self->registry, id, type, PW_VERSION_METADATA, 0));
            if (!m->proxy) return;
            static const pw_metadata_events metadataEvents = [] {
                pw_metadata_events events{};
                events.version = PW_VERSION_METADATA_EVENTS;
                events.property = &Impl::onMetadata;
                return events;
            }();
            pw_metadata_add_listener(m->proxy, &m->listener, &metadataEvents, m.get());
            self->metadata.push_back(std::move(m));
        }
    }

    static void onGlobalRemove(void* data, uint32_t id) {
        auto* self = static_cast<Impl*>(data);
        self->nodes.erase(id);
        self->ports.erase(id);
        for (auto it = self->metadata.begin(); it != self->metadata.end(); ++it) {
            if ((*it)->id != id) continue;
            spa_hook_remove(&(*it)->listener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>((*it)->proxy));
            self->metadata.erase(it);
            break;
        }
        // The device went away (unplugged, its card's profile changed).
        if (self->deviceOpen() && (id == self->outputNode || id == self->inputNode)) self->raise(DeviceEvent::Stopped);
    }

    static int onMetadata(void* data, uint32_t subject, const char* key, const char*, const char* value) {
        auto* m = static_cast<Metadata*>(data);
        Impl* self = m->self;
        if (subject != PW_ID_CORE || !key) return 0;
        const std::string_view k(key);
        if (m->defaults) {
            if (k == "default.audio.sink") {
                self->defaultSink = jsonName(value);
                // Following the default output: open again on the new one.
                if (self->followsDefault && !self->defaultSink.empty() && self->defaultSink != self->outputNodeName) {
                    self->raise(DeviceEvent::ResetRequest);
                }
            } else if (k == "default.audio.source") {
                self->defaultSource = jsonName(value);
                if (self->inputIsDefault && !self->opened.inputChannels.empty() && !self->defaultSource.empty() &&
                    self->defaultSource != self->inputNodeName) {
                    self->raise(DeviceEvent::ResetRequest);
                }
            }
        } else {
            if (k == "clock.rate") self->clockRate = number(value, self->clockRate);
            else if (k == "clock.quantum") self->clockQuantum = number(value, self->clockQuantum);
            else if (k == "clock.min-quantum") self->minQuantum = number(value, self->minQuantum);
            else if (k == "clock.max-quantum") self->maxQuantum = number(value, self->maxQuantum);
            else if (k == "clock.allowed-rates") self->allowedRates = jsonRates(value);
        }
        return 0;
    }

    static void onFilterState(void* data, pw_filter_state, pw_filter_state state, const char* error) {
        auto* self = static_cast<Impl*>(data);
        self->filterState = state;
        if (error) self->filterError = error;
        if (state == PW_FILTER_STATE_ERROR) self->raise(DeviceEvent::Stopped);
        pw_thread_loop_signal(self->loop, false);
    }

    // --- the graph's cycle (PipeWire's data thread, real-time) ---

    static void onProcess(void* data, spa_io_position* position) {
        auto* self = static_cast<Impl*>(data);
        if (!position) return;
        const int64_t hostTime = hostTimeNs();
        const uint32_t frames = static_cast<uint32_t>(position->clock.duration);
        const uint32_t clockRate = position->clock.rate.denom;
        if (frames == 0) return;
        self->noteClock(clockRate, frames);

        const size_t numIn = self->inPorts.size();
        const size_t numOut = self->outPorts.size();
        for (size_t i = 0; i < numIn; ++i) {
            self->inBase[i] = static_cast<const float*>(pw_filter_get_dsp_buffer(self->inPorts[i], frames));
        }
        for (size_t o = 0; o < numOut; ++o) {
            self->outBase[o] = static_cast<float*>(pw_filter_get_dsp_buffer(self->outPorts[o], frames));
        }
        if (!self->started.load(std::memory_order_acquire)) {
            for (float* out : self->outBase) {
                if (out) std::fill_n(out, frames, 0.f);
            }
            return;
        }
        for (uint32_t done = 0; done < frames;) {
            const uint32_t n = std::min(kChunk, frames - done);
            for (size_t i = 0; i < numIn; ++i) {
                self->inputs[i] = self->inBase[i] ? self->inBase[i] + done : self->silence.data();
            }
            for (size_t o = 0; o < numOut; ++o) {
                self->outputs[o] = self->outBase[o] ? self->outBase[o] + done : self->discard.data() + o * kChunk;
            }
            AudioIO io;
            io.inputs = self->inputs.data();
            io.numInputs = static_cast<uint32_t>(numIn);
            io.outputs = self->outputs.data();
            io.numOutputs = static_cast<uint32_t>(numOut);
            io.frames = n;
            io.sampleTime = static_cast<int64_t>(position->clock.position) + done;
            io.hostTimeNs = hostTime;
            self->callback->audioCallback(io);
            done += n;
        }
    }

    // The rate and quantum the graph runs us at: the first cycle's are the
    // device's; another quantum is new latencies, another rate a reset.
    void noteClock(uint32_t clockRate, uint32_t frames) noexcept {
        if (!cycled.load(std::memory_order_relaxed)) {
            rate.store(clockRate, std::memory_order_relaxed);
            quantum.store(frames, std::memory_order_relaxed);
            cycled.store(true, std::memory_order_release);
            return;
        }
        const bool running = started.load(std::memory_order_relaxed) && !closing.load(std::memory_order_relaxed);
        if (clockRate != 0 && clockRate != rate.load(std::memory_order_relaxed) &&
            newRate.exchange(clockRate, std::memory_order_relaxed) != clockRate && running) {
            callback->deviceEvent(DeviceEvent::ResetRequest);
        }
        if (frames != quantum.exchange(frames, std::memory_order_relaxed) && running) {
            callback->deviceEvent(DeviceEvent::LatencyChanged);
        }
    }

    // --- the device (the engine's thread) ---

    // A name for each output device, unique among them.
    std::string deviceName(uint32_t id) const {
        const Node& node = nodes.at(id);
        for (const auto& [other, n] : nodes) {
            if (other != id && isOutputDevice(n) && n.description == node.description) {
                return node.description + " (" + node.name + ")";
            }
        }
        return node.description;
    }

    uint32_t findOutput(const std::string& name) const {
        uint32_t first = SPA_ID_INVALID;
        for (const auto& [id, node] : nodes) {
            if (!isOutputDevice(node)) continue;
            if (name.empty() ? node.name == defaultSink : deviceName(id) == name) return id;
            if (first == SPA_ID_INVALID) first = id;
        }
        return name.empty() ? first : SPA_ID_INVALID;  // no default known: the first
    }

    // The inputs that go with an output: its own (Duplex), the source on the
    // same card, else the default source.
    uint32_t findInput(uint32_t output, bool& isDefault) const {
        isDefault = false;
        const Node& out = nodes.at(output);
        if (isInputDevice(out)) return output;
        if (!out.deviceId.empty()) {
            for (const auto& [id, node] : nodes) {
                if (isInputDevice(node) && node.deviceId == out.deviceId) return id;
            }
        }
        for (const auto& [id, node] : nodes) {
            if (isInputDevice(node) && node.name == defaultSource) {
                isDefault = true;
                return id;
            }
        }
        return SPA_ID_INVALID;
    }

    // A device's ports in its own order: the inputs it plays (output = false),
    // or those it captures (true; a source's monitors only if it has nothing else).
    std::vector<uint32_t> devicePorts(uint32_t node, bool output) const {
        std::vector<uint32_t> found;
        bool anyPlain = false;
        for (const auto& [id, port] : ports) {
            if (port.node == node && port.output == output) {
                found.push_back(id);
                anyPlain = anyPlain || !port.monitor;
            }
        }
        if (output && anyPlain) {
            std::erase_if(found, [&](uint32_t id) { return ports.at(id).monitor; });
        }
        std::sort(found.begin(), found.end(), [&](uint32_t a, uint32_t b) { return ports.at(a).index < ports.at(b).index; });
        return found;
    }

    uint32_t ourPort(uint32_t node, const std::string& name) const {
        for (const auto& [id, port] : ports) {
            if (port.node == node && port.name == name) return id;
        }
        return SPA_ID_INVALID;
    }

    void link(uint32_t outNode, uint32_t outPort, uint32_t inNode, uint32_t inPort) {
        pw_properties* props = pw_properties_new(PW_KEY_OBJECT_LINGER, "false", nullptr);
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", outNode);
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", outPort);
        pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", inNode);
        pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", inPort);
        auto* proxy = static_cast<pw_proxy*>(
            pw_core_create_object(core, "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0));
        pw_properties_free(props);
        if (proxy) links.push_back(proxy);
    }

    // Under the lock.
    void closeDevice() {
        if (!filter) return;
        closing.store(true);
        for (pw_proxy* proxy : links) pw_proxy_destroy(proxy);
        links.clear();
        spa_hook_remove(&filterListener);
        pw_filter_destroy(filter);  // takes the node off the data thread: the callback has returned
        filter = nullptr;
        inPorts.clear();
        outPorts.clear();
        callback = nullptr;
        outputNode = inputNode = SPA_ID_INVALID;
        outputNodeName.clear();
        inputNodeName.clear();
        followsDefault = inputIsDefault = false;
        opened = DeviceState{};
        started.store(false);
        cycled.store(false);
        rate.store(0);
        quantum.store(0);
        newRate.store(0);
    }

    void open(const DeviceConfig& config, AudioCallback* cb);
};

void PipeWireBackend::Impl::open(const DeviceConfig& config, AudioCallback* cb) {
    {
        Lock lock(loop);
        sync();  // the graph as it is now
        const uint32_t output = findOutput(config.name);
        if (output == SPA_ID_INVALID) {
            throw std::runtime_error(config.name.empty() ? "PipeWire has no audio output"
                                                         : "Audio device not found: " + config.name);
        }
        bool inputIsDefaultSource = false;
        const uint32_t input = findInput(output, inputIsDefaultSource);
        const std::vector<uint32_t> devOut = devicePorts(output, false);
        const std::vector<uint32_t> devIn = input != SPA_ID_INVALID ? devicePorts(input, true) : std::vector<uint32_t>{};
        const std::string name = deviceName(output);
        if (devOut.empty()) throw std::runtime_error(name + " has no audio outputs");

        std::vector<int> outs = config.outputChannels;
        if (outs.empty()) outs = devOut.size() >= 2 ? std::vector<int>{0, 1} : std::vector<int>{0};
        auto check = [&](const std::vector<int>& channels, size_t count, const char* what) {
            std::set<int> seen;
            for (int c : channels) {
                if (c < 0 || static_cast<size_t>(c) >= count || !seen.insert(c).second) {
                    throw std::runtime_error(name + " has no " + what + " channel " + std::to_string(c + 1));
                }
            }
        };
        check(outs, devOut.size(), "output");
        check(config.inputChannels, devIn.size(), "input");

        // The rate and buffer size asked for: the graph runs at the smallest
        // latency its nodes ask for, and at our rate if it is allowed to change.
        const uint32_t askedRate = config.sampleRate ? config.sampleRate : clockRate;
        pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Duplex",
                                                 PW_KEY_MEDIA_ROLE, "Production", PW_KEY_NODE_NAME, "SUBstation",
                                                 PW_KEY_NODE_DESCRIPTION, "SUBstation", nullptr);
        if (config.bufferFrames) pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%u/%u", config.bufferFrames, askedRate);
        if (config.sampleRate) pw_properties_setf(props, PW_KEY_NODE_RATE, "1/%u", config.sampleRate);

        callback = cb;
        closing.store(false);
        filterState = PW_FILTER_STATE_UNCONNECTED;
        filterError.clear();
        // Until its first cycle says otherwise.
        rate.store(askedRate);
        quantum.store(config.bufferFrames ? config.bufferFrames : clockQuantum);
        filter = pw_filter_new(core, "SUBstation", props);
        if (!filter) {
            callback = nullptr;
            throw std::runtime_error("Could not open audio device: PipeWire refused a filter");
        }
        static const pw_filter_events filterEvents = [] {
            pw_filter_events events{};
            events.version = PW_VERSION_FILTER_EVENTS;
            events.state_changed = &Impl::onFilterState;
            events.process = &Impl::onProcess;
            return events;
        }();
        pw_filter_add_listener(filter, &filterListener, &filterEvents, this);
        auto addPort = [&](pw_direction direction, const std::string& portName) {
            void* port = pw_filter_add_port(filter, direction, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(int),
                                            pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                                                              PW_KEY_PORT_NAME, portName.c_str(), nullptr),
                                            nullptr, 0);
            if (!port) throw std::runtime_error("Could not open audio device: PipeWire refused a port");
            return port;
        };
        try {
            for (size_t i = 0; i < config.inputChannels.size(); ++i) {
                inPorts.push_back(addPort(PW_DIRECTION_INPUT, "input_" + std::to_string(i + 1)));
            }
            for (size_t o = 0; o < outs.size(); ++o) {
                outPorts.push_back(addPort(PW_DIRECTION_OUTPUT, "output_" + std::to_string(o + 1)));
            }
            silence.assign(kChunk, 0.f);
            discard.assign(static_cast<size_t>(kChunk) * outs.size(), 0.f);
            inBase.assign(inPorts.size(), nullptr);
            outBase.assign(outPorts.size(), nullptr);
            inputs.assign(inPorts.size(), nullptr);
            outputs.assign(outPorts.size(), nullptr);
            if (pw_filter_connect(filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0) < 0) {
                throw std::runtime_error("Could not open audio device: PipeWire refused the connection");
            }

            // Our node and ports, once the daemon has them.
            const auto deadline = std::chrono::steady_clock::now() + kOpenTimeout;
            std::vector<uint32_t> ourIn, ourOut;
            uint32_t node = SPA_ID_INVALID;
            for (;;) {
                if (!sync()) throw std::runtime_error("Lost the connection to PipeWire");
                if (filterState == PW_FILTER_STATE_ERROR) {
                    throw std::runtime_error("Could not open audio device: " + filterError);
                }
                node = pw_filter_get_node_id(filter);
                ourIn.clear();
                ourOut.clear();
                if (node != SPA_ID_INVALID) {
                    for (size_t i = 0; i < inPorts.size(); ++i) ourIn.push_back(ourPort(node, "input_" + std::to_string(i + 1)));
                    for (size_t o = 0; o < outPorts.size(); ++o) ourOut.push_back(ourPort(node, "output_" + std::to_string(o + 1)));
                    auto missing = [](uint32_t id) { return id == SPA_ID_INVALID; };
                    if (std::none_of(ourIn.begin(), ourIn.end(), missing) &&
                        std::none_of(ourOut.begin(), ourOut.end(), missing)) {
                        break;
                    }
                }
                if (std::chrono::steady_clock::now() > deadline) {
                    throw std::runtime_error("Could not open audio device: PipeWire didn't add its ports");
                }
            }

            coreError.clear();
            for (size_t o = 0; o < outs.size(); ++o) link(node, ourOut[o], output, devOut[static_cast<size_t>(outs[o])]);
            for (size_t i = 0; i < config.inputChannels.size(); ++i) {
                link(input, devIn[static_cast<size_t>(config.inputChannels[i])], node, ourIn[i]);
            }
            if (!sync()) throw std::runtime_error("Lost the connection to PipeWire");
            if (!coreError.empty()) throw std::runtime_error("Could not connect to " + name + ": " + coreError);
        } catch (...) {
            closeDevice();
            throw;
        }

        outputNode = output;
        inputNode = config.inputChannels.empty() ? SPA_ID_INVALID : input;
        outputNodeName = nodes.at(output).name;
        inputNodeName = input != SPA_ID_INVALID ? nodes.at(input).name : std::string();
        followsDefault = config.name.empty();
        inputIsDefault = followsDefault || inputIsDefaultSource;

        opened.driver = kName;
        opened.name = name;
        opened.inputChannels = config.inputChannels;
        opened.outputChannels = outs;
        for (uint32_t id : devIn) opened.capabilities.inputNames.push_back(channelLabel(ports.at(id)));
        for (uint32_t id : devOut) opened.capabilities.outputNames.push_back(channelLabel(ports.at(id)));
        std::vector<uint32_t> rates = allowedRates;
        if (std::find(rates.begin(), rates.end(), clockRate) == rates.end()) rates.push_back(clockRate);
        std::sort(rates.begin(), rates.end());
        opened.capabilities.sampleRates = rates;
        for (uint32_t size = 16; size <= std::min<uint32_t>(maxQuantum, 8192); size *= 2) {
            if (size >= minQuantum) opened.capabilities.bufferSizes.push_back(size);
        }
        opened.capabilities.preferredBufferFrames = clockQuantum;
    }
    // The rate and quantum it runs at come with its first cycle (not if the
    // device is suspended: then those asked for).
    const auto deadline = std::chrono::steady_clock::now() + kFirstCycleTimeout;
    while (!cycled.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

PipeWireBackend::PipeWireBackend() : d_(std::make_unique<Impl>()) {}

PipeWireBackend::~PipeWireBackend() {
    close();
    d_.reset();
}

std::vector<AudioDeviceInfo> PipeWireBackend::devices() {
    std::vector<AudioDeviceInfo> result;
    if (!d_->connect()) return result;
    Impl::Lock lock(d_->loop);
    d_->sync();
    for (const auto& [id, node] : d_->nodes) {
        if (isOutputDevice(node)) result.push_back({d_->deviceName(id), node.name == d_->defaultSink});
    }
    return result;
}

void PipeWireBackend::open(const DeviceConfig& config, AudioCallback* callback) {
    close();
    if (!d_->connect()) throw std::runtime_error("PipeWire isn't running");
    d_->open(config, callback);
}

void PipeWireBackend::start() {
    if (isOpen()) d_->started.store(true, std::memory_order_release);
}

void PipeWireBackend::close() {
    if (!d_->filter) return;
    Impl::Lock lock(d_->loop);
    d_->closeDevice();
}

bool PipeWireBackend::isOpen() const { return d_->filter != nullptr; }

DeviceState PipeWireBackend::state() const {
    if (!isOpen()) return {};
    DeviceState state = d_->opened;
    state.sampleRate = d_->rate.load();
    state.bufferFrames = d_->quantum.load();
    // A cycle each way: the graph reads the inputs one quantum before, and plays
    // what we wrote in the cycle after.
    state.outputLatency = state.bufferFrames;
    state.inputLatency = state.inputChannels.empty() ? 0 : state.bufferFrames;
    return state;
}

void PipeWireBackend::adjustForReset(DeviceConfig& config) {
    if (const uint32_t rate = d_->newRate.load()) config.sampleRate = rate;
}

}  // namespace sub
