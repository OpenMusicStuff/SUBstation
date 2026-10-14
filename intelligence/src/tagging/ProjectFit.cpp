#include "tagging/ProjectFit.h"

#include <algorithm>
#include <cmath>

#include "tagging/SoundWords.h"

namespace sub::intelligence::tagging {

namespace {

constexpr float kGenre = 0.35f, kNeighbour = 0.35f, kKey = 0.3f;  // how much each part counts
constexpr float kClearly = 0.08f;  // a project "is" a word whose mean degree is this much above 0.5

// The pitch classes of a key's scale (natural minor for a minor key), its tonic and fifth marked.
bool inScale(int pitchClass, const ProjectKey& key) {
    static constexpr int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int kMinor[7] = {0, 2, 3, 5, 7, 8, 10};
    const int step = (pitchClass - key.tonic + 12) % 12;
    const int* scale = key.minor ? kMinor : kMajor;
    return std::find(scale, scale + 7, step) != scale + 7;
}

}  // namespace

ProjectFit::ProjectFit(const std::vector<SoundTags>& sounds, std::optional<ProjectKey> key) : key_(key) {
    for (int w = 0; w < wordCount(); ++w)
        if (wordLearned(w)) words_.push_back(w);
    if (!sounds.empty()) {
        profile_.assign(words_.size(), 0.f);
        for (const SoundTags& s : sounds)
            for (size_t i = 0; i < words_.size(); ++i) profile_[i] += wordDegree(s, words_[i]);
        for (float& p : profile_) p = p / static_cast<float>(sounds.size()) - 0.5f;
        for (size_t i = 0; i < sounds.size() && i < kNeighbours; ++i) {
            std::vector<float> d(kDescription);
            d.resize(describe(sounds[i], d.data()));
            neighbours_.push_back(std::move(d));
        }
    }
}

size_t ProjectFit::describe(const SoundTags& sound, float* out) const {
    size_t n = 0;
    for (int a = 0; a < kAttributes; ++a) out[n++] = sound[static_cast<Attribute>(a)];
    for (const int w : words_) out[n++] = wordDegree(sound, w);
    return n;
}

float ProjectFit::genreFit(const SoundTags& sound) const {
    float weight = 0.f, sum = 0.f;
    for (size_t i = 0; i < profile_.size(); ++i) {
        if (profile_[i] <= kClearly) continue;  // (what the project isn't particularly says nothing)
        weight += profile_[i];
        sum += profile_[i] * wordDegree(sound, words_[i]);
    }
    return weight > 0.f ? sum / weight : 0.5f;
}

float ProjectFit::neighbourFit(const SoundTags& sound) const {
    if (neighbours_.empty()) return 0.5f;
    float d[kDescription];  // (no allocation: the Best sort asks this of every file)
    const size_t dims = describe(sound, d);
    float best = 0.f;
    for (const std::vector<float>& n : neighbours_) {
        float distance = 0.f;
        for (size_t i = 0; i < dims; ++i) distance += std::abs(d[i] - n[i]);
        best = std::max(best, 1.f - distance / static_cast<float>(dims));
    }
    // Two unrelated descriptions are about 0.65 alike: spread that to 0..1.
    return std::clamp((best - 0.65f) / 0.35f, 0.f, 1.f);
}

float ProjectFit::keyFit(const SoundTags& sound) const {
    if (!key_ || !std::isfinite(sound.pitch)) return 0.5f;
    const int pitchClass = static_cast<int>((std::lround(sound.pitch) % 12 + 12) % 12);
    const int step = (pitchClass - key_->tonic + 12) % 12;
    if (step == 0 || step == 7) return 1.f;
    return inScale(pitchClass, *key_) ? 0.85f : 0.1f;
}

float ProjectFit::fit(const SoundTags& sound) const {
    return kGenre * genreFit(sound) + kNeighbour * neighbourFit(sound) + kKey * keyFit(sound);
}

std::vector<std::string> ProjectFit::words(size_t most) const {
    std::vector<std::pair<float, int>> ranked;
    for (size_t i = 0; i < profile_.size(); ++i)
        if (profile_[i] > kClearly) ranked.push_back({profile_[i], words_[i]});
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (size_t i = 0; i < ranked.size() && i < most; ++i) out.push_back(wordTitle(wordName(ranked[i].second)));
    return out;
}

}  // namespace sub::intelligence::tagging
