// The audio driver types elsewhere than Windows (see AudioBackends.h): PipeWire
// on Linux (when built with it), then miniaudio's choice of the system's
// backends, as "System": the fallback where PipeWire doesn't run.

#include "backends/AudioBackends.h"

#include "backends/MiniaudioBackend.h"
#if SUBSTATION_HAS_PIPEWIRE
#include "backends/PipeWireBackend.h"
#endif

namespace sub {

std::vector<std::string> audioDriverNames() {
#if SUBSTATION_HAS_PIPEWIRE
    return {PipeWireBackend::kName, MiniaudioBackend::kName};
#else
    return {MiniaudioBackend::kName};
#endif
}

std::vector<std::unique_ptr<AudioBackend>> makeAudioBackends() {
    std::vector<std::unique_ptr<AudioBackend>> backends;
#if SUBSTATION_HAS_PIPEWIRE
    backends.push_back(std::make_unique<PipeWireBackend>());
#endif
    backends.push_back(std::make_unique<MiniaudioBackend>());
    return backends;
}

}  // namespace sub
