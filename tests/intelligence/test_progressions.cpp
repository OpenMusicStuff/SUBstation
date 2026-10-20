// Progression ideas: the moods' own progressions in the song's key (cut,
// repeated or filled out to the length, in the relative key where their mode
// isn't the key's), drawn ones that play like progressions, the same ideas from
// the same seed, redrawing what isn't locked, variations, chords to swap in and
// colours, sevenths as the key has them, Roman numerals, and laying a
// progression out a chord a bar.

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "harmony/Accompaniment.h"
#include "harmony/Progressions.h"
#include "harness/Test.h"

using namespace sub::intelligence::harmony;

namespace {

const Key kAMinor{9, true};
const Key kCMajor{0, false};

// "Am F Dm Em".
std::string names(const std::vector<Chord>& chords) {
    std::string text;
    for (const Chord& c : chords) text += (text.empty() ? "" : " ") + c.name();
    return text;
}

// "i VI iv v".
std::string numerals(const std::vector<Chord>& chords, const Key& key) {
    std::string text;
    for (const Chord& c : chords) text += (text.empty() ? "" : " ") + romanNumeral(c, key);
    return text;
}

int seedNamed(const std::string& numerals) {
    for (int i = 0; i < seedCount(); ++i) {
        if (numerals == seedInfo(i).numerals) return i;
    }
    return -1;
}

// The idea made from a mood's progression, among plenty of them.
std::optional<Idea> ideaFrom(int seed, const ProgressionSettings& settings) {
    for (const Idea& idea : progressionIdeas(settings, 200, 7)) {
        if (idea.seed == seed) return idea;
    }
    return std::nullopt;
}

bool followsItself(const std::vector<Chord>& chords) {
    for (size_t i = 0; i < chords.size(); ++i) {
        if (chords.size() > 1 && chords[i] == chords[(i + 1) % chords.size()]) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("the moods' progressions are free-midi-chords' as it writes them") {
    CHECK_EQ(seedCount(), 80);
    CHECK_EQ(std::string(moodName(Mood::Sad)), std::string("Sad"));
    CHECK_EQ(std::string(moodName(Mood::Nostalgic)), std::string("Nostalgic"));
    const int sad = seedNamed("i VI iv v");
    REQUIRE(sad >= 0);
    CHECK_EQ(std::string(seedInfo(sad).tags), std::string("Sad Hopeful"));
    CHECK(seedInfo(sad).minor);
    CHECK_EQ(seedInfo(sad).moods, 1 << static_cast<int>(Mood::Sad));
    const int both = seedNamed("i VI iv III");  // Sad Nostalgic
    REQUIRE(both >= 0);
    CHECK_EQ(seedInfo(both).moods, (1 << static_cast<int>(Mood::Sad)) | (1 << static_cast<int>(Mood::Nostalgic)));
    const int modal = seedNamed("im bIIIM bVIIM IV");  // written over the major scale, its tonic minor
    REQUIRE(modal >= 0);
    CHECK(seedInfo(modal).minor);
    CHECK_EQ(std::string(seedInfo(-1).numerals), std::string(""));
    CHECK_EQ(std::string(seedInfo(seedCount()).numerals), std::string(""));
    int perMood[kMoods] = {};
    for (int i = 0; i < seedCount(); ++i) {
        for (int m = 0; m < kMoods; ++m) perMood[m] += (seedInfo(i).moods >> m) & 1;
    }
    CHECK_EQ(perMood[0], 13);
    CHECK_EQ(perMood[1], 15);
    CHECK_EQ(perMood[2], 59);
}

TEST_CASE("a mood's progressions in the key: as written, cut, repeated or filled out") {
    const ProgressionSettings sad{kAMinor, Mood::Sad, 4};
    const std::optional<Idea> plain = ideaFrom(seedNamed("i VI iv v"), sad);
    REQUIRE(plain.has_value());
    CHECK_EQ(names(plain->chords), std::string("Am F Dm Em"));
    CHECK_EQ(numerals(plain->chords, kAMinor), std::string("i VI iv v"));

    // Eight chords cut to four; colours kept as written (VIadd9 is its triad).
    const std::optional<Idea> cut = ideaFrom(seedNamed("i VII VIadd9 VII i v VI VIIsus4"), sad);
    REQUIRE(cut.has_value());
    CHECK_EQ(names(cut->chords), std::string("Am G F G"));

    // Three chords: the fourth drawn, to lead back into the first.
    const std::optional<Idea> filled = ideaFrom(seedNamed("i iv v"), sad);
    REQUIRE(filled.has_value());
    REQUIRE(filled->chords.size() == 4);
    CHECK_EQ(names({filled->chords.begin(), filled->chords.begin() + 3}), std::string("Am Dm Em"));
    CHECK(filled->chords[3] != filled->chords[2]);
    CHECK(filled->chords[3] != filled->chords[0]);

    // Eight bars: a four-chord one played twice.
    const std::optional<Idea> twice = ideaFrom(seedNamed("i VI iv v"), {kAMinor, Mood::Sad, 8});
    REQUIRE(twice.has_value());
    CHECK_EQ(names(twice->chords), std::string("Am F Dm Em Am F Dm Em"));

    // In a major key, a minor one goes into its relative minor; in a minor key, a major one into its relative major.
    const std::optional<Idea> relativeMinor = ideaFrom(seedNamed("i VI iv v"), {kCMajor, Mood::Sad, 4});
    REQUIRE(relativeMinor.has_value());
    CHECK_EQ(names(relativeMinor->chords), std::string("Am F Dm Em"));
    CHECK_EQ(numerals(relativeMinor->chords, kCMajor), std::string("vi IV ii iii"));
    const std::optional<Idea> relativeMajor = ideaFrom(seedNamed("I iii IV vi"), {kAMinor, Mood::Nostalgic, 4});
    REQUIRE(relativeMajor.has_value());
    CHECK_EQ(names(relativeMajor->chords), std::string("C Em F Am"));
    // ... in another key: E minor.
    const std::optional<Idea> inE = ideaFrom(seedNamed("i VI iv v"), {Key{4, true}, Mood::Sad, 4});
    REQUIRE(inE.has_value());
    CHECK_EQ(names(inE->chords), std::string("Em C Am Bm"));
}

TEST_CASE("ideas: the mood's own and drawn ones, one after the other, all different, the same from the same seed") {
    for (const Mood mood : {Mood::Sad, Mood::Dark, Mood::Nostalgic}) {
        const ProgressionSettings settings{kAMinor, mood, 4};
        const std::vector<Idea> ideas = progressionIdeas(settings, 6, 42);
        REQUIRE(ideas.size() == 6);
        std::set<std::string> distinct;
        for (size_t i = 0; i < ideas.size(); ++i) {
            INFO(std::string(moodName(mood)) + " " + names(ideas[i].chords));
            CHECK_EQ(ideas[i].chords.size(), size_t{4});
            if (i % 2 == 0) {
                REQUIRE(ideas[i].seed >= 0);
                CHECK((seedInfo(ideas[i].seed).moods >> static_cast<int>(mood)) & 1);
            } else {
                CHECK_EQ(ideas[i].seed, -1);
            }
            distinct.insert(names(ideas[i].chords));
        }
        CHECK_EQ(distinct.size(), size_t{6});
        CHECK(progressionIdeas(settings, 6, 42) == ideas);
        CHECK(progressionIdeas(settings, 6, 43) != ideas);
    }

    // Avoided ideas don't come again; when the mood's own run out, all are drawn.
    const ProgressionSettings sad{kAMinor, Mood::Sad, 4};
    const std::vector<Idea> first = progressionIdeas(sad, 6, 1);
    std::vector<std::vector<Chord>> avoid;
    for (const Idea& idea : first) avoid.push_back(idea.chords);
    for (const Idea& idea : progressionIdeas(sad, 6, 1, avoid)) {
        CHECK(std::find(avoid.begin(), avoid.end(), idea.chords) == avoid.end());
    }
    const std::vector<Idea> many = progressionIdeas(sad, 60, 3);
    CHECK_EQ(many.size(), size_t{60});
    // (Two of the 13 might fit to the same chords: the second is left out.)
    const auto own = std::count_if(many.begin(), many.end(), [](const Idea& i) { return i.seed >= 0; });
    CHECK(own >= 12);
    CHECK(own <= 13);
    CHECK(progressionIdeas(sad, 0, 3).empty());
}

TEST_CASE("drawn progressions play like progressions in the key") {
    std::map<std::string, int> played;
    int chords = 0, inKey = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        for (const Idea& idea : progressionIdeas({kAMinor, Mood::Sad, 4}, 6, seed)) {
            if (idea.seed >= 0) continue;
            INFO(names(idea.chords));
            std::set<std::string> distinct;
            for (const Chord& c : idea.chords) distinct.insert(c.name());
            CHECK(distinct.size() >= 3);
            CHECK(distinct.count("Am") || distinct.count("C"));  // the tonic or its relative
            CHECK(!followsItself(idea.chords));                  // nor the last into the first
            for (const Chord& c : idea.chords) {
                ++played[c.name()];
                ++chords;
                // The natural minor's chords, or its dominant (harmonic minor's V).
                if (kAMinor.isDiatonic(c) || c == (Chord{4, Quality::Major})) ++inKey;
            }
        }
    }
    CHECK(inKey > chords * 95 / 100);  // (the mood and Billboard must agree on a chord)
    // A minor's own chords are what plays most.
    std::vector<std::pair<int, std::string>> byCount;
    for (const auto& [name, count] : played) byCount.emplace_back(count, name);
    std::sort(byCount.rbegin(), byCount.rend());
    REQUIRE(byCount.size() >= 4);
    const std::set<std::string> common{"Am", "F", "C", "G", "Dm", "Em", "E"};
    for (int i = 0; i < 4; ++i) CHECK(common.count(byCount[static_cast<size_t>(i)].second));
    CHECK_EQ(byCount[0].second, std::string("Am"));

    // Eight chords, in a major key too.
    for (const Idea& idea : progressionIdeas({kCMajor, Mood::Nostalgic, 8}, 6, 5)) {
        CHECK_EQ(idea.chords.size(), size_t{8});
        CHECK(!followsItself(idea.chords) || idea.seed >= 0);
    }
}

TEST_CASE("redrawing keeps the locked chords where they are and draws the rest") {
    const ProgressionSettings sad{kAMinor, Mood::Sad, 4};
    const std::vector<Chord> chords{{9, Quality::Minor}, {5, Quality::Major7}, {2, Quality::Minor}, {4, Quality::Major}};
    for (uint64_t seed = 0; seed < 20; ++seed) {
        const Idea idea = redrawIdea(chords, {true, true, false, false}, sad, seed);
        INFO(names(idea.chords));
        REQUIRE(idea.chords.size() == 4);
        CHECK(idea.chords[0] == chords[0]);
        CHECK(idea.chords[1] == chords[1]);  // Fmaj7 kept as it is
        CHECK(idea.chords != chords);
        CHECK_EQ(idea.seed, -1);
        CHECK(idea.chords[2] != idea.chords[3]);
        CHECK(idea.chords[3] != idea.chords[0]);
    }
    // All locked: nothing to draw.
    CHECK(redrawIdea(chords, {true, true, true, true}, sad, 1).chords == chords);
    // None locked: a fresh idea of its length.
    const Idea fresh = redrawIdea(chords, {}, {kAMinor, Mood::Sad, 8}, 1);
    CHECK_EQ(fresh.chords.size(), size_t{4});
    CHECK(fresh.chords != chords);
    // Neighbours locked to the same chord: it plays twice there.
    const std::vector<Chord> twice{{9, Quality::Minor}, {9, Quality::Minor}, {5, Quality::Major}, {7, Quality::Major}};
    const Idea held = redrawIdea(twice, {true, true, false, false}, sad, 2);
    CHECK(held.chords[0] == twice[0]);
    CHECK(held.chords[1] == twice[1]);
    CHECK(held.chords != twice);
    CHECK(redrawIdea({}, {}, sad, 1).chords.empty());
}

TEST_CASE("variations change one or two chords that aren't locked") {
    const ProgressionSettings sad{kAMinor, Mood::Sad, 4};
    const std::vector<Chord> chords{{9, Quality::Minor}, {5, Quality::Major}, {2, Quality::Minor}, {4, Quality::Minor}};
    const std::vector<Idea> variations = ideaVariations(chords, {true, false, false, false}, sad, 5, 9);
    CHECK_EQ(variations.size(), size_t{5});
    std::set<std::string> distinct;
    for (const Idea& v : variations) {
        INFO(names(v.chords));
        REQUIRE(v.chords.size() == 4);
        CHECK(v.chords[0] == chords[0]);
        int changed = 0;
        for (size_t i = 0; i < 4; ++i) changed += v.chords[i] != chords[i];
        CHECK(changed >= 1);
        CHECK(changed <= 2);
        distinct.insert(names(v.chords));
    }
    CHECK_EQ(distinct.size(), variations.size());
    CHECK(ideaVariations(chords, {true, true, true, true}, sad, 5, 9).empty());
    CHECK(ideaVariations(chords, {}, sad, 5, 9) == ideaVariations(chords, {}, sad, 5, 9));
}

TEST_CASE("chords to swap in: those that lead from the one before into the one after") {
    const ProgressionSettings sad{kAMinor, Mood::Sad, 4};
    const std::vector<Chord> chords{{9, Quality::Minor}, {5, Quality::Major}, {0, Quality::Major}, {7, Quality::Major}};
    const std::vector<Chord> instead = chordAlternatives(chords, 1, sad, 8);
    CHECK_EQ(instead.size(), size_t{8});
    std::set<std::string> distinct;
    for (const Chord& c : instead) {
        CHECK(!(c.root == 5 && (c.quality == Quality::Major || c.quality == Quality::Major7)));  // not F itself
        CHECK(static_cast<int>(c.quality) < 6);  // triads
        distinct.insert(c.name());
    }
    CHECK_EQ(distinct.size(), instead.size());
    const Chord dominant{4, Quality::Major};  // (harmonic minor's V)
    for (int i = 0; i < 3; ++i) {
        const Chord& c = instead[static_cast<size_t>(i)];
        CHECK(kAMinor.isDiatonic(c) || c == dominant);
    }
    // A seventh counts as its triad: no F for Fmaj7 either.
    std::vector<Chord> coloured = chords;
    coloured[1] = {5, Quality::Major7};
    for (const Chord& c : chordAlternatives(coloured, 1, sad, 8)) CHECK(c.name() != "F");
    CHECK(chordAlternatives(chords, 4, sad, 8).empty());
    CHECK(chordAlternatives(chords, 0, sad, 0).empty());
    CHECK_EQ(chordAlternatives({{9, Quality::Minor}}, 0, sad, 3).size(), size_t{3});
}

TEST_CASE("colours, sevenths as the key has them, and Roman numerals") {
    const auto namesOf = [](const std::vector<Chord>& chords) { return names(chords); };
    CHECK_EQ(namesOf(chordColours({9, Quality::Minor}, kAMinor)), std::string("Am7 Asus4 Asus2"));
    CHECK_EQ(namesOf(chordColours({5, Quality::Major7}, kAMinor)), std::string("F Fsus4 Fsus2"));
    CHECK_EQ(namesOf(chordColours({7, Quality::Sus4}, kAMinor)), std::string("G Gm"));
    CHECK_EQ(namesOf(chordColours({11, Quality::Diminished}, kAMinor)), std::string("Bm7b5"));
    CHECK_EQ(namesOf(chordColours({0, Quality::Major, 4}, kCMajor)), std::string("Cmaj7 Csus4 Csus2"));

    const auto seventh = [](int root, Quality q, const Key& key) { return withSeventh({root, q}, key).name(); };
    CHECK_EQ(seventh(5, Quality::Major, kAMinor), std::string("Fmaj7"));
    CHECK_EQ(seventh(0, Quality::Major, kAMinor), std::string("Cmaj7"));
    CHECK_EQ(seventh(7, Quality::Major, kAMinor), std::string("G7"));
    CHECK_EQ(seventh(4, Quality::Major, kAMinor), std::string("E7"));  // the key has D, not D#
    CHECK_EQ(seventh(2, Quality::Minor, kAMinor), std::string("Dm7"));
    CHECK_EQ(seventh(11, Quality::Diminished, kAMinor), std::string("Bm7b5"));
    CHECK_EQ(seventh(7, Quality::Major, kCMajor), std::string("G7"));
    CHECK_EQ(seventh(2, Quality::Major, kCMajor), std::string("D7"));
    CHECK_EQ(seventh(1, Quality::Diminished, kCMajor), std::string("C#m7b5"));  // the key has B
    CHECK_EQ(seventh(8, Quality::Diminished, kCMajor), std::string("Abdim7"));  // ... but not F#
    CHECK_EQ(seventh(7, Quality::Sus4, kCMajor), std::string("Gsus4"));
    CHECK_EQ(seventh(7, Quality::Dominant7, kCMajor), std::string("G7"));

    const auto numeral = [](int root, Quality q, const Key& key) { return romanNumeral({root, q}, key); };
    CHECK_EQ(numeral(9, Quality::Minor, kAMinor), std::string("i"));
    CHECK_EQ(numeral(5, Quality::Major, kAMinor), std::string("VI"));
    CHECK_EQ(numeral(7, Quality::Major, kAMinor), std::string("VII"));
    CHECK_EQ(numeral(4, Quality::Major, kAMinor), std::string("V"));
    CHECK_EQ(numeral(4, Quality::Dominant7, kAMinor), std::string("V7"));
    CHECK_EQ(numeral(2, Quality::Minor7, kAMinor), std::string("iv7"));
    CHECK_EQ(numeral(11, Quality::Diminished, kAMinor), std::string("ii°"));
    CHECK_EQ(numeral(11, Quality::HalfDiminished7, kAMinor), std::string("iiø7"));
    CHECK_EQ(numeral(10, Quality::Major, kAMinor), std::string("bII"));
    CHECK_EQ(numeral(0, Quality::Augmented, kAMinor), std::string("III+"));
    CHECK_EQ(numeral(4, Quality::Sus4, kAMinor), std::string("Vsus4"));
    CHECK_EQ(numeral(6, Quality::Diminished, kAMinor), std::string("#vi°"));
    CHECK_EQ(numeral(1, Quality::Major, kAMinor), std::string("#III"));
    CHECK_EQ(numeral(10, Quality::Major, kCMajor), std::string("bVII"));
    CHECK_EQ(numeral(8, Quality::Major, kCMajor), std::string("bVI"));
    CHECK_EQ(numeral(5, Quality::Minor, kCMajor), std::string("iv"));
    CHECK_EQ(numeral(6, Quality::Diminished7, kCMajor), std::string("#iv°7"));
    CHECK_EQ(numeral(3, Quality::Major, kCMajor), std::string("bIII"));
    CHECK_EQ(numeral(0, Quality::Major7, kCMajor), std::string("Imaj7"));
    CHECK_EQ(numeral(9, Quality::Minor7, kCMajor), std::string("vi7"));
}

TEST_CASE("a progression laid out a chord a bar, round and round") {
    const std::vector<Chord> chords{{9, Quality::Minor}, {5, Quality::Major}};
    const std::vector<ChordSpan> spans = layOut(chords, 2, 14);
    REQUIRE(spans.size() == 4);
    CHECK(spans[0] == (ChordSpan{2, 4, chords[0]}));
    CHECK(spans[1] == (ChordSpan{4, 8, chords[1]}));
    CHECK(spans[2] == (ChordSpan{8, 12, chords[0]}));
    CHECK(spans[3] == (ChordSpan{12, 14, chords[1]}));
    CHECK_EQ(layOut(chords, 0, 6, 3).size(), size_t{2});  // bars of three
    CHECK(layOut({}, 0, 8).empty());
    CHECK(layOut(chords, 4, 4).empty());
}
