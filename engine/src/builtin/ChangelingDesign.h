#pragma once
// Changeling's maths: a pitch and formant shifter for a single voice (builtin/devices/Changeling.cpp), here so the
// engine tests can drive its parts on their own. It hears the pitch with GigaTune's tracker (pYIN, or SwiftF0 at
// High Quality: builtin/GigaTuneDesign.h) and moves it with TD-PSOLA as GigaTune's corrector does: analysis marks a
// period apart in the input, grains centred on the voice's glottal closures (SEDREAMS: Closures, below), synthesis
// marks an output period apart, each
// playing the grain of the analysis mark nearest it, the windows' sum divided out where grains crowd and the grains
// raised where they thin, a ratio of 1 the input exactly. What it adds, from measuring Little AlterBoy and Polyverse
// Manipulator (their behaviour is set out where each part is):
//
// - Modes, as Little AlterBoy's: Transpose moves the voice by Pitch (continuous, semitones); Quantize pulls each
//   note onto the nearest of a scale's notes (chromatic at first: LAB's) and then moves it by Pitch, the vibrato
//   gone; Robot holds the voice on one note, C4 plus Pitch. MIDI notes, as LAB takes them: in Robot the note sung
//   is the last note played (held after its note-off), in Transpose and Quantize the last note played transposes by
//   its distance from C4 (held too). Unlike LAB the notes act at the input's time, so they land where they are
//   played once the engine has delayed the other tracks by the latency.
// - Glide: how long the output takes to move to a new note (Quantize, Robot) or a new transposition (Pitch, MIDI):
//   LAB's Quantize moves in about 17 ms, its Robot in about 60, Manipulator's at once unless its Glide is raised.
// - Formant: the formants moved, the pitch kept (the grains read faster or slower than they play); Link moves them
//   with the pitch instead, as a tape sped up would (LAB's Formant Link: Formant then adds nothing, here it adds).
// - Harmonics, Manipulator's harmonic shifter: each voiced grain's content shifted up or down by a number of the
//   output's harmonics (a single-sideband shift about the grain's centre: an allpass pair gives the voice and its
//   quadrature). The grains still repeat at the output's period, so what comes out stays harmonic: shifted up by k,
//   the lowest k harmonics are gone and the rest take the amplitudes of those k below; shifted down, the spectrum
//   folds over at 0 Hz. Fractions of a harmonic thin the lowest ones partly.
// - Twin, Manipulator's grain alternator made a voice of its own: a second set of grains at Interval semitones from
//   the voice, read as a tape at that speed would play them (its formants moved with it: an octave down is
//   Manipulator's dark sub-voice), Twin its level. Voiced grains only: breath and sibilants play once.
// - Detune and Width, Manipulator's: the left channel's grains Detune cents lower, the right's higher (a doubler);
//   Width takes every other voiced grain out of one side and the ones between out of the other, so each side hears
//   half of the grains and the two together all of them.
// - Smear, Manipulator's: grains taken from up to kSmearSeconds before their marks and their marks moved at random by
//   up to kSmearJitter of a period, so the voice blurs and roughens.
// - Unvoiced input (breath, sibilants, noise) plays as it was, unshifted, as in both.
// - Elastic grains (its own, not either's): where two voiced grains overlap, the later one's left edge and the
//   earlier one's right edge are bent a few samples (EdgeWarp: nothing within a quarter of the grain about its
//   centre, the closure, and more towards its edges) so that the two agree where they overlap, and cancel less.
//
// Latency, by Quality: Live, the tracker's as GigaTune's Low Latency (21.3 ms at any rate); Studio, about
// Little AlterBoy's (48 ms): the same tracker deciding each frame kStudioLag frames after it (pYIN's Viterbi path
// traced back from there), so a note is heard as voiced from its first periods and its start is shifted with the
// rest; High Quality, SwiftF0 as GigaTune's (about 316 ms: its grains may read a quarter as fast, so reach further
// back than GigaTune's 277 ms allow).
//
// The drive (Drive below), Little AlterBoy's, measured with noise (which LAB passes without grains) and fitted:
// LAB's wet path's colour (a high-pass at 22 Hz, +1.6 dB from 5.3 kHz), then at each of LAB's Drive settings 0..10
// a pre-gain, an asymmetric clipper of variable hardness and a post-gain, and above 6 a gain before the clipper that
// falls as the input's level rises (LAB's drive compresses there); the small-signal gain rises 1.69 dB a step. Run
// at twice the rate; within 7 to 16 % (RMS) of LAB's output at every setting and level. At 0 it isn't there at all
// (LAB colours even then, and is 2.4 dB quieter: Changeling at 0 is the input exactly); from 0 to 1 it fades in.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "builtin/GigaTuneDesign.h"
#include "builtin/DspBlocks.h"

namespace sub::changeling {

enum class Mode { Transpose = 0, Quantize, Robot };
enum class Quality { Live = 0, Studio, HighQuality };
// WSOLA's correlation shift before the elastic edges (an experiment): none, every voiced grain, or only grains
// whose closure SEDREAMS is unsure of.
enum class Correlate { Off = 0, Always, Unsure };

const std::vector<std::string>& modeLabels();     // "Transpose", "Quantize", "Robot"
const std::vector<std::string>& qualityLabels();  // "Live", "Studio", "High Quality"

inline constexpr double kPitchMax = 24.0;       // Pitch: semitones either way (LAB 12, Manipulator 24)
inline constexpr double kFormantMax = 24.0;     // Formant: semitones either way (LAB 12, Manipulator 24)
inline constexpr double kHarmonicsMax = 4.0;    // Harmonics: harmonics either way (Manipulator's)
inline constexpr double kIntervalMax = 24.0;    // Twin's interval: semitones either way
inline constexpr double kDetuneMax = 50.0;      // Detune: cents each side
inline constexpr double kGlideMaxMs = 1000.0;
inline constexpr double kSmearSeconds = 0.08;   // how far back Smear takes grains, at the most
inline constexpr double kSmearJitter = 0.15;    // of a period: how far Smear moves the marks, at the most
inline constexpr int kRobotNote = 60;           // C4: Robot's note with Pitch at 0, and what MIDI transposes from
inline constexpr double kMaxShift = 36.0;       // semitones the output's pitch is moved at the most
inline constexpr double kLowestOut = 24.0;      // the output's pitch, at the least and the most (MIDI)
inline constexpr double kHighestOut = 103.0;
inline constexpr double kMaxRead = 24.0;        // semitones a grain's read speed is moved at the most (formants)
inline constexpr double kLiveSeconds = 1024.0 / 48000.0;  // Live's latency (GigaTune's Low Latency)
inline constexpr int kStudioLag = 10;           // Studio: frames a frame is decided after (2.67 ms each)
inline constexpr double kDriveMax = 10.0;

// The controls, as the device's parameters give them.
struct Controls {
    Mode mode = Mode::Transpose;
    double pitch = 0.0;      // semitones (Robot: from C4)
    double formant = 0.0;    // semitones
    bool link = false;       // the formants move with the pitch
    int notes = 0xfff;       // Quantize's notes: bit c for pitch class c (gigatune::scaleNotes())
    double glideMs = 20.0;
    double harmonics = 0.0;  // harmonics
    double twin = 0.0;       // 0..1
    double interval = -12.0; // semitones
    double detune = 0.0;     // cents
    double width = 0.0;      // 0..1
    double smear = 0.0;      // 0..1
    bool elastic = true;     // grains' edges bent to agree with their neighbours' (EdgeWarp); off: plain PSOLA
    Correlate correlate = Correlate::Off;  // each voiced grain moved whole to agree with the one before (WSOLA)
    Quality quality = Quality::Studio;  // a change starts the shifter again, at its own latency
};

// What the output plays at a sample, for the editor: the frame at its input's time. Voiced: the pitch heard and
// the pitch it comes out at (MIDI), and the note Quantize or Robot holds it on (-1: Transpose).
struct Heard {
    bool voiced = false;
    double pitch = 0.0, out = 0.0;
    int note = -1;
};

// A MIDI note into the shifter, at a sample of the block.
struct NoteEvent {
    int offset = 0;
    int key = 60;
};

class Shifter {
public:
    Shifter();
    ~Shifter();
    Shifter(const Shifter&) = delete;
    Shifter& operator=(const Shifter&) = delete;

    void prepare(double sampleRate);  // not real-time
    void reset() noexcept;            // and the notes played forgotten
    int latency() const noexcept;                 // at the quality it runs at
    int latency(Quality quality) const noexcept;  // at a quality

    // Real-time. The controls apply from the next sample in (frames are decided with the values of their time).
    void setControls(const Controls& controls) noexcept;
    // Real-time: one or two channels in place (the pitch heard in their sum), the notes played during the block
    // (offsets in order).
    void process(float* const* channels, int numChannels, int numFrames, const NoteEvent* notes = nullptr,
                 int numNotes = 0) noexcept;

    // What the output played `ago` samples before the last one out.
    Heard heard(int ago = 0) const noexcept;
    // For tests: the frames decided so far that were voiced; samples of grains that began late (Live and Studio:
    // a frame decided after a grain was planned made it wider); grains dropped because their voice had no room.
    int64_t voicedFrames() const noexcept;
    int64_t lateSamples() const noexcept;
    int64_t droppedGrains() const noexcept;
    // For tests: the overlaps of two grains whose edges were bent (EdgeWarp), and how many of those bent both
    // grains (the earlier one's right edge not yet played) rather than only the later one's left.
    int64_t elasticJunctions() const noexcept;
    int64_t sharedJunctions() const noexcept;
    // For tests: the grains moved by the correlation shift, and how far in all (samples).
    int64_t correlatedGrains() const noexcept;
    double correlatedSamples() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Little AlterBoy's drive (see above): 0 (none) to 10. One or two channels, twice the rate inside.
class Drive {
public:
    void prepare(double sampleRate, int maxBlock);  // not real-time
    void reset() noexcept;
    void setDrive(double drive) noexcept;            // takes effect from the next block
    // A fixed delay (the oversampling's), whatever the drive: at 0 the input delayed by it exactly.
    int latency() const noexcept;
    void process(float* const* channels, int numChannels, int numFrames) noexcept;

    // The curve at a drive, for a level `envelope` (RMS) of its input: one sample, without the oversampling and
    // the DC blocker (tests, and the editor's drawing of it).
    static double curve(double drive, double x, double envelope = 0.0) noexcept;

private:
    static constexpr int kMaxChannels = 2;
    double drive_ = 0.0, active_ = 0.0;  // asked for, and the block's
    std::array<dsp::Oversampler, kMaxChannels> oversamplers_;
    std::array<dsp::DelayLine, kMaxChannels> delays_;
    std::array<dsp::DcBlocker, kMaxChannels> dc_;
    // LAB's wet path's colour (fitted from noise through it at Drive 0): a first-order high-pass, a high shelf and a
    // gentle low-pass, faded in with the drive from 0 to 1.
    dsp::BiquadCoefficients shelf_, top_;
    float highPass_ = 0.f;
    std::array<dsp::Biquad, kMaxChannels> shelves_, tops_;
    std::array<dsp::OnePole, kMaxChannels> lows_;
    std::array<double, kMaxChannels> power_{};  // the input's mean square, followed
    double attack_ = 0.0, release_ = 0.0;
    int latency_ = 0;
};

// The allpass pair whose outputs differ by 90 degrees (within 0.2 from 30 Hz to 20 kHz at 48 kHz): `i` and `q`
// together are the input's analytic signal (both delayed alike). Exposed for tests.
class Quadrature {
public:
    void reset() noexcept;
    void process(double x, double& i, double& q) noexcept;

private:
    static constexpr int kSections = 5;
    std::array<double, kSections> iState1_{}, iState2_{}, qState1_{}, qState2_{};
    std::array<double, kSections> iIn1_{}, iIn2_{}, qIn1_{}, qIn2_{};
    double qDelay_ = 0.0;
};

// One side of a grain bent in time ("elastic PSOLA"): the grain reads its input at x(t + d(u)), u output samples out
// from its centre on that side (positive, either side), d in input samples. d is 0 up to kDead of the side's half
// width (the closure, at the centre, stays where it is), then a cubic Hermite through d = values[k] at kKnots[k]
// of it (tangents by central differences, 0 at the dead zone's end), the last value past the edge. The shifter
// bounds values[k] by kLimits[k] of a period (1 : 2 : 4 out to the edge, a tenth of a period there), so the warp is
// slight and smooth: its slope changes the pitch a grain reads by 1 + d'. (A few samples, whatever the period, do
// nothing measurable: neighbouring grains overlap a closure's tail with the next one's head, tens of samples out.)
// Exposed for tests.
struct EdgeWarp {
    static constexpr double kDead = 0.25;
    static constexpr std::array<double, 3> kKnots = {0.4, 0.7, 1.0};
    static constexpr std::array<double, 3> kLimits = {0.025, 0.05, 0.1};

    std::array<double, 4> knots{};   // u of the dead zone's end and of each knot (output samples)
    std::array<double, 4> values{};  // d there (values[0]: 0)
    std::array<double, 4> slopes{};  // dd/du there
    bool on = false;

    // A side `half` output samples wide, bent by `d` (the three knots' values).
    void set(double half, const std::array<double, 3>& d) noexcept;
    double at(double u) const noexcept;
    double most() const noexcept { return on ? std::max({0.0, values[1], values[2], values[3]}) : 0.0; }
};

// The voice's glottal closures, where the shifter centres its grains: SEDREAMS (Drugman & Dutoit, "Glottal closure
// and opening instant detection from speech signals", Interspeech 2009), as the input comes. The mean-based signal
// (the input under a Blackman window 1.75 periods long) has a minimum each period, a little before the closure; the
// closure is the strongest peak of the linear-prediction residual in an interval after it, 0.7 of the minimum-to-
// maximum distance long and placed by where the residual's strongest peaks have been lying in those cycles (the paper
// takes their median over the whole signal; here the first few's mean, then followed: learn()). The residual: order rate / 1000 + 2, 25 ms Hann windows every 5 ms (each hop
// filtered by the window ending with it), its sign the one its skewness gives (closures are its peaks, RESKEW).
// Exposed for tests.
class Closures {
public:
    // Not real-time: `keep` samples kept, for periods up to `longest` samples.
    void prepare(double sampleRate, int keep, double longest);
    void reset() noexcept;
    void push(double x) noexcept;
    int64_t written() const noexcept { return written_; }
    int64_t ready() const noexcept { return ready_; }  // the samples whose residual is known (a hop at a time)

    // The closure in the period about `p` (period samples long): its offset from p, how sure that is (0..1: how far
    // its residual peak stands out of the cycle's), and where the cycle's strongest residual peak lies from its
    // mean-based minimum (in minimum-to-maximum distances: what learn() takes; NaN where no peak stands out). False
    // where the input doesn't reach far enough past p yet: it is then looked for as many whole periods earlier as it
    // takes.
    bool find(double p, double period, double& offset, double& sureness, double& relative) const noexcept;
    // A closure's `relative` from find(): the interval's place follows them (NaN: nothing learnt).
    void learn(double relative) noexcept;
    double ratio() const noexcept { return ratio_; }
    double residualAt(int64_t n) const noexcept { return residual_[static_cast<size_t>(n) & mask_]; }

private:
    static constexpr int kMaxOrder = 50;
    // A closure looked for: where and with what (the place, the period, the ratio, the sign), and what was found.
    struct Found {
        double p = 0.0, period = 0.0, ratio = 0.0, sign = 0.0, closure = 0.0, sureness = 0.0, relative = 0.0;
        int learnt = 0;
    };
    void analyse() noexcept;
    void look(Found& f, int64_t half) const noexcept;
    double inputAt(int64_t n) const noexcept { return input_[static_cast<size_t>(n) & mask_]; }

    int order_ = 0, window_ = 0, hop_ = 0;
    std::vector<double> input_, residual_, hann_, windowed_, history_;
    size_t mask_ = 0;
    std::array<double, kMaxOrder + 1> lpc_{};
    int64_t written_ = 0, ready_ = 0;
    double moment3_ = 0.0, momentRate_ = 0.0;  // the residual's third moment, followed (its skewness's sign)
    double ratio_ = 0.0;
    int learnt_ = 0;
    // find()'s working space (the mean-based signal and its running sums)
    mutable std::vector<double> sums0_, mean_;
    mutable std::vector<double> sums1Re_, sums1Im_, sums2Re_, sums2Im_;
    mutable std::array<Found, 8> found_{};  // the latest looked for
    mutable size_t nextFound_ = 0;
};

}  // namespace sub::changeling
