// Built-in "Arpeggiator" device, a MIDI effect (Processor::isMidiEffect()): it
// plays the notes held on its track one after another, in a pattern, at a
// rate, over a number of octaves.
//
// The notes it plays come out instead of the notes held: the renderer runs it
// before its chain's devices (wherever it sits) and they hear its notes. Raw
// MIDI (controllers, pitch bend) passes through; per-note bends are dropped
// (each step is a note of its own).
//
// Timing: steps of Rate (synced: a note value at the tempo; free: Hz). Synced
// while the song plays, the steps are on the song's grid (step k at k note
// values from the start), so it locks to the beat wherever playback starts or
// loops; otherwise (stopped, or free in Hz) its clock starts with the first
// note held. Swing puts every odd step late, by up to half a step. The first
// note held plays at the next step (at once if it is on the step, or no more
// than kLate of a step after it). A step's note lasts Gate of a step (past 100%
// it overlaps the next); a step finding the same key still sounding ends it first.
//
// Hold keeps the notes after they are let go, until a note comes with none
// held: then those notes replace them.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/DspBlocks.h"

namespace sub {
namespace {

class ArpeggiatorProcessor final : public BuiltinProcessor {
public:
    enum Param { Style = 0, Sync, Rate, Synced, Gate, Octaves, Swing, VelocityMode, FixedVelocity, Hold, NumParams };
    enum StyleKind { Up = 0, Down, UpDown, DownUp, Converge, Diverge, Played, Random, Chord };
    enum Velocity { AsPlayed = 0, Fixed };

    ArpeggiatorProcessor() : BuiltinProcessor(infos()) {}

    std::string typeId() const override { return "builtin:arpeggiator"; }
    std::string name() const override { return "Arpeggiator"; }
    bool isMidiEffect() const override { return true; }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        reset();
    }

    void reset() override {
        numHeld_ = 0;
        numSounding_ = 0;
        orderCount_ = 0;
        clock_ = 0;
        freeSteps_ = 0.0;
        lastSteps_ = 0.0;
        onGrid_ = false;
        running_ = false;
        nextStep_ = 0;
        patternStep_ = 0;
    }

    void endMidiNotes(EventOutput& out) noexcept override {
        for (int s = 0; s < numSounding_; ++s) out.add(offFor(sounding_[static_cast<size_t>(s)], 0));
        numSounding_ = 0;
        numHeld_ = 0;  // (the note-offs it misses while off would leave them held)
        running_ = false;
    }

    // Beats a synced step lasts, by Synced's index.
    static double syncedBeats(int index) noexcept {
        static constexpr double kBeats[] = {4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0,  // 1/1 .. 1/4T
                                            0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};  // 1/8 .. 1/32
        return kBeats[std::clamp(index, 0, static_cast<int>(std::size(kBeats)) - 1)];
    }

protected:
    void render(const ProcessContext& ctx, float* const*, int, int numFrames) override {
        const bool sync = isOn(Sync);
        const double samplesPerBeat = ctx.samplesPerBeat();
        const double stepBeats = syncedBeats(choiceIndex(Synced));
        // Samples a step lasts (without a tempo, as at 120 BPM).
        const double beatSamples = samplesPerBeat > 0.0 ? samplesPerBeat : sampleRate_ / 2.0;
        const double stepSamples =
            sync ? std::max(1.0, stepBeats * beatSamples) : sampleRate_ / std::max(0.01f, param(Rate));
        const bool grid = sync && ctx.playing && samplesPerBeat > 0.0;
        const double swing = kMaxSwing * param(Swing) / 100.0;
        if (!isOn(Hold)) dropLetGo();

        size_t event = 0;
        for (int i = 0; i < numFrames; ++i) {
            // Where the step clock is: the song's grid, or its own.
            const double steps = grid ? (ctx.beatPos + i / samplesPerBeat) / stepBeats : freeSteps_;
            if (grid != onGrid_ || (grid && (steps < lastSteps_ - 1e-6 || steps > lastSteps_ + 1.0))) {
                // From its own clock to the song's (or back), or the song jumped (a loop): the next step from here.
                onGrid_ = grid;
                if (!grid) freeSteps_ = 0.0;
                align(grid ? steps : 0.0);
            }
            lastSteps_ = steps;

            for (; event < ctx.inEvents.count && ctx.inEvents.events[event].sampleOffset <= i; ++event) {
                handle(ctx, ctx.inEvents.events[event], i, grid ? steps : 0.0);
            }
            endNotesDue(ctx, i);
            if (running_ && numHeld_ > 0) {
                // A step whose time has come (each odd one late by the swing).
                const double now = grid ? steps : freeSteps_;
                for (int played = 0; now >= stepTime(nextStep_, swing) - 1e-9; ++played) {
                    if (played == kMaxStepsAtOnce) {  // far behind (can't be, but): on from here
                        align(now);
                        break;
                    }
                    playStep(ctx, i, stepSamples);
                    ++nextStep_;
                }
            } else if (numHeld_ == 0) {
                running_ = false;
            }
            if (!grid) freeSteps_ += 1.0 / stepSamples;
            ++clock_;
        }
        // Anything left (an offset past the stretch) passes on at its end.
        for (; event < ctx.inEvents.count; ++event) {
            if (ctx.inEvents.events[event].type == ProcessEvent::Type::Midi) {
                ProcessEvent pass = ctx.inEvents.events[event];
                pass.sampleOffset = std::max(0, numFrames - 1);
                emit(ctx, pass);
            }
        }
    }

private:
    static constexpr int kMaxHeld = 32;
    static constexpr int kMaxSounding = 64;
    static constexpr int kMaxOctaves = 4;
    static constexpr double kMaxSwing = 0.5;  // full swing: odd steps half a step late
    static constexpr double kLate = 0.1;      // of a step: a note this late still plays the step it missed
    static constexpr int kMaxStepsAtOnce = 2;  // steps one sample plays (two, where the swing brings them together)

    struct Held {
        uint8_t key, velocity, channel;
        bool down;      // still held (with Hold, notes stay after they are let go)
        uint32_t order; // when it came, for Played
    };
    struct Sounding {
        uint8_t key, channel;
        int32_t noteId;
        int64_t endAt;  // on the clock
    };

    static double stepTime(int64_t step, double swing) noexcept {
        return static_cast<double>(step) + ((step & 1) != 0 ? swing : 0.0);
    }

    // The next step from `steps`: this one if it is no more than kLate past it.
    void align(double steps) noexcept {
        const double floor = std::floor(steps + 1e-9);
        nextStep_ = static_cast<int64_t>(steps - floor <= kLate ? floor : floor + 1.0);
    }

    void handle(const ProcessContext& ctx, const ProcessEvent& in, int offset, double steps) noexcept {
        if (in.type == ProcessEvent::Type::Midi) {  // controllers, pitch bend: as they are
            ProcessEvent pass = in;
            pass.sampleOffset = offset;
            emit(ctx, pass);
            return;
        }
        if (in.type == ProcessEvent::Type::NoteBend) return;
        if (in.endsNote()) {
            for (int h = 0; h < numHeld_; ++h) {
                Held& note = held_[static_cast<size_t>(h)];
                if (note.key == in.key() && note.channel == in.channel()) note.down = false;
            }
            if (!isOn(Hold)) dropLetGo();
            return;
        }
        if (!in.startsNote()) return;
        const bool anyDown = std::any_of(held_.begin(), held_.begin() + numHeld_, [](const Held& h) { return h.down; });
        if (!anyDown) {  // a new chord (with Hold, in place of the last): the pattern from its start
            numHeld_ = 0;
            start(steps);
        }
        for (int h = 0; h < numHeld_; ++h) {
            Held& note = held_[static_cast<size_t>(h)];
            if (note.key == in.key() && note.channel == in.channel()) {
                note.velocity = in.velocity();
                note.down = true;
                return;
            }
        }
        if (numHeld_ < kMaxHeld) {
            held_[static_cast<size_t>(numHeld_++)] = {in.key(), in.velocity(), in.channel(), true, orderCount_++};
        }
    }

    // The pattern from its start, at the next step.
    void start(double steps) noexcept {
        running_ = true;
        patternStep_ = 0;
        if (!onGrid_) freeSteps_ = 0.0;
        align(onGrid_ ? steps : 0.0);
    }

    void dropLetGo() noexcept {
        int kept = 0;
        for (int h = 0; h < numHeld_; ++h) {
            if (held_[static_cast<size_t>(h)].down) held_[static_cast<size_t>(kept++)] = held_[static_cast<size_t>(h)];
        }
        numHeld_ = kept;
    }

    ProcessEvent offFor(const Sounding& note, int offset) const noexcept {
        ProcessEvent off = ProcessEvent::noteOff(offset, note.key);
        off.data[2] = note.channel;
        off.noteId = note.noteId;
        return off;
    }

    void endNotesDue(const ProcessContext& ctx, int offset) noexcept {
        int kept = 0;
        for (int s = 0; s < numSounding_; ++s) {
            const Sounding& note = sounding_[static_cast<size_t>(s)];
            if (note.endAt <= clock_) {
                emit(ctx, offFor(note, offset));
            } else {
                sounding_[static_cast<size_t>(kept++)] = note;
            }
        }
        numSounding_ = kept;
    }

    // The notes to arpeggiate, in the order the style walks (Played: as they came; the others: low to high),
    // over the octaves.
    int pattern(std::array<Held, kMaxHeld * kMaxOctaves>& out, bool played) const noexcept {
        std::array<Held, kMaxHeld> notes{};
        std::copy(held_.begin(), held_.begin() + numHeld_, notes.begin());
        std::sort(notes.begin(), notes.begin() + numHeld_, [played](const Held& a, const Held& b) {
            return played ? a.order < b.order : a.key < b.key;
        });
        const int octaves = std::clamp(choiceIndex(Octaves), 1, kMaxOctaves);
        int count = 0;
        for (int o = 0; o < octaves; ++o) {
            for (int n = 0; n < numHeld_; ++n) {
                Held note = notes[static_cast<size_t>(n)];
                const int key = note.key + 12 * o;
                if (key > 127) continue;
                note.key = static_cast<uint8_t>(key);
                out[static_cast<size_t>(count++)] = note;
            }
        }
        return count;
    }

    // Which of the pattern's `count` notes step `k` plays.
    int indexFor(StyleKind style, int64_t k, int count) const noexcept {
        if (count <= 1) return 0;
        const auto n = static_cast<int64_t>(count);
        switch (style) {
            case Down: return static_cast<int>(n - 1 - k % n);
            case UpDown:
            case DownUp: {
                const int64_t period = 2 * n - 2;
                const int64_t at = k % period;
                const int64_t up = at < n ? at : period - at;
                return static_cast<int>(style == UpDown ? up : n - 1 - up);
            }
            case Converge:
            case Diverge: {
                const int64_t at = k % n;
                // Converge: lowest, highest, second lowest, second highest...; Diverge: the same from the middle out.
                const int64_t from = style == Converge ? at : n - 1 - at;
                return static_cast<int>(from % 2 == 0 ? from / 2 : n - 1 - from / 2);
            }
            case Random:
                return static_cast<int>(dsp::hash32(static_cast<uint32_t>(k) * 2654435761U + 12345U) %
                                        static_cast<uint32_t>(n));
            case Up:
            case Played:
            case Chord:
            default: return static_cast<int>(k % n);
        }
    }

    void playStep(const ProcessContext& ctx, int offset, double stepSamples) noexcept {
        const auto style = choice<StyleKind>(Style);
        std::array<Held, kMaxHeld * kMaxOctaves> notes{};
        const int count = pattern(notes, style == Played);
        if (count == 0) return;
        const auto length = static_cast<int64_t>(std::max(1.0, std::round(stepSamples * param(Gate) / 100.0)));
        const auto play = [&](const Held& note) {
            // The same key still sounding ends first.
            int kept = 0;
            for (int s = 0; s < numSounding_; ++s) {
                const Sounding& on = sounding_[static_cast<size_t>(s)];
                if (on.key == note.key && on.channel == note.channel) {
                    emit(ctx, offFor(on, offset));
                } else {
                    sounding_[static_cast<size_t>(kept++)] = on;
                }
            }
            numSounding_ = kept;
            if (numSounding_ >= kMaxSounding) return;
            const uint8_t velocity = choice<Velocity>(VelocityMode) == Fixed
                                         ? static_cast<uint8_t>(std::clamp(choiceIndex(FixedVelocity), 1, 127))
                                         : note.velocity;
            ProcessEvent on = ProcessEvent::noteOn(offset, note.key, velocity);
            on.data[2] = note.channel;
            on.noteId = nextId_;
            nextId_ = nextId_ >= 0x3fffffff ? 1 : nextId_ + 1;
            if (!emit(ctx, on)) return;
            sounding_[static_cast<size_t>(numSounding_++)] = {note.key, note.channel, on.noteId, clock_ + length};
        };
        if (style == Chord) {  // every note held at once, an octave further each step
            const int octaves = std::max(1, count / std::max(1, numHeld_));
            const int octave = static_cast<int>(patternStep_ % octaves);
            for (int n = 0; n < numHeld_ && octave * numHeld_ + n < count; ++n) {
                play(notes[static_cast<size_t>(octave * numHeld_ + n)]);
            }
        } else {
            play(notes[static_cast<size_t>(indexFor(style, patternStep_, count))]);
        }
        ++patternStep_;
    }

    static const std::vector<ParamInfo>& infos() {
        const std::vector<std::string>& kOnOff = offOnLabels();
        static const std::vector<ParamInfo> kInfos = {
            {"style", "Style", "", 0.f, 8.f, 0.f, false,
             {"Up", "Down", "Up-Down", "Down-Up", "Converge", "Diverge", "Played", "Random", "Chord"}},
            {"sync", "Sync", "", 0.f, 1.f, 1.f, false, kOnOff},
            {"rate", "Rate", "Hz", 0.5f, 40.f, 8.f, true},
            {"synced", "Rate (Synced)", "", 0.f, 9.f, 7.f, false,
             {"1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"}},
            {"gate", "Gate", "%", 5.f, 200.f, 50.f},
            {"octaves", "Octaves", "", 1.f, 4.f, 1.f, false, {}, 3},
            {"swing", "Swing", "%", 0.f, 100.f, 0.f},
            {"velocity", "Velocity", "", 0.f, 1.f, 0.f, false, {"Played", "Fixed"}},
            {"fixed_velocity", "Fixed Velocity", "", 1.f, 127.f, 100.f, false, {}, 126},
            {"hold", "Hold", "", 0.f, 1.f, 0.f, false, kOnOff},
        };
        return kInfos;
    }

    double sampleRate_ = 48000.0;
    std::array<Held, kMaxHeld> held_{};
    int numHeld_ = 0;
    uint32_t orderCount_ = 0;
    std::array<Sounding, kMaxSounding> sounding_{};
    int numSounding_ = 0;
    int32_t nextId_ = 1;
    int64_t clock_ = 0;       // samples rendered (for the notes' ends)
    double freeSteps_ = 0.0;  // its own clock, in steps from when it started
    double lastSteps_ = 0.0;  // where the clock was a sample ago
    bool onGrid_ = false;     // on the song's grid (synced, playing) rather than its own clock
    bool running_ = false;    // a pattern is going (notes held since it started)
    int64_t nextStep_ = 0;    // the step to play next, on the clock it is on
    int64_t patternStep_ = 0; // steps played since the pattern started
};

}  // namespace

SUB_REGISTER_BUILTIN(ArpeggiatorProcessor, MidiEffect);

}  // namespace sub
