// A recording's tempo, key and chords, from its samples.
//
// What the arrangement asks of a file dragged into it (a loop, a stem, a whole
// song): how fast it goes, where its beats fall, what key it is in and which
// chords it plays when. Pure functions: no threads, no state, nothing global,
// so any number may run at once on threads of their own. A 4-minute song takes
// about a fifth of a second of one core, a 10-second loop about 15 ms
// (docs/intelligence.md, "Tempo, key and chords from audio").
//
// - The samples are mixed to mono and decimated to about 11 kHz (a low-pass
//   first), which keeps everything below 5 kHz: all the analysis needs.
// - The tempo: how much the sound's spectrum rises, band by band, frame by
//   frame (its onsets: 172 frames a second), and how that repeats (its
//   autocorrelation, by FFT). Each tempo from kMinBpm to kMaxBpm scores the mean
//   of the autocorrelation at every multiple of its period (a comb, so the
//   period is pinned to a fraction of a frame), times a prior centred on 135
//   BPM; of the best few, those whose off-beats are as strong as their beats
//   (half the real tempo) lose. A file 4, 8, 16... beats long at a tempo in the
//   range is likely a loop at it: those tempos are candidates too (their comb
//   over the onsets wrapped round, counting more), so a trimmed loop gets the
//   exact tempo its length makes. Then: near a whole BPM, the whole BPM. The
//   beats' phase: where a pulse train at that period meets the most onsets.
//   None when nothing repeats clearly enough (a one-shot, a pad, noise,
//   silence).
// - The chords: chroma, the spectrum (4096-point frames, 186 ms apart) as twelve
//   pitch classes, from its peaks alone (so noise and drums count little), each
//   less what the harmonics of the notes below it explain, tuned to the
//   recording's own A; and the bass, the lowest strong peak under 250 Hz. Frames
//   count by how tonal and how loud they are. Summed into eighth-note steps on
//   the beat grid (quarter-second steps without a tempo), in the units MIDI
//   notes make (harmony::StepProfiles), with bars where the harmony changes
//   most, then scored and smoothed exactly as MIDI's are
//   (harmony::inferChords). The key: Krumhansl and Kessler's profiles against
//   the whole file's chroma and bass (harmony::estimateKey).

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "harmony/Chords.h"

namespace sub::intelligence::audio {

struct Tempo {
    double bpm = 120.0;
    double confidence = 0.0;    // 0..1: how clearly the beat repeats (kMinTempoConfidence or more)
    double firstBeatSec = 0.0;  // where the beat grid's phase falls, in seconds: 0 <= firstBeatSec < 60 / bpm
};

// A chord and when it sounds, in seconds from the file's start.
struct ChordSpanSec {
    double start = 0.0;
    double end = 0.0;
    harmony::Chord chord;

    friend bool operator==(const ChordSpanSec&, const ChordSpanSec&) = default;
};

struct Analysis {
    std::optional<Tempo> tempo;         // none: no steady beat (a one-shot, a pad, speech)
    std::optional<harmony::Key> key;    // none: too little pitched sound to tell
    std::vector<ChordSpanSec> chords;   // by time, not overlapping; nothing where no chord sounds (drums alone: none)
};

struct Options {
    bool chords = true;             // the key and the chords (the chroma: most of the cost); else the tempo alone
    double maxTempoSeconds = 60.0;  // the tempo from at most this much of the start (a loop: all of it)
};

// The tempos told apart. A slower piece comes out at twice its tempo, a faster
// one at half.
inline constexpr double kMinBpm = 60.0;
inline constexpr double kMaxBpm = 200.0;
inline constexpr double kMinTempoConfidence = 0.3;

// `channels`: `channelCount` planar channels of `frames` samples each, at
// `sampleRate` (8 kHz or more; anything less is analysed as nothing).
Analysis analyze(const float* const* channels, int channelCount, std::size_t frames, double sampleRate,
                 const Options& options = {});

// The tempo of mono samples, all of them (analyze's first step).
std::optional<Tempo> estimateTempo(const float* mono, std::size_t frames, double sampleRate);

}  // namespace sub::intelligence::audio
