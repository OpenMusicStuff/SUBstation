#include "builtin/ChangelingDesign.h"

// Changeling's shifter and drive (see ChangelingDesign.h). The grains are GigaTune's corrector's (its marks, pulses,
// windows and the reading between samples, GigaTuneDesign.cpp), in voices: each voice has synthesis marks and
// grains of its own and plays one channel (the left and right's main voices, and their twins), all of them reading
// the analysis marks the tracker's frames place.

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "rt/FastMath.h"

namespace sub::changeling {

const std::vector<std::string>& modeLabels() {
    static const std::vector<std::string> kLabels = {"Transpose", "Quantize", "Robot"};
    return kLabels;
}

const std::vector<std::string>& qualityLabels() {
    static const std::vector<std::string> kLabels = {"Live", "Studio", "High Quality"};
    return kLabels;
}

namespace {

constexpr double kPi = std::numbers::pi;
using gigatune::kSincHalf;
using gigatune::sincAt;

// The tracker's range, as GigaTune's (a refined pitch may lie a little past pYIN's last bin).
constexpr double kRangeMargin = 1.03;
double lowestF0() { return gigatune::kFmin / kRangeMargin; }
double highestF0() { return gigatune::kFmax * kRangeMargin; }

// Grains on pulses, as GigaTune's: how sure the pulse must be at a voiced start to move the first mark onto it, how
// much of the way a grain's centre moves to its pulse at each mark (times the sureness), and how far it may be from
// its mark (of the period, or the unvoiced step if shorter).
constexpr double kOnsetSureness = 0.1;
constexpr double kOffsetFollow = 0.1;
constexpr double kOffsetShare = 0.4;
constexpr double kUnvoicedPeriod = 0.005;  // seconds between unvoiced grains
constexpr double kCommitAheadSeconds = 0.0005;
// A grain's pitch ratio, as its gain takes it, at the least (and 1 / it at the most); how much a grain shorter than
// its step is raised at the most.
constexpr double kLowestRatio = 0.4;
constexpr double kMostRaise = 2.0;

double midiHz(double pitch) { return 440.0 * std::exp2((pitch - 69.0) / 12.0); }

size_t powerOfTwoAtLeast(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// A small, fast generator for Smear (xorshift32), as uniform in [0, 1).
struct Random {
    uint32_t state = 0x9e3779b9u;
    double next() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state >> 8) * (1.0 / 16777216.0);
    }
};

}  // namespace

// --- Quadrature -------------------------------------------------------------------------------------

namespace {
// Each branch cascades (c - z^-2) / (1 - c z^-2) sections; the second is delayed a sample more. Fitted for a
// 90 degree difference (the second behind) from 30 Hz to 20 kHz at 48 kHz: within 0.18 degrees.
constexpr std::array<double, 5> kBranchI = {0.099802060100, 0.569076280421, 0.860434291157, 0.960840410601,
                                            0.990739726534};
constexpr std::array<double, 5> kBranchQ = {0.330433452155, 0.747759774769, 0.925245186905, 0.980091132693,
                                            0.997309649911};
}  // namespace

void Quadrature::reset() noexcept {
    iState1_ = iState2_ = qState1_ = qState2_ = iIn1_ = iIn2_ = qIn1_ = qIn2_ = {};
    qDelay_ = 0.0;
}

void Quadrature::process(double x, double& i, double& q) noexcept {
    double a = x;
    for (int s = 0; s < kSections; ++s) {
        const auto k = static_cast<size_t>(s);
        const double y = kBranchI[k] * (a + iState2_[k]) - iIn2_[k];
        iIn2_[k] = iIn1_[k];
        iIn1_[k] = a;
        iState2_[k] = iState1_[k];
        iState1_[k] = y;
        a = y;
    }
    i = a;
    double b = x;
    for (int s = 0; s < kSections; ++s) {
        const auto k = static_cast<size_t>(s);
        const double y = kBranchQ[k] * (b + qState2_[k]) - qIn2_[k];
        qIn2_[k] = qIn1_[k];
        qIn1_[k] = b;
        qState2_[k] = qState1_[k];
        qState1_[k] = y;
        b = y;
    }
    q = qDelay_;
    qDelay_ = b;
    // (denormals: the engine renders with them flushed to zero)
}

// --- Shifter ----------------------------------------------------------------------------------------

struct Shifter::Impl {
    static constexpr int kVoices = 4;  // the left's and the right's main voices, then their twins
    static constexpr int kMaxGrains = 128;
    static constexpr int kGrainMask = kMaxGrains - 1;
    static constexpr int kMidiEvents = 64;

    // A decided frame, as the marks read it: whether it is voiced, its pitch and the main voices' output pitch
    // (semitones), the formants' move (semitones), and for the editor the note it is held on.
    struct Control {
        bool voiced = false;
        double pitch = 0.0, out = 0.0, formant = 0.0;
        int note = -1;
    };
    // What a position on the input's timeline reads from the frames either side of it.
    struct At {
        double analysisPeriod = 0.0, out = 0.0, formant = 0.0;
        bool voiced = false, exact = false;
    };
    // A grain being played (GigaTune's, and: its gain, and for Harmonics its rotation and how much it is heard).
    struct Grain {
        double s = 0.0, a = 0.0, left = 0.0, right = 0.0, alpha = 1.0, gain = 1.0, fraction = 0.0;
        double rotRe = 1.0, rotIm = 0.0, stepRe = 1.0, stepIm = 0.0, shiftMix = 0.0;
        int64_t k = 0, j = 0, first = 0, last = -1, shift = 0;
        bool done = true, open = false, voiced = false;
        std::array<double, 2 * kSincHalf> taps{};
    };
    // A voice: its synthesis marks and grains, and what it plays.
    struct Voice {
        int input = 0, output = 0;
        bool twin = false;
        double offset = 0.0;   // semitones on the output pitch (Detune, the twin's interval)
        double formant = 0.0;  // semitones on the formants (the twin's interval)
        double voicedGain = 1.0, unvoicedGain = 1.0;
        int thinned = 0;       // Width thins its grains of this parity (0 even, 1 odd)
        std::vector<double> synthesis;
        std::vector<uint8_t> synthesisVoiced;  // (whether the step to the mark was voiced)
        int64_t synthesisCount = 0, synthesisSettled = 0, synthesisCommitted = 0;
        int64_t nextGrain = 1, nearest = 0, nearestCommitted = 0, lastFirst = 0;
        Grain plan;
        int64_t planJ = 0;
        bool planned = false;
        std::array<Grain, kMaxGrains> grains{};
        int grainHead = 0, grainCount = 0;
        Random random;
        // The main voices run on their channels throughout; a twin from when Twin comes up (taking its main voice's
        // marks, so it starts in step) until its last grain ends after Twin goes to 0. Only running voices commit
        // marks: with Twin at 0 the main voices place them as GigaTune's corrector does (a ratio of 1 is the input).
        bool running = false;
    };
    struct Midi {
        int64_t time = 0;
        int note = -1;
    };

    std::array<gigatune::PitchTracker, 2> trackers;  // lag 0 (Live, High Quality), kStudioLag (Studio)
    gigatune::PitchTracker* tracker = &trackers[0];
    double rate = 48000.0;
    int hop = 0, latency = 0;
    std::array<int, 3> latencies{};  // by quality
    Quality quality = Quality::Studio;
    double unvoicedPeriod = 240.0, longest = 0.0, reach = 0.0, smearMarks = 0.0;
    int commitAhead = 0;
    Controls controls;
    const double* sinc = nullptr;

    std::vector<Control> control;
    size_t controlMask = 0;
    int64_t controlCount = 0;

    // The output pitch's glides: of the transposition (Pitch and MIDI), and of the note Quantize or Robot holds.
    dsp::Glide shiftGlide, noteGlide;
    int target = -1;
    bool wasVoiced = false;
    bool robotSung = false;  // Robot has sung a frame since the reset (its first starts on its note)
    std::array<Midi, kMidiEvents> midi{};
    int midiCount = 0, midiHead = 0;
    int midiBefore = -1;  // the note held before the oldest one kept

    // The analysis marks, shared by the voices: positions, whether each grain is voiced, and its pulse's offset.
    std::vector<double> analysis, offsets;
    std::vector<uint8_t> analysisVoiced;
    size_t markMask = 0;
    int64_t analysisCount = 0, analysisSettled = 0, analysisCommitted = 0;

    std::array<Voice, kVoices> voices;
    int channels = 1;
    // The input per channel, and its quadrature pair (Harmonics).
    std::array<std::vector<float>, 2> input, inputI, inputQ;
    std::array<Quadrature, 2> quadrature;
    size_t inputMask = 0;
    int64_t time = 0;
    int64_t late = 0, voicedCount = 0, dropped = 0;

    void prepare(double sampleRate) {
        rate = sampleRate;
        sinc = gigatune::sincTable();
        trackers[0].prepare(sampleRate, 0);
        trackers[1].prepare(sampleRate, kStudioLag);
        // a short note pYIN's path leaves unvoiced would come out unshifted: rescued where clearly periodic
        for (auto& t : trackers) t.setRescue(true);
        hop = trackers[0].hop();
        unvoicedPeriod = kUnvoicedPeriod * rate;
        longest = std::max(rate / lowestF0(), unvoicedPeriod);
        // A grain reaches back half its width: from its centre to the one before (a step, and the two centres'
        // offsets), read at the slowest (formants down kMaxRead), its centre up to an offset from its mark.
        const double slowest = std::exp2(-kMaxRead / 12.0);
        const double longestSynthesis = std::max(longest, rate / midiHz(kLowestOut));
        const double offsetMost = kOffsetShare * unvoicedPeriod;
        reach = std::max({(longest + 2.0 * offsetMost) / slowest, 2.0 * unvoicedPeriod, longest}) + offsetMost;
        // High Quality: behind its input far enough for every mark a grain needs to be settled when it starts.
        const double behind = trackers[0].delay(gigatune::Quality::HighQuality) + hop +
                              std::max(2.5 * longest, 0.5 * longestSynthesis) + reach;
        latencies[static_cast<size_t>(Quality::HighQuality)] = static_cast<int>(std::ceil(behind)) + 2;
        latencies[static_cast<size_t>(Quality::Live)] = static_cast<int>(std::lround(kLiveSeconds * rate));
        latencies[static_cast<size_t>(Quality::Studio)] = latencies[0] + kStudioLag * hop;
        commitAhead = static_cast<int>(std::lround(kCommitAheadSeconds * rate));
        const int most = *std::max_element(latencies.begin(), latencies.end());

        const double smearSamples = kSmearSeconds * rate;
        const auto span = static_cast<size_t>(most) + static_cast<size_t>(std::ceil(4.0 * longest / slowest + smearSamples)) +
                          static_cast<size_t>(4 * kSincHalf * std::exp2(kMaxRead / 12.0)) + 64;
        for (auto* rings : {&input, &inputI, &inputQ})
            for (auto& c : *rings) c.assign(powerOfTwoAtLeast(span), 0.f);
        inputMask = input[0].size() - 1;
        const double shortestStep = std::min(unvoicedPeriod, rate / midiHz(kHighestOut));
        smearMarks = smearSamples / std::min(unvoicedPeriod, rate / highestF0()) + 2.0;
        const auto marks = static_cast<size_t>(std::ceil(static_cast<double>(span) / std::max(1.0, shortestStep))) +
                           static_cast<size_t>(smearMarks) + 64;
        analysis.assign(powerOfTwoAtLeast(marks), 0.0);
        offsets.assign(analysis.size(), 0.0);
        analysisVoiced.assign(analysis.size(), 0);
        markMask = analysis.size() - 1;
        for (Voice& v : voices) {
            v.synthesis.assign(analysis.size(), 0.0);
            v.synthesisVoiced.assign(analysis.size(), 0);
        }
        for (int i = 0; i < kVoices; ++i) {
            voices[static_cast<size_t>(i)].input = voices[static_cast<size_t>(i)].output = i % 2;
            voices[static_cast<size_t>(i)].twin = i >= 2;
            voices[static_cast<size_t>(i)].thinned = i % 2;
        }
        control.assign(powerOfTwoAtLeast(span / static_cast<size_t>(hop) + kStudioLag + 16), {});
        controlMask = control.size() - 1;
        selectTracker();
        latency = latencies[static_cast<size_t>(quality)];
        reset();
    }

    void selectTracker() noexcept {
        tracker = &trackers[quality == Quality::Studio ? 1 : 0];
        tracker->setQuality(quality == Quality::HighQuality ? gigatune::Quality::HighQuality
                                                            : gigatune::Quality::LowLatency);
    }

    void reset() noexcept {
        for (auto& t : trackers) t.reset();
        for (auto* rings : {&input, &inputI, &inputQ})
            for (auto& c : *rings) std::fill(c.begin(), c.end(), 0.f);
        for (auto& q : quadrature) q.reset();
        controlCount = 0;
        shiftGlide.snap(0.0);
        noteGlide.snap(kRobotNote);
        target = -1;
        wasVoiced = false;
        robotSung = false;
        midiCount = midiHead = 0;
        midiBefore = -1;
        // a mark before the start, and one at it (both unvoiced: the input before the start is silence)
        analysis[0] = -unvoicedPeriod;
        analysis[1] = 0.0;
        offsets[0] = offsets[1] = 0.0;
        analysisVoiced[0] = analysisVoiced[1] = 0;
        analysisCount = analysisSettled = analysisCommitted = 2;
        for (Voice& v : voices) {
            v.synthesis[0] = -unvoicedPeriod;
            v.synthesis[1] = 0.0;
            v.synthesisVoiced[0] = v.synthesisVoiced[1] = 0;
            v.synthesisCount = v.synthesisSettled = v.synthesisCommitted = 2;
            v.nextGrain = 1;
            v.nearest = v.nearestCommitted = 0;
            v.lastFirst = std::numeric_limits<int64_t>::min();
            v.planned = false;
            v.grainHead = v.grainCount = 0;
            v.running = !v.twin && v.output < channels;
        }
        time = 0;
        late = voicedCount = dropped = 0;
    }

    double& analysisAt(int64_t k) noexcept { return analysis[static_cast<size_t>(k) & markMask]; }
    double& offsetAt(int64_t k) noexcept { return offsets[static_cast<size_t>(k) & markMask]; }
    double centreAt(int64_t k) noexcept { return analysisAt(k) + offsetAt(k); }
    bool voicedAt(int64_t k) const noexcept { return analysisVoiced[static_cast<size_t>(k) & markMask] != 0; }
    double& synthesisAt(Voice& v, int64_t k) noexcept { return v.synthesis[static_cast<size_t>(k) & markMask]; }
    double periodOf(double pitch) const noexcept { return rate / midiHz(pitch); }

    // The oldest analysis mark any voice may still read (a grain may take one Smear's reach before its nearest).
    int64_t oldestNeeded() const noexcept {
        int64_t oldest = std::numeric_limits<int64_t>::max();
        for (const Voice& v : voices)
            if (v.running) oldest = std::min(oldest, v.nearestCommitted);
        return oldest - static_cast<int64_t>(smearMarks) - 4;
    }

    // Where the pulse of the period about `p` lies from it (period T), and how sure that is (0..1): GigaTune's (the
    // centroid of the low-passed signal's energy at the fundamental, over two periods). False where the input doesn't
    // reach p + T yet (the window is then as many whole periods earlier as it takes).
    bool pulseAt(double p, double period, double& offset, double& confidence) const noexcept {
        const auto newest = static_cast<double>(tracker->written() - 1);
        bool whole = true;
        if (std::floor(p + period) > newest) {
            p -= period * std::ceil((std::floor(p + period) - newest) / period);
            whole = false;
        }
        const auto first = static_cast<int64_t>(std::ceil(p - period));
        const auto last = static_cast<int64_t>(std::floor(p + period));
        double s0, c0, s1, c1, sh, ch, s2, c2;
        fastmath::sincos(2.0 * kPi * (static_cast<double>(first) - p) / period, s0, c0);
        fastmath::sincos(kPi * (static_cast<double>(first) - p) / period, sh, ch);
        fastmath::sincos(2.0 * kPi / period, s1, c1);
        fastmath::sincos(kPi / period, s2, c2);
        double re = 0.0, im = 0.0, total = 0.0;
        for (int64_t n = first; n <= last; ++n) {
            const double x = tracker->lowAt(n), e = (0.5 + 0.5 * ch) * x * x;
            re += e * c0;
            im -= e * s0;
            total += e;
            const double c = c0 * c1 - s0 * s1, sn = s0 * c1 + c0 * s1;
            c0 = c;
            s0 = sn;
            const double hc = ch * c2 - sh * s2, hs = sh * c2 + ch * s2;
            ch = hc;
            sh = hs;
        }
        confidence = total > 0.0 ? std::hypot(re, im) / total : 0.0;
        offset = confidence > 0.0 ? -period * std::atan2(im, re) / (2.0 * kPi) : 0.0;
        return whole;
    }

    // The frames either side of a position: the analysis period, the main voices' output pitch, the formants' move
    // and whether it is voiced (the nearer frame's voicing). Past the last decided frame, that one's: `exact` false.
    At at(double position) const noexcept {
        At r;
        r.analysisPeriod = unvoicedPeriod;
        if (controlCount == 0) return r;
        const double x = position / hop;
        const double whole = std::floor(x);
        auto g0 = static_cast<int64_t>(whole);
        double f = x - whole;
        if (g0 < 0) {
            g0 = 0;
            f = 0.0;
        } else if (g0 + 1 < controlCount) {
            r.exact = true;
        } else {
            g0 = controlCount - 1;
            f = 0.0;
        }
        const Control& c0 = control[static_cast<size_t>(g0) & controlMask];
        const Control& c1 = r.exact ? control[static_cast<size_t>(g0 + 1) & controlMask] : c0;
        r.voiced = f < 0.5 ? c0.voiced : c1.voiced;
        if (!r.voiced) return r;
        double pitch;
        if (c0.voiced && c1.voiced) {
            pitch = c0.pitch + (c1.pitch - c0.pitch) * f;
            r.out = c0.out + (c1.out - c0.out) * f;
            r.formant = c0.formant + (c1.formant - c0.formant) * f;
        } else {
            const Control& c = c0.voiced ? c0 : c1;
            pitch = c.pitch;
            r.out = c.out;
            r.formant = c.formant;
        }
        r.analysisPeriod = periodOf(pitch);
        return r;
    }

    // The note held at an input time by the MIDI played (-1: none yet).
    int midiAt(int64_t t) const noexcept {
        int note = midiBefore;
        for (int i = 0; i < midiCount; ++i) {
            const Midi& m = midi[static_cast<size_t>((midiHead + i) % kMidiEvents)];
            if (m.time > t) break;
            note = m.note;
        }
        return note;
    }

    void addMidi(int note) noexcept {
        if (midiCount == kMidiEvents) {  // the oldest goes: its note is the one held before the others
            midiBefore = midi[static_cast<size_t>(midiHead)].note;
            midiHead = (midiHead + 1) % kMidiEvents;
            --midiCount;
        }
        midi[static_cast<size_t>((midiHead + midiCount) % kMidiEvents)] = {time, note};
        ++midiCount;
    }

    double glideCoefficient() const noexcept {
        // Two one-poles in a row (dsp::Glide): 10 % to 90 % of a step in about 3.36 time constants.
        if (!(controls.glideMs > 0.0)) return 1.0;
        const double tau = controls.glideMs / 3.36 * 0.001 * rate;
        return 1.0 - std::exp(-static_cast<double>(hop) / tau);
    }

    // A decided frame: the output's pitch at its time; the provisional marks are placed again.
    void onFrame(const gigatune::PitchFrame& frame) noexcept {
        const int note = midiAt(frame.index * hop);
        const double k = glideCoefficient();
        const double transpose = controls.pitch + (note >= 0 && controls.mode != Mode::Robot ? note - kRobotNote : 0);
        // From a reset (playback starting, an offline render) the glides start where they are going: no scoop into
        // the first note.
        if (controlCount == 0) shiftGlide.snap(transpose);
        const double shift = shiftGlide.next(transpose, k, 1e-6);
        Control c;
        if (frame.voiced) {
            const double pitch = 69.0 + 12.0 * std::log2(frame.f0 / 440.0);
            double out = pitch + shift;
            int held = -1;
            if (controls.mode == Mode::Quantize) {
                target = gigatune::targetNote(pitch, controls.notes, target);
                if (!wasVoiced) noteGlide.snap(target);
                out = noteGlide.next(target, k, 1e-6) + shift;
                held = target;
            } else if (controls.mode == Mode::Robot) {
                const double robot = (note >= 0 ? note : kRobotNote) + controls.pitch;
                if (!robotSung) noteGlide.snap(robot);
                robotSung = true;
                out = noteGlide.next(robot, k, 1e-6);
                held = static_cast<int>(std::lround(robot));
            }
            out = std::clamp(out, std::max(kLowestOut, pitch - kMaxShift), std::min(kHighestOut, pitch + kMaxShift));
            c = {true, pitch, out, controls.formant + (controls.link ? out - pitch : 0.0), held};
            ++voicedCount;
        } else {
            target = -1;
        }
        wasVoiced = frame.voiced;
        control[static_cast<size_t>(frame.index) & controlMask] = c;
        controlCount = frame.index + 1;
        analysisCount = std::min(analysisCount, std::max(analysisSettled, analysisCommitted));
        for (Voice& v : voices) {
            v.synthesisCount = std::min(v.synthesisCount, std::max(v.synthesisSettled, v.synthesisCommitted));
            v.nearest = std::min(v.nearest, v.nearestCommitted);
            v.planned = false;
        }
    }

    // The next analysis mark: GigaTune's (a period on; the first voiced one after unvoiced ones moved onto its
    // pulse, the grains' centres following the pulse slowly after it). False when the ring is full.
    bool addAnalysis() noexcept {
        const int64_t m = analysisCount;
        if (m - oldestNeeded() + 16 >= static_cast<int64_t>(analysis.size())) return false;
        const double a = analysisAt(m - 1);
        const At here = at(a);
        const At mid = at(a + 0.5 * here.analysisPeriod);
        const double t2 = mid.analysisPeriod;
        bool exact = here.exact && mid.exact;
        double next = a + t2, offset = 0.0;
        if (mid.voiced) {
            double pulse, sure;
            exact = pulseAt(next, t2, pulse, sure) && exact;
            if (!here.voiced || m == 2 || !voicedAt(m - 1)) {
                if (sure >= kOnsetSureness) {
                    if (next + pulse > a + longest) pulse -= t2;
                    if (next + pulse >= a + 0.25 * t2) next += pulse;
                }
            } else {
                const double previous = offsetAt(m - 1);
                const double towards = pulse - t2 * std::round((pulse - previous) / t2);
                const double limit = kOffsetShare * std::min(t2, unvoicedPeriod);
                offset = std::clamp(previous + kOffsetFollow * sure * (towards - previous), -limit, limit);
                offset = std::max(offset, a + previous + 0.25 * t2 - next);
            }
        }
        offsetAt(m) = offset;
        analysisVoiced[static_cast<size_t>(m) & markMask] = mid.voiced ? 1 : 0;
        analysisAt(m) = next;
        analysisCount = m + 1;
        if (exact && analysisSettled == m) analysisSettled = m + 1;
        return true;
    }

    // A voice's next synthesis mark: an output period on where voiced (its own pitch: the main voices' moved by its
    // offset; Smear moves it at random), else the next analysis mark. False when a ring is full.
    bool addSynthesis(Voice& v) noexcept {
        const int64_t k = v.synthesisCount;
        if (k - v.nextGrain + 16 >= static_cast<int64_t>(v.synthesis.size())) return false;
        const double s = synthesisAt(v, k - 1);
        const At here = at(s);
        double next;
        uint8_t stepVoiced = 0;
        bool exact = here.exact;
        if (here.voiced && v.synthesisVoiced[static_cast<size_t>(k - 1) & markMask] != 0) {
            const double t1 = periodOf(here.out + v.offset);
            const At mid = at(s + 0.5 * t1);
            double t2 = mid.voiced ? periodOf(mid.out + v.offset) : unvoicedPeriod;  // (as the analysis marks step)
            // (moved in proportion, up or down alike: the pitch it averages to is the voice's)
            if (controls.smear > 0.0)
                t2 *= std::exp2(controls.smear * std::log2(1.0 + kSmearJitter) * (2.0 * v.random.next() - 1.0));
            next = s + t2;
            stepVoiced = mid.voiced ? 1 : 0;
            exact = exact && mid.exact;
        } else {
            while (!(analysisAt(analysisCount - 1) > s))
                if (!addAnalysis()) return false;
            int64_t i = analysisCount - 1;
            while (i > v.nearestCommitted && analysisAt(i - 1) > s) --i;
            next = analysisAt(i);
            stepVoiced = voicedAt(i) ? 1 : 0;
            exact = exact && i < analysisSettled;
        }
        v.synthesisVoiced[static_cast<size_t>(k) & markMask] = stepVoiced;
        synthesisAt(v, k) = next;
        v.synthesisCount = k + 1;
        if (exact && v.synthesisSettled == k) v.synthesisSettled = k + 1;
        return true;
    }

    // Synthesis mark k of a voice playing analysis mark j's grain: where, how fast it is read, how loud.
    void shape(Voice& v, int64_t k, int64_t j, Grain& g) noexcept {
        const bool voicedGrain = voicedAt(j);
        const double offset = voicedGrain ? offsetAt(j) : 0.0;
        g.k = k;
        g.j = j;
        g.voiced = voicedGrain;
        g.s = synthesisAt(v, k) + offset;
        g.a = analysisAt(j) + offset;
        g.shiftMix = 0.0;
        if (voicedGrain) {
            // Formant reads it faster or slower (the frame's move, and the twin's interval); unvoiced grains play as
            // they were.
            const At here = at(analysisAt(j));
            const double move = std::clamp(here.formant + v.formant, -kMaxRead, kMaxRead);
            g.alpha = std::exp2(move / 12.0);
            g.left = (g.a - centreAt(j - 1)) / g.alpha;
            const double outPeriod = 0.5 * (synthesisAt(v, k + 1) - synthesisAt(v, k - 1));
            const double r = std::clamp((analysisAt(j + 1) - analysisAt(j - 1)) / (synthesisAt(v, k + 1) - synthesisAt(v, k - 1)),
                                        kLowestRatio, 1.0 / kLowestRatio);
            const double q = r / g.alpha;
            // (raised by kMostRaise at the most: grains read far faster than they play, a short burst each period,
            // would come out spiky)
            g.gain = std::sqrt(r) * (q < 1.0 ? std::min(1.0 / q, kMostRaise) : 1.0) * v.voicedGain;
            if (controls.width > 0.0 && (k & 1) == v.thinned) g.gain *= 1.0 - controls.width;
            // Harmonics: the content shifted by h of the output's harmonics, about the grain's centre.
            const double h = controls.harmonics;
            if (h != 0.0 && outPeriod > 0.0) {
                g.shiftMix = std::min(1.0, 2.0 * std::abs(h));
                const double step = 2.0 * kPi * h / outPeriod;
                fastmath::sincos(step, g.stepIm, g.stepRe);
            }
        } else {
            g.alpha = 1.0;
            g.gain = v.unvoicedGain;
            const double past = kOffsetShare * unvoicedPeriod;
            g.left = std::max(2.0 * unvoicedPeriod, std::min(g.s - synthesisAt(v, k - 1), longest) + past);
        }
        g.first = static_cast<int64_t>(std::floor(g.s - g.left)) + 1;
        shapeRight(v, g);
    }

    void shapeRight(Voice& v, Grain& g) noexcept {
        if (g.voiced) {
            g.right = (centreAt(g.j + 1) - g.a) / g.alpha;
        } else {
            const double past = kOffsetShare * unvoicedPeriod;
            g.right = std::max(2.0 * unvoicedPeriod, std::min(synthesisAt(v, g.k + 1) - g.s, longest) + past);
        }
        g.last = static_cast<int64_t>(std::ceil(g.s + g.right)) - 1;
    }

    bool settleRight(Voice& v, Grain& g) noexcept {
        while (analysisCount <= g.j + 1)
            if (!addAnalysis()) return false;
        while (v.synthesisCount <= g.k + 1)
            if (!addSynthesis(v)) return false;
        shapeRight(v, g);
        analysisCommitted = std::max(analysisCommitted, g.j + 2);
        v.synthesisCommitted = std::max(v.synthesisCommitted, g.k + 2);
        analysisSettled = std::max(analysisSettled, analysisCommitted);
        v.synthesisSettled = std::max(v.synthesisSettled, v.synthesisCommitted);
        if (g.alpha > 1.0 && lookahead(g) > latency) {
            const double taps = std::ceil(kSincHalf * g.alpha) + 1.0;
            g.right = std::max(0.0, (latency - taps - (g.a - g.s)) / (g.alpha - 1.0) - 1.0);
            g.last = static_cast<int64_t>(std::ceil(g.s + g.right)) - 1;
        }
        g.open = false;
        return true;
    }

    static double lookahead(const Grain& g) noexcept {
        const double taps = g.alpha > 1.0 ? std::ceil(kSincHalf * g.alpha) + 1.0 : kSincHalf + 1.0;
        const double atFirst = g.a - g.s + (static_cast<double>(g.first) - g.s) * (g.alpha - 1.0);
        const double atLast = g.a - g.s + (static_cast<double>(g.last) - g.s) * (g.alpha - 1.0);
        return std::max(atFirst, atLast) + taps;
    }

    // Synthesis mark k's grain as it would play now: the analysis mark nearest it (Smear: one further back, at
    // random), or (Live, Studio) the nearest before that whose grain reads no input that won't be in.
    bool planGrain(Voice& v, int64_t k, Grain& g, int64_t& j) noexcept {
        while (v.synthesisCount <= k + 1)
            if (!addSynthesis(v)) return false;
        const double s = synthesisAt(v, k);
        for (;;) {
            while (analysisCount <= v.nearest + 1)
                if (!addAnalysis()) return false;
            if (!(analysisAt(v.nearest + 1) <= s)) break;
            ++v.nearest;
        }
        j = std::max<int64_t>(s - analysisAt(v.nearest) <= analysisAt(v.nearest + 1) - s ? v.nearest : v.nearest + 1, 1);
        if (controls.smear > 0.0 && voicedAt(j)) {
            const At here = at(analysisAt(j));
            const double back = controls.smear * kSmearSeconds * rate / here.analysisPeriod * v.random.next();
            const int64_t oldest = std::max<int64_t>(1, analysisCount - static_cast<int64_t>(analysis.size()) + 32);
            int64_t smeared = std::max(oldest, j - static_cast<int64_t>(back));
            while (smeared < j && !voicedAt(smeared)) ++smeared;  // a voiced grain for a voiced one
            j = smeared;
        }
        while (analysisCount <= j + 1)
            if (!addAnalysis()) return false;
        for (;; --j) {
            shape(v, k, j, g);
            if (j <= std::max<int64_t>(1, v.nearestCommitted - 4 - static_cast<int64_t>(smearMarks)) || lookahead(g) <= latency)
                break;
        }
        return true;
    }

    static void squeeze(Grain& g, int64_t from) noexcept {
        g.left = std::max(0.5, g.s - static_cast<double>(from) + 0.5);
        g.first = static_cast<int64_t>(std::floor(g.s - g.left)) + 1;
    }

    // Starts a voice's grains whose windows begin by output sample n (GigaTune's: High Quality as soon as it might,
    // Live and Studio as late as they can, their marks placed with the frames decided by then).
    void startGrains(Voice& v, int64_t n) noexcept {
        const bool early = quality == Quality::HighQuality;
        while (v.grainCount < kMaxGrains) {
            if (early) {
                while (v.synthesisCount <= v.nextGrain)
                    if (!addSynthesis(v)) return;
                if (synthesisAt(v, v.nextGrain) - reach > static_cast<double>(n)) return;
            }
            if (!v.planned) {
                if (!planGrain(v, v.nextGrain, v.plan, v.planJ)) return;
                if (!early && v.plan.first < v.lastFirst) squeeze(v.plan, v.lastFirst);
                v.planned = true;
            }
            if (!early && v.plan.first > n + commitAhead) return;
            Grain& g = v.plan;
            const int64_t j = v.planJ;
            v.planned = false;
            analysisCommitted = std::max({analysisCommitted, j + 1, v.nearest + 1});
            v.synthesisCommitted = std::max(v.synthesisCommitted, v.nextGrain + 1);
            analysisSettled = std::max(analysisSettled, analysisCommitted);
            v.synthesisSettled = std::max(v.synthesisSettled, v.synthesisCommitted);
            v.nearestCommitted = v.nearest;
            ++v.nextGrain;
            if (g.first < n) {
                late += n - g.first;
                squeeze(g, n);
            }
            if (lookahead(g) > latency) {
                late += std::max<int64_t>(0, g.last + 1 - g.first);
                continue;
            }
            v.lastFirst = g.first;
            if (g.alpha == 1.0) {
                const double offset = g.a - g.s;
                const double whole = std::floor(offset);
                g.fraction = offset - whole;
                g.shift = static_cast<int64_t>(whole);
                if (g.fraction != 0.0)
                    for (int t = 0; t < 2 * kSincHalf; ++t)
                        g.taps[static_cast<size_t>(t)] = sincAt(sinc, g.fraction - (t - (kSincHalf - 1)));
            }
            if (g.shiftMix > 0.0) {  // the rotation at the grain's first sample (phase 0 at its centre)
                const double phase = std::atan2(g.stepIm, g.stepRe) * (static_cast<double>(g.first) - g.s);
                fastmath::sincos(phase, g.rotIm, g.rotRe);
            }
            g.done = false;
            g.open = true;
            v.grains[static_cast<size_t>((v.grainHead + v.grainCount) & kGrainMask)] = g;
            ++v.grainCount;
        }
        if (v.planned || v.grainCount == kMaxGrains) ++dropped;
    }

    // Output sample n of a voice: its grains summed, divided by their windows' sum where it passes 1.
    double render(Voice& v, int64_t n) noexcept {
        double sum = 0.0, weights = 0.0;
        const float* x = input[static_cast<size_t>(v.input)].data();
        const float* xi = inputI[static_cast<size_t>(v.input)].data();
        const float* xq = inputQ[static_cast<size_t>(v.input)].data();
        const double* table = sinc;
        for (int k = 0; k < v.grainCount; ++k) {
            Grain& g = v.grains[static_cast<size_t>((v.grainHead + k) & kGrainMask)];
            if (g.done || n < g.first) continue;
            if (g.open && static_cast<double>(n) + 1.0 > g.s && !settleRight(v, g)) g.last = n;
            if (n > g.last) {
                g.done = true;
                continue;
            }
            const double u = static_cast<double>(n) - g.s;
            const double w = window(u, g.left, g.right);
            double value = 0.0, vi = 0.0, vq = 0.0;
            const bool shifted = g.shiftMix > 0.0;
            if (g.alpha == 1.0) {
                const int64_t base = n + g.shift;
                if (g.fraction == 0.0) {
                    const size_t at = static_cast<size_t>(base) & inputMask;
                    value = x[at];
                    if (shifted) {
                        vi = xi[at];
                        vq = xq[at];
                    }
                } else {
                    for (int t = 0; t < 2 * kSincHalf; ++t) {
                        const size_t at = static_cast<size_t>(base + t - (kSincHalf - 1)) & inputMask;
                        const double tap = g.taps[static_cast<size_t>(t)];
                        value += tap * x[at];
                        if (shifted) {
                            vi += tap * xi[at];
                            vq += tap * xq[at];
                        }
                    }
                }
            } else {
                // read faster or slower: the taps follow the read, low-passed to 1 / alpha of the band read faster
                const double scale = g.alpha > 1.0 ? 1.0 / g.alpha : 1.0;
                const int half = g.alpha > 1.0 ? static_cast<int>(std::ceil(kSincHalf * g.alpha)) : kSincHalf;
                const double position = g.a + u * g.alpha;
                const double whole = std::floor(position);
                const double fraction = position - whole;
                const auto base = static_cast<int64_t>(whole);
                for (int t = 1 - half; t <= half; ++t) {
                    const double tap = sincAt(table, (fraction - t) * scale) * scale;
                    const size_t at = static_cast<size_t>(base + t) & inputMask;
                    value += tap * x[at];
                    if (shifted) {
                        vi += tap * xi[at];
                        vq += tap * xq[at];
                    }
                }
            }
            if (shifted) {
                const double rotated = vi * g.rotRe - vq * g.rotIm;
                value += g.shiftMix * (rotated - value);
                const double re = g.rotRe * g.stepRe - g.rotIm * g.stepIm;
                g.rotIm = g.rotIm * g.stepRe + g.rotRe * g.stepIm;
                g.rotRe = re;
            }
            sum += g.gain * w * value;
            weights += w;
            if (n >= g.last) g.done = true;
        }
        while (v.grainCount > 0 && v.grains[static_cast<size_t>(v.grainHead)].done) {
            v.grainHead = (v.grainHead + 1) & kGrainMask;
            --v.grainCount;
        }
        return weights > 1.0 ? sum / weights : sum;
    }

    static double window(double u, double left, double right) noexcept {
        const double x = u < 0.0 ? -u / left : u / right;
        if (!(x < 1.0)) return 0.0;
        return 0.5 + 0.5 * fastmath::cos(kPi * x);
    }

    Heard heard(int ago) const noexcept {
        const int64_t n = time - 1 - ago - latency;
        if (n < 0 || controlCount == 0) return {};
        const int64_t index = std::min((n + hop / 2) / hop, controlCount - 1);
        if (index < controlCount - static_cast<int64_t>(control.size())) return {};
        const Control& c = control[static_cast<size_t>(index) & controlMask];
        if (!c.voiced) return {};
        return {true, c.pitch, c.out, c.note};
    }

    // The voices' settings from the controls: Detune's offsets, the twin's interval, the levels.
    // A twin starts: from its main voice's marks as they are (those placed for good, and the next grain), its grains
    // its own from there.
    void startTwin(Voice& twin, const Voice& main) noexcept {
        std::copy(main.synthesis.begin(), main.synthesis.end(), twin.synthesis.begin());
        std::copy(main.synthesisVoiced.begin(), main.synthesisVoiced.end(), twin.synthesisVoiced.begin());
        twin.synthesisCount = twin.synthesisSettled = twin.synthesisCommitted = std::max(main.synthesisCommitted, main.nextGrain + 1);
        twin.nextGrain = main.nextGrain;
        twin.nearest = twin.nearestCommitted = main.nearestCommitted;
        twin.lastFirst = main.lastFirst;
        twin.planned = false;
        twin.grainHead = twin.grainCount = 0;
        twin.running = true;
    }

    void applyControls() noexcept {
        const double detune = std::clamp(controls.detune, 0.0, kDetuneMax) / 100.0;
        const double twin = std::clamp(controls.twin, 0.0, 1.0);
        const double width = std::clamp(controls.width, 0.0, 1.0);
        // Width leaves each side half of the grains at 1: kept as loud (a thinned grain's window still counts in the
        // windows' sum, so the gaps stay gaps).
        const double widthGain = std::sqrt(2.0 / (1.0 + (1.0 - width) * (1.0 - width)));
        const double main = 1.0 / std::sqrt(1.0 + twin * twin);
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = voices[static_cast<size_t>(i)];
            const double side = (i % 2 == 0) ? -detune : detune;
            v.offset = side + (v.twin ? controls.interval : 0.0);
            v.formant = v.twin ? controls.interval : 0.0;
            v.voicedGain = (v.twin ? twin * main : main) * widthGain;
            v.unvoicedGain = v.twin ? 0.0 : 1.0;
        }
    }

    void process(float* const* io, int numChannels, int numFrames, const NoteEvent* notes, int numNotes) noexcept {
        if (controls.quality != quality) {  // another tracker, another latency: from the start again
            quality = controls.quality;
            selectTracker();
            latency = latencies[static_cast<size_t>(quality)];
            reset();
        }
        applyControls();
        if (numChannels < 1) return;
        if (std::min(numChannels, 2) != channels) {  // (a voice that wasn't playing would start from long ago)
            channels = std::min(numChannels, 2);
            reset();
        }
        const bool twins = controls.twin > 0.0;
        int nextNote = 0;
        for (int i = 0; i < numFrames; ++i) {
            while (nextNote < numNotes && notes[nextNote].offset <= i) addMidi(notes[nextNote++].key);
            const size_t in = static_cast<size_t>(time) & inputMask;
            double mono = 0.0;
            for (int c = 0; c < channels; ++c) {
                const float x = io[c][i];
                input[static_cast<size_t>(c)][in] = x;
                double qi, qq;
                quadrature[static_cast<size_t>(c)].process(x, qi, qq);
                inputI[static_cast<size_t>(c)][in] = static_cast<float>(qi);
                inputQ[static_cast<size_t>(c)][in] = static_cast<float>(qq);
                mono += x;
            }
            if (tracker->push(mono / channels)) onFrame(tracker->decided());
            const int64_t n = time - latency;
            double out[2] = {0.0, 0.0};
            for (int vi = 0; vi < kVoices; ++vi) {
                Voice& v = voices[static_cast<size_t>(vi)];
                if (v.output >= channels) continue;
                if (v.twin) {
                    if (twins && !v.running) startTwin(v, voices[static_cast<size_t>(v.output)]);
                    if (!twins && v.running && v.grainCount == 0) v.running = false;
                    if (!v.running) continue;
                }
                if (!v.twin || twins) startGrains(v, n);
                out[v.output] += render(v, n);
            }
            for (int c = 0; c < channels; ++c) io[c][i] = static_cast<float>(out[c]);
            ++time;
        }
        while (nextNote < numNotes) addMidi(notes[nextNote++].key);
    }
};

Shifter::Shifter() : impl_(std::make_unique<Impl>()) {}
Shifter::~Shifter() = default;
void Shifter::prepare(double sampleRate) { impl_->prepare(sampleRate); }
void Shifter::reset() noexcept { impl_->reset(); }
int Shifter::latency() const noexcept { return impl_->latency; }
int Shifter::latency(Quality quality) const noexcept { return impl_->latencies[static_cast<size_t>(quality)]; }
void Shifter::setControls(const Controls& controls) noexcept { impl_->controls = controls; }
void Shifter::process(float* const* channels, int numChannels, int numFrames, const NoteEvent* notes,
                      int numNotes) noexcept {
    impl_->process(channels, numChannels, numFrames, notes, numNotes);
}
Heard Shifter::heard(int ago) const noexcept { return impl_->heard(ago); }
int64_t Shifter::voicedFrames() const noexcept { return impl_->voicedCount; }
int64_t Shifter::lateSamples() const noexcept { return impl_->late; }
int64_t Shifter::droppedGrains() const noexcept { return impl_->dropped; }

// --- Drive ------------------------------------------------------------------------------------------

namespace {

// Little AlterBoy's drive at each of its settings, fitted to noise through it: y = A (s(B x + b) - s(b)),
// s(u) = u / (1 + |u|^h)^(1/h). The A's are LAB's divided by its gain at 0 (0.759: LAB is 2.4 dB quieter there),
// so 0 is unity and each step up 1.69 dB louder, as in LAB.
struct DriveShape {
    double a, b, bias, hardness;
};
constexpr double kLabGainAtZero = 0.759;
constexpr std::array<DriveShape, 11> kDriveShapes = {{
    {2.95834, 0.2753, -0.7984, 12.000},
    {1.34121, 0.7150, -0.6274, 7.309},
    {0.61068, 1.8588, -0.1264, 2.535},
    {0.50648, 2.7183, -0.0765, 2.512},
    {0.42942, 3.8999, -0.0481, 2.555},
    {0.37275, 5.5521, -0.0423, 2.688},
    {0.30149, 8.2616, -0.0704, 3.379},
    {0.24445, 11.9138, -0.1055, 4.809},
    {0.20057, 17.2062, -0.1140, 5.670},
    {0.16641, 25.9025, -0.0802, 4.688},
    {0.13882, 38.2658, -0.0563, 3.933},
}};
// Above 6 LAB's drive compresses: the gain before the clipper is 1 / (1 + kappa (rms - kCompressFrom)) of an input
// at `rms` (RMS), kappa rising as kCompressMost ((drive - 6) / 4)^1.5.
// LAB's wet path's colour, fitted to noise through it at Drive 0 (within 0.6 dB RMS from 25 Hz to 22 kHz): a
// first-order high-pass at 22 Hz, a shelf of +1.6 dB from 5.3 kHz, and a low-pass at 22 kHz, as RBJ's at 48 kHz.
constexpr double kColourHighPassHz = 22.2;
constexpr double kColourShelfHz = 5277.0, kColourShelfQ = 0.83, kColourShelfDb = 1.6;
constexpr double kColourTopHz = 22022.0, kColourTopQ = 0.30;
constexpr double kCompressFrom = 0.07;
constexpr double kCompressMost = 5.0;
constexpr double kCompressAttackMs = 30.0, kCompressReleaseMs = 200.0;

double clip(double u, double h) noexcept {
    // u / (1 + |u|^h)^(1/h), its powers as exp2 and log2 (fast forms: the shape, not the last ulp, matters)
    const double a = std::abs(u);
    if (a < 1e-12) return u;
    const double p = fastmath::exp2(h * fastmath::log2(a));
    return u * fastmath::exp2(-fastmath::log2(1.0 + p) / h);
}

DriveShape shapeAt(double drive) noexcept {
    const double d = std::clamp(drive, 0.0, kDriveMax);
    const auto i = std::min(static_cast<int>(d), 9);
    const double t = d - i;
    const DriveShape& p = kDriveShapes[static_cast<size_t>(i)];
    const DriveShape& q = kDriveShapes[static_cast<size_t>(i + 1)];
    return {p.a + (q.a - p.a) * t, p.b + (q.b - p.b) * t, p.bias + (q.bias - p.bias) * t,
            p.hardness + (q.hardness - p.hardness) * t};
}

}  // namespace

// The curve at a drive, worked out once for many samples.
struct PreparedCurve {
    double drive = 0.0, a = 1.0, b = 1.0, bias = 0.0, hardness = 2.0, atBias = 0.0, kappa = 0.0;

    explicit PreparedCurve(double d) noexcept : drive(d) {
        const DriveShape s = shapeAt(d);
        a = s.a / kLabGainAtZero;
        b = s.b;
        bias = s.bias;
        hardness = s.hardness;
        atBias = clip(bias, hardness);
        kappa = d > 6.0 ? kCompressMost * std::pow((std::min(d, kDriveMax) - 6.0) / 4.0, 1.5) : 0.0;
    }
    double operator()(double x, double envelope) const noexcept {
        if (!(drive > 0.0)) return x;
        const double gain = kappa > 0.0 ? 1.0 / (1.0 + kappa * std::max(0.0, envelope - kCompressFrom)) : 1.0;
        const double y = a * (clip(b * gain * x + bias, hardness) - atBias);
        // From 0 to 1 the curve fades in from the input itself (LAB's 0 colours a little; Changeling's 0 doesn't).
        return drive < 1.0 ? x + (y - x) * drive : y;
    }
};

double Drive::curve(double drive, double x, double envelope) noexcept { return PreparedCurve(drive)(x, envelope); }

void Drive::prepare(double sampleRate, int maxBlock) {
    rate_ = sampleRate;
    for (int c = 0; c < kMaxChannels; ++c) {
        oversamplers_[static_cast<size_t>(c)].prepare(maxBlock, 1);
        oversamplers_[static_cast<size_t>(c)].setFactorLog2(1);
        dc_[static_cast<size_t>(c)].prepare(2.0 * sampleRate, 5.0);
    }
    latency_ = dsp::Oversampler::latencyFor(1);
    for (auto& d : delays_) d.prepare(maxBlock + latency_ + 4);
    highPass_ = dsp::onePoleCutoff(kColourHighPassHz, sampleRate);
    shelf_ = dsp::BiquadCoefficients::highShelf(kColourShelfHz, kColourShelfQ, kColourShelfDb, sampleRate);
    top_ = dsp::BiquadCoefficients::lowpass(kColourTopHz, kColourTopQ, sampleRate);
    attack_ = std::exp(-1.0 / (kCompressAttackMs * 0.001 * 2.0 * sampleRate));
    release_ = std::exp(-1.0 / (kCompressReleaseMs * 0.001 * 2.0 * sampleRate));
    reset();
}

void Drive::reset() noexcept {
    for (auto& o : oversamplers_) o.reset();
    for (auto& d : delays_) d.reset();
    for (auto& d : dc_) d.reset();
    for (auto& b : shelves_) b.reset();
    for (auto& b : tops_) b.reset();
    for (auto& l : lows_) l.reset();
    power_ = {};
}

void Drive::setDrive(double drive) noexcept { drive_ = std::clamp(drive, 0.0, kDriveMax); }

int Drive::latency() const noexcept { return latency_; }

void Drive::process(float* const* channels, int numChannels, int numFrames) noexcept {
    const int chs = std::clamp(numChannels, 0, kMaxChannels);
    const double before = active_;
    active_ = drive_;
    const bool none = !(active_ > 0.0) && !(before > 0.0);
    for (int c = 0; c < chs; ++c) {
        float* io = channels[c];
        dsp::DelayLine& delay = delays_[static_cast<size_t>(c)];
        dsp::Oversampler& os = oversamplers_[static_cast<size_t>(c)];
        // The oversampling runs at 0 too (its output unused), so turning the drive up starts it with its history.
        for (int i = 0; i < numFrames; ++i) delay.push(io[i]);
        // LAB's colour (before the clipper: nearer LAB at high drives than after it), in as the drive comes up
        // from 0 to 1, kept running at 0 so it starts with its history
        dsp::Biquad& shelf = shelves_[static_cast<size_t>(c)];
        dsp::Biquad& top = tops_[static_cast<size_t>(c)];
        dsp::OnePole& low = lows_[static_cast<size_t>(c)];
        for (int i = 0; i < numFrames; ++i) {
            const float x = io[i];
            const float coloured = top.process(top_, shelf.process(shelf_, low.highpass(x, highPass_)));
            const double d = before + (active_ - before) * (static_cast<double>(i + 1) / numFrames);
            const float share = static_cast<float>(std::min(1.0, d));
            io[i] = x + share * (coloured - x);
        }
        float* up = os.up(io, numFrames);
        double& power = power_[static_cast<size_t>(c)];
        dsp::DcBlocker& dc = dc_[static_cast<size_t>(c)];
        const int n2 = 2 * numFrames;
        constexpr int kStretch = 16;  // the drive glides from the last block's to this one's, a stretch at a time
        for (int from = 0; from < n2; from += kStretch) {
            const int to = std::min(n2, from + kStretch);
            const PreparedCurve shape(before + (active_ - before) * (static_cast<double>(to) / n2));
            for (int i = from; i < to; ++i) {
                const double x = up[i];
                const double p = x * x;
                power = p + (p > power ? attack_ : release_) * (power - p);
                up[i] = dc.process(static_cast<float>(none ? x : shape(x, std::sqrt(power))));
            }
        }
        os.down(up, numFrames, io);
        // none: the input exactly, as late as the oversampling makes it
        if (none)
            for (int i = 0; i < numFrames; ++i) io[i] = delay.tap(numFrames - 1 - i + latency_);
    }
}

}  // namespace sub::changeling
