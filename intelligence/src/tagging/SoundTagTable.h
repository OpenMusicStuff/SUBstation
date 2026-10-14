// The tags of a library's sounds, looked up by path: what the browser's
// searches and rows read. Immutable once made, and shared freely (the browser's
// search thread reads it while the index makes the next one).
//
// Paths are looked up by their 64-bit FNV-1a hash (SimilarityResult's
// pathHash: the path as the library gave it), in an open-addressing table, so
// the browser can look a file up from its folder's path and its name without
// joining them (pathHash() of the two, as Snapshot::join would join them).

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "tagging/SoundTagger.h"

namespace sub::intelligence::tagging {

class SoundTagTable {
public:
    // The hash a path is looked up by (never 0).
    static uint64_t pathHash(std::string_view path);
    // Of `path` continued with `more` (a hash of a folder's path and its
    // separator, then a name): the same as pathHash() of the two joined.
    static uint64_t extend(uint64_t hash, std::string_view more);
    static uint64_t finish(uint64_t hash) { return hash ? hash : 1; }

    explicit SoundTagTable(std::vector<std::pair<uint64_t, SoundTags>> sounds, uint64_t version = 0);

    uint64_t version() const { return version_; }  // the index's, when it was made
    size_t size() const { return tags_.size(); }
    const SoundTags* find(uint64_t hash) const;
    const SoundTags* find(std::string_view path) const { return find(pathHash(path)); }
    // Where a sound find() gave is in the table (0..size()): for what is kept per sound beside it.
    size_t indexOf(const SoundTags* tags) const { return static_cast<size_t>(tags - tags_.data()); }

private:
    std::vector<uint64_t> keys_;   // open addressing; 0 is empty
    std::vector<uint32_t> slots_;  // into tags_
    std::vector<SoundTags> tags_;
    uint64_t mask_ = 0;
    uint64_t version_ = 0;
};

}  // namespace sub::intelligence::tagging
