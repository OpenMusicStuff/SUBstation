#include "plugins/Vst2Preset.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace sub::vst2 {

namespace {

constexpr size_t kNameSize = 28;
constexpr size_t kBankReserved = 128;  // (version 2: the current program, then 124)

constexpr uint32_t fourCC(const char (&code)[5]) {
    return (uint32_t(uint8_t(code[0])) << 24) | (uint32_t(uint8_t(code[1])) << 16) |
           (uint32_t(uint8_t(code[2])) << 8) | uint32_t(uint8_t(code[3]));
}

constexpr uint32_t kChunkMagic = fourCC("CcnK");
constexpr uint32_t kProgram = fourCC("FxCk");
constexpr uint32_t kProgramChunk = fourCC("FPCh");
constexpr uint32_t kBank = fourCC("FxBk");
constexpr uint32_t kBankChunk = fourCC("FBCh");

class Writer {
public:
    void u32(uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
    void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
    void f32(float value) { u32(std::bit_cast<uint32_t>(value)); }
    void text(const std::string& value, size_t size) {
        const size_t used = std::min(value.size(), size - 1);
        bytes.insert(bytes.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(used));
        bytes.insert(bytes.end(), size - used, 0);
    }
    void zeros(size_t count) { bytes.insert(bytes.end(), count, 0); }
    void raw(const std::vector<uint8_t>& data) { bytes.insert(bytes.end(), data.begin(), data.end()); }
    // The size field after "CcnK" at `at`: the bytes after it.
    void patchSize(size_t at) {
        const auto size = static_cast<uint32_t>(bytes.size() - at - 4);
        for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<uint8_t>(size >> (24 - 8 * i));
    }

    std::vector<uint8_t> bytes;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool u32(uint32_t& value) {
        if (size_ - at_ < 4) return false;
        value = (uint32_t(data_[at_]) << 24) | (uint32_t(data_[at_ + 1]) << 16) | (uint32_t(data_[at_ + 2]) << 8) |
                uint32_t(data_[at_ + 3]);
        at_ += 4;
        return true;
    }
    bool i32(int32_t& value) {
        uint32_t raw = 0;
        if (!u32(raw)) return false;
        value = static_cast<int32_t>(raw);
        return true;
    }
    bool f32(float& value) {
        uint32_t raw = 0;
        if (!u32(raw)) return false;
        value = std::bit_cast<float>(raw);
        return true;
    }
    bool text(std::string& value, size_t size) {
        if (size_ - at_ < size) return false;
        const auto* begin = reinterpret_cast<const char*>(data_ + at_);
        value.assign(begin, strnlen(begin, size));
        at_ += size;
        return true;
    }
    bool skip(size_t count) {
        if (size_ - at_ < count) return false;
        at_ += count;
        return true;
    }
    bool raw(std::vector<uint8_t>& out, size_t count) {
        if (size_ - at_ < count) return false;
        out.assign(data_ + at_, data_ + at_ + count);
        at_ += count;
        return true;
    }
    size_t left() const noexcept { return size_ - at_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t at_ = 0;
};

void writeOne(Writer& out, const Preset& preset) {
    out.u32(kChunkMagic);
    const size_t sizeAt = out.bytes.size();
    out.u32(0);
    switch (preset.kind) {
        case Preset::Kind::Program: out.u32(kProgram); break;
        case Preset::Kind::ProgramChunk: out.u32(kProgramChunk); break;
        case Preset::Kind::Bank: out.u32(kBank); break;
        case Preset::Kind::BankChunk: out.u32(kBankChunk); break;
    }
    out.i32(preset.isBank() ? 2 : 1);
    out.i32(preset.pluginId);
    out.i32(preset.pluginVersion);
    if (preset.isBank()) {
        const auto count = preset.kind == Preset::Kind::Bank ? static_cast<int32_t>(preset.programs.size())
                                                             : preset.numPrograms;
        out.i32(count);
        out.i32(preset.currentProgram);
        out.zeros(kBankReserved - 4);
        if (preset.kind == Preset::Kind::Bank) {
            for (const Preset& program : preset.programs) {
                Preset one = program;
                one.kind = Preset::Kind::Program;
                one.pluginId = preset.pluginId;
                one.pluginVersion = preset.pluginVersion;
                writeOne(out, one);
            }
        }
    } else {
        out.i32(preset.kind == Preset::Kind::Program ? static_cast<int32_t>(preset.values.size()) : preset.numParams);
        out.text(preset.name, kNameSize);
        if (preset.kind == Preset::Kind::Program) {
            for (const float value : preset.values) out.f32(value);
        }
    }
    if (preset.hasChunk()) {
        out.u32(static_cast<uint32_t>(preset.chunk.size()));
        out.raw(preset.chunk);
    }
    out.patchSize(sizeAt);
}

std::optional<Preset> readOne(Reader& in) {
    uint32_t magic = 0, size = 0, kind = 0;
    if (!in.u32(magic) || magic != kChunkMagic || !in.u32(size) || !in.u32(kind)) return std::nullopt;
    Preset preset;
    if (kind == kProgram) {
        preset.kind = Preset::Kind::Program;
    } else if (kind == kProgramChunk) {
        preset.kind = Preset::Kind::ProgramChunk;
    } else if (kind == kBank) {
        preset.kind = Preset::Kind::Bank;
    } else if (kind == kBankChunk) {
        preset.kind = Preset::Kind::BankChunk;
    } else {
        return std::nullopt;
    }
    int32_t version = 0;
    if (!in.i32(version) || !in.i32(preset.pluginId) || !in.i32(preset.pluginVersion)) return std::nullopt;
    if (preset.isBank()) {
        if (!in.i32(preset.numPrograms) || preset.numPrograms < 0) return std::nullopt;
        if (version >= 2) {
            if (!in.i32(preset.currentProgram) || !in.skip(kBankReserved - 4)) return std::nullopt;
        } else if (!in.skip(kBankReserved)) {
            return std::nullopt;
        }
        if (preset.kind == Preset::Kind::Bank) {
            for (int32_t p = 0; p < preset.numPrograms; ++p) {
                auto program = readOne(in);
                if (!program || program->kind != Preset::Kind::Program) return std::nullopt;
                preset.programs.push_back(std::move(*program));
            }
        }
    } else {
        if (!in.i32(preset.numParams) || preset.numParams < 0 || !in.text(preset.name, kNameSize)) return std::nullopt;
        if (preset.kind == Preset::Kind::Program) {
            if (in.left() / 4 < static_cast<size_t>(preset.numParams)) return std::nullopt;
            preset.values.resize(static_cast<size_t>(preset.numParams));
            for (float& value : preset.values) in.f32(value);
        }
    }
    if (preset.hasChunk()) {
        uint32_t chunkSize = 0;
        if (!in.u32(chunkSize) || !in.raw(preset.chunk, chunkSize)) return std::nullopt;
    }
    return preset;
}

}  // namespace

std::vector<uint8_t> writePreset(const Preset& preset) {
    Writer out;
    writeOne(out, preset);
    return std::move(out.bytes);
}

std::optional<Preset> readPreset(const uint8_t* data, size_t size) {
    if (!data) return std::nullopt;
    Reader in(data, size);
    return readOne(in);
}

std::string vst2Uid(int32_t id) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    const auto value = static_cast<uint32_t>(id);
    std::string text(8, '0');
    for (int i = 0; i < 8; ++i) text[7 - i] = kDigits[(value >> (4 * i)) & 0xF];
    return text;
}

std::optional<int32_t> vst2IdFromUid(const std::string& uid) {
    if (uid.size() != 8) return std::nullopt;
    uint32_t value = 0;
    for (const char c : uid) {
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        if (digit < 0) return std::nullopt;
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    return static_cast<int32_t>(value);
}

}  // namespace sub::vst2
