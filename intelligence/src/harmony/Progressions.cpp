#include "harmony/Progressions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <optional>

#include "harmony/ProgressionData.inc"

namespace sub::intelligence::harmony {

namespace {

constexpr int kKinds = data::kKinds;
constexpr int kStates = data::kStates;
using Row = std::array<double, kStates>;

constexpr int kRareCount = 400;         // drawn: chords Billboard plays at least 1 in this many times...
constexpr double kBillboardPrior = 5.0; // a Billboard row's pull to how often each chord plays (counts)
constexpr double kMoodPrior = 1.0;      // a mood's row's pull to how often each chord plays in it (counts)
constexpr double kMoodWeight = 0.5;     // the mood's say in each next chord's chance (an exponent)
constexpr double kRepeat = 1e-4;        // a chord following itself: only where locked chords make it so
constexpr int kAttempts = 60;           // draws tried for an idea that is new and plays like one

constexpr std::array<const char*, kMoods> kMoodNames{"Sad", "Dark", "Nostalgic"};
constexpr std::array<int, 7> kMajorScale{0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> kMinorScale{0, 2, 3, 5, 7, 8, 10};
constexpr std::array<const char*, 7> kNumerals{"I", "II", "III", "IV", "V", "VI", "VII"};

// SplitMix64: the same numbers from the same seed everywhere.
class Random {
public:
    explicit Random(uint64_t seed) : state_(seed) {}
    uint64_t next() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // [0, 1).
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    // [0, n), n > 0.
    int below(int n) { return std::min(n - 1, static_cast<int>(uniform() * n)); }
    // An index picked by its weight (none positive: -1).
    template <typename Weights>
    int pick(const Weights& weights) {
        const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
        if (!(total > 0.0)) return -1;
        double at = uniform() * total;
        int last = -1;
        for (int i = 0; i < static_cast<int>(weights.size()); ++i) {
            if (weights[i] <= 0.0) continue;
            last = i;
            if (at < weights[i]) return i;
            at -= weights[i];
        }
        return last;
    }

private:
    uint64_t state_;
};

int kindOf(Quality quality) {
    switch (quality) {
    case Quality::Major:
    case Quality::Dominant7:
    case Quality::Major7: return 0;
    case Quality::Minor:
    case Quality::Minor7: return 1;
    case Quality::Diminished:
    case Quality::HalfDiminished7:
    case Quality::Diminished7: return 2;
    case Quality::Augmented: return 3;
    case Quality::Sus2: return 4;
    case Quality::Sus4: return 5;
    }
    return 0;
}

// A chord in a key: (semitones above the tonic) * kKinds + its kind.
int stateOf(const Chord& chord, const Key& key) { return pitchClass(chord.root - key.tonic) * kKinds + kindOf(chord.quality); }
Chord chordOf(int state, const Key& key) {
    return {pitchClass(key.tonic + state / kKinds), static_cast<Quality>(state % kKinds)};
}

bool hasMood(int seed, Mood mood) { return (data::kSeedMoods[seed] >> static_cast<int>(mood)) & 1; }

// A mood's progression's chords in `key`: in its relative key where its mode isn't the key's.
std::vector<Chord> seedChords(int seed, const Key& key) {
    const int shift = data::kSeedMinor[seed] == key.minor ? 0 : key.minor ? 3 : -3;
    std::vector<Chord> chords;
    for (int i = data::kSeedStart[seed]; i < data::kSeedStart[seed + 1]; ++i) {
        chords.push_back({pitchClass(key.tonic + shift + data::kSeedChords[2 * i]),
                          static_cast<Quality>(data::kSeedChords[2 * i + 1])});
    }
    return chords;
}

// What the draws pick chords by, for a key's mode and a mood.
struct Model {
    Key key;
    std::vector<double> next = std::vector<double>(kStates * kStates, 0.0);  // P(b | a) at [a * kStates + b]
    Row start{};                                                              // the first chord's chances
    std::array<bool, kStates> drawn{};

    double chance(int from, int to) const { return from == to ? kRepeat : next[from * kStates + to]; }
    // Into a chord that is fixed (locked, or kept from another mood): one the draws never pick (a colour swapped
    // in) is still reached, rarely.
    double reaching(int from, int to) const { return std::max(chance(from, to), kRepeat); }
};

Model buildModel(const ProgressionSettings& settings) {
    Model m;
    m.key = settings.key;
    const int mode = settings.key.minor ? 1 : 0;
    const int* billboard = data::kTransitions[mode];
    const int* counts = data::kCounts[mode];

    // The mood's progressions in the key: how often each chord plays, follows another (round and round), starts.
    std::vector<double> moodNext(kStates * kStates, 0.0);
    Row moodCount{};
    for (int seed = 0; seed < data::kSeedCount; ++seed) {
        if (!hasMood(seed, settings.mood)) continue;
        std::vector<int> states;
        for (const Chord& c : seedChords(seed, settings.key)) states.push_back(stateOf(c, settings.key));
        m.start[states.front()] += 1.0;
        for (size_t i = 0; i < states.size(); ++i) {
            moodCount[states[i]] += 1.0;
            const int a = states[i], b = states[(i + 1) % states.size()];
            if (a != b) moodNext[a * kStates + b] += 1.0;
        }
    }

    const double total = std::accumulate(counts, counts + kStates, 0.0);
    Row played{}, moodPlayed{};
    for (int s = 0; s < kStates; ++s) {
        m.drawn[s] = counts[s] * kRareCount >= total || moodCount[s] > 0.0;
        if (m.drawn[s]) played[s] = counts[s];
    }
    const double playedTotal = std::accumulate(played.begin(), played.end(), 0.0);
    const double moodTotal = std::accumulate(moodCount.begin(), moodCount.end(), 0.0);
    for (int s = 0; s < kStates; ++s) {
        if (playedTotal > 0.0) played[s] /= playedTotal;
        if (m.drawn[s]) moodPlayed[s] = (moodCount[s] + played[s]) / (moodTotal + 1.0);
    }

    for (int a = 0; a < kStates; ++a) {
        double billboardRow = 0.0, moodRow = 0.0;
        for (int b = 0; b < kStates; ++b) {
            if (b == a || !m.drawn[b]) continue;
            billboardRow += billboard[a * kStates + b];
            moodRow += moodNext[a * kStates + b];
        }
        double sum = 0.0;
        for (int b = 0; b < kStates; ++b) {
            if (b == a || !m.drawn[b]) continue;
            const double fromBillboard =
                (billboard[a * kStates + b] + kBillboardPrior * played[b]) / (billboardRow + kBillboardPrior);
            const double fromMood = (moodNext[a * kStates + b] + kMoodPrior * moodPlayed[b]) / (moodRow + kMoodPrior);
            // Both must agree: a chord the mood's progressions never play there comes up rarely.
            const double p = std::pow(fromBillboard, 1.0 - kMoodWeight) * std::pow(fromMood, kMoodWeight);
            m.next[a * kStates + b] = p;
            sum += p;
        }
        if (sum > 0.0) {
            for (int b = 0; b < kStates; ++b) m.next[a * kStates + b] /= sum;
        }
    }
    return m;
}

// States for a loop of fixed.size() chords: fixed[i] where it is >= 0, the rest
// drawn so that each fits the chord before it and the one after it, the last
// leading into the first. With none fixed, the first is drawn from m.start.
// Empty if no loop can be made.
std::vector<int> drawStates(const Model& m, std::vector<int> fixed, Random& rng) {
    const int n = static_cast<int>(fixed.size());
    if (n == 0) return {};
    auto first = std::find_if(fixed.begin(), fixed.end(), [](int s) { return s >= 0; });
    if (first == fixed.end()) {
        int start = rng.pick(m.start);
        if (start < 0) {
            Row any{};
            for (int s = 0; s < kStates; ++s) any[s] = m.drawn[s] ? 1.0 : 0.0;
            start = rng.pick(any);
        }
        if (start < 0) return {};
        fixed[0] = start;
        first = fixed.begin();
    }
    // Turned so a fixed chord is first: y[t] = fixed[(r + t) % n], y[n] = y[0].
    const int r = static_cast<int>(first - fixed.begin());
    const auto at = [&](int t) { return fixed[static_cast<size_t>((r + t) % n)]; };
    const auto allowed = [&](int t, int s) { return at(t) >= 0 ? at(t) == s : m.drawn[s]; };
    const auto chance = [&](int from, int to, int t) { return at(t) >= 0 ? m.reaching(from, to) : m.chance(from, to); };

    // Backward: beta[t][s], the chance (scaled) of reaching y[n] from s at t.
    std::vector<Row> beta(static_cast<size_t>(n) + 1, Row{});
    beta[n][at(0)] = 1.0;
    for (int t = n - 1; t >= 1; --t) {
        double sum = 0.0;
        for (int s = 0; s < kStates; ++s) {
            if (!allowed(t, s)) continue;
            double v = 0.0;
            for (int u = 0; u < kStates; ++u) {
                if (beta[t + 1][u] > 0.0) v += chance(s, u, t + 1) * beta[t + 1][u];
            }
            beta[t][s] = v;
            sum += v;
        }
        if (!(sum > 0.0)) return {};
        for (double& v : beta[t]) v /= sum;
    }
    // Forward: each chord by the one before it and what it leads to.
    std::vector<int> y(static_cast<size_t>(n));
    y[0] = at(0);
    for (int t = 1; t < n; ++t) {
        Row w{};
        for (int s = 0; s < kStates; ++s) w[s] = chance(y[t - 1], s, t) * beta[t][s];
        y[t] = rng.pick(w);
        if (y[t] < 0) return {};
    }
    std::vector<int> states(static_cast<size_t>(n));
    for (int t = 0; t < n; ++t) states[static_cast<size_t>((r + t) % n)] = y[t];
    return states;
}

// A fresh draw plays like a progression: three different chords or more (two
// in a shorter one), the key's tonic or its relative among them.
bool playsLikeOne(const std::vector<int>& states, const Key& key) {
    std::vector<int> distinct = states;
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    const size_t n = states.size();
    if (distinct.size() < std::min<size_t>(n, n >= 4 ? 3 : 2)) return false;
    const int tonic = key.minor ? 1 : 0;                         // i, I (on the tonic: minor, major)
    const int relative = key.minor ? 3 * kKinds : 9 * kKinds + 1;  // III, vi
    return std::any_of(states.begin(), states.end(), [&](int s) { return s == tonic || s == relative; });
}

bool taken(const std::vector<Chord>& chords, const std::vector<std::vector<Chord>>& avoid) {
    return std::find(avoid.begin(), avoid.end(), chords) != avoid.end();
}

std::vector<Chord> chordsOf(const std::vector<int>& states, const Key& key) {
    std::vector<Chord> chords;
    for (const int s : states) chords.push_back(chordOf(s, key));
    return chords;
}

// A fresh drawn idea of n chords not in `avoid`; none if none was found.
std::optional<std::vector<Chord>> freshIdea(const Model& m, int n, Random& rng,
                                            const std::vector<std::vector<Chord>>& avoid) {
    std::optional<std::vector<Chord>> fallback;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const std::vector<int> states = drawStates(m, std::vector<int>(static_cast<size_t>(n), -1), rng);
        if (states.empty()) break;
        std::vector<Chord> chords = chordsOf(states, m.key);
        if (taken(chords, avoid)) continue;
        if (playsLikeOne(states, m.key)) return chords;
        if (!fallback) fallback = std::move(chords);
    }
    return fallback;
}

// A mood's progression fitted to `length` chords: cut, played again, or with
// the bars after it drawn.
std::vector<Chord> fitted(int seed, const Model& m, int length, Random& rng) {
    std::vector<Chord> chords = seedChords(seed, m.key);
    const int n = static_cast<int>(chords.size());
    if (n >= length) {
        chords.resize(static_cast<size_t>(length));
        return chords;
    }
    if (length % n == 0) {
        std::vector<Chord> repeated;
        for (int i = 0; i < length; ++i) repeated.push_back(chords[static_cast<size_t>(i % n)]);
        return repeated;
    }
    std::vector<int> fixed(static_cast<size_t>(length), -1);
    for (int i = 0; i < n; ++i) fixed[static_cast<size_t>(i)] = stateOf(chords[static_cast<size_t>(i)], m.key);
    const std::vector<int> states = drawStates(m, fixed, rng);
    for (int i = n; i < length; ++i) {
        chords.push_back(states.empty() ? chords[static_cast<size_t>(i % n)] : chordOf(states[static_cast<size_t>(i)], m.key));
    }
    return chords;
}

Quality triadOf(Quality quality) { return static_cast<Quality>(kindOf(quality)); }

bool isUpper(Quality quality) { return kindOf(quality) != 1 && kindOf(quality) != 2; }

}  // namespace

const char* moodName(Mood mood) { return kMoodNames[static_cast<size_t>(mood)]; }

int seedCount() { return data::kSeedCount; }

SeedInfo seedInfo(int index) {
    if (index < 0 || index >= data::kSeedCount) return {};
    return {data::kSeedNumerals[index], data::kSeedTags[index], data::kSeedMinor[index], data::kSeedMoods[index]};
}

std::vector<Idea> progressionIdeas(const ProgressionSettings& settings, int count, uint64_t seed,
                                   const std::vector<std::vector<Chord>>& avoid) {
    const int length = std::max(1, settings.length);
    const Model m = buildModel(settings);
    Random rng(seed);
    std::vector<int> pool;
    for (int i = 0; i < data::kSeedCount; ++i) {
        if (hasMood(i, settings.mood)) pool.push_back(i);
    }
    for (int i = static_cast<int>(pool.size()) - 1; i > 0; --i) std::swap(pool[i], pool[rng.below(i + 1)]);

    // The mood's own, fitted, in the order they come up. Drawn ideas don't take
    // their chords before they do: what is one of them is said to be.
    std::vector<std::vector<Chord>> own;
    for (const int s : pool) own.push_back(fitted(s, m, length, rng));

    std::vector<std::vector<Chord>> seen = avoid;
    std::vector<Idea> ideas;
    size_t nextSeed = 0;
    while (static_cast<int>(ideas.size()) < count) {
        bool added = false;
        if (ideas.size() % 2 == 0) {  // the mood's own, while there are any left
            while (!added && nextSeed < pool.size()) {
                const size_t i = nextSeed++;
                if (taken(own[i], seen)) continue;
                seen.push_back(own[i]);
                ideas.push_back({own[i], pool[i]});
                added = true;
            }
        }
        if (!added) {
            std::vector<std::vector<Chord>> unavailable = seen;
            unavailable.insert(unavailable.end(), own.begin() + static_cast<std::ptrdiff_t>(nextSeed), own.end());
            std::optional<std::vector<Chord>> chords = freshIdea(m, length, rng, unavailable);
            if (!chords) break;
            seen.push_back(*chords);
            ideas.push_back({std::move(*chords), -1});
        }
    }
    return ideas;
}

Idea redrawIdea(const std::vector<Chord>& chords, const std::vector<bool>& locked, const ProgressionSettings& settings,
                uint64_t seed, const std::vector<std::vector<Chord>>& avoid) {
    const int n = static_cast<int>(chords.size());
    if (n == 0) return {};
    const Model m = buildModel(settings);
    Random rng(seed);
    std::vector<std::vector<Chord>> seen = avoid;
    seen.push_back(chords);
    std::vector<int> fixed(static_cast<size_t>(n), -1);
    bool any = false;
    for (int i = 0; i < n && i < static_cast<int>(locked.size()); ++i) {
        if (locked[static_cast<size_t>(i)]) {
            fixed[static_cast<size_t>(i)] = stateOf(chords[static_cast<size_t>(i)], settings.key);
            any = true;
        }
    }
    if (!any) {
        std::optional<std::vector<Chord>> fresh = freshIdea(m, n, rng, seen);
        return fresh ? Idea{std::move(*fresh), -1} : Idea{chords, -1};
    }
    std::optional<std::vector<Chord>> fallback;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const std::vector<int> states = drawStates(m, fixed, rng);
        if (states.empty()) break;
        std::vector<Chord> result = chords;
        for (int i = 0; i < n; ++i) {
            if (fixed[static_cast<size_t>(i)] < 0) result[static_cast<size_t>(i)] = chordOf(states[static_cast<size_t>(i)], settings.key);
        }
        if (taken(result, seen)) continue;
        if (playsLikeOne(states, settings.key)) return {std::move(result), -1};
        if (!fallback) fallback = std::move(result);
    }
    return {fallback ? std::move(*fallback) : chords, -1};
}

std::vector<Idea> ideaVariations(const std::vector<Chord>& chords, const std::vector<bool>& locked,
                                 const ProgressionSettings& settings, int count, uint64_t seed,
                                 const std::vector<std::vector<Chord>>& avoid) {
    const int n = static_cast<int>(chords.size());
    std::vector<int> open;
    for (int i = 0; i < n; ++i) {
        if (i >= static_cast<int>(locked.size()) || !locked[static_cast<size_t>(i)]) open.push_back(i);
    }
    if (open.empty() || count <= 0) return {};
    const Model m = buildModel(settings);
    Random rng(seed);
    std::vector<int> fixed;
    for (const Chord& c : chords) fixed.push_back(stateOf(c, settings.key));
    std::vector<std::vector<Chord>> seen = avoid;
    seen.push_back(chords);
    std::vector<Idea> ideas;
    for (int attempt = 0; attempt < count * kAttempts && static_cast<int>(ideas.size()) < count; ++attempt) {
        std::vector<int> positions = open;
        const int changes = positions.size() >= 2 && rng.below(2) == 1 ? 2 : 1;
        for (int i = 0; i < changes; ++i) {
            std::swap(positions[static_cast<size_t>(i)], positions[static_cast<size_t>(i + rng.below(static_cast<int>(positions.size()) - i))]);
        }
        std::vector<int> draw = fixed;
        for (int i = 0; i < changes; ++i) draw[static_cast<size_t>(positions[static_cast<size_t>(i)])] = -1;
        const std::vector<int> states = drawStates(m, draw, rng);
        if (states.empty()) continue;
        std::vector<Chord> result = chords;
        for (int i = 0; i < changes; ++i) {
            const auto p = static_cast<size_t>(positions[static_cast<size_t>(i)]);
            result[p] = chordOf(states[p], settings.key);
        }
        if (taken(result, seen)) continue;
        seen.push_back(result);
        ideas.push_back({std::move(result), -1});
    }
    return ideas;
}

std::vector<Chord> chordAlternatives(const std::vector<Chord>& chords, int index, const ProgressionSettings& settings,
                                     int count) {
    const int n = static_cast<int>(chords.size());
    if (index < 0 || index >= n || count <= 0) return {};
    const Model m = buildModel(settings);
    const int current = stateOf(chords[static_cast<size_t>(index)], settings.key);
    const int before = stateOf(chords[static_cast<size_t>((index + n - 1) % n)], settings.key);
    const int after = stateOf(chords[static_cast<size_t>((index + 1) % n)], settings.key);
    std::vector<std::pair<double, int>> scored;
    for (int s = 0; s < kStates; ++s) {
        if (s == current || !m.drawn[s]) continue;
        scored.emplace_back(m.chance(before, s) * m.reaching(s, after), s);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<Chord> result;
    for (const auto& [score, s] : scored) {
        if (static_cast<int>(result.size()) == count) break;
        result.push_back(chordOf(s, settings.key));
    }
    return result;
}

std::vector<Chord> chordColours(const Chord& chord, const Key& key) {
    const Chord plain{chord.root, chord.quality};
    std::vector<Chord> colours{withSeventh(plain, key), {chord.root, triadOf(chord.quality)}};
    const int kind = kindOf(chord.quality);
    if (kind == 0 || kind == 1) {
        colours.push_back({chord.root, Quality::Sus4});
        colours.push_back({chord.root, Quality::Sus2});
    } else if (kind == 4 || kind == 5) {
        colours.push_back({chord.root, Quality::Major});
        colours.push_back({chord.root, Quality::Minor});
    }
    std::vector<Chord> result;
    for (const Chord& c : colours) {
        if (c != plain && std::find(result.begin(), result.end(), c) == result.end()) result.push_back(c);
    }
    return result;
}

Chord withSeventh(const Chord& chord, const Key& key) {
    switch (chord.quality) {
    case Quality::Major:
        return {chord.root, key.contains(chord.root + 11) || !key.contains(chord.root + 10) ? Quality::Major7
                                                                                             : Quality::Dominant7};
    case Quality::Minor: return {chord.root, Quality::Minor7};
    case Quality::Diminished:
        return {chord.root, key.contains(chord.root + 10) ? Quality::HalfDiminished7 : Quality::Diminished7};
    default: return chord;
    }
}

std::string romanNumeral(const Chord& chord, const Key& key) {
    const int offset = pitchClass(chord.root - key.tonic);
    const auto& scale = key.minor ? kMinorScale : kMajorScale;
    std::string accidental;
    int degree = static_cast<int>(std::find(scale.begin(), scale.end(), offset) - scale.begin());
    if (degree == 7) {
        // Between two degrees: bII, bIII, #IV, bVI, bVII in a major key; bII, #III, #IV, #VI, #VII in a minor one.
        const bool flat = key.minor ? offset == 1 : offset != 6;
        accidental = flat ? "b" : "#";
        const int target = flat ? offset + 1 : offset - 1;
        degree = static_cast<int>(std::find(scale.begin(), scale.end(), target) - scale.begin());
    }
    std::string numeral = kNumerals[static_cast<size_t>(degree % 7)];
    if (!isUpper(chord.quality)) std::transform(numeral.begin(), numeral.end(), numeral.begin(), [](char c) { return c - 'A' + 'a'; });
    static constexpr std::array<const char*, kQualities> kSuffixes{
        "", "", "°", "+", "sus2", "sus4", "7", "maj7", "7", "ø7", "°7"};
    return accidental + numeral + kSuffixes[static_cast<size_t>(chord.quality)];
}

}  // namespace sub::intelligence::harmony
