// Words for sounds: what a search's words ask of a sound ("hard trap snare",
// "low chill kick", "long crash", "vocal chop", "acoustic kick"), and what a
// sound is called in a few words ("Snare · Punchy · Bright").
//
// Each word a search has is read as one of:
//
// - a kind or kinds ("kick", "hats", "cymbal": crash or ride, "drums", "piano":
//   keys, "vox": vocal): the sound is likely that (its three likeliest kinds
//   together at least kKindMatch);
// - a loop or a one-shot ("loop", "break"; "one-shot", "hit", "chop", "stab");
// - a note ("c2", "f#", "a#1"): its pitch, to the semitone (and the octave if
//   one is given);
// - a description: a blend of the tagger's attributes (graded among the
//   sound's own kind: "bright", "long", "low"...), and of the words it learned
//   from what people call sounds ("punchy", "dubstep", "lo-fi"...), each with
//   its weight; a sound is it when the blend is 0.5 or more;
// - a mood ("chill", "emo", "cinematic", "happy"...): a blend too, but one that
//   no data says a sound is or isn't (no labelled library has moods), so it
//   only orders the sounds, never leaves one out.
//
// A learned word is used as such only if, held out, it told the sounds people
// call it from the others (wordLearned()); otherwise its blend falls back to
// the attributes it stands for (in the table, SoundWords.cpp's vocabulary()).
// Words it doesn't know are left to the browser's own matching (names,
// folders): a sound matches a search when every word is in its name or folder,
// or is heard in it.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tagging/SoundTagger.h"

namespace sub::intelligence::tagging {

inline constexpr float kKindMatch = 0.5f;

// What a word asks of a sound.
struct Meaning {
    enum class Type : uint8_t { None, Kinds, Loop, OneShot, Note, Blend, Mood };
    struct Part {
        enum class Of : uint8_t { Attribute, Word } of = Of::Attribute;
        int index = 0;      // the Attribute, or the word
        bool up = true;     // false: the opposite ("dark" is Bright down)
        float weight = 1.f;
    };
    Type type = Type::None;
    uint32_t kinds = 0;     // Kinds: kindBit()s
    int note = -1;          // Note: 0..11, its pitch class
    int octave = -100;      // Note: its octave (C4 is middle C), -100 if none was given
    std::vector<Part> parts;  // Blend, Mood
    // How well a sound is it, 0..1 (Kinds: their probability; Loop: how likely it is one...).
    float degree(const SoundTags& tags) const;
    bool heardIn(const SoundTags& tags) const;
};

// A word as the tagger reads it (lower case; a plural, hyphens and the like
// are read as the word: "kicks", "hi-hat", "lo-fi"). None if it isn't one.
Meaning meaningOf(std::string_view word);

// A search's words (as the browser splits them: lower case), read once for all
// the sounds it is matched against.
class WordQuery {
public:
    explicit WordQuery(const std::vector<std::string>& terms);

    // Whether any of its words is one the tagger hears (if none, a search
    // needn't look at sounds at all).
    bool any() const { return heard_ != 0; }
    size_t size() const { return meanings_.size(); }
    const Meaning& meaning(size_t term) const { return meanings_[term]; }

    // Which of its words the sound is heard as (bit t for term t; moods are
    // heard in every sound), and how well it fits all those (0..1: the
    // geometric mean of their degrees).
    uint64_t hear(const SoundTags& tags, float& score) const;

private:
    std::vector<Meaning> meanings_;
    uint64_t heard_ = 0;  // the terms the tagger reads
};

// What a sound is called, in a few words, the most telling first: its kind
// (if it is sure of it), "Loop", its note (a tonal sound's, a tuned drum's),
// and up to `traits` of how it sounds and what people would call it ("Punchy",
// "Dark", "Acoustic", "Dubstep"), only those it is clearly.
std::vector<std::string> labelWords(const SoundTags& tags, int traits = 3);
// The same, joined: "Snare · Punchy · Bright".
std::string label(const SoundTags& tags, int traits = 3);
// A learned word as labels show it: "Punchy", "Lo-Fi", "DnB", "808".
std::string wordTitle(std::string_view word);

}  // namespace sub::intelligence::tagging
