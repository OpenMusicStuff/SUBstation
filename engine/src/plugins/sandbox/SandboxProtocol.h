#pragma once
// What the engine and a plug-in sandbox (the plug-in host process,
// substation-plugin-host: tools/pluginhost) say to each other. See
// docs/engine/sandboxes.md.
//
// Two ways:
// - Messages, on the sandbox's channel (platform/Ipc.h), a frame each: its
//   size (u32), then its kind, the plug-in instance it is about and the
//   request it answers or asks (u32 each), then its own fields (platform/
//   Bytes.h's, little-endian). The host's main thread works through the
//   engine's in order, but for loads, which wait while anything else is there
//   to do; the engine reads the host's on a thread of its own and takes them
//   on the main thread (Engine::idle()).
// - Audio: each plug-in's Block, in memory both processes map. The thread
//   rendering the plug-in fills the block, moves `request`, and queues the
//   plug-in's slot on the sandbox's Doorbell (memory both map too, with a
//   wake-up on its counter); one of the host's audio workers takes it,
//   processes the block in place, moves `done` to match and wakes the engine's
//   thread (a wake-up on `done`).
//
// Both sides are built from the same sources, so the block is laid out alike
// in both; the host says which version (and block size) it speaks when it
// starts (Ready), and a sandbox that speaks another is not used.

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "Processor.h"
#include "platform/Bytes.h"

namespace sub::platform {
class Channel;
}

namespace sub::sandbox {

inline constexpr uint32_t kVersion = 2;

enum class Kind : uint32_t {
    // Engine → host. (Instance: the plug-in it is about; request: answered by a Reply with it.)
    Create = 1,           // path, uid, sample rate (f64), max block (u32), block's memory name, state, name, slot (u32)
    Destroy,              // -
    Prepare,              // sample rate (f64), max block (u32)
    ResetOffline,         // - (request)
    SetParam,             // index (u32), plain value (f32): the plug-in's controller (its audio hears it in the block)
    ParamText,            // index (u32), plain value (f32) (request: the text)
    GetState,             // - (request: the state)
    SetState,             // state (request)
    OpenEditor,           // owner window (u64), title (request: opened)
    CloseEditor,          // -
    ShowEditor,           // shown (u8)
    SetEditorTitle,       // title
    CloseForemostEditor,  // - (request: one was shown)
    Prioritize,           // count (u32), instances (u32 each): those load first, in this order
    SetKeys,              // count (u32), (key, modifiers) (u32 each): the keys the application takes from editors
    HoldIdle,             // held (u8): no idle() for the plug-ins meanwhile (a render in the background)
    Quit,                 // -

    // Host → engine.
    Ready = 100,     // version (u32), block size (u32), process id (u32)
    Attached,        // - (it has the block: its name may go)
    Loaded,          // LoadedInfo
    LoadFailed,      // message
    Reply,           // (request) ok (u8), then the text, state or error message
    Report,          // what changed since the last one (Report flags), then events
    Busy,            // loading (u8): the main thread is in a plug-in's load
    Key,             // key (u32), modifiers (u32), repeat (u8), text field (u8): taken from an editor
    EditorRaised,    // - (the instance's editor came to the front)
};

struct Message {
    Kind kind = Kind::Quit;
    uint32_t instance = 0;
    uint32_t request = 0;
    std::string body;  // the kind's own fields

    platform::ByteReader reader() const { return platform::ByteReader(body); }
};

// A message's frame, as written.
std::string encode(const Message& message);
// Writes one (the caller makes sure one writer at a time); false if the channel closed.
bool writeMessage(platform::Channel& channel, const Message& message);
// Reads the next; false once the channel has ended (or a frame was garbled).
bool readMessage(platform::Channel& channel, Message& out);

// Fields beyond Bytes.h's.
void writeF64(platform::ByteWriter& out, double value);
double readF64(platform::ByteReader& in);
void writeBytes(platform::ByteWriter& out, const std::vector<uint8_t>& bytes);
std::vector<uint8_t> readBytes(platform::ByteReader& in);
void writeParams(platform::ByteWriter& out, const std::vector<ParamInfo>& params);
std::vector<ParamInfo> readParams(platform::ByteReader& in);
void writeValues(platform::ByteWriter& out, const std::vector<float>& values);
std::vector<float> readValues(platform::ByteReader& in);

// What a Report holds besides its events (flags, then in this order).
enum ReportPart : uint32_t {
    kReportParams = 1,   // the parameter list was rebuilt: parameters, then every value
    kReportValues = 2,   // values that changed: count (u32), (index u32, value f32) each
    kReportLatency = 4,  // latency, tail (u32 each)
    kReportPreset = 8,   // the preset's name
};

// What a plug-in is, once loaded (Loaded).
struct LoadedInfo {
    std::string name;
    bool hasEditor = false;
    bool hasSidechain = false;
    bool acceptsMidi = false;
    int latency = 0;
    int tail = 0;
    std::string presetName;
    std::vector<ParamInfo> params;
    std::vector<float> values;
    std::string problem;  // it loaded, but its state couldn't be restored: why ("" : nothing wrong)
};
void writeLoaded(platform::ByteWriter& out, const LoadedInfo& info);
LoadedInfo readLoaded(platform::ByteReader& in);

// An event a Report carries (Processor.h's ProcessorEvent, field by field).
void writeEvent(platform::ByteWriter& out, const ProcessorEvent& event);
ProcessorEvent readEvent(platform::ByteReader& in);

// --- The audio block -----------------------------------------------------------------------

inline constexpr int kMaxFrames = 1024;  // the renderer's largest block (Renderer::kMaxBlock)
inline constexpr int kMaxEvents = 2048;
inline constexpr int kMaxChanges = 512;  // parameter changes from the UI in one block
inline constexpr uint32_t kBlockMagic = 0x53554231;  // "SUB1"

enum BlockFlag : uint32_t {
    kBlockReset = 1,                // reset() it first (Processor::reset)
    kBlockSidechain = 2,            // the sidechain buffers hold its sidechain (else none: silence)
    kBlockSidechainConnected = 4,   // Processor::setSidechainConnected
};

// ProcessContext without its events (they are in the block).
struct BlockContext {
    double sampleRate = 48000.0;
    int64_t samplePos = 0;
    double beatPos = 0.0;
    double tempo = 120.0;
    double loopStartBeat = 0.0;
    double loopEndBeat = 0.0;
    int32_t timeSigNum = 4;
    int32_t timeSigDen = 4;
    int32_t keyTonic = -1;
    uint8_t playing = 0;
    uint8_t looping = 0;
    uint8_t keyMinor = 0;
    uint8_t offline = 0;

    static BlockContext from(const ProcessContext& ctx) noexcept;
    ProcessContext context() const noexcept;  // (without events)
};

// A parameter value the UI set, on its way to the plug-in's audio (VST3's id and normalized value).
struct BlockChange {
    uint32_t id = 0;
    uint32_t pad = 0;
    double normalized = 0.0;
};

// One plug-in's audio, both ways. The engine makes it (in memory both map) and
// fills in what the plug-in is to process; the host processes `audio` in place.
struct Block {
    uint32_t magic = kBlockMagic;
    uint32_t size = sizeof(Block);
    alignas(64) std::atomic<uint32_t> request{0};  // the engine's: the last block asked for
    alignas(64) std::atomic<uint32_t> done{0};     // the host's: the last block processed

    // What to process (the engine's, while request != done the host's to read).
    alignas(64) int32_t frames = 0;
    uint32_t flags = 0;
    BlockContext context;
    uint32_t numEvents = 0;
    uint32_t numAutomation = 0;
    uint32_t numChanges = 0;
    ProcessEvent events[kMaxEvents];
    ParamAutomation automation[Processor::kMaxAutomation];
    BlockChange changes[kMaxChanges];
    alignas(64) float audio[2][kMaxFrames];   // in, then out
    float sidechain[2][kMaxFrames];
};

// The name of a block's wake-up for the engine (beside the block's memory, named `memory`).
inline std::string doneSignalName(const std::string& memory) { return memory + "d"; }

// --- The doorbell ----------------------------------------------------------------------------

// Plug-ins in one sandbox at once: each has a slot of its own there (the engine
// gives it, with Create), which is what goes on the doorbell's queue.
inline constexpr uint32_t kMaxSlots = 4096;
inline constexpr uint32_t kDoorbellMagic = 0x53554244;  // "SUBD"

// A sandbox's blocks to process, in the order they were handed over: the slots
// of the plug-ins whose `request` moved. The engine's rendering threads queue
// them (push), the host's audio workers take them (pop): Vyukov's bounded
// queue, many to many, lock-free. A plug-in has one block on its way at most
// (the engine hands it the next once `done` has caught up), so the queue never
// fills; push() fails only if the memory was trampled (a crashed host's
// plug-in), and it never loops for long whatever is in it.
//
// Workers with nothing to take spin briefly, then count themselves asleep and
// wait on `rung`. Whoever queues a slot looks at `sleepers` after it (a full
// fence between) and, if any sleep, moves `rung` and wakes one (ring()).
struct Doorbell {
    uint32_t magic = kDoorbellMagic;
    uint32_t size = sizeof(Doorbell);
    alignas(64) std::atomic<uint32_t> rung{0};      // moved to wake a sleeping worker (a signal on it)
    alignas(64) std::atomic<uint32_t> sleepers{0};  // the host's workers asleep, or about to be
    alignas(64) std::atomic<uint64_t> tail{0};      // the next position to queue in
    alignas(64) std::atomic<uint64_t> head{0};      // the next position to take from
    struct Cell {
        std::atomic<uint64_t> sequence{0};
        uint32_t slot = 0;
        uint32_t pad = 0;
    };
    alignas(64) Cell cells[kMaxSlots];

    Doorbell() noexcept;  // (the engine's, in the memory it makes)
    bool push(uint32_t slot) noexcept;
    bool pop(uint32_t& slot) noexcept;
    // Whether something is queued that no worker has taken yet (a moment ago).
    bool waiting() const noexcept {
        return tail.load(std::memory_order_acquire) != head.load(std::memory_order_acquire);
    }
    // After queuing: whether a worker sleeps, to be woken (a full fence first).
    bool anyAsleep() const noexcept {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return sleepers.load(std::memory_order_seq_cst) > 0;
    }
};

// The name of a doorbell's wake-up (beside its memory, named `memory`).
inline std::string ringSignalName(const std::string& memory) { return memory + "r"; }

static_assert(std::atomic<uint64_t>::is_always_lock_free, "the doorbell's counters are shared by two processes");

}  // namespace sub::sandbox
