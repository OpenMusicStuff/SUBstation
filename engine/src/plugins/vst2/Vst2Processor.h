#pragma once
// A VST2 plug-in (instrument or effect) as a `Processor`.
//
// The track is stereo. A VST2 plug-in has numbered inputs and outputs, no buses:
// its first two inputs get the track (mixed to mono for a one-input plug-in),
// the next two its sidechain when it has more than two (as plug-ins with a
// sidechain lay theirs out; Processor::hasSidechain), silence when none; its
// first two outputs go back to the track (a mono one to both sides), the rest
// are ignored.
//
// Parameters are indexed and normalized (0..1) in VST 2: presented as they are,
// continuous, or as Off/On for those the plug-in calls switches. The UI's
// changes go to the plug-in at once (setParameter, from the main thread, as VST2
// hosts do); automation goes to it on the rendering thread, the block split
// where values change (VST 2 has no sample-accurate parameters), down to
// kMinSegment frames. Notes and MIDI go to it as MIDI events (VST 2 has no
// per-note expressions: a note's bend is dropped).
//
// Its state is an .fxb of its chunk for a plug-in that keeps one, else an .fxp
// of its program's values (Vst2Preset.h). Its editor is held by an EditorWindow
// through a VST 3 view standing in for it (EditorView), so the windows are the
// same as VST3 plug-ins'.
//
// Threads: process() and reset() on the rendering thread, all else on the main
// thread; the host callback may come from anywhere (editors from their own
// threads, the plug-in's process()), so what it touches is thread-safe.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Processor.h"
#include "plugins/editor/EditorWindow.h"
#include "plugins/PluginFormat.h"
#include "plugins/vst2/Vst2Sdk.h"
#include "plugins/vst3/Vst3Support.h"

namespace sub::vst2 {

class Module;

class Vst2Processor final : public PluginProcessor {
public:
    // Opens the instance (made by `module`) and reads its parameters. Throws std::runtime_error.
    Vst2Processor(std::shared_ptr<Module> module, vst_effect_t* effect);
    ~Vst2Processor() override;

    std::string typeId() const override { return "vst2:" + uid_; }
    std::string name() const override { return name_; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void resetOffline(bool rendering) override;
    void process(const ProcessContext& ctx, float* const* channels, int numChannels, int numFrames) override;
    int latencySamples() const override { return latency_.load(std::memory_order_relaxed); }
    int tailSamples() const override { return tail_; }
    bool hasSidechain() const override { return sidechainInputs_ > 0; }
    bool acceptsMidi() const override { return acceptsMidi_; }

    const std::vector<ParamInfo>& params() const override { return params_; }
    float getParam(int index) const override;
    void setParam(int index, float value) override;
    std::string paramText(int index, float value) const override;
    std::optional<std::pair<uint32_t, double>> setControllerParam(int index, float value) override;
    void queueParamChange(uint32_t id, double normalized) noexcept override;

    std::vector<uint8_t> getState() override;
    void setState(const std::vector<uint8_t>& state) override;
    std::string presetName() const override;

    bool idle() override;
    void takeEvents(std::vector<ProcessorEvent>& out) override;

    bool hasEditor() const override { return (effect_->flags & VST_EFFECT_FLAG_EDITOR) != 0; }
    bool openEditor(void* ownerWindow, const std::string& title) override;
    void closeEditor() override;
    bool isEditorOpen() const override;
    bool setEditorVisible(bool visible) override;
    void setEditorTitle(const std::string& title) override;

    // The plug-in's calls to us (Vst2Format's host callback), from any thread.
    intptr_t hostCall(int32_t opcode, int32_t index, int64_t value, void* ptr, float opt);

    static constexpr int kMinSegment = 32;  // automation splits blocks into calls at least this long
    static constexpr size_t kMaxEvents = 2048;

private:
    class EditorView;
    friend class EditorView;

    struct Gesture {
        uint32_t serial = 0;
        float startValue = 0.f;
    };

    intptr_t dispatch(int32_t opcode, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr,
                      float opt = 0.f) const;

    // Setup (main thread; the plug-in is not being processed).
    void activate();
    void deactivate();
    void allocateBuffers();
    void buildParams();
    bool refreshValues();  // true if any changed

    // Rendering thread.
    void buildEvents(const ProcessContext& ctx);
    void fillTime(const ProcessContext& ctx);
    void runSegment(int start, int frames, const ProcessContext& ctx);
    bool addMidi(int32_t offset, uint8_t status, uint8_t data1, uint8_t data2) noexcept;

    // Host callback.
    void edited(int index, float value);
    void beginEdit(int index);
    void endEdit(int index);
    void pushEvent(const ProcessorEvent& event);

    // The loaded program's name (main thread): its name if it isn't a placeholder,
    // else a name in its chunk. Pushes PresetChanged if it changed.
    void refreshPresetName(bool stateChanged);
    void dropEditor();

    // The library outlives the instance, so it comes first.
    std::shared_ptr<Module> module_;
    vst_effect_t* effect_;
    std::string uid_;
    std::string name_;
    std::string directory_;  // the plug-in's folder (VST_HOST_OPCODE_GET_EFFECT_DIRECTORY)
    bool acceptsMidi_ = false;
    bool replacing_ = true;  // it has process_float (else only the old accumulating process)
    bool active_ = false;
    std::atomic<bool> offline_{false};  // an offline render processes it (resetOffline()): its process level says so

    std::atomic<double> sampleRate_{48000.0};
    std::atomic<int> maxBlock_{0};
    std::atomic<int> latency_{0};
    int tail_ = 0;

    // Channels and buffers (rebuilt with the plug-in inactive).
    int numInputs_ = 0;
    int numOutputs_ = 0;
    int sidechainInputs_ = 0;  // inputs 2 and 3 (at most): the sidechain
    std::vector<std::vector<float>> inputs_, outputs_;
    std::vector<float*> inputPointers_, outputPointers_;

    // Rendering-thread state.
    vst3::ProcessGuard guard_;
    std::vector<vst_event_midi_t> midi_;  // the block's events
    std::vector<int32_t> midiOffsets_;    // and their offsets from the block's start
    size_t numMidi_ = 0;
    size_t nextMidi_ = 0;                 // the first not handed to the plug-in yet
    EventList<kMaxEvents> eventList_;     // one call's
    uint8_t held_[16][128] = {};          // note-ons without a note-off yet
    std::atomic<bool> releaseAll_{false};
    TimeInfo time_{};      // what kGetTime answers: the current call's
    TimeInfo blockTime_{};  // the block's start
    bool lastPlaying_ = false;

    vst3::ParamChangeQueue toAudio_;                        // a sandbox's values for the audio (index, value)
    SpscQueue<vst3::ParamChange, 4096> fromAudio_;          // values the plug-in set itself in process()
    std::atomic<bool> ioChanged_{false};                    // VST_HOST_OPCODE_IO_MODIFIED: re-read in idle()
    std::atomic<bool> displayChanged_{false};               // VST_HOST_OPCODE_REFRESH: names and values
    // While we set a value ourselves: the plug-in may report it back as if edited.
    std::atomic<int> settingParams_{0};

    // Automation: the last automated value of each parameter since the last idle()
    // (kNotAutomated: none), set up with the buffers.
    static constexpr float kNotAutomated = -1.f;
    std::unique_ptr<std::atomic<float>[]> automated_;
    size_t numAutomated_ = 0;
    std::atomic<bool> automationPending_{false};

    // Parameters. mutex_ guards the lists and the pending events against a host
    // call from another thread; values_ is read anywhere.
    mutable std::mutex mutex_;
    std::vector<ParamInfo> params_;
    std::unique_ptr<std::atomic<float>[]> values_;
    std::unordered_map<int, Gesture> gestures_;
    uint32_t gestureCounter_ = 0;
    std::vector<ProcessorEvent> pending_;

    // Preset name (presetName_ under mutex_): looked at when the state may have
    // changed, at most every 250 ms, and every 2 s while the editor is open.
    std::string presetName_;
    std::atomic<bool> presetStateChanged_{true};
    bool chunkHasName_ = true;  // its chunk had a name when last read: worth reading again
    std::chrono::steady_clock::time_point lastPresetCheck_{};

    std::unique_ptr<vst3::EditorWindow> editor_;
    EditorView* view_ = nullptr;  // the editor's view while editor_ holds it (main thread only)
    // The main thread (editors' and views' own), and a size the plug-in asked for
    // from another thread: the window follows in the next idle() (width << 32 |
    // height; 0: none), as the view is the main thread's alone.
    std::thread::id mainThread_ = std::this_thread::get_id();
    std::atomic<uint64_t> pendingResize_{0};
    std::optional<vst3::EditorWindow::Position> editorPosition_;
};

}  // namespace sub::vst2
