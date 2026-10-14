#include "tagging/SoundWords.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sub::intelligence::tagging {

namespace {

using Part = Meaning::Part;
using Of = Part::Of;
using Type = Meaning::Type;

constexpr uint32_t bits(std::initializer_list<Kind> kinds) {
    uint32_t mask = 0;
    for (const Kind k : kinds) mask |= kindBit(k);
    return mask;
}

Part attr(Attribute a, bool up = true, float weight = 1.f) { return {Of::Attribute, static_cast<int>(a), up, weight}; }
// A learned word, by name (one the model hasn't is left out of the blend).
Part learned(const char* name, float weight = 1.f) { return {Of::Word, wordIndex(name), true, weight}; }

struct Entry {
    const char* words;  // the word and the words that mean the same, separated by spaces
    Meaning meaning;
    std::vector<Part> fallback = {};  // the blend when none of its learned words was learned
};

Meaning kinds(uint32_t mask) { return {Type::Kinds, mask}; }
Meaning blend(std::vector<Part> parts) { return {Type::Blend, 0, -1, -100, std::move(parts)}; }
Meaning mood(std::vector<Part> parts) { return {Type::Mood, 0, -1, -100, std::move(parts)}; }

// The words the tagger reads. Kinds and their names; then descriptions: what
// is measured (attributes, among the sound's own kind) blended with what people
// call sounds (learned from Freesound's tags); then genres (learned where
// there were sounds enough tagged with them, else what they stand for); then
// moods, which no library labels: they order, never filter.
std::vector<Entry> vocabulary() {
    using enum Attribute;
    using K = Kind;
    return {
        {"kick kik bd bassdrum kickdrum", kinds(bits({K::Kick}))},
        {"snare snr sd snaredrum", kinds(bits({K::Snare}))},
        {"clap handclap", kinds(bits({K::Clap}))},
        {"hat hihat hh hi", kinds(bits({K::ClosedHat, K::OpenHat}))},
        {"closed chh closedhat", kinds(bits({K::ClosedHat}))},
        {"open ohh openhat", kinds(bits({K::OpenHat}))},
        {"crash", kinds(bits({K::Crash}))},
        {"ride", kinds(bits({K::Ride}))},
        {"cymbal cym", kinds(bits({K::Crash, K::Ride, K::OpenHat}))},
        {"tom tomtom", kinds(bits({K::Tom}))},
        {"rim rimshot sidestick", kinds(bits({K::Rim}))},
        {"cowbell", kinds(bits({K::Cowbell}))},
        {"shaker maraca maracas", kinds(bits({K::Shaker}))},
        {"tambourine tamb", kinds(bits({K::Tambourine}))},
        {"perc percussion conga bongo clave timbale djembe cajon agogo guiro woodblock",
         kinds(bits({K::Percussion, K::Cowbell, K::Shaker, K::Tambourine, K::Rim}))},
        {"drum drumkit kit", kinds(kDrumMask)},
        {"bass bassline subbass", kinds(bits({K::Bass}))},
        {"keys piano keyboard rhodes epiano wurlitzer", kinds(bits({K::Keys}))},
        {"organ", kinds(bits({K::Organ}))},
        {"guitar gtr", kinds(bits({K::Guitar}))},
        {"mallet bell marimba xylophone vibraphone vibes glockenspiel kalimba celesta",
         kinds(bits({K::Mallet}))},
        {"strings string violin viola cello contrabass orchestral", kinds(bits({K::Strings}))},
        {"brass trumpet horn trombone tuba", kinds(bits({K::Brass}))},
        {"woodwind wind flute sax saxophone clarinet oboe bassoon reed", kinds(bits({K::Woodwind}))},
        {"vocal vox voice choir sing singing sung spoken speech acapella", kinds(bits({K::Vocal}))},
        {"synths lead pad pluck stab arp supersaw", kinds(bits({K::Synth}))},
        {"tonal melodic instrument", kinds(~kDrumMask & ((1u << kKinds) - 1))},

        {"loop break groove beat", {Type::Loop}},
        {"oneshot chop", {Type::OneShot}},  // ("hit" and "shot": words of names, not sounds)

        {"bright crisp airy sharp brilliant", blend({attr(Bright, true, 0.65f), learned("bright", 0.35f)})},
        {"dark warm dull muffled mellow", blend({attr(Bright, false)})},
        {"hard aggressive harsh", blend({attr(Hard, true, 0.5f), learned("hard", 0.5f)})},
        {"punchy punch", blend({attr(Hard, true, 0.4f), learned("punchy", 0.6f)}), {attr(Hard)}},
        {"snappy tight", blend({attr(Hard, true, 0.4f), attr(Long, false, 0.3f), learned("tight", 0.3f)})},
        {"soft gentle smooth quiet", blend({attr(Hard, false, 0.7f), learned("soft", 0.3f)})},
        {"long sustained ringing ring decay", blend({attr(Long)})},
        {"short staccato", blend({attr(Long, false)})},
        {"low deep sub boomy", blend({attr(Low, true, 0.65f), learned("deep", 0.35f)})},
        {"high", blend({attr(Low, false)})},
        {"fat thick big huge phat massive", blend({learned("fat", 0.5f), attr(Low, true, 0.25f), attr(Long, true, 0.25f)}),
         {attr(Low, true, 0.5f), attr(Long, true, 0.5f)}},
        {"heavy", blend({learned("heavy", 0.5f), attr(Low, true, 0.25f), attr(Distorted, true, 0.25f)}),
         {attr(Low, true, 0.5f), attr(Distorted, true, 0.5f)}},
        {"distorted distortion dirty gritty crunchy saturated overdriven overdrive fuzz",
         blend({attr(Distorted, true, 0.5f), learned("distorted", 0.5f)})},
        {"clean", blend({attr(Distorted, false)})},
        {"wet reverb reverby roomy room spacious hall echo", blend({attr(Wet, true, 0.6f), learned("reverb", 0.4f)})},
        {"dry", blend({attr(Wet, false, 0.6f), learned("dry", 0.4f)})},
        {"acoustic real live natural organic", blend({attr(Acoustic, true, 0.7f), learned("acoustic", 0.3f)})},
        {"electric", blend({attr(Electric)})},
        {"synth synthetic synthesized synthesizer electronic electro digital analog analogue modular",
         blend({attr(Synthetic, true, 0.6f), learned("electronic", 0.4f)})},
        {"pitched tuned", blend({attr(Pitched)})},
        {"noisy unpitched atonal noise", blend({attr(Pitched, false)})},
        {"vintage retro old oldschool classic", blend({learned("vintage", 0.6f), attr(Bright, false, 0.2f), attr(Hard, false, 0.2f)})},
        {"lofi bitcrushed crushed dusty", blend({learned("lofi", 0.6f), attr(Bright, false, 0.2f), attr(Distorted, true, 0.2f)})},
        {"metallic metal steel iron", blend({learned("metallic", 0.6f), attr(Bright, true, 0.25f), attr(Long, true, 0.15f)})},
        {"wooden wood", blend({learned("wooden")})},
        {"glitch glitchy", blend({learned("glitch")})},
        {"industrial", blend({learned("industrial", 0.6f), attr(Distorted, true, 0.2f), attr(Hard, true, 0.2f)})},
        {"processed layered", blend({learned("processed", 0.6f), attr(Distorted, true, 0.2f), attr(Wet, true, 0.2f)})},

        // Genres: the learned word (where it was learned) and the attributes it stands for.
        {"house", blend({learned("house", 0.6f), attr(Synthetic, true, 0.25f), attr(Hard, true, 0.15f)})},
        {"techno", blend({learned("techno", 0.6f), attr(Synthetic, true, 0.2f), attr(Hard, true, 0.1f), attr(Bright, false, 0.1f)})},
        {"trance", blend({learned("trance", 0.6f), attr(Synthetic, true, 0.25f), attr(Bright, true, 0.15f)})},
        {"dubstep brostep riddim",
         blend({learned("dubstep", 0.6f), attr(Distorted, true, 0.15f), attr(Synthetic, true, 0.15f), attr(Hard, true, 0.1f)})},
        {"dnb drumnbass drumandbass jungle neurofunk",
         blend({learned("dnb", 0.6f), attr(Hard, true, 0.15f), attr(Bright, true, 0.15f), attr(Long, false, 0.1f)})},
        {"hiphop rap boombap", blend({learned("hiphop", 0.6f), attr(Hard, true, 0.2f), attr(Bright, false, 0.2f)})},
        {"trap", blend({learned("trap", 0.3f), learned("808", 0.2f), learned("hiphop", 0.2f), attr(Synthetic, true, 0.15f),
                        attr(Hard, true, 0.15f)})},
        {"edm dance club bigroom", blend({learned("edm", 0.6f), attr(Synthetic, true, 0.2f), attr(Hard, true, 0.2f)})},
        {"rock punk", blend({learned("rock", 0.6f), attr(Acoustic, true, 0.25f), attr(Hard, true, 0.15f)})},
        {"808 tr808", blend({learned("808", 0.6f), attr(Synthetic, true, 0.2f), attr(Low, true, 0.1f), attr(Long, true, 0.1f)})},
        {"909 tr909", blend({learned("909", 0.6f), attr(Synthetic, true, 0.25f), attr(Hard, true, 0.15f)})},

        {"chill chilled relaxed calm laidback", mood({attr(Hard, false, 0.4f), attr(Bright, false, 0.3f), attr(Distorted, false, 0.3f)})},
        {"emo sad melancholic melancholy", mood({attr(Wet, true, 0.4f), attr(Distorted, false, 0.3f), attr(Synthetic, false, 0.3f)})},
        {"happy uplifting cheerful", mood({attr(Bright, true, 0.6f), attr(Pitched, true, 0.4f)})},
        {"cinematic epic", mood({attr(Wet, true, 0.4f), attr(Long, true, 0.3f), attr(Low, true, 0.3f)})},
        {"ambient atmospheric dreamy", mood({attr(Wet, true, 0.5f), attr(Long, true, 0.3f), attr(Hard, false, 0.2f)})},
        {"angry evil menacing", mood({attr(Distorted, true, 0.4f), attr(Hard, true, 0.3f), attr(Bright, false, 0.3f)})},
    };
}

// Lower case without the marks words are written with ("hi-hat", "lo-fi", "one-shot", "drum'n'bass").
std::string normal(std::string_view word) {
    std::string out;
    for (const char c : word) {
        if (c == '-' || c == '_' || c == '.' || c == '\'' || c == '&' || c == '/') continue;
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

const std::unordered_map<std::string, Meaning>& table() {
    static const std::unordered_map<std::string, Meaning> words = [] {
        std::unordered_map<std::string, Meaning> map;
        for (Entry& e : vocabulary()) {
            Meaning meaning = e.meaning;
            if (meaning.type == Type::Blend || meaning.type == Type::Mood) {
                // Learned words the model has, and learned well enough; else what they stand for.
                std::vector<Part> parts;
                for (const Part& p : meaning.parts)
                    if (p.of == Of::Attribute || wordLearned(p.index)) parts.push_back(p);
                if (parts.empty()) parts = e.fallback;
                float total = 0.f;
                for (const Part& p : parts) total += p.weight;
                for (Part& p : parts) p.weight /= total;
                meaning.parts = std::move(parts);
                if (meaning.parts.empty()) continue;
            }
            std::string_view all = e.words;
            while (!all.empty()) {
                const size_t space = all.find(' ');
                map.emplace(std::string(all.substr(0, space)), meaning);
                all = space == std::string_view::npos ? std::string_view() : all.substr(space + 1);
            }
        }
        return map;
    }();
    return words;
}

// "c2", "f#", "a#1", "eb3": a note (with an octave, or a sharp or flat: "a" and "be" are words).
bool readNote(const std::string& w, Meaning& m) {
    if (w.size() < 2 || w.size() > 4 || w[0] < 'a' || w[0] > 'g') return false;
    static constexpr int kClass[7] = {9, 11, 0, 2, 4, 5, 7};  // a..g
    int note = kClass[w[0] - 'a'];
    size_t at = 1;
    bool accidental = false;
    if (w[at] == '#' || (w[at] == 'b' && at + 1 < w.size())) {
        note += w[at] == '#' ? 1 : -1;
        accidental = true;
        ++at;
    }
    int octave = -100;
    if (at < w.size()) {
        const bool negative = w[at] == '-';
        if (negative) ++at;
        if (at + 1 != w.size() || w[at] < '0' || w[at] > '9') return false;
        octave = (negative ? -1 : 1) * (w[at] - '0');
    } else if (!accidental) {
        return false;
    }
    m = {Type::Note};
    m.note = (note + 12) % 12;
    m.octave = octave;
    return true;
}

float partDegree(const Part& p, const SoundTags& t) {
    const float v = p.of == Of::Attribute ? t[static_cast<Attribute>(p.index)] : wordDegree(t, p.index);
    return p.up ? v : 1.f - v;
}

}  // namespace

Meaning meaningOf(std::string_view word) {
    const std::string w = normal(word);
    if (w.empty()) return {};
    const auto& words = table();
    if (const auto it = words.find(w); it != words.end()) return it->second;
    Meaning note;
    if (readNote(w, note)) return note;
    // Plurals: "kicks", "hats", "claps", "crashes", "brushes".
    for (const size_t cut : {size_t{1}, size_t{2}})
        if (w.size() > cut + 2 && w.back() == 's')
            if (const auto it = words.find(w.substr(0, w.size() - cut)); it != words.end()) return it->second;
    return {};
}

float Meaning::degree(const SoundTags& t) const {
    switch (type) {
        case Type::None: return 0.f;
        case Type::Kinds: return std::min(1.f, t.probabilityOf(kinds));
        case Type::Loop: return t.loopProbability();
        case Type::OneShot: return 1.f - t.loopProbability();
        case Type::Note: {
            if (!std::isfinite(t.pitch)) return 0.f;
            const long n = std::lround(t.pitch);
            const bool same = (n % 12 + 12) % 12 == note && (octave == -100 || n / 12 - 1 == octave);
            return same ? 1.f : 0.f;
        }
        case Type::Blend:
        case Type::Mood: {
            float sum = 0.f;
            for (const Part& p : parts) sum += p.weight * partDegree(p, t);
            return sum;
        }
    }
    return 0.f;
}

bool Meaning::heardIn(const SoundTags& t) const {
    switch (type) {
        case Type::None: return false;
        case Type::Mood: return true;
        case Type::Kinds: return degree(t) >= kKindMatch;
        default: return degree(t) >= 0.5f;
    }
}

WordQuery::WordQuery(const std::vector<std::string>& terms) {
    meanings_.reserve(terms.size());
    for (size_t i = 0; i < terms.size(); ++i) {
        meanings_.push_back(meaningOf(terms[i]));
        if (meanings_.back().type != Type::None && i < 64) heard_ |= uint64_t{1} << i;
    }
}

uint64_t WordQuery::hear(const SoundTags& tags, float& score) const {
    uint64_t heard = 0;
    double logSum = 0.0;
    int count = 0;
    for (size_t i = 0; i < meanings_.size() && i < 64; ++i) {
        const Meaning& m = meanings_[i];
        if (m.type == Type::None) continue;
        const float d = m.degree(tags);
        if (m.heardIn(tags)) heard |= uint64_t{1} << i;
        logSum += std::log(std::max(d, 1e-3f));
        ++count;
    }
    score = count ? static_cast<float>(std::exp(logSum / count)) : 0.f;
    return heard;
}

std::string wordTitle(std::string_view word) {
    if (word == "lofi") return "Lo-Fi";
    if (word == "dnb") return "DnB";
    if (word == "hiphop") return "Hip-Hop";
    if (word == "edm") return "EDM";
    std::string s(word);
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

namespace {

// What says the same as what (so a label says each thing once).
enum Group { Brightness, Attack, Length, Register, Source, Drive, Space, Genre, Material, Other };

Group groupOf(std::string_view word) {
    static const std::unordered_map<std::string_view, Group> groups = {
        {"bright", Brightness}, {"hard", Attack}, {"punchy", Attack}, {"soft", Attack}, {"tight", Attack},
        {"deep", Register}, {"high", Register}, {"fat", Register}, {"heavy", Register}, {"acoustic", Source},
        {"electronic", Source}, {"distorted", Drive}, {"reverb", Space}, {"dry", Space}, {"metallic", Material},
        {"wooden", Material}, {"house", Genre}, {"techno", Genre}, {"trance", Genre}, {"dubstep", Genre},
        {"dnb", Genre}, {"hiphop", Genre}, {"trap", Genre}, {"edm", Genre}, {"rock", Genre}, {"808", Genre},
        {"909", Genre}, {"vintage", Other}, {"lofi", Other}, {"glitch", Other}, {"industrial", Genre},
        {"processed", Other}};
    const auto it = groups.find(word);
    return it == groups.end() ? Other : it->second;
}

}  // namespace

std::vector<std::string> labelWords(const SoundTags& t, int traits) {
    std::vector<std::string> out;
    if (t.kind() != Kind::None) out.push_back(kindTitle(t.kind()));
    if (t.isLoop()) out.push_back("Loop");
    if (std::isfinite(t.pitch) && !t.isLoop()) out.push_back(noteName(t.pitch));
    // How it sounds: what it clearly is (strength 0..1), each group once, the strongest first.
    struct Trait {
        float strength;
        Group group;
        std::string word;
    };
    std::vector<Trait> found;
    auto measured = [&](Attribute a, Group g, const char* up, const char* down, float from) {
        const float v = t[a];
        if (up && v >= from) found.push_back({(v - 0.5f) * 2.f, g, up});
        if (down && v <= 1.f - from) found.push_back({(0.5f - v) * 2.f, g, down});
    };
    using enum Attribute;
    measured(Bright, Brightness, "Bright", "Dark", 0.85f);
    measured(Hard, Attack, "Hard", "Soft", 0.85f);
    measured(Long, Length, "Long", "Short", 0.85f);
    measured(Low, Register, isDrum(t.kind()) ? "Deep" : "Low", "High", 0.85f);
    measured(Acoustic, Source, "Acoustic", nullptr, 0.8f);
    measured(Synthetic, Source, "Synth", nullptr, 0.8f);
    measured(Electric, Source, "Electric", nullptr, 0.8f);
    measured(Distorted, Drive, "Distorted", nullptr, 0.8f);
    measured(Wet, Space, "Wet", nullptr, 0.8f);
    for (int w = 0; w < wordCount(); ++w) {
        if (!wordLearned(w)) continue;
        const float d = wordDegree(t, w);
        if (d >= 0.85f) found.push_back({(d - 0.5f) * 2.f * 0.9f, groupOf(wordName(w)), wordTitle(wordName(w))});
    }
    std::stable_sort(found.begin(), found.end(), [](const Trait& a, const Trait& b) { return a.strength > b.strength; });
    std::vector<Group> used;
    int added = 0;
    for (const Trait& f : found) {
        if (added >= traits) break;
        if (std::find(used.begin(), used.end(), f.group) != used.end() && f.group != Other) continue;
        if (std::find(out.begin(), out.end(), f.word) != out.end()) continue;
        used.push_back(f.group);
        out.push_back(f.word);
        ++added;
    }
    return out;
}

std::string label(const SoundTags& t, int traits) {
    std::string s;
    for (const std::string& w : labelWords(t, traits)) {
        if (!s.empty()) s += " \xC2\xB7 ";  // " · "
        s += w;
    }
    return s;
}

}  // namespace sub::intelligence::tagging
