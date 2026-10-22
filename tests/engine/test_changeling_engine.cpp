// The built-in Changeling (a pitch and formant shifter for a single voice): its listing and latencies; nothing to do
// passing the input bit for bit at every quality, rate and block size; Transpose landing on its frequency to a
// hundredth of a cent; Quantize and Robot (and their notes); MIDI notes, at the input's time and held; Glide's
// time; Formant and Link moving the envelope; Harmonics taking the lowest harmonics away and keeping the rest; the
// twin; Detune and Width; Smear; silence and noise; the extremes (finite, bounded, never late at High Quality); the
// quality switched while playing; Mix; the drive (LAB's curve, its 0 the input exactly); the quadrature pair; the
// glottal closures (SEDREAMS); elastic grains; the editor's displays.

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "builtin/BuiltinRegistry.h"
#include "builtin/ChangelingDesign.h"
#include "builtin/GigaTuneDesign.h"
#include "harness/Signal.h"
#include "harness/Standalone.h"
#include "harness/Test.h"

using namespace subtest;
namespace cl = sub::changeling;
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

// Harmonics all as loud (a band-limited pulse train), at f0.
std::vector<double> pulses(double f0, double seconds, double rate = kRate, double amplitude = 0.02) {
    std::vector<double> x(static_cast<size_t>(seconds * rate), 0.0);
    for (int h = 1; h * f0 < 0.45 * rate; ++h)
        for (size_t i = 0; i < x.size(); ++i) x[i] += amplitude * std::cos(2.0 * kPi * h * f0 * static_cast<double>(i) / rate);
    return x;
}

std::vector<double> steady(double freq, double seconds, double rate = kRate) {
    return std::vector<double>(static_cast<size_t>(seconds * rate), freq);
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
double hz(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }

// The shifter over one or two channels in blocks of 256, the latency taken off; notes as (second, key).
struct Shifted {
    std::vector<float> left, right;
    int64_t late = 0;
};
Shifted shift(const std::vector<double>& x, double rate, const cl::Controls& controls, bool stereo = false,
              const std::vector<std::pair<double, int>>& notes = {}) {
    cl::Shifter s;
    s.prepare(rate);
    s.setControls(controls);
    const int latency = s.latency(controls.quality);
    Shifted out;
    out.left.assign(x.begin(), x.end());
    out.left.resize(out.left.size() + static_cast<size_t>(latency), 0.f);
    out.right = out.left;
    for (size_t at = 0; at < out.left.size(); at += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, out.left.size() - at));
        std::vector<cl::NoteEvent> events;
        for (const auto& [second, key] : notes) {
            const auto frame = static_cast<int64_t>(second * rate);
            if (frame >= static_cast<int64_t>(at) && frame < static_cast<int64_t>(at) + n)
                events.push_back({static_cast<int>(frame - static_cast<int64_t>(at)), key});
        }
        float* p[2] = {out.left.data() + at, out.right.data() + at};
        s.process(p, stereo ? 2 : 1, n, events.data(), static_cast<int>(events.size()));
    }
    REQUIRE(s.latency() == latency);
    out.late = s.lateSamples();
    out.left.erase(out.left.begin(), out.left.begin() + latency);
    out.right.erase(out.right.begin(), out.right.begin() + latency);
    if (!stereo) out.right.clear();
    return out;
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
    for (double f = guess * 0.97; f <= guess * 1.03; f += guess * 0.001) {
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

// The pitch (MIDI) of y about `at` seconds, near `guess` (MIDI): its fundamental's peak over 40 ms.
double pitchAt(const std::vector<float>& y, double at, double guess, double rate = kRate) {
    const auto length = static_cast<int64_t>(0.04 * rate);
    const auto from = std::max<int64_t>(0, static_cast<int64_t>(at * rate) - length / 2);
    return 69.0 + 12.0 * std::log2(peakFrequency(y, from, length, hz(guess), rate) / 440.0);
}
// The same between `low` and `high` (MIDI): the strongest line there first, a cent apart, then refined.
double pitchBetween(const std::vector<float>& y, double at, double low, double high, double rate = kRate) {
    const auto length = static_cast<int64_t>(0.04 * rate);
    const auto from = std::max<int64_t>(0, static_cast<int64_t>(at * rate) - length / 2);
    const Samples s(y.begin() + from, y.begin() + from + length);
    double best = low, bestMagnitude = -1.0;
    for (double p = low; p <= high; p += 0.01) {
        const double m = windowedAmplitude(s, hz(p), 0, length, rate);
        if (m > bestMagnitude) bestMagnitude = m, best = p;
    }
    return pitchAt(y, at, best, rate);
}

double levelDb(const std::vector<float>& y, int64_t from, int64_t to) { return rmsDb(Samples(y.begin(), y.end()), from, to); }
double levelDb(const std::vector<double>& x, int64_t from, int64_t to) {
    return levelDb(std::vector<float>(x.begin(), x.end()), from, to);
}
// A harmonic's level (dB) in y over [from, from + length).
double lineDb(const std::vector<float>& y, double freq, int64_t from, int64_t length) {
    return db(windowedAmplitude(Samples(y.begin(), y.end()), freq, from, length));
}

const sub::BuiltinInfo& changelingInfo() {
    for (const auto& info : sub::BuiltinRegistry::instance().devices())
        if (info.id == "changeling") return info;
    REQUIRE(false);
    return sub::BuiltinRegistry::instance().devices().front();
}

}  // namespace

TEST_CASE("changeling: listing") {
    const sub::BuiltinInfo& info = changelingInfo();
    CHECK_EQ(info.name, std::string("Changeling"));
    CHECK(!info.isInstrument());
    std::vector<std::string> ids;
    for (const auto& p : info.params) ids.push_back(p.id);
    CHECK(ids == (std::vector<std::string>{"mode", "pitch", "formant", "link", "glide", "key", "scale", "drive", "mix",
                                           "output", "harmonics", "twin", "interval", "detune", "width", "smear",
                                           "quality"}));
    CHECK(info.params[0].valueLabels == cl::modeLabels());
    CHECK_EQ(info.params[1].minValue, -24.f);
    CHECK_EQ(info.params[1].maxValue, 24.f);
    CHECK_EQ(info.params[1].stepCount(), 0);  // continuous, as Little AlterBoy's
    CHECK(info.params[1].ownText);
    CHECK(info.params[5].valueLabels == gt::keyLabels());
    CHECK(info.params[6].valueLabels == gt::scaleLabels());
    CHECK_EQ(info.params[6].defaultValue, static_cast<float>(gt::Scale::Chromatic));  // LAB's Quantize
    CHECK_EQ(info.params[8].defaultValue, 100.f);
    CHECK_EQ(info.params[12].defaultValue, -12.f);
    CHECK_EQ(info.params[12].stepCount(), 48);
    CHECK(info.params[16].valueLabels == cl::qualityLabels());
    CHECK_EQ(info.params[16].defaultValue, 1.f);  // Studio
    CHECK(!info.params[16].automatable);

    // Live 21.3 ms (GigaTune's Low Latency), Studio kStudioLag hops more (about Little AlterBoy's 51 ms), High
    // Quality about 300 ms, the same times at any rate; the device adds its drive's oversampling (31 samples)
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        INFO(std::to_string(rate));
        cl::Shifter s;
        s.prepare(rate);
        const int live = s.latency(cl::Quality::Live), studio = s.latency(cl::Quality::Studio),
                  high = s.latency(cl::Quality::HighQuality);
        CHECK(std::abs(live - 1024.0 / 48000.0 * rate) <= 0.5);
        CHECK_EQ(studio, live + cl::kStudioLag * gt::hopLength(rate));
        CHECK(studio > 0.045 * rate && studio < 0.052 * rate);
        CHECK(high > 0.27 * rate && high < 0.33 * rate);
    }
    Standalone device("changeling", kRate);
    cl::Shifter s;
    s.prepare(kRate);
    cl::Drive drive;
    drive.prepare(kRate, 256);
    CHECK_EQ(drive.latency(), 31);
    CHECK_EQ(device.processor().latencySamples(), s.latency(cl::Quality::Studio) + drive.latency());
    CHECK(device.processor().acceptsMidi());
    CHECK_EQ(device.processor().paramText(1, 0.f), std::string("0 st"));
    CHECK_EQ(device.processor().paramText(1, 2.4f), std::string("+2.40 st"));
    CHECK_EQ(device.processor().paramText(2, -12.f), std::string("-12.00 st"));
    CHECK_EQ(device.processor().paramText(10, 1.5f), std::string("+1.50"));
}

TEST_CASE("changeling: nothing to do passes the input bit for bit") {
    // Pitch, Formant and the rest at 0: voice, noise and silence come out as they went in, latency() samples
    // later, both channels (or one), at any rate and block size, at every quality; through the device too (its drive
    // at 0 a delay, its Mix at 100 % the wet signal exactly)
    for (const auto quality : {cl::Quality::Live, cl::Quality::Studio, cl::Quality::HighQuality})
    for (const double rate : {44100.0, 48000.0, 96000.0})
    for (const int channels : {1, 2}) {
        INFO(std::to_string(static_cast<int>(quality)) + " " + std::to_string(rate) + " " + std::to_string(channels));
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
        cl::Shifter s;
        s.prepare(rate);
        cl::Controls controls;
        controls.quality = quality;
        s.setControls(controls);
        for (size_t at = 0, block = 1; at < left.size(); at += block, block = block * 7 % 1013 + 1) {
            block = std::min(block, left.size() - at);
            float* p[2] = {left.data() + at, right.data() + at};
            s.process(p, channels, static_cast<int>(block));
        }
        const auto latency = static_cast<size_t>(s.latency());
        size_t differ = 0, first = 0;
        for (size_t i = 0; i < left.size(); ++i) {
            const float wantLeft = i >= latency ? inLeft[i - latency] : 0.f, wantRight = i >= latency ? inRight[i - latency] : 0.f;
            if (left[i] != wantLeft || (channels == 2 && right[i] != wantRight))
                if (differ++ == 0) first = i;
        }
        INFO("first differing sample " + std::to_string(first) + " of " + std::to_string(left.size()));
        CHECK_EQ(differ, size_t{0});
        CHECK(s.voicedFrames() > 0);
        if (quality == cl::Quality::HighQuality) CHECK_EQ(s.lateSamples(), int64_t{0});
    }
    // the device, two channels in blocks of 256 (automation's splits too)
    Standalone device("changeling", kRate);
    const auto voice = harmonic(steady(200.0, 1.5), kRate);
    const int latency = device.processor().latencySamples();
    Samples left(voice.begin(), voice.end());
    left.resize(left.size() + static_cast<size_t>(latency), 0.f);
    Samples right = left;
    for (auto& v : right) v *= 0.25f;
    const Samples inLeft = left, inRight = right;
    device.run({&left, &right}, {{20000, "output", 0.f}});
    size_t differ = 0;
    for (size_t i = static_cast<size_t>(latency); i < left.size(); ++i)
        differ += (left[i] != inLeft[i - static_cast<size_t>(latency)] || right[i] != inRight[i - static_cast<size_t>(latency)]) ? 1 : 0;
    CHECK_EQ(differ, size_t{0});
}

TEST_CASE("changeling: transpose lands on the frequency") {
    // a steady tone moved by whole and fractional semitones comes out at its frequency to a hundredth of a cent,
    // every quality, the level within a dB or two up to a fifth (further up, a falling spectrum's harmonics thin: this
    // tone's 1/h loses 3 dB an octave)
    for (const auto quality : {cl::Quality::Live, cl::Quality::Studio, cl::Quality::HighQuality})
    for (const double pitch : {-12.0, -5.0, 0.37, 7.0, 12.0, 19.5}) {
        INFO(std::to_string(static_cast<int>(quality)) + " " + std::to_string(pitch));
        const auto x = harmonic(steady(180.0, 1.5), kRate);
        cl::Controls c;
        c.pitch = pitch;
        c.quality = quality;
        const auto y = shift(x, kRate, c).left;
        const double want = 180.0 * std::exp2(pitch / 12.0);
        CHECK(std::abs(cents(peakFrequency(y, 24000, 24000, want), want)) < 0.01);
        if (std::abs(pitch) <= 7.0) CHECK(std::abs(levelDb(y, 24000, 60000) - levelDb(x, 24000, 60000)) < 2.0);
    }
    // From the start (a reset), a long Glide doesn't scoop into the first note: Transpose and Robot are on it at once
    const auto x = harmonic(steady(hz(57.0), 1.0), kRate);
    for (const auto mode : {cl::Mode::Transpose, cl::Mode::Robot}) {
        INFO(std::to_string(static_cast<int>(mode)));
        cl::Controls c;
        c.mode = mode;
        c.pitch = 5.0;
        c.glideMs = 400.0;
        const double want = mode == cl::Mode::Robot ? 65.0 : 62.0;
        CHECK(std::abs(pitchAt(shift(x, kRate, c).left, 0.1, want) - want) < 0.02);
    }
}

TEST_CASE("changeling: quantize and robot") {
    // Quantize: A3 +30 cents with a vibrato comes out on A3 itself (the vibrato gone), plus Pitch; the nearest of the
    // scale's notes. Robot: C4 plus Pitch, whatever is sung (as Little AlterBoy's)
    std::vector<double> f;
    for (int i = 0; i < static_cast<int>(1.5 * kRate); ++i)
        f.push_back(hz(57.3) * std::exp2(0.15 / 12.0 * std::sin(2 * kPi * 5.5 * i / kRate)));
    const auto x = harmonic(f, kRate);
    const auto check = [&](cl::Controls c, double want) {
        const auto y = shift(x, kRate, c).left;
        double worst = 0.0;
        for (double at = 0.4; at < 1.3; at += 0.05) worst = std::max(worst, std::abs(pitchAt(y, at, want) - want));
        return worst;
    };
    cl::Controls q;
    q.mode = cl::Mode::Quantize;
    q.glideMs = 0.0;
    CHECK(check(q, 57.0) < 0.02);
    q.pitch = 2.4;
    CHECK(check(q, 59.4) < 0.02);
    q.pitch = 0.0;
    q.notes = gt::scaleNotes(1, gt::Scale::MajorPentatonic, -1, false);  // C D E G A: A3 is in it
    CHECK(check(q, 57.0) < 0.02);
    q.notes = gt::scaleNotes(1, gt::Scale::Major, -1, false) & ~(1 << 9);  // no A: B is nearer than G
    CHECK(check(q, 59.0) < 0.02);
    cl::Controls r;
    r.mode = cl::Mode::Robot;
    CHECK(check(r, 60.0) < 0.02);
    r.pitch = -3.5;
    CHECK(check(r, 56.5) < 0.02);
}

TEST_CASE("changeling: MIDI notes") {
    // Robot: the voice on the last note played, from its time in the input (the latency taken off), held past its
    // note-off; Transpose: moved by the last note's distance from C4 (Little AlterBoy's); Glide's time between them
    const auto x = harmonic(steady(hz(50), 2.0), kRate);
    const std::vector<std::pair<double, int>> notes = {{0.5, 55}, {1.2, 62}};
    cl::Controls robot;
    robot.mode = cl::Mode::Robot;
    robot.glideMs = 0.0;
    const auto y = shift(x, kRate, robot, false, notes).left;
    CHECK(std::abs(pitchAt(y, 0.3, 60) - 60.0) < 0.02);  // none yet: C4
    CHECK(std::abs(pitchAt(y, 0.8, 55) - 55.0) < 0.02);
    CHECK(std::abs(pitchAt(y, 1.6, 62) - 62.0) < 0.02);  // (held)
    // the note takes over where it was played: within a period or two of 1.2 s
    CHECK(std::abs(pitchAt(y, 1.2 + 0.03, 62) - 62.0) < 0.05);
    CHECK(std::abs(pitchAt(y, 1.2 - 0.03, 55) - 55.0) < 0.05);

    cl::Controls transpose;
    transpose.glideMs = 0.0;
    const auto t = shift(x, kRate, transpose, false, notes).left;
    CHECK(std::abs(pitchAt(t, 0.3, 50) - 50.0) < 0.02);
    CHECK(std::abs(pitchAt(t, 0.8, 45) - 45.0) < 0.02);
    CHECK(std::abs(pitchAt(t, 1.6, 52) - 52.0) < 0.02);

    // Glide: 10 % to 90 % of the move from 55 to 62 in about Glide's time (two one-poles: within a frame or two)
    for (const double ms : {30.0, 120.0}) {
        INFO(std::to_string(ms));
        robot.glideMs = ms;
        const auto g = shift(x, kRate, robot, false, notes).left;
        double t10 = 0.0, t90 = 0.0;
        for (double at = 1.15; at < 1.6; at += 0.001) {
            const double p = (pitchBetween(g, at, 54.0, 63.0) - 55.0) / 7.0;
            if (t10 == 0.0 && p > 0.1) t10 = at;
            if (t90 == 0.0 && p > 0.9) t90 = at;
        }
        INFO("10 % to 90 % in " + std::to_string((t90 - t10) * 1000.0) + " ms");
        CHECK(std::abs((t90 - t10) * 1000.0 - ms) < 0.25 * ms + 12.0);
    }
}

TEST_CASE("changeling: formant and link") {
    // a 150 Hz tone whose harmonics follow a resonance at 1 kHz: Formant moves the resonance and not the pitch;
    // Link moves it with the pitch (and Formant on top)
    std::vector<double> x(static_cast<size_t>(2.0 * kRate), 0.0);
    for (int h = 1; h * 150.0 < 8000.0; ++h) {
        const double g = 0.1 / (1.0 + std::pow((h * 150.0 - 1000.0) / 150.0, 2));
        for (size_t i = 0; i < x.size(); ++i) x[i] += g * std::sin(2 * kPi * h * 150.0 * i / kRate + h);
    }
    const auto envelopePeak = [&](const std::vector<float>& y, double f0) {
        const Samples s(y.begin(), y.end());
        std::vector<double> level;
        for (int h = 1; h * f0 < 6000.0; ++h) level.push_back(std::log(std::abs(transformAt(s, h * f0, 24000, 48000))));
        const auto at = static_cast<size_t>(std::max_element(level.begin(), level.end()) - level.begin());
        if (at == 0 || at + 1 >= level.size()) return static_cast<double>(at + 1) * f0;
        const double offset = 0.5 * (level[at - 1] - level[at + 1]) / (level[at - 1] - 2 * level[at] + level[at + 1]);
        return (static_cast<double>(at + 1) + offset) * f0;
    };
    struct Case {
        double pitch, formant;
        bool link;
        double moves;  // where the resonance goes (semitones)
    };
    for (const Case& k : {Case{0, 7, false, 7}, Case{0, -12, false, -12}, Case{0, 19, false, 19}, Case{-7, 0, false, 0},
                          Case{5, 0, true, 5}, Case{-9, 0, true, -9}, Case{5, -3, true, 2}}) {
        INFO(std::to_string(k.pitch) + " " + std::to_string(k.formant) + " " + std::to_string(k.link));
        cl::Controls c;
        c.pitch = k.pitch;
        c.formant = k.formant;
        c.link = k.link;
        const auto y = shift(x, kRate, c).left;
        const double f0 = 150.0 * std::exp2(k.pitch / 12.0);
        CHECK(std::abs(cents(peakFrequency(y, 24000, 48000, f0), f0)) < 0.05);
        CHECK(std::abs(cents(envelopePeak(y, f0), 1000.0 * std::exp2(k.moves / 12.0))) < 150.0);
    }
}

TEST_CASE("changeling: harmonics") {
    // a pulse train's harmonics all as loud: shifted up by k harmonics, the lowest k are gone (40 dB down at least)
    // and the rest keep their level; shifted down by whole ones, they all stay (the spectrum folds onto itself);
    // the output's harmonics stay where they were (no line between them)
    const auto x = pulses(110.0, 1.5);
    const auto in = std::vector<float>(x.begin(), x.end());
    for (const double k : {1.0, 2.0, 3.0, -1.0, -2.0}) {
        INFO(std::to_string(k));
        cl::Controls c;
        c.harmonics = k;
        const auto y = shift(x, kRate, c).left;
        for (int h = 1; h <= 12; ++h) {
            INFO(std::to_string(h));
            const double change = lineDb(y, h * 110.0, 24000, 48000) - lineDb(in, h * 110.0, 24000, 48000);
            if (k > 0 && h <= static_cast<int>(k)) CHECK(change < -40.0);
            else if (k < 0 && h <= static_cast<int>(-k)) CHECK(std::abs(change) < 3.0);  // (a folded harmonic, beside its own)
            else CHECK(std::abs(change) < 1.0);
        }
        CHECK(lineDb(y, 165.0, 24000, 48000) < lineDb(in, 110.0, 24000, 48000) - 40.0);
    }
}

TEST_CASE("changeling: twin") {
    // Twin at 100 %, an octave down: the voice and a line an octave below it, as loud together as the voice alone
    // (within a dB or two); a fifth up: the fifth's line too. Noise (unvoiced) isn't doubled.
    const auto x = harmonic(steady(200.0, 1.5), kRate);
    for (const double interval : {-12.0, 7.0}) {
        INFO(std::to_string(interval));
        cl::Controls c;
        c.twin = 1.0;
        c.interval = interval;
        const auto y = shift(x, kRate, c).left;
        const double twinF0 = 200.0 * std::exp2(interval / 12.0);
        CHECK(lineDb(y, twinF0, 24000, 48000) > lineDb(y, 200.0, 24000, 48000) - 10.0);
        CHECK(std::abs(cents(peakFrequency(y, 24000, 48000, twinF0), twinF0)) < 0.05);
        CHECK(std::abs(levelDb(y, 24000, 60000) - levelDb(x, 24000, 60000)) < 2.5);
    }
    const Samples n = noise(96000, 7, 0.2f);
    const std::vector<double> nx(n.begin(), n.end());
    cl::Controls c;
    c.twin = 1.0;
    const auto y = shift(nx, kRate, c).left;
    CHECK(std::abs(levelDb(y, 24000, 72000) - levelDb(nx, 24000, 72000)) < 0.6);
}

TEST_CASE("changeling: detune and width") {
    // Detune: the left channel that many cents down, the right up. Width: the two sides differ, each about as loud
    // as the voice, and together the voice's harmonics
    const auto x = harmonic(steady(200.0, 1.5), kRate);
    cl::Controls d;
    d.detune = 25.0;
    const auto dy = shift(x, kRate, d, true);
    CHECK(std::abs(cents(peakFrequency(dy.left, 24000, 48000, 200.0), 200.0) + 25.0) < 0.05);
    CHECK(std::abs(cents(peakFrequency(dy.right, 24000, 48000, 200.0), 200.0) - 25.0) < 0.05);

    cl::Controls w;
    w.width = 1.0;
    const auto wy = shift(x, kRate, w, true);
    CHECK(wy.left != wy.right);
    CHECK(std::abs(levelDb(wy.left, 24000, 60000) - levelDb(x, 24000, 60000)) < 1.5);
    CHECK(std::abs(levelDb(wy.right, 24000, 60000) - levelDb(x, 24000, 60000)) < 1.5);
    std::vector<float> sum(wy.left.size());
    for (size_t i = 0; i < sum.size(); ++i) sum[i] = wy.left[i] + wy.right[i];
    // each side has every other grain: a line an octave below; the two together have none
    CHECK(lineDb(wy.left, 100.0, 24000, 48000) > lineDb(wy.left, 200.0, 24000, 48000) - 20.0);
    CHECK(lineDb(sum, 100.0, 24000, 48000) < lineDb(sum, 200.0, 24000, 48000) - 40.0);
}

TEST_CASE("changeling: smear") {
    // Smear blurs the voice (another signal than without it) but keeps its level and its pitch about where it was
    // (over seconds: Smear's grains wander)
    const auto x = harmonic(steady(200.0, 4.0), kRate);
    cl::Controls c;
    c.smear = 1.0;
    const auto y = shift(x, kRate, c).left;
    CHECK(allFinite(y));
    CHECK(std::abs(levelDb(y, 24000, 60000) - levelDb(x, 24000, 60000)) < 1.5);
    CHECK(std::abs(dominantFreq(std::vector<float>(y.begin() + 24000, y.begin() + 168000)) - 200.0) < 5.0);
    const auto plain = shift(x, kRate, cl::Controls{}).left;
    double difference = 0.0;
    for (size_t i = 24000; i < 60000; ++i) difference += std::pow(y[i] - plain[i], 2);
    CHECK(10.0 * std::log10(difference / energy(Samples(plain.begin() + 24000, plain.begin() + 60000))) > -10.0);
}

TEST_CASE("changeling: silence and noise") {
    cl::Controls c;
    c.pitch = 5.0;
    c.formant = 3.0;
    c.harmonics = 1.5;
    c.smear = 0.5;
    const auto quiet = shift(std::vector<double>(48000, 0.0), kRate, c).left;
    CHECK(allEqual(quiet, 0.0));
    // noise is unvoiced (but for a few frames pYIN hears a pitch in): its grains play where they were
    const Samples n = noise(96000, 7, 0.2f);
    const std::vector<double> x(n.begin(), n.end());
    const auto y = shift(x, kRate, c).left;
    CHECK(allFinite(y));
    CHECK(std::abs(levelDb(y, 24000, 72000) - levelDb(x, 24000, 72000)) < 0.5);
}

TEST_CASE("changeling: the extremes") {
    // every control at its ends, a low and a high voice, a gap and noise, at 44.1 to 192 kHz: finite and bounded;
    // High Quality's grains never late
    for (const auto quality : {cl::Quality::Studio, cl::Quality::HighQuality})
    for (const double rate : {44100.0, 192000.0})
    for (const double f0 : {66.0, 1500.0})
    for (const double pitch : {-24.0, 24.0})
    for (const double formant : {-24.0, 24.0}) {
        INFO(std::to_string(static_cast<int>(quality)) + " " + std::to_string(rate) + " " + std::to_string(f0) + " " +
             std::to_string(pitch) + " " + std::to_string(formant));
        std::vector<double> f;
        for (int i = 0; i < static_cast<int>(0.6 * rate); ++i) f.push_back(f0 * std::exp2(0.8 * std::sin(2 * kPi * 1.3 * i / rate) / 12.0));
        auto x = harmonic(f, rate);
        x.resize(x.size() + static_cast<size_t>(0.1 * rate), 0.0);
        std::mt19937 rng(9);
        std::normal_distribution<double> nd(0.0, 0.1);
        for (int i = 0; i < static_cast<int>(0.15 * rate); ++i) x.push_back(nd(rng));
        cl::Controls c;
        c.pitch = pitch;
        c.formant = formant;
        c.link = true;
        c.quality = quality;
        c.harmonics = pitch > 0 ? 4.0 : -4.0;
        c.twin = 1.0;
        c.interval = -pitch;
        c.detune = cl::kDetuneMax;
        c.width = 1.0;
        c.smear = 1.0;
        const auto y = shift(x, rate, c, true);
        if (quality == cl::Quality::HighQuality) CHECK_EQ(y.late, int64_t{0});
        CHECK(allFinite(y.left));
        CHECK(allFinite(y.right));
        INFO("peaks " + std::to_string(maxAbs(y.left)) + " " + std::to_string(maxAbs(y.right)) + ", input " + std::to_string(maxAbs(x)));
        CHECK(maxAbs(y.left) < 4.0);
        CHECK(maxAbs(y.right) < 4.0);
    }
}

TEST_CASE("changeling: quality switched while playing") {
    // the shifter starts again at the other quality's latency: from latency() after the switch, the input as it went
    // in, that much later; the device asks the engine to realign the tracks once
    std::vector<double> f;
    for (int i = 0; i < static_cast<int>(3 * kRate); ++i) f.push_back(200.0 * std::exp2(0.5 / 12.0 * std::sin(2 * kPi * 3.0 * i / kRate)));
    const auto voice = harmonic(f, kRate);
    std::vector<float> y(voice.begin(), voice.end());
    cl::Shifter s;
    s.prepare(kRate);
    cl::Controls controls;
    controls.quality = cl::Quality::Live;
    s.setControls(controls);
    const size_t switchAt = 256 * 188;
    for (size_t at = 0; at < y.size(); at += 256) {
        if (at == switchAt) {
            controls.quality = cl::Quality::HighQuality;
            s.setControls(controls);
        }
        float* p[1] = {y.data() + at};
        s.process(p, 1, static_cast<int>(std::min<size_t>(256, y.size() - at)));
    }
    const auto high = static_cast<size_t>(s.latency(cl::Quality::HighQuality));
    CHECK_EQ(s.latency(), static_cast<int>(high));
    size_t differ = 0;
    for (size_t i = switchAt + high; i < y.size(); ++i) differ += y[i] != static_cast<float>(voice[i - high]) ? 1 : 0;
    CHECK_EQ(differ, size_t{0});

    Standalone device("changeling", kRate);
    CHECK(!device.processor().idle());
    device.set("quality", 0.f);
    CHECK_EQ(device.processor().latencySamples(), s.latency(cl::Quality::Live) + 31);
    CHECK(device.processor().idle());  // realign
    CHECK(!device.processor().idle());
}

TEST_CASE("changeling: mix") {
    // Mix at 0: the input as it went in, as late as the wet signal (whatever Pitch does); halfway: half of each
    const auto voice = harmonic(steady(220.0, 1.0), kRate);
    Standalone dry("changeling", kRate, {{"mix", 0.f}, {"pitch", 7.f}, {"drive", 4.f}});
    const int latency = dry.processor().latencySamples();
    Samples x(voice.begin(), voice.end());
    x.resize(x.size() + static_cast<size_t>(latency), 0.f);
    const Samples in = x;
    const Samples out = dry.play(x);
    size_t differ = 0;
    for (size_t i = static_cast<size_t>(latency); i < out.size(); ++i) differ += out[i] != in[i - static_cast<size_t>(latency)] ? 1 : 0;
    CHECK_EQ(differ, size_t{0});
    Standalone half("changeling", kRate, {{"mix", 50.f}, {"pitch", 12.f}});
    const Samples h = half.play(in);
    const auto from = static_cast<int64_t>(latency + 12000), length = int64_t{24000};
    CHECK(std::abs(db(windowedAmplitude(h, 220.0, from, length)) - db(windowedAmplitude(in, 220.0, from - latency, length)) + 6.02) < 0.5);
}

TEST_CASE("changeling: drive") {
    // Little AlterBoy's curve: 0 is the input; from 1 the small-signal gain rises 1.69 dB a step (0.759 x 1.215^d
    // of LAB's, divided by its 0's), the curve saturating at a ceiling that falls as the drive rises, lopsided
    CHECK_EQ(cl::Drive::curve(0.0, 0.3), 0.3);
    double lastCeiling = 1e9;
    for (int d = 2; d <= 10; ++d) {
        INFO(std::to_string(d));
        const double small = cl::Drive::curve(d, 1e-4) / 1e-4;
        CHECK(std::abs(db(small) - 1.69 * d) < 1.2);
        const double ceiling = cl::Drive::curve(d, 1.0);
        if (d >= 5) {
            CHECK(ceiling < lastCeiling);
            lastCeiling = ceiling;
        }
        CHECK(ceiling < small);  // it compresses
        CHECK(std::abs(cl::Drive::curve(d, 1.0) + cl::Drive::curve(d, -1.0)) > 1e-3);  // lopsided
    }
    // above 6 a louder input is driven less
    CHECK(cl::Drive::curve(9.0, 0.01, 0.2) < cl::Drive::curve(9.0, 0.01, 0.0));
    CHECK_EQ(cl::Drive::curve(5.0, 0.01, 0.2), cl::Drive::curve(5.0, 0.01, 0.0));

    // the processor: at 0 the input exactly, latency() later; driven, finite and no louder than its curve allows
    cl::Drive drive;
    drive.prepare(kRate, 512);
    const Samples s = sine(300.0, 0.5, 0.9);
    Samples a = s, b = s;
    float* p[2] = {a.data(), b.data()};
    drive.process(p, 2, 512);
    for (size_t at = 512; at < a.size(); at += 512) {
        float* q[2] = {a.data() + at, b.data() + at};
        drive.process(q, 2, static_cast<int>(std::min<size_t>(512, a.size() - at)));
    }
    const auto latency = static_cast<size_t>(drive.latency());
    size_t differ = 0;
    for (size_t i = latency; i < a.size(); ++i) differ += a[i] != s[i - latency] ? 1 : 0;
    CHECK_EQ(differ, size_t{0});
    drive.setDrive(10.0);
    Samples c = s;
    for (size_t at = 0; at < c.size(); at += 512) {
        float* q[1] = {c.data() + at};
        drive.process(q, 1, static_cast<int>(std::min<size_t>(512, c.size() - at)));
    }
    CHECK(allFinite(c));
    CHECK(maxAbs(Samples(c.begin() + 4800, c.end())) < 0.3);
}

TEST_CASE("changeling: quadrature") {
    // the pair's outputs 90 degrees apart (the second behind), as loud as each other, from 30 Hz to 20 kHz
    for (const double freq : {40.0, 200.0, 1000.0, 8000.0, 19000.0}) {
        INFO(std::to_string(freq));
        cl::Quadrature q;
        Samples i, qq;
        for (int n = 0; n < 96000; ++n) {
            double a, b;
            q.process(std::sin(2.0 * kPi * freq * n / kRate), a, b);
            i.push_back(static_cast<float>(a));
            qq.push_back(static_cast<float>(b));
        }
        const auto length = static_cast<int64_t>(std::round(std::floor(48000 * freq / kRate) * kRate / freq));
        const Complex ti = transformAt(i, freq, 48000, length), tq = transformAt(qq, freq, 48000, length);
        double phase = std::arg(tq / ti) * 180.0 / kPi;
        CHECK(std::abs(phase + 90.0) < 0.3);
        CHECK(std::abs(db(std::abs(tq) / std::abs(ti))) < 0.01);
    }
}

// A vowel /a/ whose glottal closures are known: Rosenberg pulses (the flow's derivative, opening over 40 % of each
// period and closing over 20 %, its fall ending at the closure) at f0 following `freq` (each period off by up to
// `jitter` of itself, at random), through three formants. The closures' times (samples) into `closures`.
std::vector<double> vowel(const std::vector<double>& freq, std::vector<double>& closures, double jitter = 0.0,
                          double rate = kRate) {
    std::mt19937 random(11);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    std::vector<double> flow(freq.size(), 0.0);
    double start = 0.0;
    while (start < static_cast<double>(freq.size())) {
        const double T = rate / freq[static_cast<size_t>(start)] * (1.0 + jitter * uniform(random));
        const double open = 0.4 * T, close = 0.2 * T;
        for (auto n = static_cast<int64_t>(std::ceil(start)); n < static_cast<int64_t>(start + T) &&
                                                              n < static_cast<int64_t>(flow.size()); ++n) {
            const double t = static_cast<double>(n) - start;
            flow[static_cast<size_t>(n)] = t < open ? 0.5 - 0.5 * std::cos(kPi * t / open)
                                           : t < open + close ? std::cos(0.5 * kPi * (t - open) / close) : 0.0;
        }
        closures.push_back(start + open + close);
        start += T;
    }
    std::vector<double> x(freq.size(), 0.0);
    for (size_t n = 1; n < x.size(); ++n) x[n] = 0.05 * (flow[n] - flow[n - 1]) * (rate / 48000.0);
    for (const auto& [f, bandwidth] : {std::pair{700.0, 80.0}, {1220.0, 90.0}, {2600.0, 120.0}}) {
        const double r = std::exp(-kPi * bandwidth / rate), a1 = -2.0 * r * std::cos(2.0 * kPi * f / rate), a2 = r * r;
        double y1 = 0.0, y2 = 0.0;
        for (double& v : x) {
            const double y = (1.0 + a1 + a2) * v - a1 * y1 - a2 * y2;
            y2 = y1;
            y1 = y;
            v = y;
        }
    }
    return x;
}

TEST_CASE("changeling: glottal closures") {
    // SEDREAMS finds a vowel's closures wherever it is asked about the period, gliding an octave with 2 % jitter,
    // either way up: all of them a steady 0.1 ms after the flow's corner (the residual's peak), within a sample of
    // each other, and sure of them
    std::vector<double> freq(72000);
    for (size_t n = 0; n < freq.size(); ++n) freq[n] = 100.0 * std::exp2(static_cast<double>(n) / freq.size());
    std::vector<double> truth;
    const auto x = vowel(freq, truth, 0.02);
    for (const double sign : {1.0, -1.0}) {
        INFO(sign > 0.0 ? "up" : "down");
        cl::Closures c;
        c.prepare(kRate, static_cast<int>(x.size()) + 4096, kRate / 60.0);
        for (const double v : x) c.push(sign * v);
        std::mt19937 random(5);
        std::uniform_real_distribution<double> uniform(-0.3, 0.3);
        int asked = 0;
        double sure = 0.0, latest = -1e9, earliest = 1e9;
        for (const double t : truth) {
            if (t < 0.2 * kRate || t > static_cast<double>(x.size()) - 0.05 * kRate) continue;
            const double T = kRate / freq[static_cast<size_t>(t)];
            const double p = t + uniform(random) * T;
            double offset, sureness, relative;
            CHECK(c.find(p, T, offset, sureness, relative));
            earliest = std::min(earliest, p + offset - t);
            latest = std::max(latest, p + offset - t);
            sure += sureness;
            c.learn(relative);
            ++asked;
        }
        CHECK(asked > 150);
        CHECK(earliest > 3.0);
        CHECK(latest < 8.0);
        CHECK(latest - earliest < 1.0);
        CHECK(sure / asked > 0.9);
    }
    // a pulse train's pulses (at the mean-based signal's maxima, not after its minima: the place is learnt), found
    // as the input comes, each period asked about as soon as it may be
    {
        const double T = kRate / 110.0;
        const auto y = pulses(110.0, 1.0);
        cl::Closures c;
        c.prepare(kRate, 8192, kRate / 60.0);
        double p = 0.25 * T, worst = 0.0;
        int asked = 0;
        for (size_t n = 0; n < y.size(); ++n) {
            c.push(y[n]);
            double offset, sureness, relative;
            if (!c.find(p, T, offset, sureness, relative)) continue;
            c.learn(relative);
            if (p > 0.1 * kRate) {
                const double found = p + offset;
                worst = std::max(worst, std::abs(found - T * std::round(found / T)));
                CHECK(sureness > 0.9);
                ++asked;
            }
            p += T;
        }
        CHECK(asked > 90);
        CHECK(worst < 1.0);
    }
    // unsure in noise
    const Samples n = noise(96000, 7, 0.2f);
    cl::Closures c;
    c.prepare(kRate, 100000, kRate / 60.0);
    for (const float v : n) c.push(v);
    double sure = 0.0;
    int asked = 0;
    for (double p = 24000.0; p < 90000.0; p += 300.0, ++asked) {
        double offset, sureness, relative;
        c.find(p, 300.0, offset, sureness, relative);
        sure += sureness;
    }
    CHECK(sure / asked < 0.05);
}

TEST_CASE("changeling: elastic grains") {
    // a side's warp: nothing in the dead zone (the closure stays put), through its knots, smooth, held past the edge
    cl::EdgeWarp w;
    w.set(200.0, {1.0, 2.0, 4.0});
    CHECK(w.on);
    for (double u = -10.0; u <= 50.0; u += 0.5) CHECK_EQ(w.at(u), 0.0);
    CHECK(std::abs(w.at(80.0) - 1.0) < 1e-12);
    CHECK(std::abs(w.at(140.0) - 2.0) < 1e-12);
    CHECK(std::abs(w.at(200.0) - 4.0) < 1e-12);
    CHECK_EQ(w.at(260.0), 4.0);
    double steepest = 0.0, previous = 0.0;
    for (double u = 0.0; u <= 260.0; u += 0.25) {
        const double d = w.at(u);
        steepest = std::max(steepest, std::abs(d - previous) / 0.25);
        previous = d;
    }
    CHECK(steepest < 0.05);  // (no step anywhere: d' stays small, the pitch read barely moves)
    CHECK(w.most() == 4.0);
    cl::EdgeWarp none;
    none.set(200.0, {0.0, 0.0, 0.0});
    CHECK(!none.on);

    // shifting a jittery vowel, overlapping grains are bent (both sides where the earlier one's right edge is still
    // to play), and they cancel less: the level lost to the shift comes back by more than a third here
    std::vector<double> freq(96000);
    for (size_t n = 0; n < freq.size(); ++n) freq[n] = 110.0 * (1.0 + 0.02 * std::sin(2.0 * kPi * 5.5 * n / kRate));
    std::vector<double> truth;
    const auto x = vowel(freq, truth, 0.02);
    double lost[2];
    int64_t late[2];
    for (const bool elastic : {false, true}) {
        INFO(elastic ? "elastic" : "plain");
        cl::Controls c;
        c.pitch = 4.0;
        c.elastic = elastic;
        cl::Shifter s;
        s.prepare(kRate);
        s.setControls(c);
        std::vector<float> y(x.begin(), x.end());
        y.resize(y.size() + static_cast<size_t>(s.latency()), 0.f);
        for (size_t at = 0; at < y.size(); at += 256) {
            float* p[1] = {y.data() + at};
            s.process(p, 1, static_cast<int>(std::min<size_t>(256, y.size() - at)));
        }
        if (elastic) {
            CHECK(s.elasticJunctions() > 200);
            CHECK(s.sharedJunctions() > s.elasticJunctions() / 2);
        } else {
            CHECK_EQ(s.elasticJunctions(), 0);
        }
        late[elastic ? 1 : 0] = s.lateSamples();
        y.erase(y.begin(), y.begin() + s.latency());
        CHECK(allFinite(y));
        lost[elastic ? 1 : 0] = levelDb(x, 24000, 90000) - levelDb(y, 24000, 90000);
    }
    CHECK(lost[0] > 0.5);
    CHECK(lost[1] < lost[0] - 0.4);
    CHECK_EQ(late[1], late[0]);  // (bending only reads input that is in)
}

TEST_CASE("changeling: the editor's displays") {
    // A3 +30 cents after half a second of silence, Quantize with Pitch 2: the pitch heard, the pitch out and the
    // note held, a value per 256 samples out, voiced from when the voice comes out
    const auto tone = harmonic(steady(hz(57.3), 1.5), kRate);
    const size_t silence = static_cast<size_t>(0.5 * kRate);
    Standalone device("changeling", kRate, {{"mode", 1.f}, {"pitch", 2.f}, {"glide", 1.f}});
    const int latency = device.processor().latencySamples();
    Samples x(silence, 0.f);
    x.insert(x.end(), tone.begin(), tone.end());
    x.resize(x.size() + static_cast<size_t>(latency + 0.1 * kRate), 0.f);
    device.run({&x});
    const std::vector<float> pitch = device.display("pitch"), out = device.display("out"), note = device.display("note");
    REQUIRE(pitch.size() == x.size() / 256);
    REQUIRE(out.size() == pitch.size());
    REQUIRE(note.size() == pitch.size());
    size_t first = pitch.size();
    for (size_t i = 0; i < pitch.size() && first == pitch.size(); ++i)
        if (pitch[i] > 0.f) first = i;
    REQUIRE(first < pitch.size());
    const double comes = static_cast<double>(first * 256 + 255) - static_cast<double>(silence + latency);
    CHECK(std::abs(comes) < 0.04 * kRate);
    const size_t from = (silence + static_cast<size_t>(latency) + static_cast<size_t>(0.25 * kRate)) / 256;
    const size_t to = (silence + static_cast<size_t>(latency) + tone.size() - static_cast<size_t>(0.25 * kRate)) / 256;
    for (size_t i = from; i < to; ++i) {
        CHECK_EQ(note[i], 57.f);
        CHECK(std::abs(pitch[i] - 57.3) < 0.005);
        CHECK(std::abs(out[i] - 59.0) < 0.005);
    }
    CHECK_EQ(note.back(), -1.f);
    CHECK_EQ(pitch.back(), 0.f);
}
