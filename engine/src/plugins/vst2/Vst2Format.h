#pragma once
// VST2 hosting, through Xaymar's clean-room vst2sdk (Vst2Sdk.h): loaded
// libraries (shared by all instances from the same file and unloaded with the
// last one), what the host answers plug-ins (the host callback), scanning and
// instantiation. A VST2 plug-in is known by its unique id (8 hex digits:
// vst2Uid); a "shell" library holds several (Waves'), each made by answering
// its id when the library asks which one to make.

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "plugins/PluginFormat.h"
#include "plugins/vst2/Vst2Sdk.h"

namespace sub::vst2 {

// A VST2 library loaded, and its entry point.
class Module {
public:
    // Loads it. Throws std::runtime_error with the system's reason.
    explicit Module(std::string path);
    ~Module();
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    const std::string& path() const noexcept { return path_; }
    // Whether it has VST 2's entry point (VSTPluginMain, or VST 1's main).
    bool isPlugin() const noexcept { return entry_ != nullptr; }
    // A new instance (the host callback set; the instance not opened yet), or
    // null. In a shell, `shellId` says which of its plug-ins (0: the shell).
    vst_effect_t* create(int32_t shellId) const;

private:
    using Entry = vst_effect_t*(VST_FUNCTION_INTERFACE*)(vst_host_callback_t);
    std::string path_;
    void* library_ = nullptr;
    Entry entry_ = nullptr;
};

// What the host answers a plug-in that no Vst2Processor holds (yet): while it is
// made, or while it is scanned. The processors answer the rest (Vst2Processor::hostCall).
intptr_t defaultHostCall(int32_t opcode, int32_t index, int64_t value, void* ptr, float opt);

// The rendering thread marks itself while in a plug-in's process call (kProcessLevel).
void setInAudioThread(bool inAudio) noexcept;
bool inAudioThread() noexcept;

class Vst2Format final : public PluginFormat {
public:
    static Vst2Format& instance();

    std::string name() const override { return "VST2"; }
    // The system's VST2 folders (Vst2Platform.h).
    std::vector<std::string> defaultSearchPaths() const override;
    // Whether a file is a VST2 plug-in's by its name (".dll" on Windows, ".so" elsewhere).
    static bool isPluginFile(const std::string& path);
    // A library without VST 2's entry point (a helper library beside the
    // plug-ins) holds none: an empty list, not an error.
    std::vector<PluginDescription> scanFile(const std::string& path) override;
    std::shared_ptr<Processor> instantiate(const std::string& path, const std::string& uid, double sampleRate,
                                           int maxBlockSize) override;

private:
    Vst2Format() = default;
    std::shared_ptr<Module> loadModule(const std::string& path);

    std::mutex mutex_;
    std::map<std::string, std::weak_ptr<Module>> modules_;  // by normalised path
};

// Asks an instance (any opcode); `ptr` as the call takes it.
inline intptr_t dispatch(vst_effect_t* effect, int32_t opcode, int32_t index = 0, intptr_t value = 0,
                         void* ptr = nullptr, float opt = 0.f) {
    return effect->control(effect, opcode, index, value, ptr, opt);
}
// A text the plug-in writes into a buffer (its name, a parameter's): trimmed, at most 255 bytes.
std::string dispatchText(vst_effect_t* effect, int32_t opcode, int32_t index = 0);

// What the scan says of an instance (opened): its name (else `fallbackName`), vendor,
// version, VST3-style category ("Fx|Delay", "Instrument|Synth"), and whether it is an instrument.
PluginDescription describe(vst_effect_t* effect, const std::string& path, const std::string& fallbackName);

}  // namespace sub::vst2
