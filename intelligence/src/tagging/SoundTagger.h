// The sound tagger: what a sound is ("snare", "bass", "vocal"...), whether it
// is a loop, how it sounds ("bright", "hard", "long", "low", "distorted",
// "acoustic"...) and what people would call it ("punchy", "dubstep", "lo-fi",
// "808"...), from the fingerprint the similarity makes anyway
// (EssentiaExtractor's descriptors): no decoding or analysis of its own.
//
// Two parts, both pure functions:
//
// - The model (infer): an ensemble of small MLPs trained on labelled sample
//   libraries (tools/sound_tagger: 70 drum machines, 10 000 Freesound
//   percussion one-shots and their uploaders' tags, NSynth's and TinySOL's
//   notes whole and cut short, vocal chops of LibriSpeech and VocalSet, loops
//   made from them), compiled in (TaggerModel.inc). Its outputs are kept with
//   the fingerprint (EssentiaExtractor appends them): the kinds' probabilities
//   (temperature-scaled), whether it is a loop, acoustic, electric or
//   synthesized, NSynth's qualities, the words' scores, and an embedding (the
//   similarity's Embedding aspect). About 20 microseconds a sound.
// - Reading them (describe): the kind, if the model is sure enough of it (its
//   threshold held 90% of held-out predictions right); attributes graded
//   0..1, each *relative to the sound's kind*: a snare is bright among snares,
//   a kick long among kicks (each kind's 20th and 80th percentiles of the
//   descriptor in the training libraries are 0.1 and 0.9); and which words it
//   is heard as, again among its own kind (a word's score above its 60th
//   percentile for the kind). So "bright kick" and "bright hat" each find the
//   brighter of their own kind, and "hard snare" the harder snares.
//
// What words find which sounds is SoundWords.h.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace sub::intelligence::tagging {

enum class Kind : int8_t {
    None = -1,
    Kick, Snare, Clap, ClosedHat, OpenHat, Crash, Ride, Tom, Rim, Cowbell, Shaker, Tambourine, Percussion,
    Bass, Keys, Organ, Guitar, Mallet, Strings, Brass, Woodwind, Vocal, Synth,
};
inline constexpr int kKinds = 23;
inline constexpr int kDrumKinds = 13;  // Kick .. Percussion

inline constexpr uint32_t kindBit(Kind kind) { return kind == Kind::None ? 0u : 1u << static_cast<int>(kind); }
inline constexpr uint32_t kDrumMask = (1u << kDrumKinds) - 1;

const char* kindName(Kind kind);   // "kick", "closed hat"...: as the model's classes are named
std::string kindTitle(Kind kind);  // "Kick", "Closed Hat"
inline bool isDrum(Kind kind) { return kind != Kind::None && static_cast<int>(kind) < kDrumKinds; }

// How a sound sounds, each 0..1 (0.5: as most of its kind).
enum class Attribute : uint8_t {
    Bright,     // brighter than most of its kind (its spectral centroid); 0: darker
    Hard,       // starts faster (log attack time); 0: softer
    Long,       // rings longer (effective duration); 0: shorter
    Low,        // lower: more sub-bass for drums, a lower note for tonal sounds; 0: higher
    Distorted,  // NSynth's "distortion"
    Wet,        // NSynth's "reverb"
    Acoustic,   // played on an acoustic instrument (or kit)
    Electric,   // an electric one (an electric piano, guitar)
    Synthetic,  // synthesized (a synth, a drum machine)
    Pitched,    // has a clear pitch (YIN's confidence)
};
inline constexpr int kAttributes = 10;

// The words the model learned from what people call sounds (TaggerModel.inc's
// kWordNames: "punchy", "hard", ..., "dubstep", "808"), and how many.
int wordCount();
const char* wordName(int word);
int wordIndex(std::string_view name);  // -1 if not one
bool wordLearned(int word);           // held out, it told the sounds called it from the others (AUC >= 0.7)
inline constexpr int kMaxWords = 64;

// The model's outputs, as the fingerprint keeps them after the descriptors and
// the embedding.
namespace output {
enum : uint32_t {
    Kinds = 0,                  // kKinds probabilities
    Loop = Kinds + kKinds,      // 0..1
    Sources = Loop + 1,         // 3 probabilities: acoustic, electric (NSynth's "electronic"), synthetic
    Qualities = Sources + 3,    // NSynth's 10, each 0..1 (bright, dark, distortion, fast_decay, long_release,
                                // multiphonic, nonlinear_env, percussive, reverb, tempo-synced)
    Words = Qualities + 10,     // wordCount() scores, each 0..1
};
}  // namespace output
int outputCount();     // output::Words + wordCount()
int embeddingDims();
// What identifies the model compiled in (a hash of its weights): part of the
// fingerprints' schema, so a library is analysed again with a new model.
const char* modelId();

// Runs the model on a fingerprint's descriptors (EssentiaExtractor's kDims):
// writes embeddingDims() floats to `embedding` and outputCount() to `outputs`.
void infer(const float* descriptors, float* embedding, float* outputs);

// A sound as the tagger hears it. Small and plain (about 80 bytes): the
// browser keeps one a file. Values 0..1 are kept as bytes (0..255).
struct SoundTags {
    struct Guess {
        Kind kind = Kind::None;
        uint8_t probability = 0;
    };
    std::array<Guess, 3> kinds{};  // the three likeliest kinds, the likeliest first
    bool sure = false;             // kinds[0] is what it is called (its probability >= kindThreshold())
    uint8_t loop = 0;              // how likely it is a loop
    int8_t row = kKinds;           // the kind it is graded among (kKinds: any kind's)
    std::array<uint8_t, kAttributes> attributes{};
    std::array<uint8_t, kMaxWords> words{};  // each word's score
    uint64_t heard = 0;                      // the words it is called (a bit a word)
    float pitch = std::numeric_limits<float>::quiet_NaN();  // MIDI note (69 = A4), NaN when it has no clear one

    static float unit(uint8_t v) { return static_cast<float>(v) * (1.f / 255.f); }
    Kind kind() const { return sure ? kinds[0].kind : Kind::None; }
    float loopProbability() const { return unit(loop); }
    bool isLoop() const { return loop >= 128; }
    float operator[](Attribute a) const { return unit(attributes[static_cast<size_t>(a)]); }
    float word(int w) const { return unit(words[static_cast<size_t>(w)]); }
    bool calls(int w) const { return (heard >> w) & 1u; }
    // How likely it is any of these kinds (a mask of kindBit()s), as far as its three likeliest say.
    float probabilityOf(uint32_t kinds) const;
};

// Reads a whole fingerprint (descriptors, embedding, outputs: EssentiaExtractor's layout).
SoundTags describe(const float* fingerprint);
// The same, from the descriptors and the model's outputs apart.
SoundTags describe(const float* descriptors, const float* outputs);

// A word's score as a degree 0..1 for a sound of its row: 0.5 at its cut (where
// the sound starts being called it), 1 at a score of 1.
float wordDegree(const SoundTags& tags, int word);

// The kind's probability below which a sound isn't called anything (TaggerModel.inc).
float kindThreshold();

// "C#2" for MIDI 37.
std::string noteName(float midi);

}  // namespace sub::intelligence::tagging
