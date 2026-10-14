// The sound tagger: the compiled-in model against what it gave in PyTorch,
// sounds told apart, attributes graded among a sound's own kind, the words
// searches are read by, labels, the tag table's hashes, how sounds fit a
// project.

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "Sounds.h"
#include "platform/Bytes.h"
#include "similarity/EssentiaExtractor.h"
#include "tagging/ProjectFit.h"
#include "tagging/SoundTagTable.h"
#include "tagging/SoundTagger.h"
#include "tagging/SoundWords.h"
#include "TaggerReference.inc"

using namespace subtest;
using namespace sub::intelligence;
using namespace sub::intelligence::tagging;

namespace {

std::vector<float> fingerprintOf(const Samples& s) {
    static EssentiaExtractor extractor;  // (the tests run on one thread)
    std::vector<float> fp(essentiaSchema().dims());
    REQUIRE(extractor.extract(SoundBuffer{s.data(), s.size(), static_cast<uint32_t>(kRate)}, fp.data()) == Extraction::Done);
    return fp;
}

SoundTags tagsOf(const Samples& s) { return describe(fingerprintOf(s).data()); }

// A sound's outputs with all of one kind, the rest at `rest`.
std::vector<float> outputsOf(Kind kind, float rest = 0.f) {
    std::vector<float> out(static_cast<size_t>(outputCount()), rest);
    for (int k = 0; k < kKinds; ++k) out[output::Kinds + static_cast<size_t>(k)] = 0.f;
    out[output::Kinds + static_cast<size_t>(kind)] = 1.f;
    out[output::Loop] = 0.f;
    return out;
}

}  // namespace

TEST_CASE("the compiled-in model gives what it gave in PyTorch") {
    REQUIRE(kReferenceSounds > 0);
    std::vector<float> embedding(static_cast<size_t>(embeddingDims())), outputs(static_cast<size_t>(outputCount()));
    for (int s = 0; s < kReferenceSounds; ++s) {
        infer(kReferenceFeatures[s], embedding.data(), outputs.data());
        for (int k = 0; k < kKinds; ++k) CHECK_NEAR(outputs[output::Kinds + static_cast<size_t>(k)], kReferenceKinds[s][k], 2e-4);
        CHECK_NEAR(outputs[output::Loop], kReferenceLoop[s], 2e-4);
        for (int i = 0; i < embeddingDims(); ++i) CHECK_NEAR(embedding[static_cast<size_t>(i)], kReferenceEmbedding[s][i], 2e-3);
        for (int w = 0; w < wordCount(); ++w) CHECK_NEAR(outputs[output::Words + static_cast<size_t>(w)], kReferenceWords[s][w], 2e-4);
    }
}

TEST_CASE("the kinds' probabilities sum to one; every output is 0..1") {
    const std::vector<float> fp = fingerprintOf(snare());
    const float* out = fp.data() + kDims + embeddingDims();
    float sum = 0.f;
    for (int k = 0; k < kKinds; ++k) sum += out[k];
    CHECK_NEAR(sum, 1.f, 1e-4);
    for (int i = 0; i < outputCount(); ++i) CHECK(out[i] >= 0.f && out[i] <= 1.f);
}

TEST_CASE("drums are told apart, and so are one-shots from loops") {
    CHECK(tagsOf(kick()).kinds[0].kind == Kind::Kick);
    const Kind hatKind = tagsOf(hat()).kinds[0].kind;
    CHECK(hatKind == Kind::ClosedHat || hatKind == Kind::OpenHat || hatKind == Kind::Shaker);
    // (A snare with its body: Sounds.h's default is 70% full-band noise, as much a hat as a snare.)
    CHECK(tagsOf(snare(190.0, 0.12, 0.5)).kind() == Kind::Snare);
    CHECK(tagsOf(clap()).kinds[0].kind == Kind::Clap);
    CHECK(tagsOf(stab()).kinds[0].kind == Kind::Synth);
    CHECK(!tagsOf(kick()).isLoop());
    const Samples loop = sequence({kick(), hat(), snare(), hat(), kick(), hat(), snare(), hat()}, 0.25, 2.0);
    CHECK(tagsOf(loop).isLoop());
}

TEST_CASE("a tonal sound's pitch is its note; noise has none") {
    const SoundTags a2 = tagsOf(tone(110.0, 1.0, 0.8));
    REQUIRE(std::isfinite(a2.pitch));
    CHECK_EQ(noteName(a2.pitch), std::string("A2"));
    CHECK(a2[Attribute::Pitched] > 0.5f);
    const SoundTags hiss = tagsOf(noise(0.5));
    CHECK(!std::isfinite(hiss.pitch));
    CHECK(hiss[Attribute::Pitched] < 0.5f);
}

TEST_CASE("attributes are graded among the sound's own kind") {
    // The same descriptors, the brightness moved: brighter is Bright, darker isn't.
    std::vector<float> d = fingerprintOf(kick());
    const std::vector<float> outputs = outputsOf(Kind::Kick);
    d[feature::Centroid] = -5.f;  // (31 Hz)
    const SoundTags dark = describe(d.data(), outputs.data());
    d[feature::Centroid] = 2.f;  // (4 kHz)
    const SoundTags bright = describe(d.data(), outputs.data());
    CHECK(dark.kind() == Kind::Kick);
    CHECK(dark[Attribute::Bright] < 0.2f);
    CHECK(bright[Attribute::Bright] > 0.8f);
    // A centroid that is bright for a kick is dark for a hat.
    const SoundTags hatOne = describe(d.data(), outputsOf(Kind::ClosedHat).data());
    CHECK(hatOne[Attribute::Bright] < bright[Attribute::Bright]);
    // Long: by the effective duration, among the kind's.
    d[feature::Duration] = std::log10(0.02f);
    CHECK(describe(d.data(), outputs.data())[Attribute::Long] < 0.2f);
    d[feature::Duration] = std::log10(3.f);
    CHECK(describe(d.data(), outputs.data())[Attribute::Long] > 0.8f);
}

TEST_CASE("a kind the model isn't sure of isn't said") {
    std::vector<float> outputs = outputsOf(Kind::Snare);
    outputs[output::Kinds + static_cast<size_t>(Kind::Snare)] = 0.5f;
    outputs[output::Kinds + static_cast<size_t>(Kind::Clap)] = 0.5f;
    const std::vector<float> d = fingerprintOf(snare());
    const SoundTags unsure = describe(d.data(), outputs.data());
    CHECK(unsure.kind() == Kind::None || kindThreshold() <= 0.5f);
    CHECK_NEAR(unsure.probabilityOf(kindBit(Kind::Snare) | kindBit(Kind::Clap)), 1.f, 0.01);
}

TEST_CASE("words are read as kinds, loops, notes, descriptions and moods") {
    using Type = Meaning::Type;
    CHECK(meaningOf("kick").type == Type::Kinds);
    CHECK_EQ(meaningOf("kicks").kinds, kindBit(Kind::Kick));
    CHECK_EQ(meaningOf("Hi-Hats").kinds, kindBit(Kind::ClosedHat) | kindBit(Kind::OpenHat));
    CHECK_EQ(meaningOf("crashes").kinds, kindBit(Kind::Crash));
    CHECK_EQ(meaningOf("piano").kinds, kindBit(Kind::Keys));
    CHECK_EQ(meaningOf("vox").kinds, kindBit(Kind::Vocal));
    CHECK((meaningOf("drums").kinds & kindBit(Kind::Snare)) != 0);
    CHECK_EQ(meaningOf("plucks").kinds, kindBit(Kind::Synth));
    CHECK_EQ(meaningOf("synths").kinds, kindBit(Kind::Synth));
    CHECK(meaningOf("synth").type == Type::Blend);  // (synthesized: "synth bass" is a bass)
    CHECK(meaningOf("loop").type == Type::Loop);
    CHECK(meaningOf("one-shot").type == Type::OneShot);
    CHECK(meaningOf("chop").type == Type::OneShot);
    const Meaning c2 = meaningOf("c#2");
    CHECK(c2.type == Type::Note);
    CHECK_EQ(c2.note, 1);
    CHECK_EQ(c2.octave, 2);
    CHECK_EQ(meaningOf("F#").octave, -100);
    CHECK(meaningOf("a").type == Type::None);   // (a word, not a note)
    CHECK(meaningOf("be").type == Type::None);
    CHECK(meaningOf("bright").type == Type::Blend);
    CHECK(meaningOf("lo-fi").type == Type::Blend);
    CHECK(meaningOf("dubstep").type == Type::Blend);
    CHECK(meaningOf("trap").type == Type::Blend);
    CHECK(meaningOf("chill").type == Type::Mood);
    CHECK(meaningOf("emo").type == Type::Mood);
    CHECK(meaningOf("zyxw").type == Type::None);
    // A blend's weights sum to one, and only learned words are in it.
    for (const char* word : {"hard", "punchy", "dubstep", "acoustic", "trap", "lofi", "fat"}) {
        INFO(word);
        const Meaning m = meaningOf(word);
        float total = 0.f;
        for (const Meaning::Part& p : m.parts) {
            total += p.weight;
            if (p.of == Meaning::Part::Of::Word) CHECK(wordLearned(p.index));
        }
        CHECK_NEAR(total, 1.f, 1e-5);
    }
}

TEST_CASE("a search hears its words in sounds; moods order, never leave out") {
    const std::vector<float> d = fingerprintOf(kick());
    std::vector<float> outputs = outputsOf(Kind::Kick);
    const SoundTags kickTags = describe(d.data(), outputs.data());
    const WordQuery query({"low", "chill", "kick", "zyxw"});
    CHECK(query.any());
    float score = 0.f;
    const uint64_t heard = query.hear(kickTags, score);
    CHECK((heard & 4u) != 0);  // kick
    CHECK((heard & 2u) != 0);  // chill: every sound
    CHECK((heard & 8u) == 0);  // not a word it knows: only names match it
    CHECK(score > 0.f && score <= 1.f);
    const SoundTags snareTags = describe(d.data(), outputsOf(Kind::Snare).data());
    CHECK((query.hear(snareTags, score) & 4u) == 0);
    CHECK(!WordQuery({"zyxw", "folder"}).any());
}

TEST_CASE("a sound is labelled with its kind, then what it clearly is") {
    std::vector<float> d = fingerprintOf(kick());
    d[feature::Centroid] = 3.f;
    const SoundTags t = describe(d.data(), outputsOf(Kind::Kick).data());
    const std::vector<std::string> words = labelWords(t);
    REQUIRE(!words.empty());
    CHECK_EQ(words.front(), std::string("Kick"));
    CHECK(std::find(words.begin(), words.end(), std::string("Bright")) != words.end());
    CHECK(label(t).rfind("Kick \xC2\xB7 ", 0) == 0);
    CHECK(labelWords(t, 0).size() <= 2);  // (the kind, and its note if it has one)
    std::vector<float> loopOutputs = outputsOf(Kind::Kick);
    loopOutputs[output::Loop] = 0.9f;
    const std::vector<std::string> loopWords = labelWords(describe(d.data(), loopOutputs.data()));
    CHECK(std::find(loopWords.begin(), loopWords.end(), std::string("Loop")) != loopWords.end());
}

TEST_CASE("the tag table finds a path, also by its folder's hash and its name") {
    SoundTags a, b;
    a.loop = 10;
    b.loop = 200;
    const std::string folder = "/samples/Drums";
    const std::string path = folder + "/Kick 01.wav";
    const SoundTagTable table({{SoundTagTable::pathHash(path), a}, {SoundTagTable::pathHash("/elsewhere.wav"), b}}, 7);
    CHECK_EQ(table.size(), size_t{2});
    CHECK_EQ(table.version(), uint64_t{7});
    REQUIRE(table.find(path) != nullptr);
    CHECK_EQ(table.find(path)->loop, uint8_t{10});
    CHECK(table.find("/nowhere.wav") == nullptr);
    const uint64_t byParts = SoundTagTable::finish(SoundTagTable::extend(sub::platform::fnv1a(folder + "/"), "Kick 01.wav"));
    CHECK_EQ(byParts, SoundTagTable::pathHash(path));
}

TEST_CASE("sounds fit a project by its sounds and its key") {
    // No sounds, no key: everything fits alike.
    const ProjectFit nothing;
    CHECK(nothing.empty());
    SoundTags some;
    CHECK_NEAR(nothing.fit(some), 0.5f, 1e-6);
    // A key: a note on its tonic or fifth fits best, in its scale well, out of it not.
    const ProjectFit aMinor({}, ProjectKey{9, true});
    SoundTags note;
    note.pitch = 57.f;  // A3
    CHECK_NEAR(aMinor.keyFit(note), 1.f, 1e-6);
    note.pitch = 60.f;  // C4: in A minor
    CHECK(aMinor.keyFit(note) > 0.5f);
    note.pitch = 61.f;  // C#4: not
    CHECK(aMinor.keyFit(note) < 0.5f);
    SoundTags unpitched;
    CHECK_NEAR(aMinor.keyFit(unpitched), 0.5f, 1e-6);
    // Its sounds: one like them fits better than one unlike them.
    std::vector<float> d = fingerprintOf(kick());
    d[feature::Centroid] = -3.f;
    d[feature::AttackTime] = -0.5f;
    const SoundTags darkSoft = describe(d.data(), outputsOf(Kind::Kick).data());
    d[feature::Centroid] = 3.f;
    d[feature::AttackTime] = -3.f;
    const SoundTags brightHard = describe(d.data(), outputsOf(Kind::Kick).data());
    const ProjectFit project({darkSoft, darkSoft}, std::nullopt);
    CHECK(!project.empty());
    CHECK(project.neighbourFit(darkSoft) > project.neighbourFit(brightHard));
    CHECK(project.fit(darkSoft) > project.fit(brightHard));
    CHECK_NEAR(project.neighbourFit(darkSoft), 1.f, 1e-5);
}

TEST_CASE("tagging a sound takes no time worth speaking of") {
    const std::vector<float> d = fingerprintOf(snare());
    std::vector<float> embedding(static_cast<size_t>(embeddingDims())), outputs(static_cast<size_t>(outputCount()));
    const auto start = std::chrono::steady_clock::now();
    constexpr int kRuns = 200;
    for (int i = 0; i < kRuns; ++i) infer(d.data(), embedding.data(), outputs.data());
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kRuns;
    INFO("microseconds a sound: " + std::to_string(us));
    CHECK(us < 2000.0);  // (about 22 in a release build: generous for debug builds and slow machines)
}
