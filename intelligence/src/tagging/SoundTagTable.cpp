#include "tagging/SoundTagTable.h"

#include "platform/Bytes.h"

namespace sub::intelligence::tagging {

uint64_t SoundTagTable::pathHash(std::string_view path) { return finish(platform::fnv1a(path)); }

uint64_t SoundTagTable::extend(uint64_t hash, std::string_view more) { return platform::fnv1a(more, hash); }

SoundTagTable::SoundTagTable(std::vector<std::pair<uint64_t, SoundTags>> sounds, uint64_t version) : version_(version) {
    size_t capacity = 16;
    while (capacity < sounds.size() * 2) capacity *= 2;
    keys_.assign(capacity, 0);
    slots_.assign(capacity, 0);
    mask_ = capacity - 1;
    tags_.reserve(sounds.size());
    for (auto& [hash, tags] : sounds) {
        const uint64_t key = finish(hash);
        for (uint64_t slot = key & mask_;; slot = (slot + 1) & mask_) {
            if (keys_[slot] == key) {
                tags_[slots_[slot]] = tags;  // (the same path twice: the last)
                break;
            }
            if (keys_[slot] == 0) {
                keys_[slot] = key;
                slots_[slot] = static_cast<uint32_t>(tags_.size());
                tags_.push_back(tags);
                break;
            }
        }
    }
}

const SoundTags* SoundTagTable::find(uint64_t hash) const {
    const uint64_t key = finish(hash);
    for (uint64_t slot = key & mask_;; slot = (slot + 1) & mask_) {
        if (keys_[slot] == key) return &tags_[slots_[slot]];
        if (keys_[slot] == 0) return nullptr;
    }
}

}  // namespace sub::intelligence::tagging
