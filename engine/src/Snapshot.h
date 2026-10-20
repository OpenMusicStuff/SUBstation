#pragma once
// The immutable render graph handed to the audio thread. The engine builds a new
// RenderSnapshot on the edit side whenever the arrangement changes and publishes
// it with a single atomic pointer swap. All positions are already converted to
// samples, so the audio thread never deals with beats or seconds.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "AudioSource.h"
#include "Automation.h"
#include "Modulation.h"
#include "NoteBend.h"
#include "Processor.h"
#include "Scheduler.h"
#include "Warp.h"
#include "music/TempoMap.h"
#include "rt/RtUtils.h"

namespace sub {

class WarpRender;  // WarpCache.h

// Per-track state that changes continuously and therefore lives outside the
// snapshot. Shared between all snapshots that contain the track. The master
// has one too (it never mutes or solos).
//
// Solo follows routing (Renderer.h): soloing a track keeps everything it feeds
// heard (its groups, the returns it sends to), but not the other tracks going
// into them; soloing a bus (a group, a return) keeps what feeds it going into it.
struct TrackParams {
    // Written by the API, read by the audio thread.
    std::atomic<float> gain{1.f};
    std::atomic<float> pan{0.f};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    // The API's writes, kept in range: gain from 0, pan from -1 (left) to 1 (right).
    void setGain(float value) noexcept { gain.store(std::max(0.f, value)); }
    void setPan(float value) noexcept { pan.store(std::clamp(value, -1.f, 1.f)); }

    // Written by the audio thread, read and reset by the API.
    std::atomic<float> peakLeft{0.f};
    std::atomic<float> peakRight{0.f};
    // The time spent rendering it (its clips, devices and fader; nanoseconds), while measured.
    std::atomic<uint64_t> cpuNs{0};

    // Audio-thread-only fader smoothing; persists across snapshots. Automation
    // replaces volume or pan sample by sample; where it stops, the smoothing
    // carries on from its last value.
    SmoothedValue audible;  // 0 when muted (or another track is soloed), else 1
    SmoothedValue volume;
    SmoothedValue panLeft;
    SmoothedValue panRight;
    double smoothingSampleRate = 0.0;
    // A live render's: the automated switch's gain at the end of the last chunk
    // (1: none, or on), which the mute's ramp takes over from when it stops.
    float switchGain = 1.f;
};

// An automation envelope as the renderer plays it: a processor's parameter, or
// a mixer control of a track or the master (or a send's level).
struct AutomationRender {
    std::vector<AutomationNode> nodes;    // sorted by time; empty: not automated
    std::shared_ptr<Processor> processor;  // null for a mixer control
    int param = 0;                         // the processor's parameter index
    int steps = 0;                         // > 0: a discrete parameter (values snap to its steps)
    int insert = -1;                       // the processor's place in the strip's chain
    // How late the target hears the timeline: the latency on its path before it
    // (what feeds its strip, then the devices before it; for a fader, the
    // strip's delay-compensated total). Its automation is delayed as much, so
    // it stays with the audio.
    int latency = 0;

    bool empty() const noexcept { return nodes.empty(); }
};

struct ClipRender {
    enum class Playback : uint8_t {
        Direct,     // source frames 1:1 (unwarped, or warped at its own tempo, no pitch change)
        Resample,   // Re-Pitch: speed and pitch change together
        Stretch,    // time stretcher: speed and pitch independent
    };

    std::shared_ptr<const AudioSource> source;
    int64_t start = 0;          // timeline position, samples
    int64_t length = 0;         // timeline samples
    int64_t sourceOffset = 0;   // first source frame played
    double rate = 1.0;          // source frames per timeline frame (project tempo / clip tempo; under a curve, on average)
    // Warped under a tempo that changes along it: its source position, point by point (null: `rate` throughout).
    std::shared_ptr<const WarpCurve> curve;
    float gain = 1.f;
    float panLeft = 1.f;        // clip pan as balance gains
    float panRight = 1.f;
    Playback playback = Playback::Direct;
    StretchConfig stretchConfig = StretchConfig::Standard;
    float transpose = 0.f;      // semitones (Stretch only)
    bool preserveFormants = false;
    std::shared_ptr<const WarpRender> rendered;  // Stretch: the whole clip rendered, once ready (live playback)
    // Stretch: the render the clip played from before it changed (a new tempo
    // or transpose), while its own isn't ready: a voice taking over mid-way
    // crossfades from it (Renderer::assignVoices()).
    std::shared_ptr<const WarpRender> fading;
    uint64_t key = 0;           // identifies the clip across snapshots (stretch voice continuity)
    // Its fades, in timeline samples from each end (together at most `length`),
    // and their curves (automationShape()).
    int64_t fadeIn = 0;
    int64_t fadeOut = 0;
    float fadeInCurve = 0.f;
    float fadeOutCurve = 0.f;

    // Source position (fractional frames) heard at timeline sample `t`, and how fast it moves there.
    double sourceAt(int64_t t) const noexcept {
        if (curve) return static_cast<double>(sourceOffset) + warpcurve::sourceAt(*curve, static_cast<double>(t - start));
        return sourceOffset + static_cast<double>(t - start) * rate;
    }
    double rateAt(int64_t t) const noexcept {
        return curve ? warpcurve::rateAt(*curve, static_cast<double>(t - start)) : rate;
    }
};

// Delays a routing edge's signal so that it lines up with the latest one going
// into the same place (plug-in delay compensation). Rendering-thread state; the
// edit side allocates it and keeps it across snapshots.
class DelayLine {
public:
    explicit DelayLine(int capacity)
        : left_(static_cast<size_t>(std::max(capacity, 1))), right_(static_cast<size_t>(std::max(capacity, 1))) {}
    int capacity() const noexcept { return static_cast<int>(left_.size()); }

    // Real-time. When the delay changes it starts again from silence rather than
    // replaying stale audio.
    void process(float* left, float* right, int frames, int delay) noexcept {
        delay = std::clamp(delay, 0, capacity() - 1);
        if (delay != delay_) {
            std::fill(left_.begin(), left_.end(), 0.f);
            std::fill(right_.begin(), right_.end(), 0.f);
            delay_ = delay;
            write_ = 0;
        }
        if (delay == 0) return;
        const int size = capacity();
        if (frames + delay <= size) {
            // The whole chunk goes in before anything comes out: what it reads from
            // before this chunk (delay samples back) is out of the way of its writes.
            const int read = write_ - delay < 0 ? write_ - delay + size : write_ - delay;
            const auto at = static_cast<size_t>(write_), from = static_cast<size_t>(read), n = static_cast<size_t>(frames);
            copyIntoRing(left_.data(), left_.size(), at, left, n);
            copyIntoRing(right_.data(), right_.size(), at, right, n);
            copyOutOfRing(left_.data(), left_.size(), from, left, n);
            copyOutOfRing(right_.data(), right_.size(), from, right, n);
            write_ = (write_ + frames) % size;
            return;
        }
        for (int i = 0; i < frames; ++i) {  // a line shorter than delay + chunk (offline lines are just long enough)
            left_[write_] = left[i];
            right_[write_] = right[i];
            int read = write_ - delay;
            if (read < 0) read += size;
            left[i] = left_[read];
            right[i] = right_[read];
            if (++write_ == size) write_ = 0;
        }
    }

private:
    std::vector<float> left_, right_;
    int write_ = 0;
    int delay_ = 0;
};

// A device its automation switches on and off (its lane kDeviceOnLane). While
// it is off the renderer doesn't process it and passes its input on, delayed
// by the device's own latency (so what comes after it stays in time); it
// starts again from silence as it comes back on. Switching fades over
// Renderer::kSwitchFade.
struct SwitchRender {
    AutomationRender lane;  // its envelope (lane.insert: the device's place in its chain)
    int latency = 0;        // the device's: how late its input is passed on while it is off
    int delayIndex = -1;    // its place in RenderSnapshot::switchDelays (offline renders bring lines of their own)
    std::shared_ptr<DelayLine> delay;  // for the live renderer
};

// A note's bend (NoteBend.h) as the renderer plays it: times in samples from
// the note's start, vibratos' rates in cycles a sample.
struct NoteBendRender {
    std::vector<BendPoint> points;  // sorted by time
    std::vector<VibratoSpan> vibrato;

    double at(double t) const noexcept { return bend::at(points, vibrato, t); }
};

// A note of a MIDI clip, already cut to its clip: the renderer sends a note-on
// at `start` and a note-off at `end`, and while it sounds its bend (if any).
struct NoteRender {
    int64_t start = 0;  // timeline samples
    int64_t end = 0;    // > start
    // The same in beats: a new tempo map moves a sounding note's ends to these beats (Renderer::syncTempo()).
    double startBeat = 0.0;
    double endBeat = 0.0;
    uint8_t key = 60;
    uint8_t velocity = 100;
    std::shared_ptr<const NoteBendRender> bend;  // null: it doesn't bend
};

// A track's notes in a snapshot, sorted by start: shared by every snapshot built
// while they, the tempo and the sample rate stay as they are (the edit side keeps
// them with the track: Engine::TrackModel::rendered), so a snapshot doesn't make
// them again. Read as a vector; never null.
class NoteList {
public:
    using Notes = std::vector<NoteRender>;
    NoteList() : notes_(none()) {}
    explicit NoteList(std::shared_ptr<const Notes> notes) : notes_(notes ? std::move(notes) : none()) {}
    NoteList(Notes notes) : notes_(std::make_shared<const Notes>(std::move(notes))) {}  // (a list of its own)

    size_t size() const noexcept { return notes_->size(); }
    bool empty() const noexcept { return notes_->empty(); }
    const NoteRender& operator[](size_t index) const noexcept { return (*notes_)[index]; }
    Notes::const_iterator begin() const noexcept { return notes_->begin(); }
    Notes::const_iterator end() const noexcept { return notes_->end(); }
    const std::shared_ptr<const Notes>& shared() const noexcept { return notes_; }

private:
    static const std::shared_ptr<const Notes>& none() {
        static const std::shared_ptr<const Notes> empty = std::make_shared<const Notes>();
        return empty;
    }
    std::shared_ptr<const Notes> notes_;
};

// A device's MIDI input from another track (Engine::setProcessorMidiInput()):
// what it hears instead of its own track's notes. Rendering-thread state, kept
// with the device across snapshots: the prologue copies the source track's
// events for the chunk into it, before the graph runs (where the source's own
// devices rebase theirs to their stretches), and the device reads its copy.
struct MidiFeed {
    std::vector<ProcessEvent> events;
    int numEvents = 0;

    explicit MidiFeed(int capacity) : events(static_cast<size_t>(capacity)) {}
};

// A device's feed in the snapshot, and where it comes from.
struct MidiFeedRender {
    int source = -1;  // the snapshot track whose notes it takes (-1: none in this snapshot)
    std::shared_ptr<MidiFeed> feed;
};

// How deep racks nest: a rack in a strip's own chain is at depth 0, one in a
// chain of that rack at depth 1, and so on. The renderer has scratch for each depth.
constexpr int kMaxRackDepth = 8;

struct RackRender;

// A parameter a modulator moves (Modulation.h), in the chain its device is in.
struct ModulationRender {
    std::shared_ptr<Processor> modulator;  // what moves it: one of its strip's `modulators`
    std::shared_ptr<Processor> processor;  // the device
    int param = 0;                         // its parameter's index
    int steps = 0;                         // > 0: a discrete parameter (values snap to its steps)
    int insert = -1;                       // the device's place in its chain
    float low = 0.f, high = 1.f;           // the parameter's normalized values at the modulator's 0 and 1
    bool invert = false;
};

// A modulator in a strip (its own chain's, or in a rack's chain however deep),
// which the renderer runs before the strip's devices (Renderer::runModulators).
struct ModulatorRender {
    std::shared_ptr<Processor> processor;
    const RackRender* rack = nullptr;  // the rack whose chain it is in (null: the strip's own chain); the snapshot owns it
    int chain = 0;                     // that chain's index in the rack
    int insert = 0;                    // its place in its chain
};

// What every strip has: a chain of devices, and a fader with its meter. Tracks
// are strips fed by their clips and notes (and what goes into them); the master
// is the strip fed by what goes into it. Delay compensation is the edges'.
// A rack's chains are strips too (ChainRender), inside the strip they are on.
struct StripRender {
    std::shared_ptr<TrackParams> params;
    std::vector<std::shared_ptr<Processor>> inserts;
    // Samples the enabled inserts add, and the delays before sidechained ones
    // (to line them up with their sidechain: EdgeRender::deviceDelay).
    int latency = 0;
    std::vector<AutomationRender> automation;  // of its devices' parameters, in chain order
    std::vector<SwitchRender> switches;        // its devices switched on and off by automation, in chain order
    AutomationRender volume, pan;              // of its mixer
    // A track's switch (its activator, kTrackOnLane): while it has one it is
    // heard where it is on, whatever its mute (the master has none).
    AutomationRender on;
    // Per insert, the edges going into its sidechain (aux) input (summed: its
    // own sidechain, and tracks' outputs going into it); empty if none has any.
    std::vector<std::vector<int>> sidechains;
    // The edges leaving it after one of its devices (EdgeRender::Tap::AfterDevice), by
    // device: those before its first device (tapDevice -1) first. A rack's chain
    // has those taken after its own devices (its strip's source's sidechains).
    std::vector<int> deviceTaps;
    // Per insert, its chains if it is a rack (null: a device); empty if none is.
    std::vector<std::shared_ptr<const RackRender>> racks;
    // Per insert, the MIDI it takes from another track instead of the strip's
    // notes (null: the strip's); empty if none takes any.
    std::vector<std::shared_ptr<MidiFeed>> midiFeeds;
    // The parameters of its devices that modulators move, in chain order (their
    // automation, if any, isn't played meanwhile).
    std::vector<ModulationRender> modulation;
    // A strip's own (not a rack chain's): every modulator on it, racks' too,
    // in the order they run (at most kMaxModulators of them do).
    std::vector<ModulatorRender> modulators;
};

// A chain of a rack: a strip inside a strip. The rack hands each chain its own
// input, and sums what they put out: its devices, then its fader (with its
// mute, and solo among the rack's chains), then a delay that lines it up with
// the rack's slowest chain (Routing.h). Its devices hear the strip's notes.
struct ChainRender : StripRender {
    uint32_t id = 0;
    int compensation = 0;  // samples it is delayed to line up with the rack's slowest chain
    int delayIndex = -1;   // its place in RenderSnapshot::chainDelays (offline renders bring lines of their own)
    std::shared_ptr<DelayLine> delay;  // for the live renderer
    // A Drum Rack's pad: the key whose notes alone its devices hear, as C3 (60); -1: every note.
    int receiveNote = -1;
};

// A rack as the renderer runs it (Renderer::processRack). Without chains it
// passes its input on.
struct RackRender {
    std::vector<ChainRender> chains;
    int depth = 0;  // how many racks it is in (its scratch: < kMaxRackDepth)
};

// A routing edge's state outside the snapshot: the edit side allocates it with
// the edge (a track's output, a send, a sidechain) and keeps it while the edge exists.
struct EdgeState {
    std::atomic<float> gain{1.f};  // a send's level (an output's stays 1). Written by the API.

    // Rendering-thread state.
    std::vector<float> left, right;  // its own signal, if it needs one (EdgeRender::ownSignal())
    bool live = true;                // per chunk: solo lets it through (Renderer.h)
    // Live renders: its level and whether solo lets it through, ramped like a
    // fader. Automation replaces the level sample by sample, as it does a fader's.
    SmoothedValue level;
    SmoothedValue audible;
    double smoothingSampleRate = 0.0;

    explicit EdgeState(int frames) : left(static_cast<size_t>(frames), 0.f), right(static_cast<size_t>(frames), 0.f) {}
};

// An edge of the routing graph (Routing.h): a strip's signal going into another
// strip (a group's bus, a return), or into the master. Each track has at most
// one output edge (post-fader) and any number of sends, tapped after its fader
// or before it. The destination sums its incoming edges, each at its level. An
// input edge (resampling) is a track's input taken from another track's output
// (tapped as a sidechain is): it isn't summed, but heard instead of the track's
// clips while the track is monitored (InputEdge), and recorded; it isn't
// delay-compensated. A Track In edge is an output into a track that takes what
// goes into it as its input (Engine::setTrackInMonitored): heard as an input
// edge is, but not recorded. A sidechain goes into one device of its
// destination (a track's, or the master's), into its aux input: tapped after
// the source's fader, before it, after one of its devices or before all of
// them, and lined up with the destination's signal at that device (a track's
// output going into a device's sidechain is one too, after the fader). It
// isn't heard on its own, so the source's mute (and solo) silence it only
// after the fader.
struct EdgeRender {
    enum class Kind : uint8_t { Output, Send, Input, TrackIn, Sidechain };
    enum class Tap : uint8_t { PostFader, PreFader, AfterDevice };

    int from = 0;   // the snapshot track it leaves
    int to = -1;    // the snapshot track it goes into; -1: the master
    Kind kind = Kind::Output;
    Tap tap = Tap::PostFader;
    // AfterDevice: the insert it is taken after (-1: before the first), in the
    // source's own chain or in the rack chain whose deviceTaps list it.
    int tapDevice = -1;
    int compensation = 0;              // samples it is delayed to line up with the latest edge into `to`
    std::shared_ptr<DelayLine> delay;  // for the live renderer (offline renders bring their own)
    std::shared_ptr<EdgeState> state;  // never null in an engine's snapshot
    // A send's automated level (as a fader's volume). Applied where the edge is
    // summed, after its delay: its latency is how late the destination hears its inputs.
    AutomationRender level;
    // A sidechain: the destination's insert whose aux input it feeds, and how much
    // the destination's own signal is delayed just before that insert to line up
    // with it (when it arrives later than that signal; else the edge is delayed).
    int device = -1;
    int deviceDelay = 0;
    std::shared_ptr<DelayLine> deviceDelayLine;  // for the live renderer (offline renders bring their own)

    // Whether the source writes the edge's signal into the edge's own buffer: a
    // tap before the fader (the source's buffer holds it after the fader), or a
    // signal delayed for this edge alone. Otherwise the destination reads the source's buffer.
    bool ownSignal() const noexcept { return tap != Tap::PostFader || compensation > 0; }
    // Whether its destination sums it into its input (an input or Track In edge
    // is heard only while monitored, a sidechain only by its device).
    bool sums() const noexcept { return kind == Kind::Output || kind == Kind::Send; }
};

// Where a strip's input comes from: the device's inputs (a mono channel or a
// stereo pair), another track's output (an input edge: resampling it), or the
// master's output (resampling the mix: it is recorded after the master, so it
// can't be monitored, which would feed it back).
struct InputEdge {
    enum class Source : uint8_t { None, Device, Track, Master };
    Source source = Source::None;
    // Device: indices into the device's open inputs (the callback's order); -1:
    // not open (the input is silent). A mono input has both the same.
    int left = -1;
    int right = -1;
    int edge = -1;  // Track: the snapshot edge (Kind::Input) it comes in on
    int arrival = 0;  // Track: how late its source's signal leaves where it is tapped

    bool fromDevice() const noexcept { return source == Source::Device; }
    // Whether the track can hear it (monitoring): the device's, or another track's.
    bool monitorable() const noexcept { return source == Source::Device || source == Source::Track; }
};

// When a track hears its input instead of its clips (input monitoring).
enum class MonitorMode : uint8_t {
    Off,   // never
    In,    // always
    Auto,  // while armed, unless it plays back without recording
};

// Which MIDI input a track hears (and records): one input or all of them, one
// channel or all.
struct MidiInputRoute {
    static constexpr int kAllPorts = -1;
    static constexpr int kAllChannels = -1;
    bool enabled = false;
    int port = kAllPorts;        // the engine's port id of an input
    int channel = kAllChannels;  // 0-15

    bool accepts(uint16_t fromPort, uint8_t status) const noexcept {
        return enabled && (port == kAllPorts || port == fromPort) &&
               (channel == kAllChannels || channel == (status & 0x0F));
    }
};

// A track's working state for one chunk (rendering-thread scratch): its signal,
// and what the serial part of the chunk worked out for it before the graph
// runs (Renderer.h). Every track has its own, so tracks can render on any
// thread; the edit side allocates it and keeps it across snapshots, like a
// DelayLine. A bus reads the signals of the tracks that go into it from theirs.
struct TrackBuffers {
    static constexpr int kMaxEvents = 1024;     // note events per chunk
    static constexpr int kMaxClipVoices = 32;   // stretched clips playing per chunk
    struct ClipVoice {                           // the stretcher a clip plays through in this chunk
        const ClipRender* clip = nullptr;
        WarpVoice* voice = nullptr;
        const WarpRender* rendered = nullptr;    // or its render, instead of a voice
        bool continuing = false;                 // it played this clip in the block before
    };

    std::vector<float> left, right;  // its inputs summed, its clips (or live input), then its strip
    std::vector<ProcessEvent> events;  // its note events, in the order its devices get them
    int numEvents = 0;
    std::array<ClipVoice, kMaxClipVoices> voices{};  // in the order its clips play
    int numVoices = 0;
    bool monitored = false;  // plays its live input instead of its clips
    // Heard as it is played, without delay compensation: monitored, or live
    // while compensation is constrained (TrackRender::live).
    bool uncompensated = false;
    int clipDelay = 0;       // its clips' notes' delay in the chunk before (TrackRender::clipDelay)
    bool recorded = false;   // being recorded: its clips are silent (the take replaces them)
    bool audible = true;     // not muted, and solo lets one of its edges through
    // Solo, per chunk: it is soloed; it is soloed or fed by something soloed
    // (downstream of a solo); it is soloed or feeds something soloed (upstream).
    bool soloed = false;
    bool soloDown = false;
    bool soloUp = false;
    // What rendering it takes, in nanoseconds per frame, smoothed over the last
    // chunks (the thread that renders it measures; the scheduler orders by it).
    std::atomic<float> cost{0.f};

    explicit TrackBuffers(int frames)
        : left(static_cast<size_t>(frames), 0.f), right(static_cast<size_t>(frames), 0.f), events(kMaxEvents) {}
    bool pushEvent(const ProcessEvent& event) noexcept {
        if (numEvents >= static_cast<int>(events.size())) return false;
        events[static_cast<size_t>(numEvents++)] = event;
        return true;
    }
};

// A track in the routing graph (Routing.h). The snapshot lists tracks in an
// order in which each comes after everything that feeds it. A track's input is
// what comes in on its incoming edges (summed in that order, but for an input
// edge), plus its clips and notes (or the live input); its signal leaves on its
// outgoing edges.
struct TrackRender : StripRender {
    uint32_t id = 0;
    std::vector<int> incoming;      // edges into it (snapshot edges), in the order it sums them
    std::vector<int> outgoing;      // edges out of it: its output (if any) first, then its sends (and input edges and sidechains)
    int inputCount = 0;             // incoming.size(): the scheduler runs it once they are done
    // How late it hears what its summed edges bring. (Its own clips and notes
    // play on time: they aren't delayed to line up with its inputs, and nor is
    // the input it monitors. Groups and returns have none.)
    int inputLatency = 0;
    std::shared_ptr<TrackBuffers> buffers;  // its signal and chunk state (never null in an engine's snapshot)
    InputEdge input;
    MidiInputRoute midiInput;
    MonitorMode monitor = MonitorMode::Auto;
    bool armed = false;
    // Frozen (Engine::setTrackFrozen): it plays its clips (its frozen audio)
    // and doesn't hear what goes into it; it has no devices, notes or input then.
    bool frozen = false;
    // Other tracks' outputs come into it as its input (Track In edges): it can
    // be monitored without an input of its own.
    bool trackIn = false;
    // Constrained: a monitored track (In, or Auto and armed), or a strip one is
    // heard through. Its edges aren't delay-compensated, playing back or not.
    bool live = false;
    // Live, how late its own clips' notes play (samples): what its way out
    // would have been delayed, so they stay in time while only what is played
    // into it comes early.
    int clipDelay = 0;
    std::vector<ClipRender> clips;  // sorted by start
    int64_t maxClipLength = 0;      // bounds the binary search window
    NoteList notes;  // sorted by start
};

// Calls f(clip, from, to) for each clip of the track that plays in [segStart,
// segEnd), in order, with the part of the segment it covers.
template <typename F>
void forEachClip(const TrackRender& track, int64_t segStart, int64_t segEnd, F&& f) {
    // Clips are sorted by start; no clip starting before this bound can reach the segment.
    const int64_t earliest = segStart - track.maxClipLength;
    auto it = std::lower_bound(track.clips.begin(), track.clips.end(), earliest,
                               [](const ClipRender& clip, int64_t value) { return clip.start < value; });
    for (; it != track.clips.end() && it->start < segEnd; ++it) {
        const int64_t from = std::max(segStart, it->start);
        const int64_t to = std::min(segEnd, it->start + it->length);
        if (from < to) f(*it, from, to);
    }
}

struct RenderSnapshot {
    double sampleRate = 48000.0;
    // The song's tempo and time signatures (music/TempoMap.h), never null; `tempoVersion` is a
    // new number with each new tempo map, so the renderer knows to move what it holds in samples.
    std::shared_ptr<const TempoMap> tempoMap = std::make_shared<const TempoMap>();
    std::shared_ptr<const MeterMap> meter = std::make_shared<const MeterMap>();
    uint64_t tempoVersion = 0;
    int keyTonic = -1;  // the song's key (Engine::setSongKey): -1 none
    bool keyMinor = false;
    bool loopEnabled = false;
    int64_t loopStart = 0;
    int64_t loopEnd = 0;
    // Punch In and Out (Engine::setPunch): outside [loopStart, loopEnd), recorded tracks play their clips.
    bool punchIn = false;
    bool punchOut = false;
    int maxLatency = 0;  // the tracks reach the master this late (delay-compensated alike)
    // Delay compensation is constrained (Engine::setConstrainLatency()).
    bool constrained = false;
    std::vector<TrackRender> tracks;  // in routing order: every track after those that feed it
    std::vector<EdgeRender> edges;    // the routing graph's edges, by source in snapshot order
    std::vector<int> masterInputs;    // the edges the master sums, in snapshot order (not sidechains into its devices)
    // The tracks' dependencies, for the scheduler (null: the tracks render in order).
    std::shared_ptr<TaskGraph> graph;
    int parallelWork = 0;  // tracks worth a thread of their own (devices, stretched clips)
    StripRender master;  // its params are null in a snapshot made without an engine
    WarpVoiceSet warpVoices;  // stretchers for the live renderer (offline renders bring their own)
    // Every rack chain's compensation, by its delayIndex (offline renders make lines that long).
    std::vector<int> chainDelays;
    // Every switched device's latency, by its SwitchRender::delayIndex (likewise).
    std::vector<int> switchDelays;
    // Every device's MIDI input from another track (StripRender::midiFeeds), for the prologue to fill.
    std::vector<MidiFeedRender> midiFeeds;

    // The output lags the timeline by this much: the tracks' latency, then the
    // master's devices. The metronome is delayed as much.
    int outputLatency() const { return maxLatency + master.latency; }

    // Beats and timeline samples, both ways, by the tempo map. A steady tempo
    // counts as it always has (beats times samples a beat), to the bit.
    double sampleTime(double beat) const noexcept {
        if (tempoMap->constant()) return beat * (sampleRate * 60.0 / tempoMap->points().front().bpm);
        return tempoMap->secondsAt(beat) * sampleRate;
    }
    int64_t sampleAt(double beat) const noexcept { return std::llround(sampleTime(beat)); }
    double beatAt(double sample) const noexcept {
        if (tempoMap->constant()) return sample / (sampleRate * 60.0 / tempoMap->points().front().bpm);
        return tempoMap->beatAt(sample / sampleRate);
    }
    double tempoAt(double sample) const noexcept { return tempoMap->bpmAt(beatAt(sample)); }
    // Samples from `beat` to `beat + beats` (fractional), where they are.
    double samplesBetween(double beat, double beats) const noexcept {
        if (tempoMap->constant()) return beats * (sampleRate * 60.0 / tempoMap->points().front().bpm);
        return tempoMap->secondsBetween(beat, beat + beats) * sampleRate;
    }

    // The track with this id; null if the snapshot has none.
    const TrackRender* findTrack(uint32_t id) const noexcept {
        for (const TrackRender& track : tracks)
            if (track.id == id) return &track;
        return nullptr;
    }
};

}  // namespace sub
