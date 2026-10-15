#pragma once
// Plug-in formats: scanning files for plug-ins and instantiating them as
// `Processor`s, which the renderer hosts in each track's insert chain. VST3
// (Vst3Format) and VST2 (Vst2Format) are implemented; CLAP would be another
// implementation. pluginFormat() finds one by name, pluginFormatOfFile() by a
// file's extension.
//
// Hosting notes:
//  * Scanning loads plug-in code, so the app runs it in a child process
//    (substation-scan, tools/scanner): a crashing plug-in cannot take the DAW
//    down.
//  * Plug-ins are created, configured and destroyed on the main thread.
//  * Main-thread work plug-ins ask for (restarts, parameter updates, editor
//    events) is done in Engine::idle(), which the UI calls on a timer.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Processor.h"

namespace sub {

struct PluginDescription {
    std::string format;     // "VST3" or "VST2"
    std::string path;       // bundle or file on disk
    std::string uid;        // VST3 class id (32 hex digits); VST2 unique id (8 hex digits)
    std::string name;
    std::string vendor;
    std::string version;
    std::string category;   // VST3 sub-categories, e.g. "Instrument|Synth" or "Fx|Delay" (VST2's
                            // category in the same words)
    bool isInstrument = false;
};

class PluginFormat {
public:
    virtual ~PluginFormat() = default;

    virtual std::string name() const = 0;
    virtual std::vector<std::string> defaultSearchPaths() const = 0;

    // Lists the plug-ins in one file or bundle. Loads its code. Throws on failure.
    virtual std::vector<PluginDescription> scanFile(const std::string& path) = 0;

    // Creates a prepared processor, ready to be inserted into a track. Main
    // thread only. Throws std::runtime_error with a message for the user.
    virtual std::shared_ptr<Processor> instantiate(const std::string& path, const std::string& uid,
                                                   double sampleRate, int maxBlockSize) = 0;
};

// A hosted plug-in, whatever its format: what a plug-in sandbox's host
// (tools/pluginhost) needs besides the Processor. setParam() in two halves,
// for a sandbox, where the UI's changes reach the audio with the block and the
// plug-in's controller in a message: the controller and the value (main thread;
// returns the parameter's id and normalized value, none for a read-only one),
// and the audio (any thread: the rendering one). The id is the format's: VST3's
// ParamID, VST2's index.
class PluginProcessor : public Processor {
public:
    virtual std::optional<std::pair<uint32_t, double>> setControllerParam(int index, float value) = 0;
    virtual void queueParamChange(uint32_t id, double normalized) noexcept = 0;
};

// The formats there are, by name ("VST3", "VST2"; null for another).
PluginFormat* pluginFormat(const std::string& name);
// The format a plug-in file is (by its extension: ".vst3" is VST3, a library,
// ".dll" or ".so", VST2); null for none.
PluginFormat* pluginFormatOfFile(const std::string& path);

}  // namespace sub
