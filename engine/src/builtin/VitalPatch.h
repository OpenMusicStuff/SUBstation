#pragma once
// The Vital instrument's patches (builtin/devices/Vital.cpp): its parameters
// and its state besides them, and Vital's own preset files (.vital) read into
// them and written from them. No Vital headers here: the application layer
// includes it (it reads .vital presets into devices, edits the modulations).
//
// A patch is what Vital saves in a preset: every parameter (by Vital's own
// ids, "osc_1_level"), and as JSON text, as Vital writes it, the modulation
// routing, the three oscillators' wavetables, the eight LFOs' shapes, the
// sample, the tuning, and the preset's name, author, comments, style and macro
// names. The device keeps the parameters as its parameters and the rest as its
// state values (BuiltinProcessor::StateValues), named as below; a value left
// out is Vital's init patch's.

#include <map>
#include <string>
#include <vector>

#include "Processor.h"

namespace sub::vitalpatch {

inline constexpr const char* kTypeId = "builtin:vital";
inline constexpr const char* kKind = "vital";

// The state values (JSON text, as in a .vital file's "settings").
inline constexpr const char* kModulations = "modulations";  // [{"source", "destination"[, "line_mapping"]}, ...]
inline constexpr const char* kWavetables = "wavetables";    // the three oscillators' WavetableCreator states
inline constexpr const char* kLfos = "lfos";                // the eight LFOs' LineGenerator states
inline constexpr const char* kSample = "sample";            // the sample's state
inline constexpr const char* kTuning = "tuning";            // a Tuning's state (none: 12-tone equal)
// Plain text.
inline constexpr const char* kPresetName = "preset_name";
inline constexpr const char* kAuthor = "author";
inline constexpr const char* kComments = "comments";
inline constexpr const char* kStyle = "preset_style";
// "macro1".."macro4": the macros' names.
std::string macroKey(int index);

inline constexpr int kNumMacros = 4;
inline constexpr int kNumOscillators = 3;
inline constexpr int kNumLfos = 8;
inline constexpr int kMaxModulations = 64;  // slots, as Vital has them ("modulation_1_amount"...)

// The device's parameters: every one of Vital's parameters that its engine has
// (not the few its older versions had), in Vital's order. Values are Vital's
// own (what its knobs are linear in, and its presets hold); paramText() turns
// them into what Vital shows (ownText). The ones a host sets otherwise (bypass,
// tempo, MPE, the spectrogram view) are hidden.
const std::vector<ParamInfo>& params();
// Its index in params(), or -1.
int paramIndex(const std::string& id);
// A value as Vital shows it ("1.20 secs", "-6.0 dB", "Saw"...).
std::string paramText(int index, float value);

struct Patch {
    std::map<std::string, float> params;       // by id; those left out: their defaults
    std::map<std::string, std::string> state;  // the state values above
};

// A .vital preset's text (JSON) as a patch: older versions' presets upgraded as
// Vital upgrades them, unknown parameters left out, the name from the file's
// `name` if it has none. Throws std::runtime_error if it isn't a Vital preset.
Patch readPreset(const std::string& text, const std::string& name = {});
// A patch as a .vital preset's text (the parts it leaves out as Vital's init
// patch has them), for Vital (and this device) to read.
std::string writePreset(const Patch& patch);

// One modulation: a source ("lfo_1", "env_2", "macro_control_1", ...) to a
// destination (a parameter's id); its curve (`lineMapping`, a LineGenerator's
// JSON) if not linear.
struct Modulation {
    std::string source;
    std::string destination;
    std::string lineMapping;
    bool operator==(const Modulation&) const = default;
};
// The modulations' state value read (slot by slot: an empty slot has no source
// or destination), and written. Throws std::runtime_error if it isn't one.
std::vector<Modulation> readModulations(const std::string& value);
std::string writeModulations(const std::vector<Modulation>& modulations);
// The sources and destinations Vital's engine has, sorted.
const std::vector<std::string>& modulationSources();
const std::vector<std::string>& modulationDestinations();
// What Vital calls a source ("LFO 1", "Envelope 2", "Macro 1", ...).
std::string sourceName(const std::string& source);
// Whether Vital makes a new modulation from this source bipolar (an LFO's, a random one's, the pitch wheel's...).
bool sourceBipolar(const std::string& source);

}  // namespace sub::vitalpatch
