// Progression ideas: chord progressions to try in a song, in a mood (sad,
// dark, nostalgic), a chord a bar, played round and round. Rules and counts,
// not a model, compiled in (ProgressionData.inc, made by
// tools/progressions/build_tables.py):
//
// - The mood's own progressions: free-midi-chords' progressions tagged with it
//   (80 for the three moods), in the song's key. One in a major key goes into
//   a minor key's relative major, and one in a minor key into a major key's
//   relative minor: the same notes, so they fit the song.
// - Progressions drawn from how chords follow each other: each next chord
//   picked by how often it followed the one before in the McGill Billboard
//   annotations (in songs of the key's mode: major or minor) and how often it
//   does in the mood's own progressions, the two multiplied (the square root
//   of each): both must agree, so a chord the mood never plays there comes up
//   rarely, and the draws move as the mood's progressions do, in the ways real
//   songs do. Chords are where they are in the key (semitones
//   above the tonic) and their kind (major, minor, diminished, augmented, sus2,
//   sus4): 72 of them, of which those Billboard plays at least 1 in 400 times
//   in the key's mode, or the mood's progressions play at all, are drawn. A
//   chord never follows itself, and the last leads back into the first (a
//   progression goes round): the chords are drawn together with what is fixed
//   (locked chords, the first one) so that every drawn one fits both of its
//   neighbours (forward filtering, backward sampling over the loop). A fresh
//   draw starts on a chord the mood's progressions start on, has at least
//   three different chords (two in a shorter one), and plays the key's tonic
//   or its relative (i or III in a minor key, I or vi in a major one).
//
// Everything random is drawn from a seed (SplitMix64, and none of the standard
// library's distributions, which differ between standard libraries): the same
// settings and seed make the same ideas everywhere.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "harmony/ChordInference.h"
#include "harmony/Chords.h"

namespace sub::intelligence::harmony {

enum class Mood : uint8_t { Sad, Dark, Nostalgic };
inline constexpr int kMoods = 3;
// "Sad", "Dark", "Nostalgic".
const char* moodName(Mood mood);

struct ProgressionSettings {
    Key key;               // the song's
    Mood mood = Mood::Sad;
    int length = 4;        // chords: a bar each
};

struct Idea {
    std::vector<Chord> chords;
    // The mood's own progression it was made from (seedInfo), fitted to the
    // length; -1: drawn, or changed since.
    int seed = -1;

    friend bool operator==(const Idea&, const Idea&) = default;
};

// The moods' own progressions (free-midi-chords'): as it writes them ("i VI iv
// v", "Sad Hopeful"), whether in a minor key, and the moods they have of the
// three (bits: 1 << Mood).
struct SeedInfo {
    const char* numerals = "";
    const char* tags = "";
    bool minor = false;
    int moods = 0;
};
int seedCount();
SeedInfo seedInfo(int index);

// `count` ideas (fewer only if that many can't be found), none with the
// chords of another nor of one in `avoid`: the mood's own progressions and
// drawn ones, one after the other, the mood's first (a drawn one never has the
// chords of one of the mood's own that is still to come: it comes up as that
// one, so it is credited to it). A progression longer than
// `length` is cut; a shorter one is played again to fill it if it divides it,
// else the bars after it are drawn.
std::vector<Idea> progressionIdeas(const ProgressionSettings& settings, int count, uint64_t seed,
                                   const std::vector<std::vector<Chord>>& avoid = {});

// The chords of `chords` not `locked` drawn again, the locked ones kept as they
// are where they are: an idea that is neither `chords` nor in `avoid` if one
// can be found (else `chords` as they are). `locked` is by position (missing
// ones are unlocked); with none locked, a fresh idea (as progressionIdeas
// draws them). `settings.length` is ignored: the idea is as long as `chords`.
Idea redrawIdea(const std::vector<Chord>& chords, const std::vector<bool>& locked, const ProgressionSettings& settings,
                uint64_t seed, const std::vector<std::vector<Chord>>& avoid = {});

// Up to `count` ideas like `chords`: one or two of its chords that aren't
// locked drawn again, each idea neither `chords` nor in `avoid` nor another.
std::vector<Idea> ideaVariations(const std::vector<Chord>& chords, const std::vector<bool>& locked,
                                 const ProgressionSettings& settings, int count, uint64_t seed,
                                 const std::vector<std::vector<Chord>>& avoid = {});

// Chords to play in place of chords[index], best first, up to `count`: those
// most likely to come after the chord before it and to lead into the one after
// it (round and round), in the mood (as the draws weigh them). Triads, none
// with the same root and kind as the chord itself (chordColours has those).
std::vector<Chord> chordAlternatives(const std::vector<Chord>& chords, int index, const ProgressionSettings& settings,
                                     int count);
// The same chord coloured otherwise: its seventh as the key has it
// (withSeventh), its triad (a seventh's), sus4 and sus2 (a major or minor
// one's); not the chord itself.
std::vector<Chord> chordColours(const Chord& chord, const Key& key);

// A triad with its seventh: a major one's the major seventh if the key has it,
// else the minor (dominant) seventh; a minor one's the minor seventh; a
// diminished one's the minor seventh (half-diminished) if the key has it, else
// the diminished. Other chords (sevenths, suspended, augmented) as they are.
Chord withSeventh(const Chord& chord, const Key& key);

// Its Roman numeral in the key: the degree of the key's scale (major or natural
// minor) its root is on, with a flat or sharp where it is between them (bII,
// bIII, #IV, bVI, bVII in a major key; bII, #III, #IV, #VI, #VII in a minor
// one), upper case for a major chord, lower for a minor or diminished one, then
// its kind: "i", "VI", "bVII", "iv7", "V7", "ii°", "IIImaj7", "iiø7", "Vsus4",
// "III+".
std::string romanNumeral(const Chord& chord, const Key& key);

}  // namespace sub::intelligence::harmony
