// Vital: Matt Tytel's spectral warping wavetable synthesizer (GPLv3), its own
// synthesis engine vendored in engine/third_party/vital, as a built-in
// instrument. Its patch is its parameters (all of Vital's, by Vital's ids)
// and its state values (modulations, wavetables, LFO shapes, sample, tuning,
// names: VitalPatch.h).
//
// Vital's engine is built whole for a patch (VitalEngine.h) off the audio
// thread: on a state change (a preset, a modulation edited), or when the
// oversampling or the sample rate changes, a new engine is built and handed
// to the rendering thread, which swaps it in between blocks, fades the old one
// out over 10 ms, plays the notes still held on the new one, and leaves the
// old one for the main thread to free (idle()). Parameters go to the engine's
// controls between blocks, as Vital's host does.
//
// Notes: each note gets a MIDI channel of its own (1..15, in turn), as an MPE
// controller would give it, so its per-note pitch bend (a NoteBend, ±48
// semitones) bends that note alone: Vital's per-channel pitch wheel, its
// "local" pitch bend, is 48 semitones wide. MIDI messages apply to every
// channel: pitch bend (over Vital's Pitch Bend Range), the mod wheel, the
// sustain and sostenuto pedals, channel and polyphonic pressure, CC74 (MPE's
// slide), all notes and sounds off.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/VitalEngine.h"
#include "voice_handler.h"

namespace sub {
namespace {

using vitalpatch::Engine;

class VitalProcessor final : public BuiltinProcessor {
public:
    VitalProcessor() : BuiltinProcessor(vitalpatch::params()), pushed_(vitalpatch::params().size()) {}
    ~VitalProcessor() override {
        delete pending_.exchange(nullptr);
        delete retired_.exchange(nullptr);
    }

    std::string typeId() const override { return vitalpatch::kTypeId; }
    std::string name() const override { return "Vital"; }
    bool acceptsMidi() const override { return true; }
    // Its envelopes' releases and its effects' tails (a reverb up to tens of seconds) have no bound it
    // could name: as long as a long reverb's.
    int tailSamples() const override { return 10 * rate_.load(std::memory_order_relaxed); }
    std::string paramText(int index, float value) const override { return vitalpatch::paramText(index, value); }

    void prepare(double sampleRate, int /*maxBlockSize*/) override {
        const int rate = std::max(1, static_cast<int>(std::lround(sampleRate)));
        rate_.store(rate, std::memory_order_relaxed);
        // (Audio is stopped: the engine can be changed here.)
        if (Engine* pending = pending_.exchange(nullptr)) live_.reset(pending);
        delete retired_.exchange(nullptr);
        fading_.reset();
        limbo_.reset();
        if (!live_) {
            buildAndHandOver();
            live_.reset(pending_.exchange(nullptr));
        }
        if (live_ && live_->sampleRate != rate) {
            live_->sound->setSampleRate(rate);
            live_->sound->checkOversampling();
            live_->sound->updateAllModulationSwitches();
            live_->sampleRate = rate;
            builtRate_.store(rate, std::memory_order_relaxed);
        }
        std::fill(pushed_.begin(), pushed_.end(), std::numeric_limits<float>::quiet_NaN());
        clearNotes();
    }

    void reset() override {
        if (live_) live_->sound->allSoundsOff();
        if (fading_) retire(std::move(fading_));
        clearNotes();
        silentSamples_ = 0;
        sleeping_ = false;
    }

    // Main thread: frees the engine the rendering thread let go of, and builds a
    // new one when its oversampling or the sample rate isn't the one wanted.
    bool idle() override {
        delete retired_.exchange(nullptr);
        const int oversampling = choiceIndex(oversamplingIndex());
        if (builtOversampling_.load(std::memory_order_relaxed) != oversampling ||
            builtRate_.load(std::memory_order_relaxed) != rate_.load(std::memory_order_relaxed)) {
            buildAndHandOver();
        }
        return false;
    }

protected:
    StateValues stateValues() const override {
        std::lock_guard lock(stateMutex_);
        return state_;
    }

    void setStateValues(const StateValues& values) override {
        StateValues kept;
        for (const auto& [key, value] : values) {
            if (!value.empty() && knownKey(key)) kept.emplace(key, value);
        }
        std::lock_guard lock(stateMutex_);
        auto engine = vitalpatch::buildEngine(kept, currentValues(), rate_.load(std::memory_order_relaxed));  // throws
        state_ = std::move(kept);
        handOver(std::move(engine));
    }

    void render(const ProcessContext& ctx, float* const* ch, int numChannels, int numFrames) override {
        adopt();
        for (int c = 0; c < numChannels; ++c) std::fill(ch[c], ch[c] + numFrames, 0.f);
        if (!live_) return;
        vital::SoundEngine& sound = *live_->sound;
        pushParams(*live_);
        if (ctx.tempo > 0.0) sound.setBpm(static_cast<float>(ctx.tempo));
        // Where the song is, for synced LFOs and delays (as Vital's plug-in does: correctToTime per block).
        if (ctx.playing && ctx.tempo > 0.0) seconds_ = ctx.beatPos * 60.0 / ctx.tempo;

        const EventList& events = ctx.inEvents;
        if (events.count > 0) sleeping_ = false;
        if (sleeping_ && !fading_) {
            seconds_ += numFrames / static_cast<double>(live_->sampleRate);
            return;
        }
        size_t next = 0;
        int position = 0;
        while (position < numFrames) {
            // A stretch: up to Vital's largest block, ending where something that changes a note's sound,
            // or every note's (a bend, a controller), comes after its start. Notes go in at their offsets.
            int end = std::min(numFrames, position + vital::kMaxBufferSize);
            for (size_t i = next; i < events.count && events.events[i].sampleOffset < end; ++i) {
                if (!isNote(events.events[i]) && events.events[i].sampleOffset > position) {
                    end = events.events[i].sampleOffset;
                    break;
                }
            }
            for (; next < events.count && events.events[next].sampleOffset < end; ++next) {
                const ProcessEvent& event = events.events[next];
                const int offset = std::clamp(event.sampleOffset, position, end - 1);
                if (event.startsNote()) {
                    noteOn(sound, event, offset - position, bendAtStart(events, next));
                } else {
                    apply(sound, event, offset - position);
                }
            }
            processStretch(ch, numChannels, position, end - position);
            position = end;
        }
    }

private:
    static constexpr int kChannels = vital::kNumMidiChannels;
    static constexpr int kMaxHeld = 256;
    static constexpr int kFadeSamples = 480;  // the old engine's fade, when a new one takes over
    static constexpr float kSilence = 1e-6f;  // -120 dBFS: below it for a second with no voices, it sleeps

    struct Held {
        bool on = false;
        int32_t noteId = -1;
        uint8_t key = 0;
        uint8_t inChannel = 0;  // the event's channel
        uint8_t channel = 0;    // the channel it plays on in the engine
        float velocity = 0.f;
        uint32_t order = 0;
    };

    static bool isNote(const ProcessEvent& event) {
        return event.type == ProcessEvent::Type::NoteOn || event.type == ProcessEvent::Type::NoteOff;
    }

    // The bend a note starts with: one of its own at its note-on's sample (the
    // renderer sends a note's bends after its note-on), or none (0). Vital takes
    // a voice's bend as it starts it, so it is set first.
    static float bendAtStart(const EventList& events, size_t noteOn) {
        const ProcessEvent& on = events.events[noteOn];
        float bend = 0.f;
        for (size_t i = noteOn + 1; i < events.count && events.events[i].sampleOffset == on.sampleOffset; ++i) {
            const ProcessEvent& e = events.events[i];
            if (e.type != ProcessEvent::Type::NoteBend) continue;
            if (on.noteId >= 0 ? e.noteId == on.noteId : e.noteId < 0 && e.key() == on.key()) bend = e.bend;
        }
        return bend;
    }

    static bool knownKey(const std::string& key) {
        using namespace vitalpatch;
        if (key == kModulations || key == kWavetables || key == kLfos || key == kSample || key == kTuning ||
            key == kPresetName || key == kAuthor || key == kComments || key == kStyle) {
            return true;
        }
        for (int i = 0; i < kNumMacros; ++i) {
            if (key == macroKey(i)) return true;
        }
        return false;
    }

    static int oversamplingIndex() {
        static const int kIndex = vitalpatch::paramIndex("oversampling");
        return kIndex;
    }

    std::vector<float> currentValues() const {
        std::vector<float> values(params().size());
        for (size_t i = 0; i < values.size(); ++i) values[i] = param(static_cast<int>(i));
        return values;
    }

    // Builds an engine for the state as it is and the parameters as they are, and hands it over.
    void buildAndHandOver() {
        std::lock_guard lock(stateMutex_);
        std::unique_ptr<Engine> engine;
        try {
            engine = vitalpatch::buildEngine(state_, currentValues(), rate_.load(std::memory_order_relaxed));
        } catch (const std::exception&) {
            // (The state was read when it was set: only an engine of the init patch is left.)
            engine = vitalpatch::buildEngine({}, currentValues(), rate_.load(std::memory_order_relaxed));
        }
        handOver(std::move(engine));
    }

    // (With stateMutex_ held, so the newest build is the one handed over.)
    void handOver(std::unique_ptr<Engine> engine) {
        builtRate_.store(engine->sampleRate, std::memory_order_relaxed);
        builtOversampling_.store(engine->oversampling, std::memory_order_relaxed);
        delete pending_.exchange(engine.release());  // (one not taken yet was never played)
    }

    // Rendering thread: a new engine takes over, once the last one let go of has
    // been freed and the one before has faded out.
    void adopt() {
        if (limbo_) retire(std::move(limbo_));
        if (fading_ || pending_.load(std::memory_order_acquire) == nullptr ||
            retired_.load(std::memory_order_acquire) != nullptr) {
            return;
        }
        std::unique_ptr<Engine> engine(pending_.exchange(nullptr, std::memory_order_acq_rel));
        if (!engine) return;
        fading_ = std::move(live_);
        fadeLeft_ = kFadeSamples;
        live_ = std::move(engine);
        std::fill(pushed_.begin(), pushed_.end(), std::numeric_limits<float>::quiet_NaN());
        // What is held plays on.
        vital::SoundEngine& sound = *live_->sound;
        if (sustain_) sound.sustainOnRange(0, kChannels - 1);
        if (sostenuto_) sound.sostenutoOnRange(0, kChannels - 1);
        for (int c = 0; c < kChannels; ++c) sound.setPitchWheel(bend_[c], c);
        for (const Held& held : held_) {
            if (held.on) sound.noteOn(held.key, held.velocity, 0, held.channel);
        }
        sleeping_ = false;
        silentSamples_ = 0;
    }

    // Rendering thread: hands an engine to the main thread to free. (The slot is
    // free whenever one is let go of: a new engine is taken only with it free,
    // and lets one go once. Were it not, the engine waits in limbo_.)
    void retire(std::unique_ptr<Engine> engine) {
        if (!engine) return;
        Engine* expected = nullptr;
        if (retired_.compare_exchange_strong(expected, engine.get(), std::memory_order_acq_rel)) {
            engine.release();
        } else {
            limbo_ = std::move(engine);
        }
    }

    void pushParams(Engine& engine) {
        static const int kModWheel = vitalpatch::paramIndex("mod_wheel");
        static const int kPitchWheel = vitalpatch::paramIndex("pitch_wheel");
        for (size_t i = 0; i < pushed_.size(); ++i) {
            const float value = param(static_cast<int>(i));
            if (value == pushed_[i]) continue;
            pushed_[i] = value;
            if (vital::Value* control = engine.controls[i]) control->set(value);
            if (static_cast<int>(i) == kModWheel) engine.sound->setModWheelAllChannels(value);
            if (static_cast<int>(i) == kPitchWheel) engine.sound->setZonedPitchWheel(value, 0, kChannels - 1);
        }
    }

    void processStretch(float* const* ch, int numChannels, int start, int frames) {
        if (frames <= 0) return;
        const double rate = static_cast<double>(live_->sampleRate);
        live_->sound->correctToTime(seconds_);
        live_->sound->process(frames);
        const auto* out = reinterpret_cast<const float*>(live_->sound->output(0)->buffer);
        constexpr int kStride = vital::poly_float::kSize;
        float peak = 0.f;
        for (int i = 0; i < frames; ++i) {
            const float left = out[kStride * i];
            const float right = out[kStride * i + 1];
            ch[0][start + i] = left;
            if (numChannels > 1) ch[1][start + i] = right;
            peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
        }
        if (fading_) {
            fading_->sound->correctToTime(seconds_);
            fading_->sound->process(frames);
            const auto* old = reinterpret_cast<const float*>(fading_->sound->output(0)->buffer);
            for (int i = 0; i < frames && fadeLeft_ > 0; ++i, --fadeLeft_) {
                const float gain = static_cast<float>(fadeLeft_) / kFadeSamples;
                ch[0][start + i] += gain * old[kStride * i];
                if (numChannels > 1) ch[1][start + i] += gain * old[kStride * i + 1];
            }
            if (fadeLeft_ <= 0) retire(std::move(fading_));
        }
        seconds_ += frames / rate;
        // Asleep after a second of silence with nothing playing; a note wakes it.
        if (peak < kSilence && live_->sound->getNumActiveVoices() == 0) {
            silentSamples_ += frames;
            if (silentSamples_ >= live_->sampleRate) sleeping_ = true;
        } else {
            silentSamples_ = 0;
        }
    }

    // --- Notes and MIDI --------------------------------------------------------------------

    void clearNotes() {
        for (Held& held : held_) held = Held{};
        for (float& bend : bend_) bend = 0.f;
        sustain_ = sostenuto_ = false;
        nextChannel_ = 1;
        order_ = 0;
    }

    Held* find(const ProcessEvent& event) {
        Held* found = nullptr;
        for (Held& held : held_) {
            if (!held.on) continue;
            const bool same = event.noteId >= 0 ? held.noteId == event.noteId
                                                : held.key == event.key() && held.inChannel == event.channel();
            // (The oldest of a key held twice ends first.)
            if (same && (found == nullptr || held.order < found->order)) found = &held;
        }
        return found;
    }

    Held* newestOnKey(uint8_t key) {
        Held* found = nullptr;
        for (Held& held : held_) {
            if (held.on && held.key == key && (found == nullptr || held.order > found->order)) found = &held;
        }
        return found;
    }

    void apply(vital::SoundEngine& sound, const ProcessEvent& event, int offset) {
        switch (event.type) {
        case ProcessEvent::Type::NoteOn:
        case ProcessEvent::Type::NoteOff:
            if (event.startsNote()) {
                noteOn(sound, event, offset, 0.f);
            } else if (Held* held = find(event)) {
                sound.noteOff(held->key, 0.f, offset, held->channel);
                held->on = false;
            }
            break;
        case ProcessEvent::Type::NoteBend:
            if (Held* held = event.noteId >= 0 ? find(event) : newestOnKey(event.key())) {
                const float bend = std::clamp(event.bend / vital::VoiceHandler::kLocalPitchBendRange, -1.f, 1.f);
                bend_[held->channel] = bend;
                sound.setPitchWheel(bend, held->channel);
            }
            break;
        case ProcessEvent::Type::Midi: midi(sound, event, offset); break;
        }
    }

    void noteOn(vital::SoundEngine& sound, const ProcessEvent& event, int offset, float bendSemitones) {
        Held* slot = nullptr;
        for (Held& held : held_) {
            if (!held.on) {
                slot = &held;
                break;
            }
        }
        if (slot == nullptr) {  // (all taken: the oldest gives way)
            slot = &held_[0];
            for (Held& held : held_) {
                if (held.order < slot->order) slot = &held;
            }
            sound.noteOff(slot->key, 0.f, offset, slot->channel);
        }
        const uint8_t channel = static_cast<uint8_t>(nextChannel_);
        nextChannel_ = nextChannel_ % (kChannels - 1) + 1;
        *slot = Held{true, event.noteId, event.key(), event.channel(), channel, event.velocity() / 127.f, ++order_};
        bend_[channel] = std::clamp(bendSemitones / vital::VoiceHandler::kLocalPitchBendRange, -1.f, 1.f);
        sound.setPitchWheel(bend_[channel], channel);
        sound.noteOn(slot->key, slot->velocity, offset, channel);
    }

    void midi(vital::SoundEngine& sound, const ProcessEvent& event, int offset) {
        const uint8_t status = event.data[0] & 0xF0;
        const uint8_t a = event.data[1] & 0x7F;
        const uint8_t b = event.data[2] & 0x7F;
        constexpr int kLast = kChannels - 1;
        switch (status) {
        case 0xB0:
            switch (a) {
            case 1: sound.setModWheelAllChannels(b / 127.f); break;
            case 64:
                sustain_ = b >= 64;
                if (sustain_) sound.sustainOnRange(0, kLast);
                else sound.sustainOffRange(offset, 0, kLast);
                break;
            case 66:
                sostenuto_ = b >= 64;
                if (sostenuto_) sound.sostenutoOnRange(0, kLast);
                else sound.sostenutoOffRange(offset, 0, kLast);
                break;
            case 74: sound.setChannelRangeSlide(0, kLast, b / 127.f, offset); break;
            case 120:
                sound.allSoundsOff();
                for (Held& held : held_) held.on = false;
                break;
            case 123:
                sound.allNotesOffRange(offset, 0, kLast);
                for (Held& held : held_) held.on = false;
                break;
            default: break;
            }
            break;
        case 0xE0: {
            const int value = (b << 7) | a;
            sound.setZonedPitchWheel(std::clamp((value - 8192) / 8191.f, -1.f, 1.f), 0, kLast);
            break;
        }
        case 0xD0: sound.setChannelRangeAftertouch(0, kLast, a / 127.f, offset); break;
        case 0xA0:
            if (Held* held = newestOnKey(a)) sound.setAftertouch(a, b / 127.f, offset, held->channel);
            break;
        default: break;
        }
    }

    // The patch (main and loading threads).
    mutable std::mutex stateMutex_;
    StateValues state_;

    // Engines: the rendering thread owns live_ and fading_; pending_ is handed to
    // it, retired_ back to the main thread.
    std::unique_ptr<Engine> live_;
    std::unique_ptr<Engine> fading_;  // the one before, fading out
    std::unique_ptr<Engine> limbo_;   // let go of, waiting for the main thread to take it
    int fadeLeft_ = 0;
    std::atomic<Engine*> pending_{nullptr};
    std::atomic<Engine*> retired_{nullptr};
    std::atomic<int> rate_{48000};
    std::atomic<int> builtRate_{0};          // the newest engine's (built, perhaps not yet playing)
    std::atomic<int> builtOversampling_{-1};

    // Rendering thread.
    std::vector<float> pushed_;  // what each control was last set to (NaN: set it)
    Held held_[kMaxHeld];
    float bend_[kChannels] = {};
    bool sustain_ = false;
    bool sostenuto_ = false;
    int nextChannel_ = 1;
    uint32_t order_ = 0;
    double seconds_ = 0.0;
    int silentSamples_ = 0;
    bool sleeping_ = false;
};

}  // namespace
SUB_REGISTER_BUILTIN(VitalProcessor, Instrument);
}  // namespace sub
