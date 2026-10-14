// How well a sound fits a project: what the browser's Best sort suggests first
// (with how well it matches the search and how much it was used).
//
// A project's sounds (the files its audio clips and samplers play, as the
// tagger hears them) say what it sounds like:
//
// - its genre and character: the learned words' degrees averaged over its
//   sounds (a project of dubstep-ish, distorted sounds); a sound fits as much
//   as it is called the words the project is, weighed by how much the project
//   is each;
// - its nearest neighbour: how close the sound's description (its attributes
//   and learned words, not its kind) is to the closest of the project's own,
//   so a dark, soft kick fits a project of dark, soft sounds whatever they are;
// - its key (the project's, or the one its notes are in): a sound with a clear
//   pitch fits if its note is in the key's scale (best on the tonic or the
//   fifth), and not if it isn't. Sounds with no pitch are neutral.
//
// A project with no sounds the tagger knows, and no key, makes every sound fit
// as well (0.5): Best is then use and matching alone.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "tagging/SoundTagger.h"

namespace sub::intelligence::tagging {

struct ProjectKey {
    int tonic = 0;  // pitch class, 0 = C
    bool minor = false;
};

class ProjectFit {
public:
    static constexpr size_t kNeighbours = 12;  // the project's sounds compared with (the first, as given)

    ProjectFit() = default;
    ProjectFit(const std::vector<SoundTags>& sounds, std::optional<ProjectKey> key);

    bool empty() const { return neighbours_.empty() && !key_; }
    // 0..1: 0.5 for a project that says nothing.
    float fit(const SoundTags& sound) const;
    // The parts apart (tests): genre and character, nearest neighbour, key (each 0..1; 0.5 when not known).
    float genreFit(const SoundTags& sound) const;
    float neighbourFit(const SoundTags& sound) const;
    float keyFit(const SoundTags& sound) const;

    // The words the project is most (learned words whose mean degree is
    // clearly above a sound's usual), the most first: "Techno", "Punchy".
    std::vector<std::string> words(size_t most = 2) const;
    const std::optional<ProjectKey>& key() const { return key_; }

private:
    static constexpr size_t kDescription = kAttributes + kMaxWords;
    // A sound's description as compared: its attributes, and its learned words' degrees; how many it wrote.
    size_t describe(const SoundTags& sound, float* out) const;

    std::vector<std::vector<float>> neighbours_;
    std::vector<float> profile_;  // the project's mean degree of each learned word, less 0.5 (what it is more than not)
    std::vector<int> words_;      // the learned words (indices)
    std::optional<ProjectKey> key_;
};

}  // namespace sub::intelligence::tagging
