// Built-in "Groove" device, a MIDI effect (Processor::isMidiEffect()): the
// notes reaching it, in time with a groove (music/Groove.h): the same notes
// out, moved, with their velocities and lengths as the groove has them.
//
// The groove it plays is its source (a state value): Swing (the Swing and Step
// knobs: MPC swing, groove::swing()), Project (the project's default groove:
// ProcessContext::groove), or a groove of its own (a library groove's copy,
// "groove": its text, groove::toText()). Timing, Velocity, Length, Random and
// Quantize are the amounts it plays it at (groove::Amounts); a note takes their
// values at the moment it comes in. Random's jitter comes from the device's seed
// (a state value), so a render is the same every time.
//
// Moving notes later: a note-on is held and played at its time; its note-off is
// moved by as much (and by Length: the note's length times its step's, as far
// as Length says), and its per-note bends follow it (its note id is kept). Raw
// MIDI passes at once. Up to kMaxNotes notes are held; one more passes at once.
// A note played on a key and channel another of its notes still sounds on ends
// that one first, so the note-off it is waiting for can't cut the new one.
//
// Moving notes earlier: a note can't come out before it comes in, but the
// renderer hands the first MIDI effect of a chain the clips' notes due within
// earlyBeats() early (ctx.earlyEvents: half a step while Allow Early is on).
// Those it places both ways (no earlier than half a step), and ignores them as
// they come again in their time (by their note ids). A note handed early that
// is ended before its time came (a locate, stopping) is dropped: it never
// sounded. Notes it doesn't get early (live input, notes an Arpeggiator before
// it plays) move later only. With Latency on it reports half a step as its
// latency instead, and plays every note that much later, so delay compensation
// lets live input move both ways too.
//
// While the transport is stopped notes pass as they come (there is no beat to
// groove them on); stopping drops the notes held that haven't sounded yet.
// Switched off, endMidiNotes() plays the sounding notes' note-offs (those held
// are dropped: they haven't sounded).
//
// Its editor lights the steps played: the display "step", one value per 64
// samples: the step of the cycle the newest sounding note fell on, -1 while
// none sounds.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/Dsp.h"
#include "music/Groove.h"
#include "music/TempoMap.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

class GrooveProcessor final : public BuiltinProcessor {
public:
    enum Param { Swing = 0, StepParam, Timing, Velocity, Length, Random, Quantize, Early, Latency, NumParams };
    enum class Source { Swing, Project, Groove };
    enum Display { StepDisplay = 0 };
    static constexpr int kDisplaySamples = 64;
    static constexpr int kMaxNotes = 64;
    static constexpr size_t kMaxQueued = 4096;  // events waiting for their time
    static constexpr size_t kBendRoom = 1024;   // bends wait only while this much room is left (for note-offs)
    static constexpr int kSwallowed = 256;      // note ids taken early whose note-on or -off comes again

    GrooveProcessor() : BuiltinProcessor(infos(), {{"step", kDisplaySamples}}) {
        swing_.lanes.resize(1);
        swing_.lanes.front().steps.reserve(static_cast<size_t>(groove::kMaxSteps));
        fillSwing(groove::kMinSwing, groove::kDefaultStep);
    }
    ~GrooveProcessor() override {
        collectRetired();
        delete pending_.exchange(nullptr);
        delete active_;
    }

    std::string typeId() const override { return "builtin:groove"; }
    std::string name() const override { return "Groove"; }
    bool isMidiEffect() const override { return true; }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        reset();
        latencyReport_.reported(latencySamples());
    }

    void reset() override {
        for (Note& note : notes_) free(note);
        queued_ = 0;
        numSwallowedOn_ = numSwallowedOff_ = 0;
        sounding_ = 0;
        lastStep_ = -1;
        displayCount_ = 0;
        wasPlaying_ = false;
    }

    void endMidiNotes(EventOutput& out) noexcept override {
        for (Note& note : notes_) {
            if (note.used && note.started && !note.ended) out.add(offFor(note, 0));
            free(note);
        }
        queued_ = 0;
        numSwallowedOn_ = numSwallowedOff_ = 0;
        sounding_ = 0;
    }

    double earlyBeats() const noexcept override {
        return isOn(Early) && !isOn(Latency) ? 0.5 * currentStep() : 0.0;
    }

    int latencySamples() const override {
        if (!isOn(Latency)) return 0;
        const double tempo = std::max(TempoMap::kMinBpm, lastTempo_.load(std::memory_order_relaxed));
        return static_cast<int>(std::lround(0.5 * currentStep() * 60.0 / tempo * sampleRate_));
    }

    bool idle() override {
        collectRetired();
        return latencyReport_.changed(latencySamples());
    }

protected:
    StateValues stateValues() const override {
        std::lock_guard lock(mutex_);
        StateValues values = others_;  // (what the application keeps with it: the library groove's id, its name)
        values["source"] = sourceName(source_.load(std::memory_order_relaxed));
        if (!grooveText_.empty()) values["groove"] = grooveText_;
        if (seed_.load(std::memory_order_relaxed) != 0) values["seed"] = std::to_string(seed_.load());
        return values;
    }

    void setStateValues(const StateValues& values) override {
        std::lock_guard lock(mutex_);
        others_ = values;
        others_.erase("source");
        others_.erase("groove");
        others_.erase("seed");
        const auto value = [&values](const char* name) {
            const auto it = values.find(name);
            return it == values.end() ? std::string() : it->second;
        };
        const std::string source = value("source");
        source_.store(source == "project" ? Source::Project : source == "groove" ? Source::Groove : Source::Swing);
        try {
            seed_.store(static_cast<uint32_t>(std::stoul(value("seed"))));
        } catch (const std::exception&) {
            seed_.store(0);
        }
        const std::string text = value("groove");
        if (text == grooveText_) return;
        grooveText_ = text;
        collectRetired();
        auto shared = std::make_unique<Shared>();
        if (std::optional<groove::Groove> parsed = groove::fromText(text)) {
            shared->groove = std::move(*parsed);
            shared->valid = true;
            grooveStep_.store(shared->groove.stepBeats, std::memory_order_relaxed);
        }
        delete pending_.exchange(shared.release(), std::memory_order_acq_rel);
    }

    void render(const ProcessContext& ctx, float* const*, int, int numFrames) override {
        takeGroove();
        lastTempo_.store(ctx.tempo, std::memory_order_relaxed);
        if (ctx.groove) projectStep_.store(ctx.groove->stepBeats, std::memory_order_relaxed);
        const groove::Groove* playing = grooveFor(ctx);
        const groove::Amounts amounts{param(Timing) / 100.0, param(Velocity) / 100.0, param(Random) / 100.0,
                                      param(Quantize) / 100.0, param(Length) / 100.0,
                                      seed_.load(std::memory_order_relaxed)};
        // Stopped: the notes held that haven't sounded never will.
        if (wasPlaying_ && !ctx.playing) dropUnsounded();
        wasPlaying_ = ctx.playing;
        const Placing placing{ctx, playing && ctx.tempoMap && ctx.playing ? playing : nullptr, amounts,
                              isOn(Latency) ? latencySamples() : 0,
                              isOn(Latency) ? 0.5 * currentStep() : earlyBeats()};

        for (size_t e = 0; e < ctx.earlyEvents.count; ++e) takeEarly(placing, ctx.earlyEvents.events[e]);
        for (size_t e = 0; e < ctx.inEvents.count; ++e) take(placing, ctx.inEvents.events[e]);

        // What is due in this stretch, in time order.
        const int64_t end = clock_ + numFrames;
        while (queued_ > 0 && queue_[0].time < end) {
            const Queued item = popQueued();
            play(ctx, item);
        }
        for (displayCount_ += numFrames; displayCount_ >= kDisplaySamples; displayCount_ -= kDisplaySamples) {
            publish(StepDisplay, sounding_ > 0 ? static_cast<float>(lastStep_) : -1.f);
        }
        clock_ = end;
    }

private:
    // A note it holds, from its note-on coming in until its note-off has gone out and come in.
    struct Note {
        bool used = false;
        uint32_t generation = 0;  // queued events of an earlier note in this place don't play
        uint8_t key = 0, channel = 0, velocity = 100;
        int32_t noteId = -1;
        int64_t inOn = 0;    // on its clock: when it would have started
        int64_t outOn = 0;   // when it starts
        double length = 1.0; // its length times this
        int step = -1;       // the step of the cycle it fell on (-1: none)
        bool early = false;    // handed early (a clip's note)
        bool started = false;  // its note-on has gone out
        bool offQueued = false;
        bool ended = false;    // its note-off has gone out (or never will: dropped)
        bool offIn = false;    // its note-off has come in
    };
    // An event waiting for its time.
    struct Queued {
        int64_t time = 0;
        uint8_t order = 0;  // at one time: note-offs, then note-ons and raw MIDI, then bends
        uint32_t serial = 0;
        int note = -1;      // its note (-1: none, as it is)
        uint32_t generation = 0;
        ProcessEvent event;
    };
    struct Shared {
        groove::Groove groove;
        bool valid = false;
    };
    // What placing a note goes by in a stretch.
    struct Placing {
        const ProcessContext& ctx;
        const groove::Groove* groove;  // null: notes as they come
        groove::Amounts amounts;
        int latency;       // samples every note plays later (Latency)
        double early;      // beats a note may move earlier, if it came early (or with Latency)
    };

    static const char* sourceName(Source source) {
        switch (source) {
            case Source::Project: return "project";
            case Source::Groove: return "groove";
            case Source::Swing: break;
        }
        return "swing";
    }

    double currentStep() const noexcept {
        switch (source_.load(std::memory_order_relaxed)) {
            case Source::Project: return projectStep_.load(std::memory_order_relaxed);
            case Source::Groove: return grooveStep_.load(std::memory_order_relaxed);
            case Source::Swing: break;
        }
        return groove::kSteps[static_cast<size_t>(std::clamp(choiceIndex(StepParam), 0, 4))].beats;
    }

    const groove::Groove* grooveFor(const ProcessContext& ctx) noexcept {
        switch (source_.load(std::memory_order_relaxed)) {
            case Source::Project: return ctx.groove;
            case Source::Groove: return active_ && active_->valid ? &active_->groove : nullptr;
            case Source::Swing: break;
        }
        const float percent = param(Swing);
        const int step = std::clamp(choiceIndex(StepParam), 0, static_cast<int>(groove::kSteps.size()) - 1);
        if (percent != swingPercent_ || step != swingStep_) fillSwing(percent, step);
        return &swing_;
    }

    // swing() into swing_, in place (its steps' room is reserved: nothing is allocated).
    void fillSwing(float percent, int step) noexcept {
        swingPercent_ = percent;
        swingStep_ = step;
        const double beats = groove::kSteps[static_cast<size_t>(step)].beats;
        swing_.stepBeats = beats;
        swing_.base = beats;
        swing_.bars = 1;
        std::vector<groove::Step>& steps = swing_.lanes.front().steps;
        steps.resize(static_cast<size_t>(groove::stepsPerCycle(swing_)));
        const double late = 2.0 * std::clamp(static_cast<double>(percent), groove::kMinSwing, groove::kMaxSwing) / 100.0 - 1.0;
        for (size_t i = 0; i < steps.size(); ++i) steps[i] = {i % 2 != 0 ? late : 0.0, 0.0, 0.0, 1.0};
    }

    // --- Notes in --------------------------------------------------------------

    // A note's place: its note-on's time (on the clock), velocity, length and step,
    // from where it would play (`at`, on the clock, at `beat` on the timeline).
    struct Place {
        int64_t on;
        uint8_t velocity;
        double length;
        int step;
    };
    Place placeNote(const Placing& p, const ProcessEvent& in, int64_t at, double beat, bool early) const noexcept {
        Place out{at + p.latency, in.velocity(), 1.0, -1};
        if (!p.groove) return out;
        const TempoMap& tempo = *p.ctx.tempoMap;
        const groove::Placed placed = groove::place(*p.groove, beat, in.key(), in.velocity(), 1.0, tempo, p.amounts);
        // No earlier than it may go: half a step if it came early (or with Latency), else not at all.
        const double earliest = early || p.latency > 0 ? beat - p.early : beat;
        const double moved = tempo.secondsAt(std::max(placed.beat, earliest)) - tempo.secondsAt(beat);
        out.on = std::max(clock_, at + p.latency + static_cast<int64_t>(std::llround(moved * sampleRate_)));
        out.velocity = static_cast<uint8_t>(std::clamp(std::lround(placed.velocity), 1L, 127L));
        out.length = placed.lengthBeats;
        out.step = placed.step;
        return out;
    }

    // The beat a note `offset` samples into the stretch is at.
    static double beatAt(const ProcessContext& ctx, int offset) noexcept {
        if (!ctx.tempoMap) return ctx.beatPos + (ctx.samplesPerBeat() > 0.0 ? offset / ctx.samplesPerBeat() : 0.0);
        return ctx.tempoMap->beatAt(ctx.tempoMap->secondsAt(ctx.beatPos) + offset / ctx.sampleRate);
    }

    void take(const Placing& p, const ProcessEvent& in) noexcept {
        const int64_t at = clock_ + std::max(0, in.sampleOffset);
        if (in.startsNote()) {
            if (in.noteId >= 0 && swallow(swallowedOn_, numSwallowedOn_, in.noteId)) return;  // it came early
            start(in, at, placeNote(p, in, at, beatAt(p.ctx, in.sampleOffset), false), false);
        } else if (in.endsNote()) {
            if (in.noteId >= 0 && swallow(swallowedOff_, numSwallowedOff_, in.noteId)) return;
            end(in, at, false);
        } else if (in.type == ProcessEvent::Type::NoteBend) {
            bend(in, at);
        } else {
            enqueue(at + p.latency, 1, -1, in);  // raw MIDI: as it comes (with Latency, as late as the notes)
        }
    }

    void takeEarly(const Placing& p, const EarlyEvent& early) noexcept {
        const ProcessEvent& in = early.event;
        const int64_t at = clock_ + std::max(0, in.sampleOffset);
        if (in.startsNote()) {
            if (in.noteId >= 0 && findNote(in.noteId) >= 0) return;  // (it has it already)
            if (start(in, at, placeNote(p, in, at, early.beat, true), true) && in.noteId >= 0) {
                remember(swallowedOn_, numSwallowedOn_, in.noteId);
            }
        } else if (in.endsNote()) {
            if (in.noteId >= 0 && findNote(in.noteId) >= 0) {
                remember(swallowedOff_, numSwallowedOff_, in.noteId);
                end(in, at, true);
            }
        }
    }

    bool start(const ProcessEvent& in, int64_t at, const Place& place, bool early) noexcept {
        Note* note = freeNote();
        ProcessEvent on = in;
        if (!note) {  // none free: as it comes (one handed early, when it comes in its time)
            if (!early) enqueue(at, 1, -1, on);
            return false;
        }
        const int index = static_cast<int>(note - notes_.data());
        const uint32_t generation = note->generation;
        *note = Note{};
        note->used = true;
        note->generation = generation;
        note->key = in.key();
        note->channel = in.channel();
        note->velocity = place.velocity;
        note->noteId = in.noteId;
        note->inOn = at;
        note->outOn = place.on;
        note->length = place.length;
        note->step = place.step;
        note->early = early;
        on.data[1] = place.velocity;
        enqueue(place.on, 1, index, on);
        return true;
    }

    // A note's note-off coming in (`early`: handed early, at its time on the clock).
    void end(const ProcessEvent& in, int64_t at, bool early) noexcept {
        const int index = in.noteId >= 0 ? findNote(in.noteId) : findHeld(in.key(), in.channel());
        if (index < 0) {  // not one of its notes: as it comes
            enqueue(at, 0, -1, in);
            return;
        }
        Note& note = notes_[static_cast<size_t>(index)];
        if (note.offIn || note.offQueued) {
            note.offIn = true;
            if (note.ended) free(note);
            return;
        }
        note.offIn = true;
        if (note.ended || (note.early && !note.started && !early)) {
            // Ended before its time came (a locate, stopping): it never sounds.
            free(note);
            return;
        }
        // (Not before it came in: one handed early, not before now.)
        const auto length = static_cast<double>(at - note.inOn) * note.length;
        const int64_t off = std::max(early ? clock_ : at, note.outOn + std::max<int64_t>(1, std::llround(length)));
        note.offQueued = true;
        ProcessEvent event = in;
        event.data[0] = note.key;
        event.data[2] = note.channel;
        enqueue(std::max(off, clock_), 0, index, event);
    }

    void bend(const ProcessEvent& in, int64_t at) noexcept {
        const int index = in.noteId >= 0 ? findNote(in.noteId) : -1;
        if (index < 0) {
            enqueue(at, 2, -1, in);
            return;
        }
        const Note& note = notes_[static_cast<size_t>(index)];
        if (note.ended || queued_ + kBendRoom >= kMaxQueued) return;
        enqueue(std::max(clock_, at + (note.outOn - note.inOn)), 2, index, in);
    }

    void dropUnsounded() noexcept {
        for (Note& note : notes_) {
            if (!note.used || note.started) continue;
            note.ended = true;  // (its note-off, coming in, frees it)
            if (note.offIn) free(note);
        }
    }

    // --- Notes out ---------------------------------------------------------------

    void play(const ProcessContext& ctx, const Queued& item) noexcept {
        ProcessEvent event = item.event;
        event.sampleOffset = static_cast<int32_t>(std::max<int64_t>(0, item.time - clock_));
        if (item.note < 0) {
            emit(ctx, event);
            return;
        }
        Note& note = notes_[static_cast<size_t>(item.note)];
        if (!note.used || note.generation != item.generation || note.ended) return;
        if (event.startsNote()) {
            // The same key still sounding (from this device) ends first.
            for (Note& other : notes_) {
                if (&other == &note || !other.used || !other.started || other.ended) continue;
                if (other.key != note.key || other.channel != note.channel) continue;
                emit(ctx, offFor(other, event.sampleOffset));
                other.ended = true;
                --sounding_;
                if (other.offIn) free(other);
            }
            if (!emit(ctx, event)) {
                note.ended = true;
                if (note.offIn) free(note);
                return;
            }
            note.started = true;
            ++sounding_;
            if (note.step >= 0) lastStep_ = note.step;
        } else if (event.endsNote()) {
            emit(ctx, event);
            note.ended = true;
            if (note.started) --sounding_;
            if (note.offIn) free(note);
        } else {
            if (note.started) emit(ctx, event);
        }
    }

    ProcessEvent offFor(const Note& note, int offset) const noexcept {
        ProcessEvent off = ProcessEvent::noteOff(offset, note.key);
        off.data[2] = note.channel;
        off.noteId = note.noteId;
        return off;
    }

    // --- Book-keeping --------------------------------------------------------------

    Note* freeNote() noexcept {
        for (Note& note : notes_) {
            if (!note.used) return &note;
        }
        // Full: one whose note-off went out, still waiting for its note-off to come in.
        for (Note& note : notes_) {
            if (note.ended) {
                free(note);
                return &note;
            }
        }
        return nullptr;
    }

    void free(Note& note) noexcept {
        const uint32_t generation = note.generation + 1;
        note = Note{};
        note.generation = generation;
    }

    int findNote(int32_t noteId) const noexcept {
        for (size_t i = 0; i < notes_.size(); ++i) {
            if (notes_[i].used && notes_[i].noteId == noteId) return static_cast<int>(i);
        }
        return -1;
    }

    // A note without an id: the oldest on its key and channel whose note-off hasn't come in.
    int findHeld(uint8_t key, uint8_t channel) const noexcept {
        int found = -1;
        for (size_t i = 0; i < notes_.size(); ++i) {
            const Note& note = notes_[i];
            if (!note.used || note.offIn || note.noteId >= 0 || note.key != key || note.channel != channel) continue;
            if (found < 0 || note.inOn < notes_[static_cast<size_t>(found)].inOn) found = static_cast<int>(i);
        }
        return found;
    }

    static void remember(std::array<int32_t, kSwallowed>& ids, int& count, int32_t id) noexcept {
        if (count == kSwallowed) {  // the oldest goes
            std::copy(ids.begin() + 1, ids.end(), ids.begin());
            --count;
        }
        ids[static_cast<size_t>(count++)] = id;
    }
    // Whether `id` was taken early (and forgets it: it comes again once).
    static bool swallow(std::array<int32_t, kSwallowed>& ids, int& count, int32_t id) noexcept {
        for (int i = 0; i < count; ++i) {
            if (ids[static_cast<size_t>(i)] != id) continue;
            std::copy(ids.begin() + i + 1, ids.begin() + count, ids.begin() + i);
            --count;
            return true;
        }
        return false;
    }

    // A binary heap on (time, order, serial): the earliest first.
    static bool later(const Queued& a, const Queued& b) noexcept {
        if (a.time != b.time) return a.time > b.time;
        if (a.order != b.order) return a.order > b.order;
        return a.serial > b.serial;
    }
    void enqueue(int64_t time, uint8_t order, int note, const ProcessEvent& event) noexcept {
        if (queued_ >= kMaxQueued) return;
        const uint32_t generation = note >= 0 ? notes_[static_cast<size_t>(note)].generation : 0;
        queue_[queued_++] = {std::max(time, clock_), order, serial_++, note, generation, event};
        std::push_heap(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(queued_), later);
    }
    Queued popQueued() noexcept {
        std::pop_heap(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(queued_), later);
        return queue_[--queued_];
    }

    // --- The groove, handed to the rendering thread ----------------------------------

    void takeGroove() noexcept {
        if (!pending_.load(std::memory_order_acquire)) return;
        if (active_ && !retired_.push(active_)) return;  // the main side hasn't freed the last ones: next block
        active_ = pending_.exchange(nullptr, std::memory_order_acq_rel);
    }
    void collectRetired() {
        Shared* old = nullptr;
        while (retired_.pop(old)) delete old;
    }

    static const std::vector<ParamInfo>& infos() {
        const std::vector<std::string>& kOnOff = offOnLabels();
        static const std::vector<ParamInfo> kInfos = [&kOnOff] {
            std::vector<std::string> steps;
            for (const groove::StepChoice& step : groove::kSteps) steps.emplace_back(step.name);
            return std::vector<ParamInfo>{
                {"swing", "Swing", "%", static_cast<float>(groove::kMinSwing), static_cast<float>(groove::kMaxSwing),
                 static_cast<float>(groove::kMinSwing)},
                {"step", "Step", "", 0.f, static_cast<float>(steps.size() - 1), static_cast<float>(groove::kDefaultStep),
                 false, steps},
                {"timing", "Timing", "%", 0.f, 200.f, 100.f},
                {"velocity", "Velocity", "%", -100.f, 100.f, 100.f},
                {"length", "Length", "%", 0.f, 100.f, 0.f},
                {"random", "Random", "%", 0.f, 100.f, 0.f},
                {"quantize", "Quantize", "%", 0.f, 100.f, 0.f},
                {"early", "Allow Early", "", 0.f, 1.f, 1.f, false, kOnOff},
                {"latency", "Latency", "", 0.f, 1.f, 0.f, false, kOnOff},
            };
        }();
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    // Main side.
    mutable std::mutex mutex_;
    StateValues others_;
    std::string grooveText_;
    std::atomic<Source> source_{Source::Swing};
    std::atomic<uint32_t> seed_{0};
    std::atomic<double> grooveStep_{0.25};   // its own groove's step
    std::atomic<double> projectStep_{0.25};  // the project's (as the rendering thread last saw it)
    std::atomic<double> lastTempo_{120.0};
    dsp::LatencyReport latencyReport_;
    std::atomic<Shared*> pending_{nullptr};
    SpscQueue<Shared*, 8> retired_;
    // Rendering thread.
    Shared* active_ = nullptr;
    groove::Groove swing_;
    float swingPercent_ = -1.f;
    int swingStep_ = -1;
    std::array<Note, kMaxNotes> notes_{};
    std::array<Queued, kMaxQueued> queue_{};
    size_t queued_ = 0;
    uint32_t serial_ = 0;
    std::array<int32_t, kSwallowed> swallowedOn_{}, swallowedOff_{};
    int numSwallowedOn_ = 0, numSwallowedOff_ = 0;
    int64_t clock_ = 0;  // samples rendered
    int sounding_ = 0;
    int lastStep_ = -1;
    int displayCount_ = 0;
    bool wasPlaying_ = false;
};

}  // namespace

SUB_REGISTER_BUILTIN(GrooveProcessor, MidiEffect);

}  // namespace sub
