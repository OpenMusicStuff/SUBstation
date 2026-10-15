#include "plugins/vst2/Vst2Format.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include "platform/Paths.h"
#include "plugins/vst2/Vst2Platform.h"
#include "plugins/vst2/Vst2Preset.h"
#include "plugins/vst2/Vst2Processor.h"
#include "plugins/vst3/Vst3Platform.h"

namespace sub::vst2 {

namespace {

// While an instance is made: the plug-in a shell is to make (kCurrentEffectId), and
// that the instance isn't a processor's yet (its host_internal may hold anything).
thread_local int32_t tlsShellId = 0;
thread_local bool tlsCreating = false;
thread_local bool tlsInAudio = false;

constexpr const char* kHostName = "SUBstation";

// What we tell a plug-in we do (VST_HOST_OPCODE_SUPPORTS).
bool hostSupports(const char* feature) {
    if (!feature) return false;
    for (const char* supported : {"sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "sizeWindow",
                                  "acceptIOChanges", "startStopProcess", "shellCategory", "supplyIdle"}) {
        if (std::strcmp(feature, supported) == 0) return true;
    }
    return false;
}

intptr_t VST_FUNCTION_INTERFACE hostCallback(vst_effect_t* effect, int32_t opcode, int32_t index, int64_t value,
                                             const char* ptr, float opt) {
    void* data = const_cast<char*>(ptr);
    if (opcode == VST_HOST_OPCODE_CURRENT_EFFECT_ID && tlsCreating) return tlsShellId;
    if (effect && !tlsCreating && effect->host_internal) {
        return static_cast<Vst2Processor*>(effect->host_internal)->hostCall(opcode, index, value, data, opt);
    }
    return defaultHostCall(opcode, index, value, data, opt);
}

std::string versionText(int32_t version) {
    if (version <= 0) return {};
    if (version >= 0x10000) {  // (bytes, as JUCE's plug-ins give it: 0x010203 is 1.2.3)
        return std::to_string((version >> 16) & 0xFF) + '.' + std::to_string((version >> 8) & 0xFF) + '.' +
               std::to_string(version & 0xFF);
    }
    return std::to_string(version);
}

std::string categoryText(int32_t category, bool instrument) {
    switch (category) {
        case VST_EFFECT_CATEGORY_INSTRUMENT: return "Instrument|Synth";
        case VST_EFFECT_CATEGORY_WAVEGENERATOR: return "Instrument|Generator";
        case VST_EFFECT_CATEGORY_METERING: return "Fx|Analyzer";
        case VST_EFFECT_CATEGORY_MASTERING: return "Fx|Mastering";
        case VST_EFFECT_CATEGORY_SPATIAL: return "Fx|Spatial";
        case VST_EFFECT_CATEGORY_DELAY_OR_ECHO: return "Fx|Delay";
        case VST_EFFECT_CATEGORY_RESTORATION: return "Fx|Restoration";
        default: return instrument ? "Instrument|Synth" : "Fx";
    }
}

// Closes an instance made for the scan.
void close(vst_effect_t* effect) { dispatch(effect, VST_EFFECT_OPCODE_DESTROY); }

}  // namespace

void setInAudioThread(bool inAudio) noexcept { tlsInAudio = inAudio; }
bool inAudioThread() noexcept { return tlsInAudio; }

intptr_t defaultHostCall(int32_t opcode, int32_t, int64_t, void* ptr, float) {
    switch (opcode) {
        case VST_HOST_OPCODE_VST_VERSION: return VST_VERSION_2_4_0_0;
        case VST_HOST_OPCODE_CURRENT_EFFECT_ID: return 0;
        case kWantMidi: return 1;
        case VST_HOST_OPCODE_GET_SAMPLE_RATE: return static_cast<intptr_t>(VST_DEFAULT_SAMPLE_RATE);
        case VST_HOST_OPCODE_GET_BLOCK_SIZE: return VST_DEFAULT_BLOCK_SIZE;
        case kProcessLevel: return tlsInAudio ? VST_HOST_ACTIVE_THREAD_AUDIO : VST_HOST_ACTIVE_THREAD_INTERFACE;
        case kAutomationState: return 1;  // (off)
        case VST_HOST_OPCODE_VENDOR_NAME:
        case VST_HOST_OPCODE_PRODUCT_NAME:
            if (ptr) std::memcpy(ptr, kHostName, std::strlen(kHostName) + 1);  // (it fits any buffer: 64 bytes)
            return 1;
        case VST_HOST_OPCODE_VENDOR_VERSION: return 1000;
        case VST_HOST_OPCODE_SUPPORTS: return hostSupports(static_cast<const char*>(ptr)) ? 1 : 0;
        case VST_HOST_OPCODE_LANGUAGE: return 1;  // English
        default: return 0;
    }
}

// ---------------------------------------------------------------------------
// Libraries

Module::Module(std::string path) : path_(std::move(path)) {
    std::string error;
    library_ = loadLibrary(path_, error);
    if (!library_) throw std::runtime_error(error.empty() ? "Could not load " + path_ : error);
    entry_ = reinterpret_cast<Entry>(librarySymbol(library_, "VSTPluginMain"));
    if (!entry_) entry_ = reinterpret_cast<Entry>(librarySymbol(library_, "main"));  // (VST 1's, still in some)
}

Module::~Module() { unloadLibrary(library_); }

vst_effect_t* Module::create(int32_t shellId) const {
    if (!entry_) return nullptr;
    tlsShellId = shellId;
    tlsCreating = true;
    vst_effect_t* effect = nullptr;
    try {
        effect = entry_(&hostCallback);
    } catch (...) {
        effect = nullptr;
    }
    tlsCreating = false;
    tlsShellId = 0;
    if (!effect) return nullptr;
    if (effect->magic_number != static_cast<int32_t>(VST_MAGICNUMBER)) return nullptr;  // (not one to close)
    effect->host_internal = nullptr;  // not a processor's (yet)
    return effect;
}

std::string dispatchText(vst_effect_t* effect, int32_t opcode, int32_t index) {
    // Plug-ins may write past the sizes VST 2 gives (8 bytes for a parameter's name): room for that.
    char buffer[512] = {};
    dispatch(effect, opcode, index, 0, buffer);
    buffer[255] = 0;
    std::string text(buffer);
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

PluginDescription describe(vst_effect_t* effect, const std::string& path, const std::string& fallbackName) {
    PluginDescription description;
    description.format = "VST2";
    description.path = path;
    description.uid = vst2Uid(effect->unique_id);
    description.name = dispatchText(effect, VST_EFFECT_OPCODE_EFFECT_NAME);
    if (description.name.empty()) description.name = dispatchText(effect, VST_EFFECT_OPCODE_PRODUCT_NAME);
    if (description.name.empty()) description.name = fallbackName;
    description.vendor = dispatchText(effect, VST_EFFECT_OPCODE_VENDOR_NAME);
    int32_t version = static_cast<int32_t>(dispatch(effect, VST_EFFECT_OPCODE_VENDOR_VERSION));
    if (version <= 0) version = effect->version;
    description.version = versionText(version);
    const auto category = static_cast<int32_t>(dispatch(effect, VST_EFFECT_OPCODE_CATEGORY));
    description.isInstrument = (effect->flags & VST_EFFECT_FLAG_INSTRUMENT) != 0 ||
                               category == VST_EFFECT_CATEGORY_INSTRUMENT ||
                               category == VST_EFFECT_CATEGORY_WAVEGENERATOR;
    description.category = categoryText(category, description.isInstrument);
    return description;
}

// ---------------------------------------------------------------------------
// The format

Vst2Format& Vst2Format::instance() {
    static Vst2Format format;
    return format;
}

std::vector<std::string> Vst2Format::defaultSearchPaths() const { return systemPluginFolders(); }

bool Vst2Format::isPluginFile(const std::string& path) {
    return isPluginFileName(platform::fromPath(platform::toPath(path).filename()));
}

std::shared_ptr<Module> Vst2Format::loadModule(const std::string& path) {
    std::lock_guard lock(mutex_);
    auto& slot = modules_[platform::fileKey(path)];  // one library per file
    if (auto module = slot.lock()) return module;
    auto module = std::make_shared<Module>(path);
    slot = module;
    return module;
}

std::vector<PluginDescription> Vst2Format::scanFile(const std::string& path) {
    vst3::prepareThreadForModules();  // (COM on Windows, which some plug-ins need as VST3 ones do)
    const auto module = loadModule(path);
    if (!module->isPlugin()) return {};  // a library beside the plug-ins
    const std::string stem = platform::fromPath(platform::toPath(path).stem());
    vst_effect_t* effect = module->create(0);
    if (!effect) throw std::runtime_error("The plug-in could not be created.");
    dispatch(effect, VST_EFFECT_OPCODE_CREATE);

    std::vector<PluginDescription> found;
    if (dispatch(effect, VST_EFFECT_OPCODE_CATEGORY) != kCategoryShell) {
        found.push_back(describe(effect, path, stem));
        close(effect);
        return found;
    }
    // A shell: its plug-ins' ids and names, then each made to read the rest
    // (while that takes less than kShellTime: a shell of hundreds keeps what it listed).
    std::vector<std::pair<int32_t, std::string>> members;
    for (;;) {
        char name[512] = {};
        const auto id = static_cast<int32_t>(dispatch(effect, VST_EFFECT_OPCODE_CONTAINER_NEXT_EFFECT_ID, 0, 0, name));
        if (id == 0 || members.size() >= 4096) break;
        name[255] = 0;
        members.emplace_back(id, name);
    }
    const std::string shellVendor = dispatchText(effect, VST_EFFECT_OPCODE_VENDOR_NAME);
    close(effect);
    constexpr auto kShellTime = std::chrono::seconds(20);
    const auto start = std::chrono::steady_clock::now();
    for (const auto& [id, name] : members) {
        PluginDescription description;
        vst_effect_t* member = std::chrono::steady_clock::now() - start < kShellTime ? module->create(id) : nullptr;
        if (member && member->unique_id == id) {
            dispatch(member, VST_EFFECT_OPCODE_CREATE);
            description = describe(member, path, name.empty() ? stem : name);
            if (!name.empty()) description.name = name;  // (the shell's list names them best)
        } else {
            description.format = "VST2";
            description.path = path;
            description.uid = vst2Uid(id);
            description.name = name.empty() ? stem : name;
            description.vendor = shellVendor;
            description.category = "Fx";
        }
        if (member) close(member);
        if (description.vendor.empty()) description.vendor = shellVendor;
        found.push_back(std::move(description));
    }
    return found;
}

std::shared_ptr<Processor> Vst2Format::instantiate(const std::string& path, const std::string& uid, double sampleRate,
                                                   int maxBlockSize) {
    vst3::prepareThreadForModules();
    const auto id = vst2IdFromUid(uid);
    if (!id) throw std::invalid_argument("Not a VST2 plug-in id: " + uid);
    const auto module = loadModule(path);
    const std::string file = platform::fromPath(platform::toPath(path).filename());
    vst_effect_t* effect = module->create(*id);
    if (effect && effect->unique_id != *id) {
        close(effect);
        effect = nullptr;
        throw std::runtime_error(file + " does not contain this plug-in any more.");
    }
    if (!effect) {
        throw std::runtime_error(module->isPlugin() ? file + ": the plug-in could not be created."
                                                    : file + " is not a VST2 plug-in.");
    }
    auto processor = std::make_shared<Vst2Processor>(module, effect);
    processor->prepare(sampleRate, maxBlockSize);
    return processor;
}

}  // namespace sub::vst2
