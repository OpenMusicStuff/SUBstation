#pragma once
// Modulation: a modulator (a device whose Processor::isModulator() is true: the
// LFO, the Envelope) moving parameters of other devices on its track, wherever
// they sit (in racks too), as automation would.
//
// The renderer runs a strip's modulators before its devices, each over the
// chunk (Renderer::runModulators): a modulator doesn't touch the audio, but
// writes its value (0..1, a sample at a time) into the buffer it is handed as
// its first channel. A modulated parameter then gets the modulator's value
// over its range (`low` at 0, `high` at 1, normalized as automation is; turned
// upside down with `invert`), handed to its device through Processor::automate()
// at the chunk's start and every kAutomationStep samples, as a sloped envelope
// would be. So it plays sample-accurately on built-in devices and plug-ins alike.
// A parameter that is modulated isn't automated meanwhile (modulation wins);
// one parameter follows one modulator (the first that names it).
//
// Engine::setTrackModulation() sets a track's; the application layer's engine
// bridge sends what the model's modulators map (app/src/audio).

#include <algorithm>
#include <cstdint>
#include <string>

namespace sub {

struct ModulationDesc {
    uint32_t modulatorId = 0;  // the modulator's processor
    uint32_t processorId = 0;  // the device it moves (on the modulator's track)
    std::string param;         // that device's parameter's id
    float low = 0.f;           // the parameter's normalized value where the modulator is at 0
    float high = 1.f;          // ... and where it is at 1
    bool invert = false;       // the modulator upside down (1 - its value)

    friend bool operator==(const ModulationDesc&, const ModulationDesc&) = default;
};

// A strip's modulators run at most this many (in chain order, racks' depth
// first); the parameters mapped to more stay where they are.
constexpr int kMaxModulators = 32;

// A modulator's value (0..1) as the normalized value of a parameter mapped to it.
inline float modulationTarget(float value, float low, float high, bool invert) noexcept {
    float v = value == value ? std::clamp(value, 0.f, 1.f) : 0.f;  // (a NaN: 0)
    if (invert) v = 1.f - v;
    return std::clamp(low + v * (high - low), 0.f, 1.f);
}

}  // namespace sub
