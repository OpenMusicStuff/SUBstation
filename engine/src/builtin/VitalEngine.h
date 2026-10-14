#pragma once
// The Vital instrument's engine (builtin/devices/Vital.cpp, VitalPatch.cpp):
// Vital's SoundEngine with what Vital's SynthBase keeps beside it (the
// oscillators' wavetable creators, the tuning), built whole from a patch's
// state values and parameter values. Building one allocates and takes a while
// (tens of milliseconds), so the device builds them off the audio thread and
// hands them over (see Vital.cpp). Includes Vital's headers: only those two
// files include this.

#include <memory>
#include <string>
#include <vector>

#include "builtin/VitalPatch.h"

// sound_engine.h first: it brings in Vital's framework/processor.h from its own
// directory. sample_source.h's bare "processor.h" would otherwise find our
// engine/src/Processor.h on a case-insensitive filesystem (Windows) and leave
// vital::Processor undeclared.
#include "sound_engine.h"

#include "line_generator.h"
#include "sample_source.h"
#include "synth_parameters.h"
#include "tuning.h"
#include "wavetable_creator.h"

namespace sub::vitalpatch {

struct Engine {
    // (In this order: the engine refers to the tuning, the creators to the engine's wavetables.)
    std::unique_ptr<Tuning> tuning;
    std::unique_ptr<vital::SoundEngine> sound;
    std::unique_ptr<WavetableCreator> wavetables[kNumOscillators];
    std::vector<vital::Value*> controls;  // by index in params(); null where the engine has none
    int sampleRate = 0;
    int oversampling = 0;  // the "oversampling" parameter it was built with (0..3: 1x..8x)
    // What its editor shows moving: the newest voice's LFO phases (encoded with the voice, as Vital
    // encodes them), its envelopes' phases (stage + position) and values. Cleared while no voice plays.
    const vital::StatusOutput* lfoPhases[kNumLfos] = {};
    const vital::StatusOutput* envelopePhases[kNumEnvelopes] = {};
    const vital::StatusOutput* envelopeValues[kNumEnvelopes] = {};

    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
};

// An engine for these state values, every parameter at `values` (params()'s
// order, all of them), at `sampleRate`, ready to play. Throws
// std::runtime_error if a state value isn't what it should be.
std::unique_ptr<Engine> buildEngine(const std::map<std::string, std::string>& state, const std::vector<float>& values,
                                    int sampleRate);

// Each parameter's default, in params()'s order.
std::vector<float> defaultValues();

}  // namespace sub::vitalpatch
