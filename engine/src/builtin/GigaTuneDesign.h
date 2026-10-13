#pragma once
// GigaTune's maths: a pitch corrector for a single voice (builtin/devices/GigaTune.cpp), here so the engine
// tests can drive its parts on their own. Everything that touches fastpitch is in GigaTuneDesign.cpp: its
// templates pick their SIMD by the compiler's flags, and only the engine is built with AVX2.
//
// How it hears the pitch (PitchTracker), with Low Latency (High Quality: SwiftF0 instead of pYIN, see Quality):
// - pYIN (Mauch & Dixon), as librosa.pyin has it, from fastpitch (third_party/fastpitch): each frame's
//   difference function, its troughs under a beta prior of thresholds, the pitch candidates and how likely
//   each is, and the chance the frame is voiced. Frames of 32 ms every 2.67 ms (in time, at any rate),
//   C2 to C7, pitch bins a tenth of a semitone wide.
// - A Viterbi decoder over pYIN's HMM (the same states, transitions and tie rule as librosa's), run as the
//   frames come: each frame is decided kLag frames after it, by tracing back from the best state then (kLag 0:
//   its own best state, as it comes). With the whole signal in (flush()), the path is the one librosa decodes.
// - The decided frame's pitch, refined to a small fraction of a cent: the difference function
//   d(tau) = sum (x[j] - x[j + tau])^2 at whole lags about the pYIN candidate, interpolated between them with
//   a windowed sinc (d is band-limited but for a part the window makes negligible) and its minimum found by
//   Brent's method. For a periodic signal d is 0 at the period exactly, so the estimate has no bias. (A low
//   voice's window reaches past what Low Latency has in: it ends at the newest input.)
//
// How it corrects it (Corrector), on a timeline latency() samples behind its input (Low Latency: kLowLatencySeconds,
// MAutoPitch's; High Quality: as far as every mark a grain needs is placed from decided frames):
// - Each voiced frame's pitch p (semitones, MIDI) gets a target: the nearest note of the key's scale, the
//   previous target held unless another is nearer by more than kHysteresis. The correction (target - p)
//   is followed by a one-pole at Retune Speed (0: at once), from nothing at each note's start; Strength
//   scales it and Pitch adds to it: the output's pitch q = p + Strength * correction + Pitch.
// - TD-PSOLA moves the pitch, the formants staying: analysis marks a period apart in the input, synthesis
//   marks an output period apart, each synthesis mark playing the grain of the analysis mark nearest it.
//   A voiced grain spans the grain centres either side of its own, with Hann halves (rising from the one
//   before, falling to the one after), so at a ratio of 1 consecutive grains add to 1 exactly. Where grains
//   overlap by more than that (a raised pitch) the sum is divided by the windows' sum; where they overlap
//   less (a lowered pitch, a raised Formant) gaps are left, as in the voice, and the grains raised so each
//   harmonic keeps its level; and by the square root of the ratio, so a flat spectrum keeps its power as
//   its harmonics thin out or crowd in. Unvoiced stretches are cut in grains every kUnvoicedPeriod, played
//   where they were (their marks the analysis ones), wider, and divided by their sum.
// - Grains are played a sample at a time as the output goes, each reading the input as it needs it, so the
//   latency need only cover the tracker's delay and the marks ahead of the output. The marks are placed as the
//   grains need them; past the frames decided (Low Latency) from the last one's pitch, and placed again as the
//   next frames come until a grain takes them. A grain takes its own marks when it starts (half a ms before its
//   window), the ones after (its right half) when the output reaches its centre; where its read would pass the
//   input in by then (a low voice read faster), it plays an earlier period's grain.
// - Grains are centred on the voice's pulses (the energy's centroid at the fundamental): the first voiced
//   mark after unvoiced ones is moved onto its pulse, and the grains' centres follow the pulse slowly from
//   there (an offset from their marks, at which each grain is also played, so a ratio of 1 still reads the
//   sample it plays). The marks themselves keep to the refined periods: the output's period has no jitter.
// - Formant reads each voiced grain faster or slower than it plays (2^(Formant / 12) samples a sample), so
//   the spectral envelope moves and the pitch doesn't; unvoiced grains (breath, sibilants) play as they
//   were. Reads between samples are windowed-sinc interpolated, low-passed for a grain read faster.
// - The output period is the synthesis marks' spacing: with Strength at 100 % and Retune Speed at 0 a note
//   comes out at its frequency exactly, whatever error the tracker made. With nothing to do (Strength 0,
//   Pitch and Formant 0) the output is the input exactly, latency() samples later.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sub::gigatune {

// --- The tracker's settings ---------------------------------------------------------------------

inline constexpr double kFmin = 65.40639132514966;   // C2 (librosa's default)
inline constexpr double kFmax = 2093.004522404789;   // C7
inline constexpr double kFrameSeconds = 1536.0 / 48000.0;
inline constexpr double kHopSeconds = 128.0 / 48000.0;
inline constexpr int kLag = 0;  // frames a frame is decided after (as it comes)

// How the pitch is heard: Low Latency, pYIN as above; High Quality, SwiftF0 (builtin/SwiftF0.h: a small neural
// network, sure of the voice where pYIN takes a sub-harmonic or misses it in noise and reverb), its 16 ms frames
// brought to this tracker's 2.67 ms ones (log-frequency between them), the pitch refined as pYIN's is. A frame is
// voiced from a confidence of 0.5, or of kRescueConfidence where the refinement finds the period clearly
// (kRescuePeriodicity: SwiftF0 is unsure of high, clean notes). It waits for SwiftF0, 176 ms after each frame.
enum class Quality { LowLatency = 0, HighQuality };
inline constexpr double kRescueConfidence = 0.3;
inline constexpr double kRescuePeriodicity = 0.15;  // the normalised difference function at the period, at most

// The analysis frame and hop at a rate (the frame even: centred on its hop).
int frameLength(double sampleRate);
int hopLength(double sampleRate);

// --- The corrector's settings -------------------------------------------------------------------

inline constexpr double kHysteresis = 0.2;       // semitones another note must be nearer by to take over
inline constexpr double kMaxMove = 14.0;         // semitones the output's pitch moves at most (Pitch and correction)
inline constexpr double kMaxFormant = 12.0;      // semitones Formant moves at most
inline constexpr double kUnvoicedPeriod = 0.005; // seconds between unvoiced grains
inline constexpr double kSpeedMaxMs = 400.0;     // Retune Speed's range
inline constexpr double kPitchMax = 12.0;        // Pitch's range, semitones either way
inline constexpr double kLowLatencySeconds = 1024.0 / 48000.0;  // Low Latency's latency (MAutoPitch's)

// --- Keys -----------------------------------------------------------------------------------------

enum class Scale {
    Song = 0,  // the song's (major or natural minor); chromatic with none known
    Chromatic,
    Major,
    Minor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    Count
};

// The Key choice's labels ("Song", then C to B), the Scale choice's and the Quality choice's.
const std::vector<std::string>& keyLabels();
const std::vector<std::string>& scaleLabels();
const std::vector<std::string>& qualityLabels();

// The pitch classes notes are corrected to: bit c for pitch class c (C = 0). `key` is the Key choice
// (0: the song's tonic, 1-12: C to B); `songTonic` the song's (-1: none known) and `songMinor` its mode.
// Song (in Key or Scale) with no song key known: all twelve.
int scaleNotes(int key, Scale scale, int songTonic, bool songMinor);

// The note (MIDI, whole) a pitch (MIDI, fractional) is corrected to among `notes` (as scaleNotes(); none:
// all twelve): the nearest, but `previous` (-1: none) while it is no further than the nearest plus
// kHysteresis.
int targetNote(double pitch, int notes, int previous);

// --- The tracker ----------------------------------------------------------------------------------

struct PitchFrame {
    int64_t index = 0;       // the frame: its centre is index * hop samples into the input
    bool voiced = false;
    int bin = 0;             // the decoded state's pitch bin (unvoiced states have one too; High Quality: -1)
    double candidate = 0.0;  // the pYIN candidate's frequency in that bin (Hz; 0: none), as refine_f0 reports;
                             // High Quality: SwiftF0's pitch there
    double f0 = 0.0;         // voiced: the refined frequency (Hz)
    double voicedProb = 0.0; // pYIN's chance the frame is voiced (High Quality: SwiftF0's confidence)
};

class PitchTracker {
public:
    PitchTracker();
    ~PitchTracker();
    PitchTracker(const PitchTracker&) = delete;
    PitchTracker& operator=(const PitchTracker&) = delete;

    // Not real-time: allocates. `lag`: frames a frame is decided after.
    void prepare(double sampleRate, int lag = kLag);
    void reset();  // real-time: as new, the input before it silence

    int frameLength() const noexcept;
    int hop() const noexcept;
    int lag() const noexcept;

    // Real-time: how the pitch is heard (Low Latency at first). A change starts the tracker again.
    void setQuality(Quality quality) noexcept;
    Quality quality() const noexcept;
    // Host samples after a frame's centre it is decided by, at the most, with a quality.
    int delay(Quality quality) const noexcept;

    // Real-time: the next input sample. True when a frame was decided with it: decided() is that frame.
    bool push(double x) noexcept;
    const PitchFrame& decided() const noexcept;

    // Not real-time: the frames not yet decided whose centres the input has reached, decided as if it ended
    // here (traced back from the best state of the last frame), appended to `out`. The tracker is then reset.
    void flush(std::vector<PitchFrame>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// fastpitch's offline pYIN (librosa.pyin's result) of a whole signal with the tracker's frame and hop, f0 as
// the candidate in the decoded bin (refine_f0): what the tracker gives with a lag past the end (Low Latency), for tests.
std::vector<PitchFrame> offlinePyin(const std::vector<double>& x, double sampleRate);

// --- The corrector --------------------------------------------------------------------------------

struct Controls {
    int notes = 0xfff;       // as scaleNotes()
    double speedMs = 0.0;    // Retune Speed: the correction's time constant
    double strength = 1.0;   // 0..1
    double pitch = 0.0;      // semitones added after the correction
    double formant = 0.0;    // semitones
    Quality quality = Quality::LowLatency;  // a change starts the corrector again, at its own latency
};

// What the output plays at a sample, for the editor: the frame at its input's time. Voiced: the pitch heard, where
// the correction takes it (Strength's share of it, before Pitch) and the note it is corrected to (MIDI).
struct Heard {
    bool voiced = false;
    double pitch = 0.0, corrected = 0.0;
    int note = -1;
};

class Corrector {
public:
    Corrector();
    ~Corrector();
    Corrector(const Corrector&) = delete;
    Corrector& operator=(const Corrector&) = delete;

    void prepare(double sampleRate);  // not real-time
    void reset() noexcept;
    int latency() const noexcept;                  // at the quality it runs at
    int latency(Quality quality) const noexcept;   // at a quality

    // Real-time. The controls apply from the next sample in (frames are decided with the values of their time).
    void setControls(const Controls& controls) noexcept;
    // Real-time: one or two channels in place (the pitch is heard in their sum).
    void process(float* const* channels, int numChannels, int numFrames) noexcept;

    // Samples by which grains' rising halves were squeezed, started after their windows would have begun: Low
    // Latency's planned before a frame changed them (the voice stopped, a long period read slowly). High Quality's
    // latency is long enough: always 0.
    int64_t lateSamples() const noexcept;
    // What the output played `ago` samples before the last one out (unvoiced before the first frame decided).
    Heard heard(int ago = 0) const noexcept;
    // The frames decided so far that were voiced, and the last one decided (for tests).
    int64_t voicedFrames() const noexcept;
    const PitchFrame& lastFrame() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sub::gigatune
