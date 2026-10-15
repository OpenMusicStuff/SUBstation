#pragma once
// VST 2's preset files, .fxp (a program) and .fxb (a bank), which hold a VST2
// plug-in's state in a project as they do on disk: what Vst2Processor's
// getState() returns and setState() takes, and what Load/Save Preset read and
// write. Big-endian throughout:
//
//   "CcnK", the size of the rest, a kind, version (1, or 2 for a bank with its
//   current program), the plug-in's id, its version, then by kind:
//     "FxCk" a program's values:  count, name (28 bytes), count floats
//     "FPCh" a program's chunk:   count, name (28 bytes), size, chunk
//     "FxBk" a bank of programs:  count, current (v2) + 124 reserved bytes, count "FxCk" programs
//     "FBCh" a bank's chunk:      count, current (v2) + 124 reserved bytes, size, chunk
//
// No plug-in code: pure functions, any thread.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sub::vst2 {

struct Preset {
    enum class Kind : uint8_t {
        Program,       // FxCk: values
        ProgramChunk,  // FPCh: the plug-in's own data for a program
        Bank,          // FxBk: programs
        BankChunk,     // FBCh: the plug-in's own data for all its programs
    };
    Kind kind = Kind::Program;
    int32_t pluginId = 0;       // the plug-in's unique id
    int32_t pluginVersion = 0;
    int32_t numParams = 0;      // a program's parameter count (its values', or what it had, with a chunk)
    std::string name;           // a program's name (at most 27 bytes kept)
    std::vector<float> values;  // Program
    std::vector<uint8_t> chunk;          // ProgramChunk, BankChunk
    int32_t numPrograms = 0;             // a bank's
    int32_t currentProgram = 0;          // a bank's (version 2)
    std::vector<Preset> programs;        // Bank: Program each

    bool isBank() const noexcept { return kind == Kind::Bank || kind == Kind::BankChunk; }
    bool hasChunk() const noexcept { return kind == Kind::ProgramChunk || kind == Kind::BankChunk; }
};

std::vector<uint8_t> writePreset(const Preset& preset);
// None if the bytes are no .fxp or .fxb (or are cut short).
std::optional<Preset> readPreset(const uint8_t* data, size_t size);
inline std::optional<Preset> readPreset(const std::vector<uint8_t>& bytes) {
    return readPreset(bytes.data(), bytes.size());
}

// The id SUBstation knows a VST2 plug-in by (PluginDescription::uid): its unique
// id as 8 upper-case hex digits ('SuG1' is "53754731"); and back (none: not one).
std::string vst2Uid(int32_t id);
std::optional<int32_t> vst2IdFromUid(const std::string& uid);

}  // namespace sub::vst2
