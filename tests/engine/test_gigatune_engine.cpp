// The built-in GigaTune (pitch correction for a single voice): its listing; keys, scales and the notes chosen
// (hysteresis); its tracker decoding as fastpitch's offline pYIN (librosa's) does, frame for frame, and refining
// the pitch to a hundredth of a cent; nothing to do passing the input bit for bit, latency() later, at any rate;
// notes landing on their frequency to a hundredth of a cent, in a key; Strength, Pitch, Retune Speed (its time
// constant, vibrato flattened or kept); Formant moving the spectral envelope and Pitch keeping it; the level
// whatever the phase of a voice's pulses; grains never late at the extremes; silence and noise; the song's key
// through the device; the editor's displays. With GIGATUNE_VOCALS set to a folder of dry vocal phrases named
// "..._<key>_dry.wav" (as SD_OMNIVOX's), it also corrects each to its key (GIGATUNE_OUT: and writes what it made
// there).

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "AudioSource.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/GigaTuneDesign.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"
#include "harness/Test.h"

using namespace subtest;
namespace gt = sub::gigatune;

namespace {

constexpr double kRate = 48000.0;

// A harmonic tone following freq (Hz, a value a sample), its harmonics falling as 1/h up to 0.45 of the rate.
std::vector<double> harmonic(const std::vector<double>& freq, double rate, double amplitude = 0.3) {
    std::vector<double> x(freq.size());
    double phase = 0.0;
    for (size_t i = 0; i < freq.size(); ++i) {
        double s = 0.0;
        for (int h = 1; h * freq[i] < 0.45 * rate && h <= 60; ++h) s += std::sin(h * phase) / h;
        x[i] = amplitude * s;
        phase += 2.0 * kPi * freq[i] / rate;
        if (phase > 2.0 * kPi * 1000.0) phase -= 2.0 * kPi * 1000.0;
    }
    return x;
}

std::vector<double> steady(double freq, double seconds, double rate = kRate) {
    return std::vector<double>(static_cast<size_t>(seconds * rate), freq);
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
double hz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }

// The corrector over one channel in blocks of 256, the latency taken off (and what came late, if asked).
std::vector<float> correct(const std::vector<double>& x, double rate, const gt::Controls& controls, int64_t* late = nullptr) {
    gt::Corrector c;
    c.prepare(rate);
    c.setControls(controls);
    const int latency = c.latency(controls.quality);
    std::vector<float> y(x.begin(), x.end());
    y.resize(y.size() + static_cast<size_t>(latency), 0.f);
    for (size_t at = 0; at < y.size(); at += 256) {
        float* p[1] = {y.data() + at};
        c.process(p, 1, static_cast<int>(std::min<size_t>(256, y.size() - at)));
    }
    REQUIRE(c.latency() == latency);
    if (late) *late = c.lateSamples();
    y.erase(y.begin(), y.begin() + latency);
    return y;
}

// A signal's frames as the tracker decides them with the whole signal in (the lag past its end).
std::vector<gt::PitchFrame> track(const std::vector<double>& x, double rate) {
    gt::PitchTracker t;
    t.prepare(rate, static_cast<int>(x.size()) / gt::hopLength(rate) + 2);
    std::vector<gt::PitchFrame> frames;
    for (double v : x)
        if (t.push(v)) frames.push_back(t.decided());
    for (int i = 0; i < t.frameLength() / 2; ++i)
        if (t.push(0.0)) frames.push_back(t.decided());
    t.flush(frames);
    return frames;
}
std::vector<gt::PitchFrame> track(const std::vector<float>& y, double rate) {
    return track(std::vector<double>(y.begin(), y.end()), rate);
}

// The worst distance (cents) of the frames from `freq` between `from` and `to` seconds (unvoiced ones: far), and
// how many there were.
double worstCents(const std::vector<gt::PitchFrame>& frames, double freq, double from, double to, double rate, int* count = nullptr) {
    const int hop = gt::hopLength(rate);
    double worst = 0.0;
    int n = 0;
    for (const auto& f : frames) {
        const double t = static_cast<double>(f.index * hop) / rate;
        if (t < from || t > to) continue;
        ++n;
        worst = std::max(worst, f.voiced ? std::abs(cents(f.f0, freq)) : 1e9);
    }
    if (count) *count = n;
    return worst;
}

// The frequency of the strongest line near `guess` over [from, from + length): a Hann-windowed DTFT's
// magnitude, maximised.
double peakFrequency(const std::vector<float>& y, int64_t from, int64_t length, double guess, double rate = kRate) {
    std::vector<double> w(static_cast<size_t>(length));
    for (int64_t n = 0; n < length; ++n)
        w[static_cast<size_t>(n)] = (0.5 - 0.5 * std::cos(2 * kPi * (n + 0.5) / length)) * y[static_cast<size_t>(from + n)];
    const auto magnitude = [&](double f) {
        const std::complex<double> step = std::polar(1.0, -2.0 * kPi * f / rate);
        std::complex<double> z = 1.0, sum = 0.0;
        for (int64_t n = 0; n < length; ++n) {
            sum += w[static_cast<size_t>(n)] * z;
            z *= step;
        }
        return std::abs(sum);
    };
    double best = guess, bestMagnitude = 0.0;
    for (double f = guess * 0.98; f <= guess * 1.02; f += guess * 0.001) {
        const double m = magnitude(f);
        if (m > bestMagnitude) bestMagnitude = m, best = f;
    }
    double a = best - guess * 0.001, b = best + guess * 0.001;
    for (int i = 0; i < 40; ++i) {
        const double c = b - 0.6180339887 * (b - a), d = a + 0.6180339887 * (b - a);
        if (magnitude(c) > magnitude(d)) b = d;
        else a = c;
    }
    return 0.5 * (a + b);
}

double levelDb(const std::vector<float>& y, int64_t from, int64_t to) { return rmsDb(Samples(y.begin(), y.end()), from, to); }
double levelDb(const std::vector<double>& x, int64_t from, int64_t to) {
    return levelDb(std::vector<float>(x.begin(), x.end()), from, to);
}

const sub::BuiltinInfo& gigatuneInfo() {
    for (const auto& info : sub::BuiltinRegistry::instance().devices())
        if (info.id == "gigatune") return info;
    REQUIRE(false);
    return sub::BuiltinRegistry::instance().devices().front();
}

}  // namespace

TEST_CASE("gigatune: listing") {
    const sub::BuiltinInfo& info = gigatuneInfo();
    CHECK_EQ(info.name, std::string("GigaTune"));
    CHECK(!info.isInstrument());
    std::vector<std::string> ids;
    for (const auto& p : info.params) ids.push_back(p.id);
    CHECK(ids == (std::vector<std::string>{"key", "scale", "speed", "strength", "pitch", "formant", "quality"}));
    CHECK(info.params[0].valueLabels == gt::keyLabels());
    CHECK(info.params[1].valueLabels == gt::scaleLabels());
    CHECK_EQ(info.params[0].defaultValue, 0.f);  // Song
    CHECK_EQ(info.params[1].defaultValue, 0.f);  // Song
    CHECK_EQ(info.params[3].defaultValue, 100.f);
    CHECK_EQ(info.params[4].stepCount(), 24);  // whole semitones
    CHECK(info.params[5].ownText);
    CHECK(info.params[6].valueLabels == gt::qualityLabels());
    CHECK_EQ(info.params[6].defaultValue, 1.f);  // High Quality
    CHECK(!info.params[6].automatable);          // (it sets the latency)

    // 21.3 ms with Low Latency (MAutoPitch's: 1024 samples at 48 kHz), about 277 ms with High Quality
    // (docs/guide/devices.md), the same time at any rate
    Standalone device("gigatune", kRate);
    gt::Corrector c;
    c.prepare(kRate);
    CHECK_EQ(device.processor().latencySamples(), c.latency(gt::Quality::HighQuality));
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        INFO(std::to_string(rate));
        gt::Corrector at;
        at.prepare(rate);
        const int low = at.latency(gt::Quality::LowLatency), high = at.latency(gt::Quality::HighQuality);
        CHECK(std::abs(low - 1024.0 / 48000.0 * rate) <= 0.5);
        CHECK(high > 0.27 * rate && high < 0.285 * rate);
    }
    CHECK_EQ(device.processor().paramText(5, 0.f), std::string("0 st"));
    CHECK_EQ(device.processor().paramText(5, 1.234f), std::string("+1.23 st"));
    CHECK_EQ(device.processor().paramText(5, -12.f), std::string("-12.00 st"));
}

TEST_CASE("gigatune: keys, scales and the notes chosen") {
    const auto bits = [](std::initializer_list<int> classes) {
        int b = 0;
        for (int c : classes) b |= 1 << c;
        return b;
    };
    CHECK_EQ(gt::scaleNotes(1, gt::Scale::Major, -1, false), bits({0, 2, 4, 5, 7, 9, 11}));          // C major
    CHECK_EQ(gt::scaleNotes(10, gt::Scale::Minor, -1, false), bits({0, 2, 4, 5, 7, 9, 11}));         // A minor
    CHECK_EQ(gt::scaleNotes(3, gt::Scale::Dorian, -1, false), bits({2, 4, 5, 7, 9, 11, 0}));         // D dorian
    CHECK_EQ(gt::scaleNotes(10, gt::Scale::Blues, -1, false), bits({9, 0, 2, 3, 4, 7}));             // A blues
    CHECK_EQ(gt::scaleNotes(5, gt::Scale::HarmonicMinor, -1, false), bits({4, 6, 7, 9, 11, 0, 3}));  // E harmonic minor
    CHECK_EQ(gt::scaleNotes(7, gt::Scale::Chromatic, -1, false), 0xfff);
    // Song: the song's tonic and mode, in Key or Scale; all twelve with none known
    CHECK_EQ(gt::scaleNotes(0, gt::Scale::Song, 9, true), bits({9, 11, 0, 2, 4, 5, 7}));      // A minor
    CHECK_EQ(gt::scaleNotes(0, gt::Scale::MajorPentatonic, 2, false), bits({2, 4, 6, 9, 11}));  // D pentatonic
    CHECK_EQ(gt::scaleNotes(4, gt::Scale::Song, 0, true), bits({3, 5, 6, 8, 10, 11, 1}));       // D# minor
    CHECK_EQ(gt::scaleNotes(0, gt::Scale::Major, -1, false), 0xfff);
    CHECK_EQ(gt::scaleNotes(5, gt::Scale::Song, -1, false), 0xfff);

    const int cMajor = gt::scaleNotes(1, gt::Scale::Major, -1, false);
    CHECK_EQ(gt::targetNote(61.1, cMajor, -1), 62);  // C#+10: D is nearer than C
    CHECK_EQ(gt::targetNote(61.0, cMajor, -1), 60);  // between: the lower
    CHECK_EQ(gt::targetNote(61.0, cMajor, 62), 62);  // the previous kept
    CHECK_EQ(gt::targetNote(60.59, 0xfff, 60), 60);  // within kHysteresis past the midpoint
    CHECK_EQ(gt::targetNote(60.61, 0xfff, 60), 61);
    CHECK_EQ(gt::targetNote(64.3, cMajor, 62), 64);  // a previous note far off: the nearest
    CHECK_EQ(gt::targetNote(55.2, 0, -1), 55);       // no notes: all twelve
}

TEST_CASE("gigatune: the tracker decodes as pYIN does") {
    // a vibrato, silence, a glide and noise: with the whole signal in, frame for frame as fastpitch's offline
    // pYIN (librosa's): voicing, pitch bin and candidate; decided 32 ms on, the same here; and decided as they come
    // (the corrector's), the same but for a few frames where the path turns
    for (const double rate : {44100.0, 48000.0}) {
        INFO(std::to_string(rate));
        std::vector<double> f;
        for (int i = 0; i < static_cast<int>(rate); ++i) f.push_back(220.0 * std::exp2(0.5 / 12.0 * std::sin(2 * kPi * 5.5 * i / rate)));
        std::vector<double> x = harmonic(f, rate);
        x.resize(x.size() + static_cast<size_t>(rate / 4), 0.0);
        std::vector<double> glide;
        for (int i = 0; i < static_cast<int>(rate); ++i) glide.push_back(150.0 * std::exp2(1.5 * i / rate));
        const auto g = harmonic(glide, rate);
        x.insert(x.end(), g.begin(), g.end());
        std::mt19937 rng(3);
        std::normal_distribution<double> nd(0.0, 0.05);
        for (int i = 0; i < static_cast<int>(rate / 2); ++i) x.push_back(nd(rng));

        const auto offline = gt::offlinePyin(x, rate);
        const auto decide = [&](int lag) {
            gt::PitchTracker t;
            t.prepare(rate, lag);
            CHECK_EQ(t.lag(), lag);
            std::vector<gt::PitchFrame> decided;
            for (double v : x)
                if (t.push(v)) decided.push_back(t.decided());
            for (int i = 0; i < t.frameLength() / 2; ++i)
                if (t.push(0.0)) decided.push_back(t.decided());
            t.flush(decided);
            return decided;
        };
        const auto differences = [&](const std::vector<gt::PitchFrame>& frames) {
            REQUIRE(frames.size() == offline.size());
            size_t differ = 0, voiced = 0;
            for (size_t i = 0; i < frames.size(); ++i) {
                CHECK_EQ(frames[i].index, static_cast<int64_t>(i));
                if (frames[i].voiced) ++voiced;
                if (frames[i].voiced != offline[i].voiced ||
                    (frames[i].voiced && (frames[i].bin != offline[i].bin || frames[i].candidate != offline[i].candidate)))
                    ++differ;
            }
            CHECK(voiced > frames.size() / 2);
            return differ;
        };
        CHECK_EQ(differences(track(x, rate)), size_t{0});
        CHECK_EQ(differences(decide(12)), size_t{0});
        CHECK(differences(decide(gt::kLag)) < offline.size() / 100);
    }
}

TEST_CASE("gigatune: the tracker refines the pitch to a hundredth of a cent") {
    // steady tones from C2 to past C7's octave below: pYIN's candidates are a few cents off, the refined pitch isn't
    // (High Quality: SwiftF0's pitch, refined the same way)
    for (const auto quality : {gt::Quality::LowLatency, gt::Quality::HighQuality})
    for (const double rate : {44100.0, 48000.0, 96000.0})
    for (const double f0 : {66.0, 110.0, 261.63, 440.0, 987.77, 2000.0}) {
        INFO(std::string(quality == gt::Quality::HighQuality ? "high quality, " : "") + std::to_string(rate) + " Hz rate, " +
             std::to_string(f0) + " Hz");
        const auto x = harmonic(steady(f0, 1.0, rate), rate);
        gt::PitchTracker t;
        t.prepare(rate);
        t.setQuality(quality);
        double worst = 0.0;
        int frames = 0;
        for (double v : x) {
            if (!t.push(v)) continue;
            const auto& f = t.decided();
            const double at = static_cast<double>(f.index * t.hop()) / rate;
            if (at < 0.2 || at > 0.6) continue;
            ++frames;
            CHECK(f.voiced);
            worst = std::max(worst, std::abs(cents(f.f0, f0)));
        }
        CHECK(frames > 100);
        CHECK(worst < 0.02);
    }
}

TEST_CASE("gigatune: nothing to do passes the input bit for bit") {
    // Strength 0, Pitch and Formant 0: voice (its pulses moved with the grains), noise and silence come out as
    // they went in, latency() samples later, both channels, at any rate and block size, at either quality (Low
    // Latency: a grain or two's rising half squeezed where the voice stops, what was planned before the frame
    // that heard it, and still the input exactly)
    for (const auto quality : {gt::Quality::LowLatency, gt::Quality::HighQuality})
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        INFO(std::string(quality == gt::Quality::HighQuality ? "high quality, " : "") + std::to_string(rate));
        std::vector<double> f;
        for (int i = 0; i < static_cast<int>(2 * rate); ++i) f.push_back(180.0 * std::exp2(0.7 / 12.0 * std::sin(2 * kPi * 4.0 * i / rate)));
        const auto voice = harmonic(f, rate);
        Samples left(voice.begin(), voice.end());
        std::mt19937 rng(5);
        std::normal_distribution<double> nd(0.0, 0.05);
        for (size_t i = left.size() * 6 / 10; i < left.size() * 7 / 10; ++i) left[i] = static_cast<float>(nd(rng));
        for (size_t i = left.size() * 8 / 10; i < left.size() * 85 / 100; ++i) left[i] = 0.f;
        Samples right = left;
        for (auto& v : right) v *= -0.5f;
        const Samples inLeft = left, inRight = right;
        gt::Corrector c;
        c.prepare(rate);
        gt::Controls controls;
        controls.strength = 0.0;
        controls.quality = quality;
        c.setControls(controls);
        for (size_t at = 0, block = 1; at < left.size(); at += block, block = block * 7 % 1013 + 1) {
            block = std::min(block, left.size() - at);
            float* p[2] = {left.data() + at, right.data() + at};
            c.process(p, 2, static_cast<int>(block));
        }
        const auto latency = static_cast<size_t>(c.latency());
        size_t differ = 0;
        for (size_t i = 0; i < left.size(); ++i) {
            const float wantLeft = i >= latency ? inLeft[i - latency] : 0.f, wantRight = i >= latency ? inRight[i - latency] : 0.f;
            if (left[i] != wantLeft || right[i] != wantRight) ++differ;
        }
        CHECK_EQ(differ, size_t{0});
        CHECK(c.voicedFrames() > 0);
        if (quality == gt::Quality::HighQuality) CHECK_EQ(c.lateSamples(), int64_t{0});
        else CHECK(c.lateSamples() <= static_cast<int64_t>(0.002 * rate));
    }
}

TEST_CASE("gigatune: notes land on their frequency") {
    // A+30 cents comes out at 440 Hz, to a hundredth of a cent: the output's period is its marks' spacing
    gt::Controls hard;
    const auto y = correct(harmonic(steady(hz(69) * std::exp2(30.0 / 1200.0), 3.0), kRate), kRate, hard);
    CHECK(std::abs(cents(peakFrequency(y, 48000, 96000, 440.0), 440.0)) < 0.01);
    int frames = 0;
    CHECK(worstCents(track(y, kRate), 440.0, 0.3, 2.6, kRate, &frames) < 0.01);
    CHECK(frames > 800);

    // in C major, C#4+10 cents goes to D4 (nearer than C4); in C# minor it stays C#
    gt::Controls cMajor;
    cMajor.notes = gt::scaleNotes(1, gt::Scale::Major, -1, false);
    const auto toD = correct(harmonic(steady(hz(61) * std::exp2(10.0 / 1200.0), 2.0), kRate), kRate, cMajor);
    CHECK(worstCents(track(toD, kRate), hz(62), 0.3, 1.6, kRate) < 0.01);
    gt::Controls csMinor;
    csMinor.notes = gt::scaleNotes(2, gt::Scale::Minor, -1, false);
    const auto toCs = correct(harmonic(steady(hz(61) * std::exp2(10.0 / 1200.0), 2.0), kRate), kRate, csMinor);
    CHECK(worstCents(track(toCs, kRate), hz(61), 0.3, 1.6, kRate) < 0.01);

    // the same with High Quality's tracker
    gt::Controls high;
    high.quality = gt::Quality::HighQuality;
    const auto yHigh = correct(harmonic(steady(hz(69) * std::exp2(30.0 / 1200.0), 3.0), kRate), kRate, high);
    CHECK(std::abs(cents(peakFrequency(yHigh, 48000, 96000, 440.0), 440.0)) < 0.01);
    CHECK(worstCents(track(yHigh, kRate), 440.0, 0.3, 2.6, kRate) < 0.01);

    // a vibrato about A+20 cents is held on A
    std::vector<double> f;
    for (int i = 0; i < static_cast<int>(3 * kRate); ++i) f.push_back(440.0 * std::exp2((20.0 + 30.0 * std::sin(2 * kPi * 5.0 * i / kRate)) / 1200.0));
    const auto flat = correct(harmonic(f, kRate), kRate, hard);
    CHECK(std::abs(cents(peakFrequency(flat, 48000, 96000, 440.0), 440.0)) < 0.01);
    CHECK(worstCents(track(flat, kRate), 440.0, 0.3, 2.6, kRate) < 0.6);  // (a window over a vibrato's correction)
}

TEST_CASE("gigatune: strength and pitch") {
    const auto in = harmonic(steady(hz(69) * std::exp2(30.0 / 1200.0), 2.0), kRate);
    gt::Controls half;
    half.strength = 0.5;
    CHECK(std::abs(cents(peakFrequency(correct(in, kRate, half), 48000, 48000, 440.0), 440.0) - 15.0) < 0.01);
    for (const int pitch : {7, -12, 12}) {
        INFO(std::to_string(pitch));
        gt::Controls moved;
        moved.pitch = pitch;
        const double want = 440.0 * std::exp2(pitch / 12.0);
        CHECK(std::abs(cents(peakFrequency(correct(in, kRate, moved), 48000, 48000, want), want)) < 0.01);
    }
    gt::Controls none;  // Strength 0 and Pitch: the pitch moved as it is
    none.strength = 0.0;
    none.pitch = 5;
    const double want = hz(74) * std::exp2(30.0 / 1200.0);
    CHECK(std::abs(cents(peakFrequency(correct(in, kRate, none), 48000, 48000, want), want)) < 0.01);
}

TEST_CASE("gigatune: retune speed is the correction's time constant") {
    // A+30 cents after silence: from where it is, onto A over Retune Speed (a one-pole); at 0 there at once
    std::vector<double> x(24000, 0.0);
    const auto tone = harmonic(steady(hz(69) * std::exp2(30.0 / 1200.0), 1.5), kRate);
    x.insert(x.end(), tone.begin(), tone.end());
    gt::Controls slow;
    slow.speedMs = 100.0;
    const auto frames = track(correct(x, kRate, slow), kRate);
    const auto offAt = [&](double seconds) {
        double sum = 0.0;
        int n = 0;
        for (const auto& f : frames) {
            const double t = static_cast<double>(f.index * gt::hopLength(kRate)) / kRate;
            if (f.voiced && std::abs(t - seconds) < 0.004) sum += cents(f.f0, 440.0), ++n;
        }
        REQUIRE(n > 0);
        return sum / n;
    };
    const double early = offAt(0.6), later = offAt(0.8);  // 100 and 300 ms into the note
    CHECK(early > 3.0 && early < 20.0);
    CHECK(std::abs(later / early - std::exp(-2.0)) < 0.03);  // e^-(200 ms / 100 ms)
    CHECK(std::abs(offAt(1.4)) < 0.05);

    gt::Controls hard;  // (from when the measuring window has the note alone)
    CHECK(worstCents(track(correct(x, kRate, hard), kRate), 440.0, 0.56, 1.5, kRate) < 0.01);

    // a slow speed keeps a vibrato (most of it), where 0 flattens it
    std::vector<double> f;
    for (int i = 0; i < static_cast<int>(3 * kRate); ++i) f.push_back(440.0 * std::exp2(40.0 * std::sin(2 * kPi * 5.5 * i / kRate) / 1200.0));
    gt::Controls slowest;
    slowest.speedMs = gt::kSpeedMaxMs;
    double low = 1e9, high = -1e9;
    for (const auto& fr : track(correct(harmonic(f, kRate), kRate, slowest), kRate)) {
        const double t = static_cast<double>(fr.index * gt::hopLength(kRate)) / kRate;
        if (!fr.voiced || t < 1.0 || t > 2.5) continue;
        low = std::min(low, cents(fr.f0, 440.0));
        high = std::max(high, cents(fr.f0, 440.0));
    }
    CHECK(high - low > 60.0);  // of 80
}

TEST_CASE("gigatune: formant moves the envelope and pitch keeps it") {
    // a 150 Hz tone whose harmonics follow a resonance at 1 kHz: Formant moves the resonance, Pitch the harmonics
    std::vector<double> x(static_cast<size_t>(2.0 * kRate), 0.0);
    for (int h = 1; h * 150.0 < 8000.0; ++h) {
        const double g = 0.1 / (1.0 + std::pow((h * 150.0 - 1000.0) / 150.0, 2));
        for (size_t i = 0; i < x.size(); ++i) x[i] += g * std::sin(2 * kPi * h * 150.0 * i / kRate + h);
    }
    // where the harmonics' envelope peaks: a parabola through the strongest harmonic's level and its neighbours'
    const auto envelopePeak = [&](const std::vector<float>& y, double f0) {
        const Samples s(y.begin(), y.end());
        std::vector<double> level;
        for (int h = 1; h * f0 < 6000.0; ++h) level.push_back(std::log(std::abs(transformAt(s, h * f0, 24000, 48000))));
        const auto at = static_cast<size_t>(std::max_element(level.begin(), level.end()) - level.begin());
        if (at == 0 || at + 1 >= level.size()) return static_cast<double>(at + 1) * f0;
        const double offset = 0.5 * (level[at - 1] - level[at + 1]) / (level[at - 1] - 2 * level[at] + level[at + 1]);
        return (static_cast<double>(at + 1) + offset) * f0;
    };
    for (const double formant : {0.0, 7.0, 12.0, -12.0})
    for (const int pitch : {0, 12, -7}) {
        INFO("formant " + std::to_string(formant) + ", pitch " + std::to_string(pitch));
        gt::Controls c;
        c.strength = 0.0;
        c.pitch = pitch;
        c.formant = formant;
        const auto y = correct(x, kRate, c);
        const double f0 = 150.0 * std::exp2(pitch / 12.0);
        CHECK(std::abs(cents(peakFrequency(y, 24000, 48000, f0), f0)) < 0.05);
        CHECK(std::abs(cents(envelopePeak(y, f0), 1000.0 * std::exp2(formant / 12.0))) < 150.0);
    }
}

TEST_CASE("gigatune: the level holds whatever the pulses' phase") {
    // grains are centred on a voice's pulses wherever they fall: a pulse train moved by parts of a period
    // comes out as loud, a few semitones up or down
    for (const double shift : {0.0, 0.25, 0.5, 0.75})
    for (const int pitch : {-5, 1, 5}) {
        INFO("shift " + std::to_string(shift) + ", pitch " + std::to_string(pitch));
        std::vector<double> x(static_cast<size_t>(1.5 * kRate), 0.0);
        for (int h = 1; h * 150.0 < 5000.0; ++h) {
            const double g = 0.05 / (1.0 + std::pow((h * 150.0 - 700.0) / 400.0, 2));
            for (size_t i = 0; i < x.size(); ++i) x[i] += g * std::cos(2 * kPi * h * 150.0 * (i / kRate - shift / 150.0));
        }
        gt::Controls c;
        c.strength = 0.0;
        c.pitch = pitch;
        CHECK(std::abs(levelDb(correct(x, kRate, c), 24000, 60000) - levelDb(x, 24000, 60000)) < 0.6);
    }
}

TEST_CASE("gigatune: grains are never late") {
    // High Quality's latency covers the longest periods and grains the settings allow: a low and a high voice,
    // Pitch and Formant at their ends, a gap and noise, at 44.1 to 192 kHz. Low Latency's grains start in time but
    // for a few planned before a frame changed them (the voice stopped, a long period's grain read slowly), their
    // rising halves squeezed into the time left; a C2 voice, its pitch or formants an octave down, the most.
    for (const auto quality : {gt::Quality::LowLatency, gt::Quality::HighQuality})
    for (const double rate : {44100.0, 96000.0, 192000.0})
    for (const double f0 : {66.0, 1900.0})
    for (const auto& [pitch, formant] : std::vector<std::pair<double, double>>{{-12, -12}, {12, -12}, {-12, 12}, {12, 12}}) {
        INFO(std::string(quality == gt::Quality::HighQuality ? "high quality, " : "") + std::to_string(rate) + " " +
             std::to_string(f0) + " " + std::to_string(pitch) + " " + std::to_string(formant));
        std::vector<double> f;
        for (int i = 0; i < static_cast<int>(0.8 * rate); ++i) f.push_back(f0 * std::exp2(0.8 * std::sin(2 * kPi * 1.3 * i / rate) / 12.0));
        auto x = harmonic(f, rate);
        x.resize(x.size() + static_cast<size_t>(0.1 * rate), 0.0);
        std::mt19937 rng(9);
        std::normal_distribution<double> nd(0.0, 0.1);
        for (int i = 0; i < static_cast<int>(0.2 * rate); ++i) x.push_back(nd(rng));
        const std::vector<double> again(x.begin(), x.begin() + static_cast<long>(0.3 * rate));
        x.insert(x.end(), again.begin(), again.end());
        gt::Controls c;
        c.pitch = pitch;
        c.formant = formant;
        c.quality = quality;
        int64_t late = -1;
        const auto y = correct(x, rate, c, &late);
        if (quality == gt::Quality::HighQuality) CHECK_EQ(late, int64_t{0});
        else CHECK(late < static_cast<int64_t>(0.03 * static_cast<double>(y.size())));
        CHECK(allFinite(y));
        CHECK(maxAbs(y) < 2.0);
    }
}

TEST_CASE("gigatune: quality switched while playing") {
    // the corrector starts again at the other quality's latency: from latency() after the switch, the input as it
    // went in (Strength 0), that much later; the device asks the engine to realign the tracks once
    std::vector<double> f;
    for (int i = 0; i < static_cast<int>(3 * kRate); ++i) f.push_back(200.0 * std::exp2(0.5 / 12.0 * std::sin(2 * kPi * 3.0 * i / kRate)));
    const auto voice = harmonic(f, kRate);
    std::vector<float> y(voice.begin(), voice.end());
    gt::Corrector c;
    c.prepare(kRate);
    gt::Controls controls;
    controls.strength = 0.0;
    c.setControls(controls);
    const size_t switchAt = 256 * 188;  // (a block's start, about a second in)
    for (size_t at = 0; at < y.size(); at += 256) {
        if (at == switchAt) {
            controls.quality = gt::Quality::HighQuality;
            c.setControls(controls);
        }
        float* p[1] = {y.data() + at};
        c.process(p, 1, static_cast<int>(std::min<size_t>(256, y.size() - at)));
    }
    const auto high = static_cast<size_t>(c.latency(gt::Quality::HighQuality));
    CHECK_EQ(c.latency(), static_cast<int>(high));
    size_t differ = 0;
    for (size_t i = switchAt + high; i < y.size(); ++i) differ += y[i] != static_cast<float>(voice[i - high]) ? 1 : 0;
    CHECK_EQ(differ, size_t{0});
    CHECK_EQ(c.lateSamples(), int64_t{0});

    Standalone device("gigatune", kRate);
    CHECK(!device.processor().idle());
    device.set("quality", 0.f);
    CHECK_EQ(device.processor().latencySamples(), c.latency(gt::Quality::LowLatency));
    CHECK(device.processor().idle());  // realign
    CHECK(!device.processor().idle());
}

TEST_CASE("gigatune: silence and noise") {
    gt::Controls hard;
    hard.formant = 3.0;
    const auto quiet = correct(std::vector<double>(48000, 0.0), kRate, hard);
    CHECK(allEqual(quiet, 0.0));
    // noise is unvoiced (but for a few frames pYIN hears a pitch in): its grains play where they were, as they were
    const Samples n = noise(96000, 7, 0.2f);
    const std::vector<double> x(n.begin(), n.end());
    const auto y = correct(x, kRate, hard);
    CHECK(allFinite(y));
    CHECK(std::abs(levelDb(y, 24000, 72000) - levelDb(x, 24000, 72000)) < 0.5);
}

TEST_CASE("gigatune: the song's key through the device") {
    // Key and Scale at Song: the context's key (A minor: G#+20 cents goes to A, not G#); without one, chromatic
    const auto in = harmonic(steady(hz(68) * std::exp2(20.0 / 1200.0), 2.0), kRate);
    for (const int tonic : {9, -1}) {
        INFO(std::to_string(tonic));
        Standalone device("gigatune", kRate, {{"speed", 0.f}});
        device.context().keyTonic = tonic;
        device.context().keyMinor = true;
        const int latency = device.processor().latencySamples();
        Samples x(in.begin(), in.end());
        x.resize(x.size() + static_cast<size_t>(latency), 0.f);
        Samples left = x, right = x;
        device.run({&left, &right});
        CHECK(left == right);
        const std::vector<float> y(left.begin() + latency, left.end());
        const double want = tonic >= 0 ? hz(69) : hz(68);
        CHECK(std::abs(cents(peakFrequency(y, 48000, 48000, want), want)) < 0.01);
    }
}

TEST_CASE("gigatune: the editor's displays") {
    // A4 +30 cents after half a second of silence: the pitch heard, where the correction takes it (Strength's share)
    // and the note, a value per 256 samples out, voiced from when the voice comes out (the latency on)
    const auto tone = harmonic(steady(hz(69) * std::exp2(30.0 / 1200.0), 1.5), kRate);
    const size_t silence = static_cast<size_t>(0.5 * kRate);
    for (const float quality : {0.f, 1.f}) {
        for (const float strength : {100.f, 50.f}) {
            INFO(std::to_string(quality) + " " + std::to_string(strength));
            Standalone device("gigatune", kRate, {{"speed", 0.f}, {"strength", strength}, {"quality", quality}});
            const int latency = device.processor().latencySamples();
            Samples x(silence, 0.f);
            x.insert(x.end(), tone.begin(), tone.end());
            x.resize(x.size() + static_cast<size_t>(latency + 0.1 * kRate), 0.f);
            device.run({&x});
            const std::vector<float> pitch = device.display("pitch"), corrected = device.display("corrected"),
                                     note = device.display("note");
            REQUIRE(pitch.size() == x.size() / 256);
            REQUIRE(corrected.size() == pitch.size());
            REQUIRE(note.size() == pitch.size());
            size_t first = pitch.size();
            for (size_t i = 0; i < pitch.size() && first == pitch.size(); ++i)
                if (pitch[i] > 0.f) first = i;
            REQUIRE(first < pitch.size());
            const double comes = static_cast<double>(first * 256 + 255) - static_cast<double>(silence + latency);
            CHECK(std::abs(comes) < 0.04 * kRate);
            for (size_t i = 0; i < first; ++i) CHECK_EQ(note[i], -1.f);
            // held: from a quarter of a second in to a quarter before the end
            const size_t from = (silence + static_cast<size_t>(latency) + static_cast<size_t>(0.25 * kRate)) / 256;
            const size_t to = (silence + static_cast<size_t>(latency) + tone.size() - static_cast<size_t>(0.25 * kRate)) / 256;
            for (size_t i = from; i < to; ++i) {
                CHECK_EQ(note[i], 69.f);
                CHECK(std::abs(pitch[i] - 69.3) < 0.005);
                CHECK(std::abs(corrected[i] - (69.3 - 0.3 * strength / 100.0)) < 0.005);
            }
            // nothing heard once the tone has gone out
            CHECK_EQ(note.back(), -1.f);
            CHECK_EQ(pitch.back(), 0.f);
        }
    }
}

// --- Real vocals (with GIGATUNE_VOCALS) ---------------------------------------------------------------

namespace {

void writeWav(const std::string& path, const std::vector<float>& x, double rate) {
    std::ofstream f(path, std::ios::binary);
    const auto put32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<uint32_t>(x.size() * 4);
    f.write("RIFF", 4);
    put32(36 + bytes);
    f.write("WAVEfmt ", 8);
    put32(16);
    put16(3);  // float
    put16(1);
    put32(static_cast<uint32_t>(rate));
    put32(static_cast<uint32_t>(rate) * 4);
    put16(4);
    put16(32);
    f.write("data", 4);
    put32(bytes);
    f.write(reinterpret_cast<const char*>(x.data()), bytes);
}

// "..._C#m_dry.wav": the notes of C# minor (all twelve if the name has no key).
int keyOfName(const std::string& name) {
    static const char* const kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const std::pair<const char*, int> kFlats[] = {{"Db", 1}, {"Eb", 3}, {"Gb", 6}, {"Ab", 8}, {"Bb", 10}};
    const auto end = name.rfind('_');
    const auto start = end == std::string::npos || end == 0 ? std::string::npos : name.rfind('_', end - 1);
    if (start == std::string::npos) return 0xfff;
    std::string key = name.substr(start + 1, end - start - 1);
    const bool minor = !key.empty() && key.back() == 'm';
    if (minor) key.pop_back();
    int tonic = -1;
    for (int i = 0; i < 12; ++i)
        if (key == kNames[i]) tonic = i;
    for (const auto& [flat, value] : kFlats)
        if (key == flat) tonic = value;
    return tonic < 0 ? 0xfff : gt::scaleNotes(tonic + 1, minor ? gt::Scale::Minor : gt::Scale::Major, -1, false);
}

}  // namespace

TEST_CASE("gigatune: real vocals") {
    // Each phrase: passed bit for bit with nothing to do; corrected to its key, the output's pitch against the
    // note the corrector chose (its own tracker and targets), away from note changes and voicing's edges.
    const char* dir = std::getenv("GIGATUNE_VOCALS");
    if (!dir) SKIP("GIGATUNE_VOCALS not set");
    const char* out = std::getenv("GIGATUNE_OUT");
    std::vector<double> all;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".wav") continue;
        const std::string name = entry.path().filename().string();
        INFO(name);
        const auto source = sub::AudioSource::load(entry.path().string(), static_cast<uint32_t>(kRate));
        std::vector<double> x(static_cast<size_t>(source->frames()), 0.0);
        for (uint32_t c = 0; c < source->channels(); ++c)
            for (size_t i = 0; i < x.size(); ++i) x[i] += source->channelData(c)[i] / source->channels();
        const int notes = keyOfName(name);

        gt::Controls neutral;
        neutral.strength = 0.0;
        int64_t late = -1;
        const auto same = correct(x, kRate, neutral, &late);
        size_t differ = 0;
        for (size_t i = 0; i < x.size(); ++i) differ += same[i] != static_cast<float>(x[i]) ? 1 : 0;
        CHECK_EQ(differ, size_t{0});
        CHECK(late < static_cast<int64_t>(0.01 * static_cast<double>(x.size())));  // (Low Latency: squeezed, as above)

        gt::PitchTracker tracker;
        tracker.prepare(kRate);
        std::vector<int> target;
        int previous = -1;
        for (double v : x) {
            if (!tracker.push(v)) continue;
            const auto& f = tracker.decided();
            previous = f.voiced ? gt::targetNote(69.0 + 12.0 * std::log2(f.f0 / 440.0), notes, previous) : -1;
            target.push_back(previous);
        }
        gt::Controls hard;
        hard.notes = notes;
        const auto tuned = correct(x, kRate, hard, &late);
        CHECK(late < static_cast<int64_t>(0.01 * static_cast<double>(x.size())));
        const auto frames = track(tuned, kRate);
        const auto guard = static_cast<size_t>(tracker.frameLength() / 2 / tracker.hop() + 2);  // frames a measuring window reaches
        std::vector<double> errors;
        for (size_t i = guard; i + guard < std::min(target.size(), frames.size()); ++i) {
            bool held = frames[i].voiced;
            for (size_t k = i - guard; k <= i + guard; ++k) held = held && target[k] == target[i] && target[i] >= 0;
            if (!held) continue;
            const double c = std::abs(cents(frames[i].f0, hz(target[i])));
            if (c < 600.0) errors.push_back(c);  // (an octave off: the measuring tracker's)
        }
        std::sort(errors.begin(), errors.end());
        all.insert(all.end(), errors.begin(), errors.end());
        if (!errors.empty())
            std::printf("  %-55s %5zu frames held: median %.2f c, 90 %% within %.2f c\n", name.c_str(), errors.size(),
                        errors[errors.size() / 2], errors[errors.size() * 9 / 10]);
        if (out) {
            const std::string stem = std::string(out) + "/" + entry.path().stem().string();
            writeWav(stem + "_0_dry.wav", std::vector<float>(x.begin(), x.end()), kRate);
            writeWav(stem + "_1_hard.wav", tuned, kRate);
            gt::Controls natural = hard;
            natural.speedMs = 60.0;
            writeWav(stem + "_2_speed60.wav", correct(x, kRate, natural), kRate);
            gt::Controls up = hard;
            up.pitch = 5;
            writeWav(stem + "_3_pitch+5.wav", correct(x, kRate, up), kRate);
            gt::Controls deeper = hard;
            deeper.formant = -3.0;
            writeWav(stem + "_4_formant-3.wav", correct(x, kRate, deeper), kRate);
        }
    }
    REQUIRE(!all.empty());
    std::sort(all.begin(), all.end());
    std::printf("  all: %zu frames held, median %.2f c, 90 %% within %.2f c, 99 %% within %.2f c\n", all.size(), all[all.size() / 2],
                all[all.size() * 9 / 10], all[all.size() * 99 / 100]);
    CHECK(all[all.size() / 2] < 2.0);
}
