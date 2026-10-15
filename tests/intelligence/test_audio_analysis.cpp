// A recording's tempo, key and chords (audio/AudioAnalysis.h): drum loops at
// the tempos of five styles, humanized, with noise under them (and trimmed to
// whole bars: the exact tempo); what has no tempo (one-shots, a pad, noise,
// silence); chord progressions of rich tones over a bass, with and without
// drums; drums alone (no chords); and how long a song takes.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "Sounds.h"
#include "audio/AudioAnalysis.h"
#include "harmony/Chords.h"
#include "harness/Test.h"

using namespace sub::intelligence;
using subtest::Samples;

namespace {

constexpr double kRate = subtest::kRate;

void mixIn(Samples& into, const Samples& sound, double at, float gain = 1.f) {
    if (at < 0.0) at = 0.0;
    const auto start = static_cast<size_t>(at * kRate);
    for (size_t i = 0; i < sound.size() && start + i < into.size(); ++i) into[start + i] += gain * sound[i];
}

// A drum pattern, a sixteenth note a character: K kick, S snare, C clap, h hat, o open hat.
struct Pattern {
    double bpm;
    std::string kick, snare, hat;  // 16 sixteenths each (a bar)
    double swing = 0.0;            // how late the off-beat eighths' sixteenths come, in sixteenths
};

const Pattern kBoomBap{90, "K.......K.K.....", "....S.......S...", "h.h.h.h.h.h.h.h.", 0.2};
const Pattern kPop{120, "K.......K.......", "....S.......S...", "h.h.h.h.h.h.h.h."};
const Pattern kHouse{128, "K...K...K...K...", "....C.......C...", "..o...o...o...o."};
const Pattern kTrap{140, "K......K..K.....", "........S.......", "hhhhhhhhhhhhhhhh"};
const Pattern kDnb{174, "K.........K.....", "....S.......S...", "h.h.h.h.h.h.h.h."};

// `bars` of a pattern from `offset` seconds, its hits up to `jitter` seconds off
// and their levels a little apart, in a file `seconds` long (0: the bars'
// length exactly), noise `noiseDb` under it all.
Samples drums(const Pattern& p, int bars, double offset = 0.0, double seconds = 0.0, double jitter = 0.004,
              double noiseDb = -50.0, uint64_t seed = 7) {
    const double sixteenth = 60.0 / p.bpm / 4.0;
    if (seconds <= 0.0) seconds = offset + bars * 16 * sixteenth;
    Samples out(subtest::frames(seconds), 0.f);
    subtest::Random random(seed);
    const Samples kick = subtest::kick(), snare = subtest::snare(), clap = subtest::clap(), hat = subtest::hat(),
                  open = subtest::hat(0.15);
    for (int bar = 0; bar < bars; ++bar) {
        for (size_t i = 0; i < 16; ++i) {
            double at = offset + (bar * 16 + static_cast<int>(i)) * sixteenth;
            if (i % 4 == 2) at += p.swing * sixteenth;
            const auto hit = [&](const Samples& s, float level) {
                mixIn(out, s, at + random.range(-jitter, jitter), level * static_cast<float>(random.range(0.8, 1.0)));
            };
            if (p.kick[i] == 'K') hit(kick, 1.f);
            if (p.snare[i] == 'S') hit(snare, 1.f);
            if (p.snare[i] == 'C') hit(clap, 1.5f);
            if (p.hat[i] == 'h') hit(hat, i % 2 ? 0.5f : 0.8f);
            if (p.hat[i] == 'o') hit(open, 0.8f);
        }
    }
    const auto noise = static_cast<float>(std::pow(10.0, noiseDb / 20.0));
    for (float& x : out) x += noise * random.noise();
    return out;
}

// A note rich in harmonics: a sawtooth's first ten (1/h), its attack 10 ms, its
// release 50 ms, dying away slowly; or plucked: hollow (the odd harmonics 1/h,
// the even ones 1/h²) and dying away over half a second.
void note(Samples& into, int pitch, double start, double length, float gain, bool plucked = false) {
    const double hz = 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
    const auto from = static_cast<size_t>(start * kRate), count = static_cast<size_t>(length * kRate);
    const double decay = plucked ? 0.5 : 3.0;
    for (size_t i = 0; i < count && from + i < into.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double envelope = std::min({1.0, t / 0.01, (length - t) / 0.05}) * std::exp(-t / decay);
        double s = 0.0;
        for (int h = 1; h <= 10 && h * hz < 0.45 * kRate; ++h)
            s += std::sin(2.0 * std::numbers::pi * h * hz * t) / (plucked && h % 2 == 0 ? h * h : h);
        into[from + i] += static_cast<float>(gain * envelope * s);
    }
}

struct Bar {
    std::vector<int> chord;  // MIDI pitches
    int bass;
};

// Each bar's chord held through it, its bass under it struck on each beat; or
// plucked: the chord struck on each beat, the bass held through the bar.
Samples progression(const std::vector<Bar>& bars, double bpm, bool plucked = false) {
    const double beat = 60.0 / bpm;
    Samples out(subtest::frames(static_cast<double>(bars.size()) * 4 * beat), 0.f);
    for (size_t b = 0; b < bars.size(); ++b) {
        const double start = static_cast<double>(b) * 4 * beat;
        if (plucked) {
            for (int i = 0; i < 4; ++i) {
                for (const int p : bars[b].chord) note(out, p, start + i * beat, beat, 0.1f, true);
            }
            note(out, bars[b].bass, start, 4 * beat, 0.12f);
        } else {
            for (const int p : bars[b].chord) note(out, p, start, 4 * beat, 0.08f);
            for (int i = 0; i < 4; ++i) note(out, bars[b].bass, start + i * beat, beat * 0.9, 0.15f);
        }
    }
    return out;
}

Samples mixed(Samples a, const Samples& b, float gain = 1.f) {
    if (b.size() > a.size()) a.resize(b.size(), 0.f);
    for (size_t i = 0; i < b.size(); ++i) a[i] += gain * b[i];
    return a;
}

audio::Analysis analyse(const Samples& mono, const audio::Options& options = {}) {
    const float* channels[] = {mono.data()};
    return audio::analyze(channels, 1, mono.size(), kRate, options);
}

std::string names(const audio::Analysis& a) {
    std::string text;
    for (const audio::ChordSpanSec& s : a.chords) text += (text.empty() ? "" : " ") + s.chord.name();
    return text;
}

std::string spans(const audio::Analysis& a) {
    std::string text;
    for (const audio::ChordSpanSec& s : a.chords) {
        char line[96];
        std::snprintf(line, sizeof line, "%s%s [%.2f, %.2f)", text.empty() ? "" : " | ", s.chord.name().c_str(),
                      s.start, s.end);
        text += line;
    }
    return text;
}

std::string describe(const std::optional<audio::Tempo>& t) {
    if (!t) return "no tempo";
    char text[96];
    std::snprintf(text, sizeof text, "%.3f BPM (confidence %.2f, first beat %.3f s)", t->bpm, t->confidence,
                  t->firstBeatSec);
    return text;
}

// How far a beat at `seconds` is from the grid (either way).
double offGrid(const audio::Tempo& t, double seconds) {
    const double period = 60.0 / t.bpm;
    const double d = std::fmod(seconds - t.firstBeatSec + 10 * period, period);
    return std::min(d, period - d);
}

const std::vector<Bar> kPopChords{{{60, 64, 67}, 36}, {{59, 62, 67}, 43}, {{60, 64, 69}, 45}, {{60, 65, 69}, 41}};
const std::vector<Bar> kMinorChords{{{57, 60, 64}, 45}, {{57, 62, 65}, 38}, {{56, 59, 64}, 40}, {{57, 60, 64}, 45}};

}  // namespace

TEST_CASE("audio tempo: drum loops of five styles, humanized, over noise") {
    for (const Pattern* p : {&kBoomBap, &kPop, &kHouse, &kTrap, &kDnb}) {
        // Not a whole number of bars: a quarter second of silence first, a half after.
        const double bars = 8;
        const double length = 0.25 + bars * 240.0 / p->bpm + 0.5;
        const Samples loop = drums(*p, static_cast<int>(bars), 0.25, length);
        const std::optional<audio::Tempo> tempo = audio::estimateTempo(loop.data(), loop.size(), kRate);
        INFO(std::to_string(p->bpm) + " BPM: " + describe(tempo));
        std::printf("    %g BPM loop: %s\n", p->bpm, describe(tempo).c_str());
        REQUIRE(tempo.has_value());
        CHECK_NEAR(tempo->bpm, p->bpm, 0.5);
        CHECK(offGrid(*tempo, 0.25) < 0.02);
    }
}

TEST_CASE("audio tempo: a loop trimmed to whole bars has its exact tempo") {
    for (const Pattern* p : {&kBoomBap, &kPop, &kHouse, &kTrap, &kDnb}) {
        for (const int bars : {1, 2, 4}) {
            const Samples loop = drums(*p, bars, 0.0, 0.0, 0.004, -50.0, static_cast<uint64_t>(bars));
            const std::optional<audio::Tempo> tempo = audio::estimateTempo(loop.data(), loop.size(), kRate);
            INFO(std::to_string(p->bpm) + " BPM, " + std::to_string(bars) + " bars: " + describe(tempo));
            std::printf("    %g BPM, %d bars: %s\n", p->bpm, bars, describe(tempo).c_str());
            REQUIRE(tempo.has_value());
            CHECK_EQ(tempo->bpm, p->bpm);
        }
    }
    // Two bars of 4 s: 120.
    const Samples loop = drums(kPop, 2);
    REQUIRE(loop.size() == size_t(4 * kRate));
    const std::optional<audio::Tempo> tempo = audio::estimateTempo(loop.data(), loop.size(), kRate);
    REQUIRE(tempo.has_value());
    CHECK_EQ(tempo->bpm, 120.0);
    CHECK(offGrid(*tempo, 0.0) < 0.02);
}

TEST_CASE("audio tempo: none in a one-shot, a pad (its chord all the same), noise or silence") {
    const auto none = [](const std::string& what, const Samples& s) {
        const std::optional<audio::Tempo> tempo = audio::estimateTempo(s.data(), s.size(), kRate);
        INFO(what + ": " + describe(tempo));
        std::printf("    %s: %s\n", what.c_str(), describe(tempo).c_str());
        CHECK(!tempo.has_value());
    };
    Samples kick(subtest::frames(3.0), 0.f), snare(subtest::frames(3.0), 0.f);
    mixIn(kick, subtest::kick(), 0.1);
    mixIn(snare, subtest::snare(), 0.0);
    none("a kick", kick);
    none("a snare", snare);
    Samples pad(subtest::frames(8.0), 0.f);
    for (const int p : {48, 55, 60, 64, 67}) note(pad, p, 0.0, 8.0, 0.1f);
    none("a pad", pad);
    // Still a chord, on a grid of its own: C major held 8 s.
    const audio::Analysis held = analyse(pad);
    CHECK(!held.tempo.has_value());
    CHECK_EQ(names(held), std::string("C"));
    REQUIRE(held.chords.size() == 1);
    CHECK_NEAR(held.chords[0].start, 0.0, 0.2);
    CHECK_NEAR(held.chords[0].end, 8.0, 0.2);
    none("white noise", subtest::noise(8.0));
    none("silence", Samples(subtest::frames(8.0), 0.f));
    none("nothing", Samples{});
}

TEST_CASE("audio chords: a progression of rich tones over a bass, alone and with drums") {
    // C | G | Am | F twice at 100 BPM.
    std::vector<Bar> twice = kPopChords;
    twice.insert(twice.end(), kPopChords.begin(), kPopChords.end());
    const Samples chords = progression(twice, 100.0);
    const audio::Analysis alone = analyse(chords);
    std::printf("    pop chords alone: %s; %s; %s\n", describe(alone.tempo).c_str(),
                alone.key ? alone.key->name().c_str() : "no key", spans(alone).c_str());
    CHECK_EQ(names(alone), std::string("C G Am F C G Am F"));
    CHECK(alone.key == (harmony::Key{0, false}));

    Pattern pop = kPop;
    pop.bpm = 100;
    const audio::Analysis withDrums = analyse(mixed(chords, drums(pop, 8), 0.7f));
    std::printf("    pop chords and drums: %s; %s; %s\n", describe(withDrums.tempo).c_str(),
                withDrums.key ? withDrums.key->name().c_str() : "no key", spans(withDrums).c_str());
    CHECK_EQ(names(withDrums), std::string("C G Am F C G Am F"));
    CHECK(withDrums.key == (harmony::Key{0, false}));
    REQUIRE(withDrums.tempo.has_value());
    CHECK_EQ(withDrums.tempo->bpm, 100.0);
    // Each chord where its bar is (2.4 s a bar), give or take a step.
    for (size_t i = 0; i < withDrums.chords.size(); ++i) {
        const audio::ChordSpanSec& s = withDrums.chords[i];
        CHECK_NEAR(s.start, i * 2.4, 0.16);
        CHECK_NEAR(s.end, (i + 1) * 2.4, 0.16);
    }
}

TEST_CASE("audio chords: a minor key, alone and with drums") {
    std::vector<Bar> twice = kMinorChords;
    twice.insert(twice.end(), kMinorChords.begin(), kMinorChords.end());
    const Samples chords = progression(twice, 90.0);
    const audio::Analysis alone = analyse(chords);
    std::printf("    minor chords alone: %s; %s; %s\n", describe(alone.tempo).c_str(),
                alone.key ? alone.key->name().c_str() : "no key", spans(alone).c_str());
    CHECK_EQ(names(alone), std::string("Am Dm E Am Dm E Am"));
    CHECK(alone.key == (harmony::Key{9, true}));

    const audio::Analysis withDrums = analyse(mixed(chords, drums(kBoomBap, 8), 0.7f));
    std::printf("    minor chords and drums: %s; %s; %s\n", describe(withDrums.tempo).c_str(),
                withDrums.key ? withDrums.key->name().c_str() : "no key", spans(withDrums).c_str());
    CHECK_EQ(names(withDrums), std::string("Am Dm E Am Dm E Am"));
    CHECK(withDrums.key == (harmony::Key{9, true}));
}

TEST_CASE("audio chords: sevenths played are sevenths; a plucked, hollow tone; an inversion") {
    // Dm7 | G7 | Cmaj7 | Cmaj7 at 110 BPM, over drums.
    const std::vector<Bar> twoFiveOne{
        {{62, 65, 69, 72}, 38}, {{59, 62, 65, 67}, 43}, {{60, 64, 67, 71}, 36}, {{60, 64, 67, 71}, 36}};
    Pattern pop = kPop;
    pop.bpm = 110;
    const audio::Analysis jazz = analyse(mixed(progression(twoFiveOne, 110.0), drums(pop, 4), 0.7f));
    std::printf("    ii-V-I: %s; %s; %s\n", describe(jazz.tempo).c_str(),
                jazz.key ? jazz.key->name().c_str() : "no key", spans(jazz).c_str());
    CHECK_EQ(names(jazz), std::string("Dm7 G7 Cmaj7"));
    CHECK(jazz.key == (harmony::Key{0, false}));

    // C/E | F | G | Am, plucked on every beat at 120 BPM, twice. (A chord is
    // one span whatever its bass does: C | C/E would be one C, as from MIDI.)
    const std::vector<Bar> bars{{{60, 64, 67}, 40}, {{60, 65, 69}, 41}, {{59, 62, 67}, 43}, {{57, 60, 64}, 45},
                                {{60, 64, 67}, 40}, {{60, 65, 69}, 41}, {{59, 62, 67}, 43}, {{57, 60, 64}, 45}};
    const audio::Analysis plucked = analyse(progression(bars, 120.0, true));
    std::printf("    plucked: %s; %s; %s\n", describe(plucked.tempo).c_str(),
                plucked.key ? plucked.key->name().c_str() : "no key", spans(plucked).c_str());
    CHECK_EQ(names(plucked), std::string("C/E F G Am C/E F G Am"));
    CHECK(plucked.key == (harmony::Key{0, false}));
    REQUIRE(plucked.tempo.has_value());
    CHECK_EQ(plucked.tempo->bpm, 120.0);
}

TEST_CASE("audio analysis: the same at any sample rate, in stereo, from part of a file") {
    std::vector<Bar> twice = kPopChords;
    twice.insert(twice.end(), kPopChords.begin(), kPopChords.end());
    Pattern pop = kPop;
    pop.bpm = 100;
    const Samples song = mixed(progression(twice, 100.0), drums(pop, 8), 0.7f);
    for (const double rate : {16000.0, 22050.0, 48000.0, 96000.0}) {
        // Resampled (linearly: good enough for this).
        Samples at(static_cast<size_t>(static_cast<double>(song.size()) * rate / kRate));
        for (size_t i = 0; i < at.size(); ++i) {
            const double p = static_cast<double>(i) * kRate / rate;
            const auto j = static_cast<size_t>(p);
            const double f = p - static_cast<double>(j);
            at[i] = static_cast<float>(song[j] * (1.0 - f) + (j + 1 < song.size() ? song[j + 1] : 0.f) * f);
        }
        const float* channels[] = {at.data(), at.data()};
        const audio::Analysis a = audio::analyze(channels, 2, at.size(), rate);
        std::printf("    at %g Hz: %s; %s\n", rate, describe(a.tempo).c_str(), spans(a).c_str());
        INFO(std::to_string(rate) + " Hz");
        REQUIRE(a.tempo.has_value());
        CHECK_EQ(a.tempo->bpm, 100.0);
        CHECK_EQ(names(a), std::string("C G Am F C G Am F"));
        CHECK(a.key == (harmony::Key{0, false}));
    }
    // The tempo from the first 5 s alone; no chords asked for: none.
    audio::Options options;
    options.maxTempoSeconds = 5.0;
    options.chords = false;
    const audio::Analysis part = analyse(song, options);
    REQUIRE(part.tempo.has_value());
    CHECK_NEAR(part.tempo->bpm, 100.0, 0.5);
    CHECK(part.chords.empty());
    CHECK(!part.key.has_value());
    // Too low a rate, no channels: nothing.
    const float* none[] = {song.data()};
    CHECK(!audio::analyze(none, 1, song.size(), 4000.0).tempo.has_value());
    CHECK(audio::analyze(none, 0, song.size(), kRate).chords.empty());
}

TEST_CASE("audio chords: none in drums alone") {
    for (const Pattern* p : {&kPop, &kHouse, &kTrap}) {
        const audio::Analysis a = analyse(drums(*p, 8));
        std::printf("    %g BPM drums: %s; %s; %s\n", p->bpm, describe(a.tempo).c_str(),
                    a.key ? a.key->name().c_str() : "no key", spans(a).c_str());
        CHECK(a.chords.empty());
        CHECK(!a.key.has_value());
        CHECK(a.tempo.has_value());
    }
}

TEST_CASE("audio analysis: a 4-minute stereo song takes a fraction of a second") {
    // 96 bars at 96 BPM (4 minutes): the pop progression 24 times, and drums,
    // the channels a little apart.
    const Samples cycle = progression(kPopChords, 96.0);
    Samples chords;
    for (int i = 0; i < 24; ++i) chords.insert(chords.end(), cycle.begin(), cycle.end());
    Pattern pop = kPop;
    pop.bpm = 96;
    const Samples song = mixed(chords, drums(pop, 96), 0.7f);
    Samples right = song;
    for (size_t i = 0; i < right.size(); ++i) right[i] = 0.9f * song[i] + 0.1f * song[i > 40 ? i - 40 : 0];
    const float* channels[] = {song.data(), right.data()};
    const auto start = std::chrono::steady_clock::now();
    const audio::Analysis a = audio::analyze(channels, 2, song.size(), kRate);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    audio::Options tempoOnly;
    tempoOnly.chords = false;
    const auto start2 = std::chrono::steady_clock::now();
    const audio::Analysis t = audio::analyze(channels, 2, song.size(), kRate, tempoOnly);
    const double tempoMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start2).count();
    const Samples loop = drums(kPop, 5);
    const auto start3 = std::chrono::steady_clock::now();
    const audio::Analysis l = analyse(loop);
    const double loopMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start3).count();
    std::printf("    %.0f s stereo song: %.1f ms (tempo alone %.1f ms); a 10 s loop: %.1f ms; %s, %zu chords\n",
                static_cast<double>(song.size()) / kRate, ms, tempoMs, loopMs, describe(a.tempo).c_str(),
                a.chords.size());
    INFO("took " + std::to_string(ms) + " ms");
    REQUIRE(a.tempo.has_value());
    CHECK_EQ(a.tempo->bpm, 96.0);
    CHECK(t.tempo.has_value());
    CHECK(l.tempo.has_value());
    CHECK(a.key == (harmony::Key{0, false}));
    CHECK_EQ(a.chords.size(), size_t{96});
    CHECK(ms < 2000.0);  // (well under half a second in a release build)
}
