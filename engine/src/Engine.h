#pragma once
// The engine's public API: what the application layer's engine bridge
// (app/src/audio/EngineBridge.h), the plug-in scanner and the engine's tests use.
// No Qt here: the engine builds and runs on its own.
//
// Routing (Routing.h): every track's output goes to the master or into another
// track (a bus: a group track), and its sends into other tracks (return
// tracks); a track may take its input from another track's output (an input
// edge: resampling), and a device its sidechain from another track's signal (an
// edge into that device). The snapshot lists the tracks so that each comes after
// what feeds it, and lines up the edges going into every bus and device.
//
// Threading model:
//  * The audio thread (device callback) only reads the published RenderSnapshot
//    and atomics. It never locks, allocates or frees.
//  * API calls may come from any thread. They serialise on `mutex_`,
//    mutate the edit model, then rebuild and publish a new snapshot.
//  * Plug-ins are created, called and destroyed on the main (UI) thread, as
//    plug-in formats require: the plug-in calls below, and idle(), must come
//    from the thread that created the engine. Plug-ins may run a message loop
//    inside a call (a licence dialog), which can call back into the engine from
//    the same thread, so `mutex_` is recursive and slow plug-in calls are made
//    without holding it.
//  * Audio devices, likewise, are opened and closed on the main thread. An
//    ASIO driver's control panel may run a message loop too; the device stays
//    as it is until the panel closes.
//  * Recording: the audio thread hands the input to a RecordingSession's
//    rings, and its own thread writes the files (Recorder.h). Anything that
//    changes the device (or its sample rate), or renders offline, ends the
//    recording first, cleanly: the takes so far are kept for stopRecording().
//  * MIDI input devices are opened and closed on the main thread too. Their
//    messages arrive on the drivers' threads, are stamped against the audio
//    device's clock there and queued for the audio thread (MidiInput.h); they
//    never take `mutex_`. Without a running audio device they are dropped.
//  * Audio threads (Scheduler.h): the thread rendering (the audio thread, or the
//    one rendering offline) shares the tracks of each chunk with a pool of
//    workers. They touch only what the snapshot hands them, like the audio thread.
//  * Renders in the background (RenderJob.h) run on a thread of their own, on
//    the snapshot they began with (which they hold), without `mutex_`; live
//    output is silent until the main thread finishes them. Meanwhile processors
//    aren't idled (a plug-in restarting would leave a gap in the render).

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "AudioDevice.h"
#include "AudioSource.h"
#include "Automation.h"
#include "MidiInput.h"
#include "Modulation.h"
#include "NoteBend.h"
#include "PluginHosting.h"
#include "Processor.h"
#include "Recorder.h"
#include "RenderJob.h"
#include "Renderer.h"
#include "Routing.h"
#include "Scheduler.h"
#include "Snapshot.h"
#include "StreamCache.h"
#include "StreamScheduler.h"
#include "Transport.h"
#include "WarpCache.h"
#include "rt/RtUtils.h"

namespace sub {

namespace sandbox {
class PluginSandboxes;
}

struct ClipDesc {
    std::string path;
    double startBeat = 0.0;
    double durationSec = 0.0;   // of source audio
    double offsetSec = 0.0;     // into the source
    float gain = 1.f;
    float pan = 0.f;            // -1 (left) .. 1 (right), balance
    // Warping. Unwarped clips play at the file's own speed, so their length in
    // beats follows the tempo. Warped clips are locked to beats: `segmentBpm` is
    // the tempo of the source audio, and they play at tempo / segmentBpm speed.
    bool warp = false;
    double segmentBpm = 0.0;
    // Warped, a source whose tempo changes along it (a track frozen under a tempo map): where its
    // seconds, from the file's start, fall in its beats. Null: segmentBpm throughout.
    std::shared_ptr<const TempoMap> sourceMap;
    WarpMode warpMode = WarpMode::Standard;
    double transpose = 0.0;     // semitones (fractions for detune); ignored by Re-Pitch
    std::string id;             // stable clip identity, so edits don't interrupt a stretching clip
    // Fades at its ends, in seconds of source audio (so they stretch with it),
    // each bent by its curve (-1..1, automationShape()). Without one an edge
    // gets the short click-free fade (setClipFadeMs), unless it is the file's
    // own start or end.
    double fadeInSec = 0.0;
    double fadeOutSec = 0.0;
    float fadeInCurve = 0.f;
    float fadeOutCurve = 0.f;
};

// A note on a MIDI track, in timeline beats. The UI flattens MIDI clips into
// these, keeping only what the clips play (notes cut at their clip's end).
// Its bend (NoteBend.h: MIDI 2.0's per-note pitch bend) is in beats from its
// start, its vibratos' rates in cycles a second (Hz).
struct NoteDesc {
    double startBeat = 0.0;
    double lengthBeats = 0.25;
    int key = 60;  // MIDI note number; 60 = C3
    int velocity = 100;
    std::vector<BendPoint> bend;       // sorted by time
    std::vector<VibratoSpan> vibrato;
};

// A track to record, and the file its take goes to (a WAV file, created anew).
// No file: the track records its MIDI input instead.
struct RecordTarget {
    uint32_t trackId = 0;
    std::string path;
};

// A take while it records, for the UI's live waveform (or notes).
struct RecordingProgress {
    uint32_t trackId = 0;
    bool started = false;     // the playhead moved (after any count-in) and input arrives
    int64_t startSample = 0;  // where it goes on the timeline (audio: latency-corrected, may be negative)
    int64_t frames = 0;
    // Peaks since the last call: (min, max) pairs, each over kPeakFrames frames of all its channels.
    std::vector<float> peaks;
    bool midi = false;
    std::vector<RecordedNote> notes;  // a MIDI take's notes so far (held ones end at -1)
    // Where the loop wrapped so far: startSample, frames and notes are in
    // recording time (RecordedTake::wraps).
    std::vector<LoopWrap> wraps;
};

// What a track played live is heard after (Engine::liveLatency()), in frames.
struct LiveLatency {
    bool live = false;       // monitored and constrained: no delay compensation on its way out
    int midiDelay = 0;       // MIDI input's (a device buffer, or none: Engine::setMidiImmediate())
    int devices = 0;         // its devices', its groups' and the master's
    int compensation = 0;    // delay compensation on its way to the master
    int output = 0;          // the device's output latency (as its driver reports it)
    int clipDelay = 0;       // live: how late its own clips' notes play, in time with the other tracks (TrackRender::clipDelay)
    int total() const { return midiDelay + devices + compensation + output; }
};

// The audio device's sample clock as MIDI input sees it (MidiInput.h).
struct AudioClockStatus {
    bool running = false;
    int64_t hostTimeNs = 0;   // when the last callback began (hostTimeNs())
    int64_t sampleTime = 0;   // the device sample it began with
    int midiDelay = 0;        // frames between a MIDI message's arrival and when it plays
};

// Where a sidechain takes its source's signal: after its fader (and pan), before
// it (after all its devices), after one of its devices, or before all of them
// (PreFx: the strip's own input, as its devices hear it).
enum class SidechainTap : uint8_t { PostFader, PreFader, AfterDevice, PreFx };

// A device's sidechain (Engine::processorSidechain()).
struct SidechainInfo {
    uint32_t trackId = 0;  // its source
    SidechainTap tap = SidechainTap::PostFader;
    uint32_t tapProcessorId = 0;  // AfterDevice: the source's device it is taken after
};

// A track's send (Engine::trackSends()).
struct SendInfo {
    uint32_t trackId = 0;  // the track it goes into (a return)
    float gain = 1.f;
    bool preFader = false;
};

// What rendering a track takes lately (Engine::trackCosts()).
struct TrackCost {
    uint32_t trackId = 0;
    float nsPerFrame = 0.f;  // 0: not rendered yet
};

struct MeterReading {
    uint32_t trackId = 0;  // 0 = master
    float left = 0.f;
    float right = 0.f;
    uint32_t chainId = 0;  // a rack chain's fader (on track trackId); 0: the track's own
};

// The time the audio thread (and its workers) spent on each track and device
// over `wallNs` of wall-clock time (Engine::takeCpu): a track's or device's
// share of one core is its time / wallNs. A track's time includes its devices';
// a rack's devices are measured each (the rack is what they add up to).
struct CpuReading {
    int64_t wallNs = 0;
    std::vector<std::pair<uint32_t, uint64_t>> tracks;      // (track id, ns); kMaster: the master
    std::vector<std::pair<uint32_t, uint64_t>> processors;  // (processor id, ns)
};

struct DeviceMeterReading {
    uint32_t processorId = 0;
    float left = 0.f;
    float right = 0.f;
};

// Whether a plug-in has loaded. One in this process has (or it wouldn't be
// there); one in a sandbox loads after it is added (PluginHosting.h).
enum class PluginStatus : uint8_t {
    Ready,    // loaded (and every built-in device)
    Loading,  // in its sandbox: it passes its input on meanwhile
    Failed,   // it couldn't be loaded there (`problem` says why)
    Crashed,  // its sandbox ended (`problem`): it passes its input on until reloaded (reloadProcessor())
};

// A processor's description for the UI.
struct ProcessorInfo {
    std::string typeId;
    std::string name;
    int latency = 0;  // samples
    int tail = 0;
    bool hasEditor = false;
    bool hasSidechain = false;  // it has a sidechain (aux) input: setProcessorSidechain()
    bool acceptsMidi = false;   // it plays notes (an instrument, an effect with a MIDI input): setProcessorMidiInput()
    bool constrained = false;  // bypassed while delay compensation is constrained (Engine::setConstrainLatency())
    PluginStatus status = PluginStatus::Ready;
    // Failed, Crashed: why; Ready: what last went wrong (ProcessorEvent::Problem: its state wasn't taken).
    std::string problem;
    std::string sandbox;  // the sandbox it runs in, as the user knows it ("" : in this process)
    int sandboxPid = 0;
};

// Which plug-in maker made a plug-in and what it is called, as the scan found
// them: which sandbox it goes to (by maker), and its name until it has loaded.
struct PluginIdentity {
    std::string name;
    std::string vendor;
};

// A sandbox plug-ins run in (Engine::pluginSandboxes()).
struct PluginSandboxInfo {
    std::string label;  // what it holds: "All plug-ins", a maker, a plug-in
    int pid = 0;
    bool running = false;
    size_t plugins = 0;  // in it now
};

// A key pressed in a sandboxed plug-in's editor that the application takes
// (Engine::setSandboxEditorKeys()): the system's key code (an X11 keysym,
// unshifted; a Windows virtual-key code), EditorHost.h's modifiers, held down,
// in a text field the system knows.
struct SandboxEditorKey {
    uint32_t key = 0;
    int modifiers = 0;
    bool repeat = false;
    bool textField = false;
};

// A ProcessorEvent and the processor it came from.
struct ProcessorEventRecord : ProcessorEvent {
    uint32_t processorId = 0;
};

// What an export writes (Engine::startExport): a WAV file, `bitDepth` 16 or 24
// (integer; 16 dithered) or 32 (float); or an MP3 file at a constant `bitrate`
// in kbps, 32 to 320 (AudioFileWriter.h).
struct ExportFormat {
    enum class Kind { Wav, Mp3 };
    Kind kind = Kind::Wav;
    int bitDepth = 24;
    int bitrate = 320;
};

// A track's render into a file of its own (Engine::startTracksRender).
struct TrackFile {
    uint32_t trackId = 0;
    std::string path;
};

struct DeviceStatus {
    bool open = false;
    std::string name;
    std::string backend;  // the driver type: "WASAPI", "ASIO", "PipeWire" or "System"
    uint32_t sampleRate = 0;
    uint32_t bufferFrames = 0;
    double latencyMs = 0.0;       // output latency
    double inputLatencyMs = 0.0;
    std::vector<int> inputChannels;   // the device channels open, in the order the engine sees them
    std::vector<int> outputChannels;  // the master plays on the first two (or mixed to mono on one)
    bool exclusive = false;
    // Dropouts since the device opened: callbacks that took longer than their
    // buffer lasts, and gaps in the device's sample clock (it lost samples: ASIO,
    // PipeWire; one late callback that caused the gap counts once).
    uint64_t dropouts = 0;
};

// Streaming long files from disk instead of decoding them into memory
// (docs/engine/streaming.md; Engine::setStreaming()).
struct StreamingSettings {
    bool enabled = false;  // (off by default until it has had a Canary release)
    // Files at least this long stream; shorter ones (one-shots, loops) are decoded into memory.
    double thresholdSeconds = 30.0;
    // The memory the pages streamed through may take (the heads of files and the starts of clips,
    // kept in memory, come on top).
    size_t poolBytes = StreamCache::kDefaultPoolBytes;
    // Where cache files (decoded files) and peak files go ("": StreamCache::defaultFolder()), and
    // how much they may take there (the least recently used go first).
    std::string cacheFolder;
    uint64_t cacheBudgetBytes = StreamCache::kDefaultFolderBudget;
    int diskThreads = StreamScheduler::kDefaultThreads;  // reads in flight: 1 for a spinning disk, more for an SSD
    double horizonSeconds = StreamScheduler::kDefaultHorizon;  // how far ahead of the playhead the disk reads

    friend bool operator==(const StreamingSettings&, const StreamingSettings&) = default;
};

// How long an edit waits for an audio callback under way to finish, and what it throws when one
// is held up (a plug-in not returning) longer: nothing changed; it may be tried again.
inline constexpr int kCallbackWaitMs = 500;
inline constexpr const char* kAudioThreadBusy =
    "The audio thread is busy (a plug-in isn't returning): try again in a moment";

class Engine final : private AudioCallback {
public:
    Engine();
    ~Engine() override;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // --- Batches --------------------------------------------------------------
    // Every edit publishes a new snapshot, which rebuilds the whole of it. Edits
    // made while a Batch lives (from any thread) only mark it stale; the last
    // Batch to end publishes it once. Whatever reads the snapshot on the edit
    // side (latencies, a processor's info, an offline render) publishes it first,
    // so a batch changes when the audio thread hears edits, never what the API
    // answers. Keep batches short: the audio thread plays the snapshot from
    // before the batch until it ends.
    class Batch {
    public:
        explicit Batch(Engine& engine) : engine_(engine) { engine_.beginBatch(); }
        ~Batch() { engine_.endBatch(); }
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;

    private:
        Engine& engine_;
    };
    void beginBatch();
    void endBatch() noexcept;
    // Snapshots built since the engine began (for tests and benchmarks).
    uint64_t snapshotBuilds();
    // A source decoded at the engine's rate and cached, as loadSource() does, but
    // not yet heard: the snapshot is only marked stale. Whoever decodes in the
    // background publishes the sources that arrived together (a Batch, or any
    // edit), instead of rebuilding the snapshot once a file.
    std::shared_ptr<AudioSource> decodeSource(const std::string& path);

    // --- Device ---------------------------------------------------------------
    // Opening and closing devices and their control panels: from the thread that
    // created the engine (ASIO drivers are COM objects in its apartment).
    static std::vector<std::string> driverTypes() { return AudioDevice::driverTypes(); }
    std::vector<AudioDeviceInfo> devices(const std::string& driver);
    void openDevice(const DeviceConfig& config);
    // Opens the last device again, with the settings its driver asked for: the
    // answer to a "reset" event.
    void reopenDevice();
    void closeDevice();
    DeviceStatus deviceStatus();
    DeviceCaps deviceCapabilities();
    // The driver's own settings dialog (ASIO). False if there is none.
    bool showDeviceControlPanel();
    double sampleRate();
    float cpuLoad() const { return shared_.cpuLoad.load(std::memory_order_relaxed); }
    // One event at a time: "", "stopped", "rerouted", "reset" (the driver needs
    // reopenDevice()) or "latency" (the device status has new latencies).
    std::string takeDeviceEvent();
    // The peak level of each open input channel since the last call.
    std::vector<float> takeInputMeters();
    // The last `frames` samples of the master output (mono, oldest first; at
    // most half the scope ring), and how many samples have been played in all
    // (it stands still while no device runs).
    std::vector<float> masterScope(size_t frames) const;
    uint64_t masterScopeWritten() const { return shared_.scopeWritten.load(std::memory_order_acquire); }

    // --- Sources --------------------------------------------------------------
    // Cached; blocking. A file long enough streams from disk while streaming is
    // on (setStreaming()): its cache file and peak file are made here first if it
    // needs them. One cached already is loaded again if streaming was switched
    // since (the clips playing it go on with the old one until the new arrives).
    std::shared_ptr<AudioSource> loadSource(const std::string& path);
    std::shared_ptr<AudioSource> cachedSource(const std::string& path);
    // The file decoded into memory whatever its length (built-in devices' samples: a device reads its
    // sample anywhere, note by note). Cached apart from loadSource()'s; blocking.
    std::shared_ptr<AudioSource> loadResidentSource(const std::string& path);
    // A source loadSource() returned, cached again if releaseUnusedSources() let go of it
    // meanwhile (not one at another rate); the clips waiting for its file become audible.
    void cacheSource(std::shared_ptr<AudioSource> source);
    void releaseUnusedSources();

    // --- Tracks ---------------------------------------------------------------
    // Track id 0 is the master: it has devices, a mixer and automation like a
    // track, but no clips or notes, and it never mutes or solos.
    static constexpr uint32_t kMaster = 0;
    uint32_t addTrack();
    void removeTrack(uint32_t trackId);
    void setTrackClips(uint32_t trackId, const std::vector<ClipDesc>& clips);
    void setTrackNotes(uint32_t trackId, const std::vector<NoteDesc>& notes);
    // Plays a note on the track's instrument right away (velocity 0 releases it),
    // e.g. while editing notes. Only heard while a device runs.
    void previewNote(uint32_t trackId, int key, int velocity);
    void setTrackGain(uint32_t trackId, float gain);  // also the master's (and pan)
    void setTrackPan(uint32_t trackId, float pan);
    void setTrackMute(uint32_t trackId, bool mute);
    void setTrackSolo(uint32_t trackId, bool solo);
    // Routing: where a track's output goes, the master (kMaster) or another
    // track, which then sums it into its own input (a group's bus), or nowhere
    // (kNoOutput: only its sends, and what takes its output as an input or a
    // sidechain, hear it: Ableton's Sends Only). The engine sees only these
    // edges, never groups. Throws std::invalid_argument for an unknown track, or
    // a route that would close a cycle (a track into itself, or into a track it
    // feeds). When a track goes, what went into it goes to the master. Delay
    // compensation lines up the inputs of every bus.
    static constexpr uint32_t kNoOutput = 0xFFFFFFFFu;
    void setTrackOutput(uint32_t trackId, uint32_t outputTrackId);
    // kMaster, a track, or kNoOutput (also while it goes into a device: trackOutputSidechain()).
    uint32_t trackOutput(uint32_t trackId);
    // A track's output into a device's sidechain (aux) input instead (Ableton's
    // Audio To a device's sidechain), after its fader: heard only there, summed
    // with the device's own sidechain (setProcessorSidechain()) and the other
    // tracks' outputs going into it, each lined up as a sidechain is. The
    // device may be on any track, or the master. setTrackOutput() takes it
    // away; when the device goes, the track goes into the master. Throws
    // std::invalid_argument for an unknown track or device, a device without a
    // sidechain input, or a route that would close a cycle (the device on the
    // track, or on one it feeds).
    void setTrackOutputSidechain(uint32_t trackId, uint32_t processorId);
    uint32_t trackOutputSidechain(uint32_t trackId);  // the device its output goes into (0: none)
    // What goes into a track (other tracks' outputs): summed into it always, as
    // a bus does (a group, a return: the default), or, as Ableton's Track In on
    // an audio track, taken as its input (`monitored`): heard only while the
    // track monitors (setTrackMonitor()), instead of its clips and with its own
    // input, without delay compensation, as that input is; not recorded.
    void setTrackInMonitored(uint32_t trackId, bool monitored);
    // Sends: a track's signal also goes into another track (a return track), at
    // `gain`, taken after its fader or before it (`preFader`; a muted track's
    // sends are silent either way). One send per pair; setting it again changes
    // it (a new gain alone is cheap: no new snapshot). Throws
    // std::invalid_argument for an unknown track, the master, or a send that
    // would close a cycle (through outputs and sends alike). When a track goes,
    // the sends into it go too. Its automation: a lane of the sending track,
    // processorId 0 and param "send:<track id>" (as volume: 0..1, +6 dB at 1).
    void setTrackSend(uint32_t trackId, uint32_t toTrackId, float gain, bool preFader);
    void removeTrackSend(uint32_t trackId, uint32_t toTrackId);
    std::vector<SendInfo> trackSends(uint32_t trackId);
    // Freezing: a frozen track plays its clips (its frozen audio: see
    // renderTrackToWav()) through its fader and on along its edges, and nothing
    // else: its devices, notes, input and what goes into it (outputs, sends) are
    // left out, and so is their latency. A track every edge of which ends at a
    // frozen track (or at a track like it: what is in a frozen group) isn't
    // rendered at all. Devices left out are reset when they play again.
    void setTrackFrozen(uint32_t trackId, bool frozen);
    bool trackFrozen(uint32_t trackId);
    void setMasterGain(float gain) { setTrackGain(kMaster, gain); }
    void setMasterPan(float pan) { setTrackPan(kMaster, pan); }
    // Peak levels since the last call: the master, each track, then each rack chain (chainId set).
    std::vector<MeterReading> takeMeters();
    // Each device's loudest output (left, right) since the last call: its meter.
    std::vector<DeviceMeterReading> takeDeviceMeters();
    // Whether live rendering times every track and device (for takeCpu()). Off by default.
    void setMeasuringCpu(bool on);
    // The time spent on each track and device since the last call (or since measuring began).
    CpuReading takeCpu();
    // The latency a track's devices add (samples, compensated; kMaster: the master's), and the whole
    // output's: the latest a track is aligned to plus the master's devices.
    int trackLatency(uint32_t trackId);
    int outputLatency();

    // --- Input and recording ------------------------------------------------------
    // A track's input: device channels (0-based, as DeviceStatus lists them): none,
    // one (mono) or two (a stereo pair). Channels the device hasn't open are silent.
    void setTrackInput(uint32_t trackId, const std::vector<int>& channels);
    // A track's input from another track's output (resampling it: an input
    // edge), or from the master's (kMaster: resampling the mix), instead of
    // device channels (setTrackInput() goes back to those). The source renders
    // first. A track's is tapped as a sidechain is (`tap`, `tapProcessorId`):
    // after its fader (Ableton's Post Mixer), before it (Post FX), after one of
    // its devices (a MIDI track's instrument: its Pre FX), or before them all
    // (Pre FX); the master's after its fader. Monitored, the track hears a
    // track's output instead of its clips, without delay compensation, as it
    // hears the device: live, and in offline renders while it monitors In; never
    // the master's (that would feed back), which it can only record. Throws
    // std::invalid_argument for an unknown track, the track itself, a track it
    // feeds (a cycle, through outputs, sends and inputs alike), or a tap after a
    // device that isn't on the source. When the source goes, the input goes too.
    void setTrackInputTrack(uint32_t trackId, uint32_t sourceTrackId, SidechainTap tap = SidechainTap::PostFader,
                            uint32_t tapProcessorId = 0);
    std::optional<uint32_t> trackInputTrack(uint32_t trackId);  // none: its input is the device's (or none)
    void setTrackMonitor(uint32_t trackId, MonitorMode mode);
    // Armed tracks are what Auto monitoring listens to; what records is up to startRecording().
    void setTrackArmed(uint32_t trackId, bool armed);
    // Constraining delay compensation (Cubase's), so a player hears what they
    // play sooner: while any track is monitored (In, or Auto and armed), the
    // devices whose latency is over `thresholdMs` on its way out are bypassed,
    // latency and all: its own, those of the groups and returns it goes into, and
    // the master's. They stay switched on (ProcessorInfo::constrained tells which
    // are bypassed) and play again once nothing is monitored or this is off.
    // The monitored tracks and the strips they are heard through then skip
    // delay compensation too (their live MIDI with them), as monitored audio
    // always does, playing back or not: what plays back on them comes that much
    // early. Offline renders (exports, freezing) hear every device.
    void setConstrainLatency(bool on, double thresholdMs = 3.0);
    bool constrainLatency();
    double constrainThresholdMs();
    // What a track played live through MIDI input is heard after, in frames:
    // the MIDI input's delay, its devices' and those of the groups its output
    // goes through and the master's, the delay compensation on that way (none
    // while it is live), and the device's output latency as the driver reports it.
    LiveLatency liveLatency(uint32_t trackId);
    // How late each open MIDI input's driver handed messages over since the last call.
    std::vector<MidiInputDelivery> takeMidiInputDelivery();
    // Records the targets' inputs from the next block the playhead moves in, and
    // starts playing (after `countInBeats` of count-in) if stopped. Throws
    // std::runtime_error (for the user) if no device runs, a target's input isn't
    // open, or a file can't be created; std::invalid_argument for a bad target.
    // A take lands where what it recorded was heard: device input is moved back
    // by the output's lag and the device's latencies; a track's output by how
    // late it leaves the track (its edge's arrival); the master's by the lag.
    void startRecording(const std::vector<RecordTarget>& targets, double countInBeats = 0.0);
    // Ends the recording (the transport plays on) and returns its takes, and those
    // of a recording ended otherwise since the last call (a device change).
    std::vector<RecordedTake> stopRecording();
    // Recording and still taking input: not after the playhead jumped (a locate,
    // stopping), nor once something else ended it.
    bool isRecording();
    std::vector<RecordingProgress> recordingProgress();
    static constexpr int kRecordPeakFrames = RecordingTake::kPeakFrames;

    // --- MIDI input -------------------------------------------------------------
    // Devices (main thread): those connected, by name; open ones play into the
    // tracks whose MIDI input takes them. open throws std::runtime_error for the user.
    std::vector<std::string> midiInputDevices();
    void openMidiInput(const std::string& name);
    void closeMidiInput(const std::string& name);
    std::vector<std::string> openMidiInputs();
    // A track's MIDI input: none (`enabled` false), every input ("") or one by
    // name (open or not), on every channel (0) or one (1-16). With a MIDI input
    // the track is recorded by a RecordTarget without a file; it hears the input
    // while monitored (In, or Auto and armed).
    void setTrackMidiInput(uint32_t trackId, bool enabled, const std::string& device, int channel);
    // A message as if it came from the input `device` at `hostTimeNs` (the
    // hostTimeNs() clock; 0: now), e.g. from a computer keyboard or a test. 1-3
    // bytes, starting with a status byte. Dropped if no audio device runs.
    void sendMidiInput(const std::string& device, const std::vector<uint8_t>& message, int64_t hostTimeNs = 0);
    // MIDI 2.0: Universal MIDI Packets (Ump.h) as if from the input `device`,
    // as sendMidiInput() (and a MIDI 2.0 backend) hands them over. Channel voice
    // messages play as MIDI 1.0's do; a per-note pitch bend bends the note it
    // names, of those the input holds. Words after the last whole packet, and
    // packets of other kinds, are ignored.
    void sendUmp(const std::string& device, const std::vector<uint32_t>& words, int64_t hostTimeNs = 0);
    AudioClockStatus audioClock() const;
    // MIDI input played at once: in the next block, at its start, instead of a
    // device buffer after it came (MidiInput.h). Up to a buffer sooner, half a
    // buffer on average, but as early as the block falls: its timing jitters by
    // up to a buffer. Recorded notes land half a buffer earlier than met to make up.
    void setMidiImmediate(bool immediate);
    bool midiImmediate();

    // --- Automation -------------------------------------------------------------
    // Replaces the automation of a track (trackId 0: the master): an envelope per
    // target, either a mixer control (processorId 0, param "volume" or "pan"), a
    // parameter (by id) of one of the track's devices (in a rack too), or the
    // fader of a rack's chain (the rack's id, param "chain:<chain id>:volume" or
    // "...:pan"). Envelopes of devices that are gone (or are on another track),
    // or parameters they don't have, are kept but not played.
    // A target automated here follows its envelope; its own value (set above)
    // counts again once its envelope is taken away.
    void setTrackAutomation(uint32_t trackId, const std::vector<AutomationLaneDesc>& lanes);

    // --- Modulation -------------------------------------------------------------
    // Replaces what a track's modulators (trackId 0: the master's) move
    // (Modulation.h): each a parameter (by id) of one of the track's devices (in
    // a rack too) moved by a modulator on the same track. Those naming devices
    // that are gone or elsewhere, a modulator that isn't one, a parameter a
    // device doesn't have (or can't be set), the modulator itself or a parameter
    // already modulated are kept but not played. A parameter modulated here
    // follows its modulator instead of its automation; its own value (set above)
    // counts again once nothing modulates it (the caller sets it back).
    void setTrackModulation(uint32_t trackId, const std::vector<ModulationDesc>& modulation);

    // --- Device chains ------------------------------------------------------------
    // Devices live in chains, each with an id: every track and the master has a
    // main chain (later, each chain of a rack is one more). Processor ids are
    // unique across chains, so a processor can move from one chain to another
    // and keep its state (a plug-in isn't loaded again).
    uint32_t trackChain(uint32_t trackId);  // a track's (or the master's) main chain
    uint32_t processorChain(uint32_t processorId);
    // `index`: where in the chain (-1 or past the end: last).
    uint32_t addBuiltinProcessor(uint32_t chainId, const std::string& type, int index);
    // Loads a plug-in (`format` "VST3" or "VST2") into the chain, where pluginHosting() says:
    // in this process (it has loaded when this returns), or in a sandbox (it is
    // in the chain at once and loads there meanwhile: ProcessorEvent::Loaded or
    // LoadFailed, processorInfo().status). Main thread; throws std::runtime_error
    // with a message for the user if it can't be loaded (or no sandbox started).
    // `state` (none: as it starts) is restored as it loads (in a sandbox, before it
    // says it has loaded); one it doesn't take is a ProcessorEvent::Problem
    // (processorInfo().problem says why), not an exception.
    uint32_t addPluginProcessor(uint32_t chainId, const std::string& format, const std::string& path,
                                const std::string& uid, int index, const PluginIdentity& identity = {},
                                const std::vector<uint8_t>& state = {});
    // Removes a device (a rack with its chains and everything in them).
    void removeProcessor(uint32_t processorId);
    // Reorders a chain; `processorIds` must be the chain's processors, each once.
    void setChainOrder(uint32_t chainId, const std::vector<uint32_t>& processorIds);
    // Moves a processor to position `index` of a chain (its own or another, on
    // any strip, in a rack too), as the chain is without it (-1: last). Its state
    // goes with it (a rack's chains and devices too), and its sidechain (and
    // those of the devices in a rack; std::invalid_argument if one would make a
    // cycle there, as for a rack moving into itself or nesting too deep); its
    // automation stays with the strip it came from, and plays again if it comes back.
    void moveProcessor(uint32_t processorId, uint32_t toChainId, int index);

    // Racks (device groups): a rack is a device in a chain whose own chains each
    // process its input, side by side; it puts out their sum (an empty rack
    // passes its input on). Each chain has its devices (racks too, at most
    // kMaxRackDepth deep), and a fader: gain, pan, mute and solo (solo among the
    // rack's chains), metered like a track's. The rack's latency is its slowest
    // chain's: the others are delayed to line up with it. Every chain's devices
    // hear the track's notes (layering instruments), but a chain given a key to
    // receive (a Drum Rack's pad: setChainReceiveNote) hears only the notes of
    // that key, as C3 (60). Throws std::invalid_argument
    // for an unknown chain or rack, or a rack nested too deep.
    static constexpr int kMaxRackDepth = sub::kMaxRackDepth;
    uint32_t addRack(uint32_t chainId, int index);
    uint32_t addRackChain(uint32_t rackId, int index);  // a new chain, at `index` (-1: last)
    void removeRackChain(uint32_t chainId);              // with its devices
    void setRackChainOrder(uint32_t rackId, const std::vector<uint32_t>& chainIds);
    std::vector<uint32_t> rackChains(uint32_t rackId);
    uint32_t chainRack(uint32_t chainId);  // the rack a chain belongs to; 0: a strip's main chain
    std::vector<uint32_t> chainProcessors(uint32_t chainId);  // in order
    void setChainGain(uint32_t chainId, float gain);
    void setChainPan(uint32_t chainId, float pan);
    void setChainMute(uint32_t chainId, bool mute);
    void setChainSolo(uint32_t chainId, bool solo);
    // The key whose notes alone a chain hears (as C3), or -1: every note. Its
    // devices are reset (a note sounding on the key before ends).
    void setChainReceiveNote(uint32_t chainId, int note);
    int chainReceiveNote(uint32_t chainId);
    // A device's sidechain: what its aux input hears (hasSidechain in its
    // ProcessorInfo), from a track (a track, a group or a return; not the master,
    // which renders after everything), tapped after the track's fader, before it,
    // after one of its devices (`tapProcessorId`, in the track's main chain or in
    // a chain of a rack there, before that chain's fader; in a rack switched off,
    // after that rack; should that device leave the track, the tap is before the
    // fader), or before all of them (PreFx). It is lined up
    // with the signal at the device: delayed, or that signal is (just before the
    // device). Throws std::invalid_argument for an unknown device or track, a
    // device without a sidechain input, the master, or a sidechain that would
    // close a cycle (its own track, or one its track feeds: through outputs,
    // sends, inputs and sidechains alike). It stays with the device when the
    // device moves (moveProcessor refuses a move that would make it a cycle);
    // when its track goes, it goes too.
    void setProcessorSidechain(uint32_t processorId, uint32_t sourceTrackId,
                               SidechainTap tap = SidechainTap::PostFader, uint32_t tapProcessorId = 0);
    void clearProcessorSidechain(uint32_t processorId);
    std::optional<SidechainInfo> processorSidechain(uint32_t processorId);
    // A device's MIDI input from another track (acceptsMidi in its ProcessorInfo:
    // an instrument, or an effect with a MIDI input, as a vocoder or a pitch
    // corrector has): it hears that track's notes, its clips', those played on
    // it by hand and its MIDI input as the track hears them (bends and all),
    // instead of its own track's; 0 (the master's id: it has no notes) for its
    // own track's again. Notes go to it when they play on the timeline, not
    // delayed to line up with its audio. A MIDI input carries no audio, so it
    // orders nothing and closes no cycle, and the source's mute and solo don't
    // stop it. It stays with the device when the device moves; when its track
    // goes, so does it. The device resets as its source changes (no note it
    // heard hangs). Throws std::invalid_argument for an unknown device or
    // track, or a device that takes no MIDI.
    void setProcessorMidiInput(uint32_t processorId, uint32_t sourceTrackId);
    uint32_t processorMidiInput(uint32_t processorId);  // 0: its own track's
    ProcessorInfo processorInfo(uint32_t processorId);
    std::vector<ParamInfo> processorParams(uint32_t processorId);
    int processorParamIndex(uint32_t processorId, const std::string& paramId);  // -1: no such parameter
    float processorParam(uint32_t processorId, int index);
    void setProcessorParam(uint32_t processorId, int index, float value);
    std::string processorParamText(uint32_t processorId, int index, float value);
    std::string processorPresetName(uint32_t processorId);  // Processor::presetName()
    // What a device's own editor draws besides its parameters (Processor::displays()),
    // and display `index`'s values since `position` (0 at first), appended to
    // `out`; returns where to read from next.
    std::vector<DisplayInfo> processorDisplays(uint32_t processorId);
    uint64_t readProcessorDisplay(uint32_t processorId, int index, uint64_t position, std::vector<float>& out);
    void setProcessorEnabled(uint32_t processorId, bool enabled);
    std::vector<uint8_t> processorState(uint32_t processorId);
    void setProcessorState(uint32_t processorId, const std::vector<uint8_t>& state);
    // Plug-in editors, in windows owned by `ownerWindow` (an HWND; 0 for none).
    bool openEditor(uint32_t processorId, uintptr_t ownerWindow, const std::string& title);
    void closeEditor(uint32_t processorId);
    bool isEditorOpen(uint32_t processorId);
    bool setEditorVisible(uint32_t processorId, bool visible);
    void setEditorTitle(uint32_t processorId, const std::string& title);
    // What processors reported since the last call (edits in a plug-in's editor...).
    std::vector<ProcessorEventRecord> takeProcessorEvents();

    // --- Plug-in hosting (PluginHosting.h, docs/engine/sandboxes.md) -------------
    // Where plug-ins loaded from now on run: in this process, or in sandboxes
    // (processes of their own, of the host program: setPluginHostProgram()),
    // grouped as the mode says. Those loaded already stay where they are, until
    // reloaded (reloadProcessor()).
    void setPluginHosting(PluginHosting hosting);
    PluginHosting pluginHosting();
    void setPluginHostProgram(const std::string& path);  // substation-plugin-host
    // Loads a plug-in again, as it was last (its state now, or the last known
    // of one whose sandbox ended), where pluginHosting() says now, in its place:
    // the same processor id, chain, sidechain and MIDI input. Its editor closes.
    // Main thread. Throws std::invalid_argument for a processor that isn't a
    // plug-in, std::runtime_error if it can't be loaded (it stays as it was).
    void reloadProcessor(uint32_t processorId);
    // These plug-ins load first in their sandboxes (those still waiting to), in this order.
    void prioritizeProcessors(const std::vector<uint32_t>& processorIds);
    // How many plug-ins in sandboxes are still loading.
    size_t pluginsLoading();
    // Waits until no plug-in in a sandbox is loading (calling idle() meanwhile), at
    // most `timeoutMs`: false if some still are. Main thread; for renders that must
    // have them, and tests.
    bool waitForPlugins(int timeoutMs);
    std::vector<PluginSandboxInfo> pluginSandboxes();
    // How long a sandbox that no plug-in uses any more stays before it quits (5 s).
    void setSandboxRetireDelay(int milliseconds);
    // How long an offline render waits for a sandboxed plug-in's block before it goes on without
    // (a minute; once: a plug-in that misses it is waited for no more until it catches up). Tests.
    void setSandboxOfflineWait(int milliseconds);
    // Sandboxed editors: the keys (key, modifiers: SandboxEditorKey's) the application
    // takes from them, and those pressed since the last call. (Editors in this process
    // ask EditorHost.h's key filter, or are the application's own windows: Windows.)
    void setSandboxEditorKeys(const std::vector<std::pair<uint32_t, int>>& keys);
    std::vector<SandboxEditorKey> takeSandboxEditorKeys();
    // Closes the foremost of the sandboxes' editors, as its close button would; false if none is shown.
    bool closeForemostSandboxEditor();
    // The sandboxes' processes, running (their windows are plug-ins' editors too).
    std::vector<int> sandboxProcessIds();

    // --- Transport ------------------------------------------------------------
    void play(double countInBeats = 0.0);  // a count-in: the metronome clicks, then the playhead moves
    bool isCountingIn() const { return shared_.countingIn.load(std::memory_order_relaxed); }
    void stop();
    bool isPlaying() const { return requestedPlaying_.load(std::memory_order_relaxed); }
    // Where the playhead is; after setPositionBeats(), its beat until the audio
    // thread plays from there (not where it was a moment before: no flash back).
    double positionBeats() const;
    void setPositionBeats(double beat);
    // The song's tempo: one throughout (setTempo()), or a tempo map (setTempoMap(): steady stretches,
    // ramps and jumps, music/TempoMap.h). tempo() is the tempo at beat 0; tempoMap() the whole map.
    // Positions everywhere stay in beats; what they are in samples follows the map.
    void setTempo(double bpm);
    double tempo();
    void setTempoMap(const TempoMap& map);
    TempoMap tempoMap();
    // The time signature: one throughout, or changes (setMeter(), each starting a bar). Throws
    // std::invalid_argument for one SUBstation doesn't play (MeterMap::valid()).
    void setTimeSignature(int numerator, int denominator);
    void setMeter(const MeterMap& meter);
    MeterMap meter();
    // The song's key, for the devices that follow the harmony (ProcessContext::keyTonic, keyMinor): its tonic's
    // pitch class (0 = C), or -1 when none is known. Throws std::invalid_argument for a tonic past 11.
    void setSongKey(int tonic, bool minor);
    // The project's default groove (music/Groove.h), for the devices that play on the beat and follow it
    // (ProcessContext::groove: the Groove device's Project source, Follow Groove); none: straight. It is
    // played as groove::normalized() makes it.
    void setGroove(const std::optional<groove::Groove>& groove);
    void setLoop(bool enabled, double startBeat, double endBeat);
    // Punch In and Out: while recording, the tracks recorded play their clips
    // before the loop's start (In) and from its end (Out), loop on or not, as
    // they do when they don't record. (What is recorded there is the
    // application's to leave out of its takes: they record all the same.)
    void setPunch(bool in, bool out);
    void setMetronome(bool enabled);
    bool metronome() const { return shared_.metronome.load(std::memory_order_relaxed); }
    // The short fade against clicks at a clip's edge that cuts into its file
    // (4 ms by default, 0 to 100): where the clip has no fade of its own there.
    void setClipFadeMs(double ms);
    // Stretched clips are rendered whole in the background, and live playback
    // reads the renders once they are ready (WarpCache). Waits until every
    // render the project needs is ready (tests and benchmarks; not under the
    // engine's mutex, which the renders don't need).
    void waitForWarpRenders();

    // --- Streaming ------------------------------------------------------------
    // Streaming long files from disk (docs/engine/streaming.md): sources loaded
    // from now on follow the settings (loadSource()); the pool, the folder and the
    // disk threads take them at once.
    void setStreaming(const StreamingSettings& settings);
    StreamingSettings streaming();
    // The settings new engines start with (tests run every engine streaming with it).
    static void setDefaultStreaming(const StreamingSettings& settings);
    static StreamingSettings defaultStreaming();
    // What streamed sources missed since the last call: frames the disk was late
    // for (played as silence), and files that became unreadable. Any thread.
    std::vector<StreamMiss> takeStreamMisses();
    StreamCache::Stats streamingStats();
    // Waits until the disk threads have read what the horizon and the pins want
    // (tests and benchmarks; not under the engine's mutex).
    void waitForStreams();
    // Called on the disk threads before each read (tests slow the disk down).
    void setStreamReadHook(StreamCache::ReadHook hook);

    // --- Browser preview ------------------------------------------------------
    void preview(const std::string& path);  // the source must already be loaded
    void stopPreview();
    bool isPreviewing() const { return shared_.previewActive.load(std::memory_order_relaxed); }

    // --- Offline rendering ----------------------------------------------------
    // All of them temporarily silence live output and render the same graph.
    std::vector<float> renderOffline(double startBeat, int64_t frames, bool loop = false,
                                     bool metronome = false);  // interleaved stereo
    void exportWav(const std::string& path, double startBeat, double endBeat, int bitDepth);
    // The same in any format (WAV or MP3).
    void exportFile(const std::string& path, double startBeat, double endBeat, const ExportFormat& format);
    // One track's signal before its fader (after its devices: what freezing it
    // keeps), lined up with the timeline: what it plays at startBeat comes first.
    // Solo is ignored (what goes into a group is heard as if nothing were
    // soloed); mute isn't. Interleaved stereo.
    std::vector<float> renderTrackOffline(uint32_t trackId, double startBeat, int64_t frames);
    // The same from startBeat to endBeat, then on for up to `tailSeconds` while
    // it isn't silent (a reverb's tail), into a new 32-bit float WAV file: cut
    // after its last frame above -100 dB, and rendered only until it has stayed
    // below that for as long as what feeds it says it may ring on (its devices'
    // tails: docs/engine/rendering.md). Returns the frames written.
    int64_t renderTrackToWav(uint32_t trackId, const std::string& path, double startBeat, double endBeat,
                             double tailSeconds);
    // renderTrackToWav() for several tracks at once (freezing them): one render
    // (of what they need: the union of what feeds each), each track's signal
    // into its own file, each file the one renderTrackToWav() writes for it, to
    // the bit. A track done (its tail silent) renders no more. Returns the
    // frames written to each, in order.
    std::vector<int64_t> renderTracksToWav(const std::vector<TrackFile>& tracks, double startBeat, double endBeat,
                                           double tailSeconds);
    // exportWav() (exportFile()) and renderTrackToWav() on a thread of their own (RenderJob.h):
    // they return at once with the job, its file created (or throw why not).
    // One job at a time. Until it is finished live output is silent, and what
    // would disturb it is refused (std::runtime_error): another render, opening
    // a device, the audio threads, recording. Other changes are taken, but the
    // job renders the project as it was when it started.
    std::shared_ptr<RenderJob> startExport(const std::string& path, double startBeat, double endBeat, int bitDepth);
    std::shared_ptr<RenderJob> startExport(const std::string& path, double startBeat, double endBeat,
                                           const ExportFormat& format);
    std::shared_ptr<RenderJob> startTrackRender(uint32_t trackId, const std::string& path, double startBeat,
                                                double endBeat, double tailSeconds);
    // renderTracksToWav() in the background: the job's finish() returns the frames
    // written to the first file, RenderJob::written() those written to each.
    std::shared_ptr<RenderJob> startTracksRender(const std::vector<TrackFile>& tracks, double startBeat,
                                                 double endBeat, double tailSeconds);
    bool isRendering();  // a job is running (or done, not finished yet)

    // --- Audio threads -----------------------------------------------------------
    // How many threads render (the audio thread and the workers); 1 renders
    // every track on the audio thread. Renders are the same whatever the number.
    static int defaultAudioThreads();  // one per core but one (at least 1)
    static constexpr int kMaxAudioThreads = 64;
    void setAudioThreads(int threads);
    int audioThreads();
    // Tracks the workers have rendered since the number of threads was last set
    // (tests and benchmarks see them work).
    uint64_t nodesOnWorkers();
    // How many of the workers (audioThreads() - 1) run at real-time priority
    // (Scheduler.h); the others were refused it and render at normal priority,
    // which the audio settings warn of.
    int realtimeWorkers();
    // Each track's render is timed. Tracks start with those with the most work
    // hanging off them (their own and the groups they go into), unless this is
    // off (for benchmarks): then in routing order. Results are the same either way.
    void setCostOrdering(bool on);
    bool costOrdering() const { return renderer_.costOrdering(); }
    // A track's render (renderTrackOffline(), freezing) renders only the track
    // and what feeds it (Renderer::setCaptures()), unless this is off (for
    // tests): then every track and the master, as an export does. Results are
    // the same either way.
    void setPruneTrackRenders(bool on);
    std::vector<TrackCost> trackCosts();

    // --- Housekeeping ---------------------------------------------------------
    // Call regularly from the UI thread: frees retired snapshots and removed
    // processors, handles device loss, and does the main-thread work plug-ins
    // asked for (restarts, parameter updates, editor events). Plug-ins take
    // their time to go, so a call destroys removed processors for a few
    // milliseconds at most (a project closed doesn't hold up the UI), and the
    // next calls go on; `releaseAll` destroys every one now (shutting down).
    void idle(bool releaseAll = false);

private:
    // A send (a routing edge), with its state and delay line kept across snapshots.
    struct SendModel {
        uint32_t to = 0;
        bool preFader = false;
        std::shared_ptr<EdgeState> state;
        std::shared_ptr<DelayLine> delay;
    };
    struct TrackModel {
        uint32_t id = 0;
        std::shared_ptr<TrackParams> params;
        std::vector<ClipDesc> clips;
        std::vector<std::string> clipKeys;  // sourceKey() of each clip's path
        std::vector<NoteDesc> notes;
        // Its notes as snapshots play them, made by tempo map `renderedTempo` (its version) at `renderedRate`: made
        // again only when its notes (setTrackNotes()), the tempo or the rate change. Null: not made yet.
        std::shared_ptr<const std::vector<NoteRender>> rendered;
        uint64_t renderedTempo = 0;
        double renderedRate = 0.0;
        uint32_t chainId = 0;               // its main chain
        uint32_t output = 0;                // where its output goes: kMaster, a track (a routing edge) or kNoOutput
        uint32_t outputProcessor = 0;       // a device whose sidechain its output goes into instead (0: none)
        bool inMonitored = false;           // what goes into it is its input (setTrackInMonitored())
        std::shared_ptr<EdgeState> outputState;  // its output edge's, kept across snapshots (not the master)
        std::shared_ptr<DelayLine> delay;   // its output edge's delay compensation, kept across snapshots
        std::vector<SendModel> sends;       // more edges, in the order they were made
        std::shared_ptr<TrackBuffers> buffers;  // its signal and chunk state, kept across snapshots (not the master)
        std::vector<AutomationLaneDesc> automation;
        std::vector<ModulationDesc> modulation;
        std::vector<int> inputChannels;  // device channels: its input (unless inputTrack)
        // Its input from a track's output (an input edge) or the master's (kMaster), instead of device channels.
        std::optional<uint32_t> inputTrack;
        SidechainTap inputTap = SidechainTap::PostFader;  // where it is taken from that track
        uint32_t inputTapProcessor = 0;                   // AfterDevice: the source's device
        std::shared_ptr<EdgeState> inputState;  // the input edge's (once it had one), kept across snapshots
        MidiInputRoute midiInput;
        MonitorMode monitor = MonitorMode::Auto;
        bool armed = false;
        bool frozen = false;  // plays its clips through its fader, nothing else (setTrackFrozen)
        bool silenced = false;  // its devices were left out of the last snapshot (frozen, or not rendered)
    };

    void audioCallback(const AudioIO& io) noexcept override;
    void deviceEvent(DeviceEvent event) noexcept override;
    // Any thread: a MIDI message from input `port`, stamped and queued for the audio thread.
    void midiInput(uint16_t port, const uint8_t* message, int size, int64_t hostTimeNs) noexcept;
    // The same for Universal MIDI Packets (MIDI 2.0), and for a message of either kind once decoded.
    void umpInput(uint16_t port, const uint32_t* words, int count, int64_t hostTimeNs) noexcept;
    void queueMidiInput(uint16_t port, MidiInputEvent event, int64_t hostTimeNs) noexcept;
    uint16_t midiPortLocked(const std::string& name);
    void discardMidiInputLocked();

    // A chain of devices: a strip's main chain, or a chain of a rack on it.
    struct ChainModel {
        uint32_t id = 0;
        uint32_t stripId = 0;     // the track (or kMaster) whose signal it processes
        uint32_t parentRack = 0;  // the rack processor it belongs to; 0: a strip's main chain
        std::vector<std::shared_ptr<Processor>> inserts;
        std::shared_ptr<TrackParams> params;  // a rack chain's fader (null for a main chain)
        std::shared_ptr<DelayLine> delay;     // a rack chain's delay compensation, kept across snapshots
        int receiveNote = -1;                 // a rack chain's key (a Drum Rack's pad); -1: every note
    };
    // A device's sidechain (a routing edge into it), with its state and delay
    // lines kept across snapshots.
    struct SidechainModel {
        uint32_t source = 0;  // the track
        SidechainTap tap = SidechainTap::PostFader;
        uint32_t tapProcessor = 0;  // AfterDevice: the source's device
        std::shared_ptr<EdgeState> state;
        std::shared_ptr<DelayLine> delay;  // the sidechain's
    };
    struct ProcessorEntry {
        uint32_t chainId = 0;
        std::shared_ptr<Processor> processor;
        std::optional<SidechainModel> sidechain;
        bool rack = false;             // a RackProcessor:
        std::vector<uint32_t> chains;  // its chains, in order
        // While its automation switches it off: the line its input is passed on
        // through (SwitchRender), kept across snapshots.
        std::shared_ptr<DelayLine> switchDelay;
        // Its own signal's delay before it, lining it up with what comes into
        // its sidechain (its own, and tracks' outputs), kept across snapshots.
        std::shared_ptr<DelayLine> sidechainWait;
        // The track whose notes it takes instead of its own track's (0: none),
        // and its copy of them each chunk, kept across snapshots.
        uint32_t midiSource = 0;
        std::shared_ptr<MidiFeed> midiFeed;
        // Its latency (ownLatency) when the snapshot was last aligned: idle()
        // realigns when it differs, whatever the processor's own idle() says.
        int alignedLatency = 0;
        // A plug-in: which (reloadProcessor() loads it again), and what last went
        // wrong with one in this process (its state wasn't taken).
        struct Plugin {
            std::string format;  // "VST3", "VST2"
            std::string path;
            std::string uid;
            PluginIdentity identity;
            std::string problem;
        };
        std::optional<Plugin> plugin;
    };
    // A plug-in loaded where hosting_ says, with `state` (none: as it starts). Not under the lock.
    std::shared_ptr<Processor> makePlugin(const ProcessorEntry::Plugin& plugin, const std::vector<uint8_t>& state,
                                          double sampleRate, std::string& problem);
    // A strip's devices depth first (Routing.h's slots): each device of its main
    // chain, and after a rack the devices of its chains, chain by chain.
    struct StripSlot {
        uint32_t processorId = 0;
        std::shared_ptr<Processor> processor;
        uint32_t chainId = 0;  // the chain it is in
        int index = 0;         // its place there
        int rack = -1;         // the slot of the rack that chain belongs to (-1: the strip's main chain)
        int chain = 0;         // and which of its chains it is
        bool enabled = true;   // it is switched on, and so is every rack it is in
        bool isRack = false;
    };
    using ProcessorIds = std::unordered_map<const Processor*, uint32_t>;

    TrackModel& trackLocked(uint32_t trackId);           // the master too
    TrackModel& arrangementTrackLocked(uint32_t trackId);  // not the master
    std::shared_ptr<Processor> processorLocked(uint32_t processorId);
    std::shared_ptr<Processor> processor(uint32_t processorId);  // locks
    ChainModel& chainLocked(uint32_t chainId);
    const std::vector<std::shared_ptr<Processor>>& insertsLocked(const TrackModel& track) const;
    uint32_t addChainLocked(uint32_t stripId, uint32_t parentRack);
    uint32_t insertProcessorLocked(uint32_t chainId, std::shared_ptr<Processor> processor, int index,
                                   bool rack = false);
    void retireProcessorLocked(std::shared_ptr<Processor> processor);
    // Asks every device of a chain (and in its racks) to reset: a MIDI effect left it.
    void resetChainLocked(uint32_t chainId);
    // Removes a rack chain and everything in it (not from its rack's list).
    void removeChainContentsLocked(uint32_t chainId);
    ProcessorEntry& rackLocked(uint32_t rackId);
    ChainModel& rackChainLocked(uint32_t chainId);  // a rack's chain (not a strip's main chain)
    int chainDepthLocked(uint32_t chainId) const;  // how many racks a chain is in (0: a strip's main chain)
    int rackHeightLocked(uint32_t rackId) const;   // 1 + the deepest nesting of racks in it
    // The devices in a rack's chains (and in the racks in them), and those chains, appended.
    void rackContentsLocked(uint32_t rackId, std::vector<uint32_t>& processors, std::vector<uint32_t>& chains) const;
    ProcessorIds processorIdsLocked() const;
    std::vector<StripSlot> stripSlotsLocked(const TrackModel& track, const ProcessorIds& ids) const;
    // After an edit: publishes a new snapshot, or marks it stale inside a batch.
    void rebuildSnapshotLocked();
    // The published snapshot, built first if stale. Every read of snapshotHold_
    // on the edit side goes through here (or publishPendingLocked()).
    const std::shared_ptr<const RenderSnapshot>& currentSnapshotLocked();
    void publishPendingLocked();  // builds the snapshot if it is stale
    void buildSnapshotLocked();   // builds and publishes it, now
    // A track's notes for the snapshot: TrackModel::rendered, made again if out of date.
    NoteList renderedNotesLocked(TrackModel& track, const RenderSnapshot& snap);
    bool pushCommandLocked(const TransportCommand& command);  // false: the queue was full
    void serviceTransportIfIdleLocked();
    void collectGarbageLocked();
    void openDeviceLocked(const DeviceConfig& config);
    void closeDeviceLocked();
    void reloadSourcesLocked();
    std::shared_ptr<AudioSource> loadSourceImpl(const std::string& path, bool publish);
    // Whether a file this long streams, as the settings are.
    bool streamsLocked(double seconds) const { return streaming_.enabled && seconds >= streaming_.thresholdSeconds; }
    void applyStreamingLocked();
    void ensureStreamSchedulerLocked();  // once a source streams
    // A snapshot build's pins: the starts of the clips of streamed sources, and of the loop.
    void pinStreamsLocked(const RenderSnapshot& snap);
    // Waits until no audio callback that may have seen the state from before the call is still
    // running (none is: at once). False if one still is after kCallbackWaitMs (a plug-in is
    // holding it up): what it may be using must then be neither freed nor shared.
    bool waitForCallbackLocked();
    InputEdge inputEdgeLocked(const TrackModel& track) const;
    void finishRecordingLocked();
    void suspendLiveLocked();
    void resumeLiveLocked();
    void renderOfflineLocked(double startBeat, int64_t frames, float* out, bool loop, bool metronome);
    // Delay lines an offline render brings, so that it neither disturbs live playback nor depends on it.
    bool pruneTrackRenders_ = true;  // setPruneTrackRenders()
    struct OfflineLines {
        WarpVoiceSet voices;
        std::vector<std::shared_ptr<DelayLine>> delays, deviceDelays, chainDelays, switchDelays;
    };
    void prepareOfflineLocked(const RenderSnapshot& snap, Renderer& offline, OfflineLines& lines, double startBeat);
    // A render in the background: its own renderer and delay lines, and the snapshot it renders.
    struct OfflineRender {
        std::shared_ptr<const RenderSnapshot> snapshot;
        Renderer renderer;
        OfflineLines lines;
    };
    // Silences live output and resets the processors (as every offline render
    // does), and prepares a render of the snapshot from startBeat.
    std::shared_ptr<OfflineRender> beginOfflineLocked(double startBeat);
    void endOfflineLocked();  // the processors reset again, live output back
    // Starts `body` as the background job (RenderJob::start) on a begun render;
    // if it can't start, the render ends.
    std::shared_ptr<RenderJob> startJobLocked(std::vector<std::string> paths, int64_t total,
                                              std::function<std::optional<int64_t>(RenderJob&)> body);
    void checkNotRenderingLocked() const;  // throws std::runtime_error while a job runs
    friend class RenderJob;
    void endJob(RenderJob& job);  // RenderJob::finish(): it is over
    // renderTrackOffline(): `frames` of the track's signal, handed to `sink` a
    // piece at a time (interleaved stereo, and how many frames).
    void renderTrackLocked(uint32_t trackId, double startBeat, int64_t frames,
                           const std::function<void(const float*, int64_t)>& sink);
    void resetProcessorsLocked(bool rendering);  // Processor::resetOffline() for each
    void ensureWarpVoicesLocked(const std::array<size_t, kNumStretchConfigs>& needed);
    // What building a strip's part of the snapshot needs to know about it.
    struct StripBuild {
        const TrackModel* track = nullptr;
        const std::vector<StripSlot>* slots = nullptr;
        std::unordered_map<const Processor*, int> slotOf;  // its devices' slots
        int inputLatency = 0;  // how late the strip's input hears the timeline
        // GraphLatencies' for this strip: from its input, by slot.
        const std::vector<int>* deviceLatency = nullptr;
        const std::vector<std::vector<int>>* chainEnd = nullptr;
        const std::vector<std::vector<int>>* chainCompensation = nullptr;
        std::vector<std::vector<int>> sidechainOf;  // per slot: the snapshot edges into its sidechain input
        const RenderSnapshot* snap = nullptr;  // the one being built: its tempo map, its rate
        // Rack chain -> the snapshot edges taken after its devices (StripRender::deviceTaps).
        const std::unordered_map<uint32_t, std::vector<int>>* chainTaps = nullptr;
    };
    // A strip's mixer envelopes in the snapshot, in samples, as late as
    // `faderLatency` (delay compensation comes after the fader, on the strip's
    // edges). Its devices' come with them (buildChainLocked).
    void buildAutomationLocked(const StripBuild& build, int faderLatency, StripRender& strip);
    // A chain's devices in the snapshot (a strip's main chain, or a rack's chain,
    // into `chain`), with the sidechains into them, their envelopes and racks. A
    // device hears the timeline as late as the strip's input, plus the latency
    // before it (the devices before it, and the delays before sidechained ones).
    void buildChainLocked(uint32_t chainId, const StripBuild& build, int depth, RenderSnapshot& snap,
                          StripRender& chain);
    // The modulators in a chain (`rack`'s chain `chainIndex`; none: a strip's
    // own) and in the racks in it, depth first, onto `out`.
    static void collectModulators(const StripRender& chain, const RackRender* rack, int chainIndex,
                                  std::vector<ModulatorRender>& out);
    // An envelope's breakpoints in samples, sorted.
    static std::vector<AutomationNode> automationNodes(const AutomationLaneDesc& desc, const RenderSnapshot& snap);
    // The routing graph's edges, as indices into tracks_ (-1: the master): each
    // track's output (into a track, or into a device's sidechain), then its
    // sends; then the input edges; then the sidechains, by destination and
    // device. `origins` (if given) gets what each is.
    static constexpr int kOutputEdge = -1;
    static constexpr int kInputEdge = -2;
    static constexpr int kSidechainEdge = -3;
    static constexpr int kOutputSidechainEdge = -4;  // a track's output into a device's sidechain
    struct EdgeOrigin {
        // The track (index) it belongs to: its source, or its destination for an
        // input or a sidechain (-1: the master).
        int track = 0;
        int send = kOutputEdge;  // its send index, or kOutputEdge, kInputEdge, kSidechainEdge, kOutputSidechainEdge
        uint32_t processor = 0;  // a sidechain's device (an output's into one too)
        int device = -1;         // and that device's place in its chain
    };
    std::vector<RouteEdge> routeEdgesLocked(std::vector<EdgeOrigin>* origins = nullptr) const;
    // Where a sidechain leaves its source, whose devices are `slots`: after its
    // fader, before it, or after one of its devices (`tap` AfterDevice): returns
    // that device's slot (-1: before them all). A device in a rack's chain counts,
    // but one in a rack switched off (or in a rack inside one) is tapped after
    // the outermost of those racks, which passes its input on. A tap after a
    // device that isn't on the source (any more) is before the fader.
    int sidechainTapLocked(SidechainTap where, uint32_t tapProcessor, const std::vector<StripSlot>& slots,
                           EdgeRender::Tap& tap) const;
    // Where a device is: its strip (as an index into tracks_; -1: the master)
    // and its slot there. None: no such device.
    std::optional<std::pair<int, int>> deviceSlotLocked(uint32_t processorId, const ProcessorIds& ids) const;
    // Tracks whose output went into a device that is gone go into the master.
    void dropGoneOutputSidechainsLocked();
    // Throws std::invalid_argument if a sidechain from `source` into a device on `strip` would close a cycle.
    void checkSidechainLocked(uint32_t source, uint32_t strip) const;
    // The device a tap of track `source` (a sidechain's, a track's input) reads after: `tapProcessorId`
    // for an AfterDevice tap, which must be on that track (in its chain or a rack's there; else
    // std::invalid_argument), 0 for any other.
    uint32_t tapProcessorLocked(SidechainTap tap, uint32_t tapProcessorId, uint32_t source);
    // The same for every sidechained device in a rack's chains (it moves to `strip`).
    void checkRackSidechainsLocked(uint32_t rackId, uint32_t strip);
    // Throws std::invalid_argument if an edge from `from` into `to` would close a cycle.
    void checkRouteLocked(uint32_t from, uint32_t to, const char* what) const;
    int trackIndexLocked(uint32_t trackId) const;  // -1: the master (or none)
    static int insertLatency(const Processor& insert);  // its latency (0 when switched off or bypassed)
    static int ownLatency(const Processor& insert);     // its latency (0 when switched off), bypassed or not
    // Bypasses the devices delay compensation is constrained by (setConstrainLatency()), and
    // lets the others play: `edges` and `silent` (by tracks_ index) as the snapshot has them.
    void constrainLatencyLocked(const std::vector<RouteEdge>& edges, const std::vector<char>& silent);
    static std::string sourceKey(const std::string& path);

    mutable std::recursive_mutex mutex_;
    // Where plug-ins run, and the sandboxes (before everything holding processors: they go after them).
    PluginHosting hosting_ = PluginHosting::InProcess;
    std::unique_ptr<sandbox::PluginSandboxes> sandboxes_;
    // Reports of the engine's own about processors (a plug-in reloaded in its place).
    std::vector<ProcessorEventRecord> engineEvents_;

    AudioDevice device_;
    bool deviceRunning_ = false;
    uint32_t openInputs_ = 0;                        // input channels of the running device
    std::vector<int> openInputChannels_;             // which they are (device channels, callback order)
    std::atomic<uint32_t> pendingDeviceEvents_{0};  // DeviceEvent flags
    std::atomic<bool> liveSuspended_{false};
    std::atomic<uint64_t> audioEpoch_{0};        // audio callbacks finished
    std::atomic<uint64_t> callbacksBegun_{0};    // and begun (one more than finished: one is running)
    // Dropouts (DeviceStatus::dropouts); the sample time the next callback
    // should start at (-1: none yet) and whether the last one ran late: the audio thread's.
    std::atomic<uint64_t> dropouts_{0};
    int64_t nextSampleTime_ = -1;
    bool lastLate_ = false;

    std::atomic<const RenderSnapshot*> snapshot_{nullptr};
    std::shared_ptr<const RenderSnapshot> snapshotHold_;
    int batchDepth_ = 0;          // Batches alive
    bool snapshotStale_ = false;  // edits since the snapshot was built
    uint64_t snapshotBuilds_ = 0;
    DeferredReleasePool releasePool_;
    SharedState shared_;
    std::unique_ptr<Scheduler> scheduler_;  // the live and offline renderers share it (never at once)
    Renderer renderer_;
    int64_t cpuTakenNs_ = 0;  // steady_clock, when takeCpu() last read (0: not yet)
    RenderJob* job_ = nullptr;  // the render in the background, until finished (it holds itself)
    std::atomic<bool> requestedPlaying_{false};
    std::atomic<uint64_t> locatesPushed_{0};  // Locate commands pushed (against shared_.locatesPublished)
    std::atomic<double> locateBeat_{0.0};     // the last one's beat

    double sampleRate_ = 48000.0;
    std::shared_ptr<const TempoMap> tempoMap_ = std::make_shared<const TempoMap>();
    uint64_t tempoVersion_ = 1;  // RenderSnapshot::tempoVersion: one more with each new map
    std::shared_ptr<const MeterMap> meter_ = std::make_shared<const MeterMap>();
    int keyTonic_ = -1;
    bool keyMinor_ = false;
    std::shared_ptr<const groove::Groove> groove_;  // the project's default (null: none)
    bool loopEnabled_ = false;
    double loopStartBeat_ = 0.0;
    double loopEndBeat_ = 16.0;
    bool punchIn_ = false;
    bool punchOut_ = false;
    double clipFadeMs_ = 4.0;
    bool constrainLatency_ = false;    // setConstrainLatency()
    double constrainThresholdMs_ = 3.0;
    bool constrainHeld_ = false;       // an offline render: every device plays
    bool midiImmediate_ = false;       // setMidiImmediate()
    std::vector<char> liveStrips_;     // by tracks_ index, the master last: TrackRender::live
    int midiBufferFrames_ = 512;       // the device's buffer: MIDI input's delay unless immediate

    std::vector<TrackModel> tracks_;
    mutable std::unordered_map<uint32_t, int> trackIndex_;  // id -> index into tracks_ (trackIndexLocked(); may be stale)
    uint32_t nextTrackId_ = 1;
    TrackModel master_{kMaster, std::make_shared<TrackParams>()};  // never has clips or notes
    std::unordered_map<uint32_t, ChainModel> chains_;              // id -> chain (with its owning strip and rack)
    uint32_t nextChainId_ = 1;
    std::unordered_map<uint32_t, ProcessorEntry> processors_;      // id -> chain, processor
    uint32_t nextProcessorId_ = 1;
    // Removed processors wait here until no snapshot uses them, so that they are
    // destroyed in idle(), on the main thread (plug-ins require it).
    std::vector<std::shared_ptr<Processor>> graveyard_;

    // Stretchers for live playback, per configuration. They only grow (a voice
    // may still be in use by the audio thread) until the sample rate changes.
    WarpVoiceSet warpVoices_;
    // Stretched clips rendered whole, for live playback (each snapshot build
    // asks for its clips' renders).
    WarpCache warpCache_;

    std::unordered_map<std::string, std::shared_ptr<AudioSource>> sources_;
    std::unordered_map<std::string, std::shared_ptr<AudioSource>> residentSources_;  // loadResidentSource()'s
    std::shared_ptr<const AudioSource> previewHold_;

    // Streaming: the pages of streamed sources (each holds it too), and the disk
    // threads, made once a source streams.
    StreamingSettings streaming_;
    std::shared_ptr<StreamCache> streamCache_;
    std::unique_ptr<StreamScheduler> streamScheduler_;

    // The recording: the audio thread sees it through liveRecording_ (published
    // and taken away like the snapshot).
    std::unique_ptr<RecordingSession> recording_;
    std::atomic<RecordingSession*> liveRecording_{nullptr};
    std::vector<RecordedTake> finishedTakes_;

    // MIDI inputs: port ids are indices into midiPorts_ (names), kept for the
    // engine's life, so that tracks can name inputs that aren't open (yet).
    std::vector<std::string> midiPorts_;
    MidiInputDevices midiDevices_;  // last: closed first, while the rest still stands
};

}  // namespace sub
