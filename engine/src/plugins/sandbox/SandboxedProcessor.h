#pragma once
// A plug-in running in a sandbox (a process of its own: Sandbox.h), as a
// Processor of the engine's. See docs/engine/sandboxes.md.
//
// It is in its chain at once and loads in its sandbox meanwhile: until it has
// (status() Loading), and if it couldn't (Failed) or its sandbox ended
// (Crashed), it passes its input on as a missing plug-in does. (Loading it again
// is a new one, from this one's last state: Engine::reloadProcessor().) Then:
//
// - Audio: process() hands each block to the sandbox in shared memory (the
//   track's audio, its sidechain, notes, automation and the UI's parameter
//   changes) and waits for it to come back processed, at most a little longer
//   than the block lasts live (offline, a minute). A block that doesn't come
//   back in time passes through unprocessed, and the next ones too until the
//   sandbox has caught up: a plug-in that hangs never holds up the audio
//   thread for more than one block, nor an offline render for more than one wait.
// - Before an offline render (resetOffline()), the reset is asked for without
//   waiting (the main thread, under the engine's lock, never waits on a sandbox
//   busy loading, or stuck); the render's first block waits for it, until 10 s
//   after it was asked: a sandbox's plug-ins wait 10 s in all, not each.
// - Parameters, latency, preset name, editor: copies of the sandbox's, kept up
//   to date from its reports, taken on the main thread in idle(); ProcessorEvents
//   say what changed, as a plug-in in this process would. Setting a value
//   changes the copy at once.
// - Questions (a value's text, the state) are asked and waited for, briefly,
//   when the sandbox is free; while it loads another plug-in the answer comes
//   later (a value's text then shows when it comes: ParamsChanged), so the
//   main thread never waits for a plug-in to load. The state is waited for (a
//   project is being saved), up to a limit; past it, or with the sandbox
//   gone, the last state known stands in.
//
// Main thread, as for any processor, but for process() and reset().

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Processor.h"
#include "platform/Ipc.h"
#include "plugins/sandbox/PluginSandboxes.h"
#include "plugins/sandbox/Sandbox.h"
#include "plugins/sandbox/SandboxProtocol.h"
#include "rt/RtUtils.h"

namespace sub::sandbox {

class SandboxedProcessor final : public Processor, private SandboxClient {
public:
    enum class Status : uint8_t { Loading, Ready, Failed, Crashed };

    // Starts loading the plug-in in the sandbox `sandboxes` picks for it, with
    // `state` (none: as it starts). Throws std::runtime_error if there is no
    // sandbox to load it in.
    SandboxedProcessor(PluginSandboxes& sandboxes, PluginOrigin origin, std::vector<uint8_t> state, double sampleRate,
                       int maxBlockSize);
    ~SandboxedProcessor() override;

    std::string typeId() const override { return "vst3:" + origin_.uid; }
    std::string name() const override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void resetOffline() override;
    void process(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) override;
    int latencySamples() const override { return latency_.load(std::memory_order_relaxed); }
    int tailSamples() const override { return tail_.load(std::memory_order_relaxed); }
    bool hasSidechain() const override { return hasSidechain_; }
    bool acceptsMidi() const override { return acceptsMidi_; }

    const std::vector<ParamInfo>& params() const override { return params_; }
    float getParam(int index) const override;
    void setParam(int index, float value) override;
    std::string paramText(int index, float value) const override;

    std::vector<uint8_t> getState() override;
    void setState(const std::vector<uint8_t>& state) override;
    std::string presetName() const override;

    bool idle() override;
    void takeEvents(std::vector<ProcessorEvent>& out) override;

    bool hasEditor() const override;
    bool openEditor(void* ownerWindow, const std::string& title) override;
    void closeEditor() override;
    bool isEditorOpen() const override { return editorOpen_ && editorVisible_; }
    bool setEditorVisible(bool visible) override;
    void setEditorTitle(const std::string& title) override;

    Status status() const { return status_; }
    // Why it isn't loaded (Failed, Crashed), or the last thing that went wrong
    // while it is (its state couldn't be restored).
    const std::string& problem() const { return problem_; }
    const std::string& sandboxLabel() const { return link_.sandbox->label(); }
    int sandboxPid() const { return link_.sandbox->pid(); }
    const std::shared_ptr<Sandbox>& sandbox() const { return link_.sandbox; }
    uint32_t instance() const { return link_.instance; }
    // Blocks that passed through because the sandbox didn't process them in time.
    uint64_t lateBlocks() const { return lateBlocks_.load(std::memory_order_relaxed); }

private:
    // Its sandbox and instance there, and the block they share.
    struct Link {
        std::shared_ptr<Sandbox> sandbox;
        uint32_t instance = 0;
        std::unique_ptr<platform::SharedMemory> memory;
        Block* block = nullptr;
        std::unique_ptr<platform::IpcSignal> request;
        std::unique_ptr<platform::IpcSignal> done;
        std::atomic<bool> ended{false};  // its sandbox ended
    };
    // What a reply that comes later was for.
    struct Asked {
        enum class What { Text, OpenEditor, SetState } what = What::Text;
        std::pair<int, uint32_t> text;  // Text: (index, value's bits)
        uint32_t editor = 0;            // OpenEditor: editorRequests_ then (a later close makes it stale)
        std::vector<uint8_t> state;     // SetState: the state, kept once it is taken
    };

    void received(Message message) override;
    void sandboxEnded() override;
    void apply(const Message& message, bool& latencyChanged);
    void applyLoaded(const LoadedInfo& info);
    void setParams(std::vector<ParamInfo> params, const std::vector<float>& values);
    void pushEvent(ProcessorEvent::Type type);
    void send(Kind kind, std::string body = {}) const;
    void ask(Kind kind, std::string body, Asked asked) const;
    bool waitDone(uint32_t sent, int64_t timeoutMicroseconds) noexcept;
    void waitForOfflineReset();
    bool loaded() const { return status_ == Status::Ready; }
    bool ended() const { return link_.ended.load(std::memory_order_acquire); }

    const PluginOrigin origin_;
    double sampleRate_;
    int maxBlock_;

    Link link_;
    std::atomic<bool> ready_{false};  // process() may hand it blocks
    uint32_t sent_ = 0;               // the rendering thread's: the block it handed over last
    bool offlineLate_ = false;        // the rendering thread's: an offline block missed its wait (no more waits)
    int64_t offlineWaitUs_ = 0;       // how long an offline block is waited for (PluginSandboxes')
    std::atomic<bool> resetPending_{false};
    std::atomic<uint64_t> lateBlocks_{0};
    SpscQueue<BlockChange, 1024> changes_;  // the UI's values on their way to the plug-in's audio

    // The offline reset asked for (resetOffline()), and its answer, from the reader thread.
    std::atomic<uint32_t> resetRequest_{0};  // 0: none to wait for
    std::atomic<std::chrono::steady_clock::rep> resetDeadline_{0};  // waited for until then (steady clock)
    std::mutex resetMutex_;
    std::condition_variable resetAnswered_;
    uint32_t resetAnswer_ = 0;  // (resetMutex_) the request answered last

    // What the reader thread received, for idle().
    std::mutex inboxMutex_;
    std::vector<Message> inbox_;
    bool endedSeen_ = false;  // (in the inbox: its sandbox ended)

    // The main thread's copy of the plug-in.
    Status status_ = Status::Loading;
    std::string problem_;
    std::string name_;
    bool hasEditor_ = false;
    bool hasSidechain_ = false;
    bool acceptsMidi_ = false;
    std::atomic<int> latency_{0};
    std::atomic<int> tail_{0};
    std::vector<ParamInfo> params_;
    std::vector<uint32_t> paramIds_;  // VST3's id of each
    mutable std::mutex valuesMutex_;  // values_ against getParam() from other threads; presetName_
    std::unique_ptr<std::atomic<float>[]> values_;
    size_t valueCount_ = 0;
    std::string presetName_;
    std::vector<uint8_t> lastState_;  // the last state it is known to have had (it loads again with this)
    bool editorOpen_ = false;
    bool editorVisible_ = true;
    uint32_t editorRequests_ = 0;  // opened or closed: replies to earlier openings don't count
    std::vector<ProcessorEvent> events_;

    // Values' texts asked of it, and what replies that come later are for.
    mutable std::map<std::pair<int, uint32_t>, std::string> texts_;
    mutable std::set<std::pair<int, uint32_t>> asking_;
    mutable std::map<uint32_t, Asked> asked_;  // request -> what
};

}  // namespace sub::sandbox
