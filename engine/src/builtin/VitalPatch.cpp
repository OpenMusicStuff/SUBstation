#include "builtin/VitalPatch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "AudioSource.h"
#include "builtin/VitalEngine.h"
#include "platform/Paths.h"
#include "load_save.h"
#include "modulation_connection_processor.h"
#include "synth_constants.h"
#include "synth_strings.h"

namespace sub::vitalpatch {

namespace {

using json = nlohmann::json;

// Parameters Vital's older versions had, which its engine no longer does (its
// presets' upgrade moves them elsewhere): not the device's.
const std::set<std::string>& retiredParams() {
    static const std::set<std::string> kRetired = {
        "compressor_low_band_unused", "filter_1_osc1_input", "filter_1_osc2_input", "filter_1_osc3_input",
        "filter_1_sample_input", "filter_2_osc1_input", "filter_2_osc2_input", "filter_2_osc3_input",
        "filter_2_sample_input", "filter_fx_filter_input", "filter_fx_osc1_input", "filter_fx_osc2_input",
        "filter_fx_osc3_input", "filter_fx_sample_input", "sub_direct_out", "sub_level", "sub_on", "sub_pan",
        "sub_transpose", "sub_transpose_quantize", "sub_tune", "sub_waveform"};
    return kRetired;
}

// Set by the host (its tempo, the device's switch), or of Vital's own window: hidden.
bool hostParam(const std::string& id) {
    return id == "bypass" || id == "beats_per_minute" || id == "mpe_enabled" || id == "view_spectrogram" ||
           (id.size() > 8 && id.compare(id.size() - 8, 8, "_view_2d") == 0);
}

struct Table {
    std::vector<ParamInfo> infos;
    std::vector<const vital::ValueDetails*> details;
    std::vector<std::vector<std::string>> names;  // a list's names, as far as Vital has them (some ranges run past them)
    std::unordered_map<std::string, int> index;
};

// The names a parameter's values have. Vital's ValueDetails point at one of
// synth_strings.h's lists without saying how long it is, and a few ranges run
// past theirs (Vital's own window never shows those values by name). So the
// list is found among synth_strings.h's by its names, the shortest first: no
// name past the end of the one it points at is read (it is one of these, so
// its own length is reached first). Those lists are this file's copies (each
// file has its own of a header's const arrays).
std::vector<std::string> namesOf(const std::string* lookup) {
    if (lookup == nullptr) return {};
    struct List {
        const std::string* names;
        size_t size;
    };
#define SUB_VITAL_LIST(name) List{strings::name, std::size(strings::name)}
    static const std::vector<List> kLists = [] {
        std::vector<List> lists = {
            SUB_VITAL_LIST(kOffOnNames), SUB_VITAL_LIST(kDelayStyleNames), SUB_VITAL_LIST(kFrequencySyncNames),
            SUB_VITAL_LIST(kSyncedFrequencyNames), SUB_VITAL_LIST(kDistortionTypeNames),
            SUB_VITAL_LIST(kDistortionFilterOrderNames), SUB_VITAL_LIST(kStereoModeNames),
            SUB_VITAL_LIST(kDestinationNames), SUB_VITAL_LIST(kCompressorBandNames), SUB_VITAL_LIST(kEqLowModeNames),
            SUB_VITAL_LIST(kEqBandModeNames), SUB_VITAL_LIST(kEqHighModeNames), SUB_VITAL_LIST(kVoicePriorityNames),
            SUB_VITAL_LIST(kVoiceOverrideNames), SUB_VITAL_LIST(kOversamplingNames), SUB_VITAL_LIST(kSyncNames),
            SUB_VITAL_LIST(kRandomNames), SUB_VITAL_LIST(kFilterStyleNames), SUB_VITAL_LIST(kFilterModelNames),
            SUB_VITAL_LIST(kUnisonStackNames), SUB_VITAL_LIST(kPhaseDistortionNames),
            SUB_VITAL_LIST(kSpectralMorphNames)};
        std::stable_sort(lists.begin(), lists.end(), [](const List& a, const List& b) { return a.size < b.size; });
        return lists;
    }();
#undef SUB_VITAL_LIST
    for (const List& list : kLists) {
        bool same = true;
        for (size_t i = 0; i < list.size && same; ++i) same = lookup[i] == list.names[i];
        if (same) return std::vector<std::string>(list.names, list.names + list.size);
    }
    return {};
}

const Table& table() {
    static const Table kTable = [] {
        Table t;
        for (int i = 0; i < vital::Parameters::getNumParameters(); ++i) {
            const vital::ValueDetails* details = vital::Parameters::getDetails(i);
            if (retiredParams().count(details->name)) continue;
            ParamInfo info;
            info.id = details->name;
            info.name = details->display_name;
            info.minValue = details->min;
            info.maxValue = details->max;
            info.defaultValue = details->default_value;
            info.ownText = true;
            std::vector<std::string> names = namesOf(details->string_lookup);
            if (details->value_scale == vital::ValueDetails::kIndexed) {
                const int count = static_cast<int>(std::lround(details->max - details->min));
                // A list: its names start at 0 (Vital names a value by itself, not by its step) and name every value.
                if (std::lround(details->min) == 0 && names.size() == static_cast<size_t>(count) + 1) {
                    info.valueLabels = names;
                } else {
                    info.steps = std::max(1, count);
                }
            }
            info.hidden = hostParam(info.id);
            // Oversampling rebuilds the engine (Vital pauses for it too): not automated.
            info.automatable = !info.hidden && info.id != "oversampling" && info.id != "effect_chain_order";
            t.index.emplace(info.id, static_cast<int>(t.infos.size()));
            t.infos.push_back(std::move(info));
            t.details.push_back(details);
            t.names.push_back(std::move(names));
        }
        return t;
    }();
    return kTable;
}

// Vital's SynthSlider::getAdjustedValue: a value as its knobs show it.
double displayValue(const vital::ValueDetails& d, double value) {
    switch (d.value_scale) {
    case vital::ValueDetails::kQuadratic: value *= value; break;
    case vital::ValueDetails::kCubic: value *= value * value; break;
    case vital::ValueDetails::kQuartic: value *= value; value *= value; break;
    case vital::ValueDetails::kExponential: value = std::pow(2.0, value); break;
    case vital::ValueDetails::kSquareRoot: value = std::sqrt(std::max(value, 0.0)); break;
    default: break;
    }
    value += d.post_offset;
    if (d.display_invert) value = 1.0 / value;
    return value * d.display_multiply;
}

json parse(const std::string& text, const char* what) {
    try {
        return json::parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Vital: its ") + what + " can't be read (" + e.what() + ")");
    }
}

std::string titled(std::string text) {
    bool start = true;
    for (char& c : text) {
        if (c == '_') {
            c = ' ';
            start = true;
        } else if (start) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            start = false;
        }
    }
    return text;
}

// The modulation routing, slot by slot, from the state value (or none).
std::vector<Modulation> modulationsOf(const std::map<std::string, std::string>& state) {
    const auto it = state.find(kModulations);
    return it == state.end() || it->second.empty() ? std::vector<Modulation>{} : readModulations(it->second);
}

// Connect `connection` (its source and destination set) as Vital's SynthBase::connectModulation does.
bool connect(vital::SoundEngine& engine, vital::ModulationConnection* connection) {
    const std::string& source = connection->source_name;
    const std::string& destination = connection->destination_name;
    if (!vital::Parameters::isParameter(destination)) return false;
    vital::modulation_change change;
    change.source = engine.getModulationSource(source);
    change.mono_destination = engine.getMonoModulationDestination(destination);
    change.mono_modulation_switch = engine.getMonoModulationSwitch(destination);
    if (!change.source || !change.mono_destination || !change.mono_modulation_switch) return false;
    change.destination_scale = vital::Parameters::getParameterRange(destination);
    change.poly_modulation_switch = engine.getPolyModulationSwitch(destination);
    change.poly_destination = engine.getPolyModulationDestination(destination);
    change.modulation_processor = connection->modulation_processor.get();
    int numAudioRate = 0;
    vital::ModulationConnectionBank& bank = engine.getModulationBank();
    for (int i = 0; i < vital::kMaxModulationConnections; ++i) {
        const vital::ModulationConnection* other = bank.atIndex(i);
        if (other->source_name == source && other->destination_name != destination &&
            !other->modulation_processor->isControlRate()) {
            ++numAudioRate;
        }
    }
    change.num_audio_rate = numAudioRate;
    // (A modulation of its own amount.)
    if (change.poly_destination && change.poly_destination->router() == change.modulation_processor) return false;
    change.disconnecting = false;
    engine.connectModulation(change);
    return true;
}

}  // namespace

std::string macroKey(int index) { return "macro" + std::to_string(index + 1); }

const std::vector<ParamInfo>& params() { return table().infos; }

int paramIndex(const std::string& id) {
    const auto& index = table().index;
    const auto it = index.find(id);
    return it == index.end() ? -1 : it->second;
}

std::vector<float> defaultValues() {
    std::vector<float> values;
    for (const ParamInfo& info : params()) values.push_back(info.defaultValue);
    return values;
}

std::string paramText(int index, float value) {
    const Table& t = table();
    if (index < 0 || index >= static_cast<int>(t.details.size())) return {};
    const vital::ValueDetails& d = *t.details[static_cast<size_t>(index)];
    if (d.value_scale == vital::ValueDetails::kIndexed) {
        const long whole = std::lround(std::clamp(value, d.min, d.max));
        const std::vector<std::string>& names = t.names[static_cast<size_t>(index)];
        if (whole >= 0 && static_cast<size_t>(whole) < names.size()) return names[static_cast<size_t>(whole)];
        return std::to_string(whole) + d.display_units;
    }
    // A cutoff (a MIDI note, 8..136): in hertz, as Vital shows it by default.
    if (d.min == 8.f && d.max == 136.f) {
        const double hz = 440.0 * std::pow(2.0, (std::clamp(value, d.min, d.max) - 69.0) / 12.0);
        char buffer[32];
        if (hz >= 1000.0) std::snprintf(buffer, sizeof buffer, "%.2f kHz", hz / 1000.0);
        else std::snprintf(buffer, sizeof buffer, "%.0f Hz", hz);
        return buffer;
    }
    // As Vital's SynthSlider::formatValue: five decimals, cut to five characters (and a sign).
    const double shown = displayValue(d, value);
    if (!std::isfinite(shown)) return shown > 0 ? "inf" + d.display_units : "-inf" + d.display_units;
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.5f", shown);
    std::string text = buffer;
    if (text == "-0.00000") text = "0.00000";
    text = text.substr(0, text[0] == '-' ? 6 : 5);
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text + d.display_units;
}

std::vector<Modulation> readModulations(const std::string& value) {
    const json data = parse(value, "modulations");
    if (!data.is_array()) throw std::runtime_error("Vital: its modulations aren't a list");
    std::vector<Modulation> modulations;
    for (const json& entry : data) {
        Modulation modulation;
        if (entry.is_object()) {
            if (entry.count("source") && entry["source"].is_string()) modulation.source = entry["source"];
            if (entry.count("destination") && entry["destination"].is_string()) {
                modulation.destination = entry["destination"];
            }
            if (entry.count("line_mapping")) modulation.lineMapping = entry["line_mapping"].dump();
        }
        modulations.push_back(std::move(modulation));
    }
    return modulations;
}

std::string writeModulations(const std::vector<Modulation>& modulations) {
    json data = json::array();
    for (const Modulation& modulation : modulations) {
        json entry;
        entry["source"] = modulation.source;
        entry["destination"] = modulation.destination;
        if (!modulation.lineMapping.empty()) entry["line_mapping"] = parse(modulation.lineMapping, "modulation curve");
        data.push_back(entry);
    }
    return data.dump();
}

namespace {

struct Routing {
    std::vector<std::string> sources;
    std::vector<std::string> destinations;
};

const Routing& routing() {
    static const Routing kRouting = [] {
        Routing r;
        vital::SoundEngine engine;
        for (const auto& [name, output] : engine.getModulationSources()) r.sources.push_back(name);
        for (const auto& [name, processor] : engine.getMonoModulationDestinations()) {
            const int index = paramIndex(name);
            if (vital::Parameters::isParameter(name) && index >= 0 && !params()[static_cast<size_t>(index)].hidden) {
                r.destinations.push_back(name);
            }
        }
        std::sort(r.sources.begin(), r.sources.end());
        std::sort(r.destinations.begin(), r.destinations.end());
        return r;
    }();
    return kRouting;
}

}  // namespace

const std::vector<std::string>& modulationSources() { return routing().sources; }
const std::vector<std::string>& modulationDestinations() { return routing().destinations; }

std::string sourceName(const std::string& source) {
    static const std::vector<std::pair<std::string, std::string>> kPrefixes = {
        {"env_", "Envelope "}, {"lfo_", "LFO "}, {"random_", "Random "}, {"macro_control_", "Macro "}};
    for (const auto& [prefix, name] : kPrefixes) {
        if (source.rfind(prefix, 0) == 0 && source.size() > prefix.size()) return name + titled(source.substr(prefix.size()));
    }
    return titled(source);
}

bool sourceBipolar(const std::string& source) { return vital::ModulationConnection::isModulationSourceDefaultBipolar(source); }

// --- The engine ------------------------------------------------------------------------------

std::unique_ptr<Engine> buildEngine(const std::map<std::string, std::string>& state, const std::vector<float>& values,
                                    int sampleRate) {
    const std::vector<ParamInfo>& infos = params();
    if (values.size() != infos.size()) throw std::invalid_argument("Vital: a value for every parameter");
    const auto value = [&](const char* key) -> const std::string* {
        const auto it = state.find(key);
        return it == state.end() || it->second.empty() ? nullptr : &it->second;
    };
    // Every state value read first, so a bad one throws before anything is built.
    std::optional<json> wavetables, lfos, sample, tuning;
    if (const std::string* text = value(kWavetables)) wavetables = parse(*text, "wavetables");
    if (const std::string* text = value(kLfos)) lfos = parse(*text, "LFOs");
    if (const std::string* text = value(kSample)) sample = parse(*text, "sample");
    if (const std::string* text = value(kTuning)) tuning = parse(*text, "tuning");
    const std::vector<Modulation> modulations = modulationsOf(state);

    auto engine = std::make_unique<Engine>();
    engine->tuning = std::make_unique<Tuning>();
    engine->sound = std::make_unique<vital::SoundEngine>();
    vital::SoundEngine& sound = *engine->sound;
    sound.setTuning(engine->tuning.get());
    try {
        if (tuning) engine->tuning->jsonToState(*tuning);

        // The controls: as Vital's LoadSave::loadControls, every one set.
        vital::control_map controls = sound.getControls();
        engine->controls.assign(infos.size(), nullptr);
        for (size_t i = 0; i < infos.size(); ++i) {
            const auto it = controls.find(infos[i].id);
            if (it == controls.end()) continue;
            engine->controls[i] = it->second;
            it->second->set(std::clamp(values[i], infos[i].minValue, infos[i].maxValue));
        }
        const int mod = paramIndex("mod_wheel");
        if (mod >= 0) sound.setModWheelAllChannels(values[static_cast<size_t>(mod)]);
        const int pitch = paramIndex("pitch_wheel");
        if (pitch >= 0) sound.setZonedPitchWheel(values[static_cast<size_t>(pitch)], 0, vital::kNumMidiChannels - 1);

        // The modulations (as LoadSave::loadModulations): slot i is modulation_<i+1>'s.
        vital::ModulationConnectionBank& bank = sound.getModulationBank();
        for (size_t i = 0; i < modulations.size() && i < static_cast<size_t>(vital::kMaxModulationConnections); ++i) {
            const Modulation& modulation = modulations[i];
            vital::ModulationConnection* connection = bank.atIndex(static_cast<int>(i));
            LineGenerator* curve = connection->modulation_processor->lineMapGenerator();
            if (!modulation.lineMapping.empty()) {
                curve->jsonToState(parse(modulation.lineMapping, "modulation curve"));
            } else {
                curve->initLinear();
            }
            if (modulation.source.empty() || modulation.destination.empty()) continue;
            connection->source_name = modulation.source;
            connection->destination_name = modulation.destination;
            if (!connect(sound, connection)) {
                connection->source_name.clear();
                connection->destination_name.clear();
            }
        }

        // The sample, the wavetables and the LFOs (as LoadSave::jsonToState, or SynthBase::initEngine).
        if (sample) sound.getSample()->jsonToState(*sample);
        else sound.getSample()->init();
        for (int i = 0; i < kNumOscillators; ++i) {
            engine->wavetables[i] = std::make_unique<WavetableCreator>(sound.getWavetable(i));
            if (wavetables && wavetables->is_array() && i < static_cast<int>(wavetables->size())) {
                engine->wavetables[i]->jsonToState((*wavetables)[static_cast<size_t>(i)]);
                engine->wavetables[i]->render();
            } else {
                engine->wavetables[i]->init();
            }
        }
        for (int i = 0; i < kNumLfos; ++i) {
            LineGenerator* lfo = sound.getLfoSource(i);
            if (lfo == nullptr) continue;
            if (lfos && lfos->is_array() && i < static_cast<int>(lfos->size())) {
                lfo->jsonToState((*lfos)[static_cast<size_t>(i)]);
            } else {
                lfo->initTriangle();
            }
            lfo->render();
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Vital: its patch can't be read (") + e.what() + ")");
    }

    // Ready to play, as Vital's plug-in does in prepareToPlay.
    sound.setSampleRate(sampleRate);
    sound.checkOversampling();
    sound.updateAllModulationSwitches();
    engine->sampleRate = sampleRate;
    const int oversampling = paramIndex("oversampling");
    engine->oversampling = oversampling >= 0 ? static_cast<int>(std::lround(values[static_cast<size_t>(oversampling)])) : 0;
    return engine;
}

// --- Presets ---------------------------------------------------------------------------------

Patch readPreset(const std::string& text, const std::string& name) {
    json data = parse(text, "preset");
    if (!data.is_object() || !data.count("settings") || !data["settings"].is_object()) {
        throw std::runtime_error("Vital: not a Vital preset");
    }
    try {
        const std::string version = data.count("synth_version") && data["synth_version"].is_string()
                                        ? data["synth_version"].get<std::string>()
                                        : std::string("0.0.0");
        // (As LoadSave::jsonToState; a newer version's preset is read for what it shares.)
        if (LoadSave::compareVersionStrings(version, ProjectInfo::versionString) < 0 ||
            data["settings"].count("sub_octave")) {
            data = LoadSave::updateFromOldVersion(data);
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Vital: an older preset that can't be upgraded (") + e.what() + ")");
    }
    const json& settings = data["settings"];
    Patch patch;
    for (const ParamInfo& info : params()) {
        float v = info.defaultValue;
        if (settings.count(info.id) && settings[info.id].is_number()) {
            v = std::clamp(settings[info.id].get<float>(), info.minValue, info.maxValue);
        }
        patch.params[info.id] = v;
    }
    for (const char* key : {kModulations, kWavetables, kLfos, kSample}) {
        if (settings.count(key) && !settings[key].is_null()) patch.state[key] = settings[key].dump();
    }
    if (data.count("tuning") && data["tuning"].is_object()) patch.state[kTuning] = data["tuning"].dump();
    const auto field = [&](const char* key) -> std::string {
        return data.count(key) && data[key].is_string() ? data[key].get<std::string>() : std::string();
    };
    std::string presetName = field("preset_name");
    if (presetName.empty()) presetName = name;
    if (!presetName.empty()) patch.state[kPresetName] = presetName;
    for (const char* key : {kAuthor, kComments, kStyle}) {
        if (!field(key).empty()) patch.state[key] = field(key);
    }
    for (int i = 0; i < kNumMacros; ++i) {
        const std::string macro = field(macroKey(i).c_str());
        if (!macro.empty()) patch.state[macroKey(i)] = macro;
    }
    return patch;
}

std::string writePreset(const Patch& patch) {
    std::vector<float> values = defaultValues();
    const std::vector<ParamInfo>& infos = params();
    for (const auto& [id, v] : patch.params) {
        const int index = paramIndex(id);
        if (index >= 0) values[static_cast<size_t>(index)] = v;
    }
    // Built, so what the patch leaves out is written as the engine has it.
    const std::unique_ptr<Engine> engine = buildEngine(patch.state, values, vital::kDefaultSampleRate);
    vital::SoundEngine& sound = *engine->sound;

    json settings;
    for (size_t i = 0; i < infos.size(); ++i) settings[infos[i].id] = values[i];
    json modulations = json::array();
    vital::ModulationConnectionBank& bank = sound.getModulationBank();
    for (int i = 0; i < vital::kMaxModulationConnections; ++i) {
        vital::ModulationConnection* connection = bank.atIndex(i);
        json entry;
        entry["source"] = connection->source_name;
        entry["destination"] = connection->destination_name;
        LineGenerator* curve = connection->modulation_processor->lineMapGenerator();
        if (!curve->linear()) entry["line_mapping"] = curve->stateToJson();
        modulations.push_back(entry);
    }
    settings["modulations"] = modulations;
    json wavetables = json::array();
    for (const auto& creator : engine->wavetables) wavetables.push_back(creator->stateToJson());
    settings["wavetables"] = wavetables;
    json lfos = json::array();
    for (int i = 0; i < kNumLfos; ++i) lfos.push_back(sound.getLfoSource(i)->stateToJson());
    settings["lfos"] = lfos;
    settings["sample"] = sound.getSample()->stateToJson();

    json data;
    data["synth_version"] = ProjectInfo::versionString;
    const auto text = [&](const std::string& key) {
        const auto it = patch.state.find(key);
        return it == patch.state.end() ? std::string() : it->second;
    };
    data["preset_name"] = text(kPresetName);
    data["author"] = text(kAuthor);
    data["comments"] = text(kComments);
    data["preset_style"] = text(kStyle);
    for (int i = 0; i < kNumMacros; ++i) {
        const std::string macro = text(macroKey(i));
        data["macro" + std::to_string(i + 1)] = macro.empty() ? "MACRO " + std::to_string(i + 1) : macro;
    }
    if (!engine->tuning->isDefault()) data["tuning"] = engine->tuning->stateToJson();
    data["settings"] = settings;
    return data.dump();
}

// --- Wavetables and LFO shapes ---------------------------------------------------------------

namespace {

// A WAV file's "clm " chunk: how a wavetable file's frames blend (Serum's and Vital's own), "" if none.
std::string clmChunk(const std::string& path) {
    std::ifstream in(platform::toPath(path), std::ios::binary);
    char header[12];
    if (!in.read(header, sizeof header) || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        return {};
    }
    char id[4];
    unsigned char size[4];
    while (in.read(id, 4) && in.read(reinterpret_cast<char*>(size), 4)) {
        const uint32_t length = size[0] | (size[1] << 8) | (size[2] << 16) | (uint32_t(size[3]) << 24);
        if (std::memcmp(id, "clm ", 4) == 0) {
            std::string data(std::min<uint32_t>(length, 4096), '\0');
            in.read(data.data(), static_cast<std::streamsize>(data.size()));
            return data;
        }
        in.seekg(length + (length & 1), std::ios::cur);
    }
    return {};
}

// As Vital's WavetableEditSection reads it: "<!>2048 0...": no blending; "1": in time; else in frequency.
FileSource::FadeStyle fadeStyleOf(const std::string& clm) {
    if (clm.rfind("<!>", 0) != 0) return FileSource::kFreqInterpolate;
    const size_t space = clm.find(' ', 3);
    if (space == std::string::npos || space + 1 >= clm.size()) return FileSource::kFreqInterpolate;
    if (clm[space + 1] == '0') return FileSource::kNoInterpolate;
    if (clm[space + 1] == '1') return FileSource::kTimeInterpolate;
    return FileSource::kFreqInterpolate;
}

std::string stemOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

void shapeLine(LineGenerator& line, Shape shape) {
    switch (shape) {
    case Shape::Sine: line.initSin(); break;
    case Shape::SawUp: line.initSawUp(); break;
    case Shape::SawDown: line.initSawDown(); break;
    case Shape::Square: line.initSquare(); break;
    case Shape::Init:
    case Shape::Triangle: line.initTriangle(); break;
    }
    line.setName(shapeName(shape == Shape::Init ? Shape::Triangle : shape));
}

json replaced(const std::string& value, int index, const std::string& entry, size_t count, const std::string& init,
              const char* what) {
    json list = value.empty() ? json::array() : parse(value, what);
    if (!list.is_array()) list = json::array();
    while (list.size() < count) list.push_back(parse(init, what));
    if (index < 0 || static_cast<size_t>(index) >= count) throw std::runtime_error("Vital: no such oscillator or LFO");
    list[static_cast<size_t>(index)] = parse(entry, what);
    return list;
}

std::vector<std::string> namesIn(const std::string& value, size_t count, const std::string& fallback) {
    std::vector<std::string> names(count, fallback);
    if (value.empty()) return names;
    try {
        const json list = json::parse(value);
        for (size_t i = 0; list.is_array() && i < list.size() && i < count; ++i) {
            if (list[i].is_object() && list[i].count("name") && list[i]["name"].is_string()) {
                const std::string name = list[i]["name"];
                if (!name.empty()) names[i] = name;
            }
        }
    } catch (const std::exception&) {
    }
    return names;
}

}  // namespace

std::string wavetableFromAudioFile(const std::string& utf8Path, WavetableStyle style) {
    std::shared_ptr<AudioSource> source;
    try {
        const AudioFileInfo info = AudioSource::probe(utf8Path);
        source = AudioSource::load(utf8Path, info.sampleRate);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Vital: the audio file can't be read (") + e.what() + ")");
    }
    if (!source || source->frames() <= 0) throw std::runtime_error("Vital: the audio file is empty");
    vital::Wavetable table(vital::kNumOscillatorWaveFrames);
    WavetableCreator creator(&table);
    const WavetableCreator::AudioFileLoadStyle load = style == WavetableStyle::Vocode  ? WavetableCreator::kVocoded
                                                      : style == WavetableStyle::Pitch ? WavetableCreator::kPitched
                                                                                       : WavetableCreator::kWavetableSplice;
    creator.initFromAudioFile(source->channelData(0), static_cast<int>(source->frames()),
                              static_cast<int>(source->sampleRate()), load, fadeStyleOf(clmChunk(utf8Path)));
    creator.setName(stemOf(utf8Path));
    return creator.stateToJson().dump();
}

std::string wavetableFromVitalTable(const std::string& text, const std::string& name) {
    const json data = parse(text, "wavetable");
    if (!WavetableCreator::isValidJson(data)) throw std::runtime_error("Vital: not a Vital wavetable");
    vital::Wavetable table(vital::kNumOscillatorWaveFrames);
    WavetableCreator creator(&table);
    try {
        creator.jsonToState(data);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Vital: the wavetable can't be read (") + e.what() + ")");
    }
    if (!name.empty()) creator.setName(name);
    return creator.stateToJson().dump();
}

std::string shapeName(Shape shape) {
    switch (shape) {
    case Shape::Init: return "Init";
    case Shape::Sine: return "Sine";
    case Shape::Triangle: return "Triangle";
    case Shape::SawUp: return "Saw Up";
    case Shape::SawDown: return "Saw Down";
    case Shape::Square: return "Square";
    }
    return {};
}

std::string wavetableOfShape(Shape shape) {
    vital::Wavetable table(vital::kNumOscillatorWaveFrames);
    WavetableCreator creator(&table);
    if (shape == Shape::Init) {
        creator.init();
    } else {
        LineGenerator line(vital::WaveFrame::kWaveformSize);
        shapeLine(line, shape);
        creator.jsonToState(line.stateToJson());  // (a line shape: as a wavetable of it)
    }
    return creator.stateToJson().dump();
}

std::string lfoOfShape(Shape shape) {
    LineGenerator line;
    shapeLine(line, shape);
    return line.stateToJson().dump();
}

std::string withWavetable(const std::string& wavetables, int index, const std::string& entry) {
    static const std::string kInit = wavetableOfShape(Shape::Init);
    return replaced(wavetables, index, entry, kNumOscillators, kInit, "wavetables").dump();
}

std::string withLfo(const std::string& lfos, int index, const std::string& entry) {
    static const std::string kInit = lfoOfShape(Shape::Init);
    return replaced(lfos, index, entry, kNumLfos, kInit, "LFOs").dump();
}

std::vector<std::string> wavetableNames(const std::string& wavetables) {
    return namesIn(wavetables, kNumOscillators, "Init");
}

std::vector<std::string> lfoNames(const std::string& lfos) { return namesIn(lfos, kNumLfos, "Triangle"); }

}  // namespace sub::vitalpatch
