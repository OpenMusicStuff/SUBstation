#pragma once
// Devices on a track's chain, racks and their chains, and the helpers that
// walk the device tree (devices in racks, in chains of racks, ...).
//
// A device may be a rack (kind "rack": a device group). Its chains each process
// its input, side by side, through their devices (racks too, at most
// kMaxRackDepth deep) and a mixer of their own (volume, pan, mute, solo among
// the rack's chains), and it puts out their sum; its chains' devices all hear
// the track's notes, so a rack of instruments layers them. Devices nested in
// racks are the track's devices as much as those in its own chain: device ids
// are unique in the project, so Project::device() finds them wherever they sit,
// and their automation is the track's (by device id). A rack's macros are its
// parameters ("macro1"..): each can be mapped to parameters of devices in it
// (MacroMapping), which follow it across their range (all of it, part of it, or
// the other way round). A rack has 1 to kMaxMacroCount of them (a new one
// kDefaultMacroCount), each with a name of its own if given one (macroNames). A
// Drum Rack has none: what its pads' devices should move together goes in a
// rack of their own (Ctrl+G).
// A modulator (a built-in LFO or envelope: isModulator() in Devices.h) moves
// parameters of other devices on its track, wherever they sit
// (ModulationMapping, in its `modulations`), as the engine plays it.
//
// The helpers hand out pointers into the vectors they walk: they stay valid
// until a vector they point into changes.

#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace sub::app {

// Which plug-in a device is: enough to load it, to find it again if it moved,
// and to name it if it is missing.
struct PluginRef {
    QString format = QStringLiteral("VST3");  // or "VST2"
    QString uid;  // VST3 class id; VST2 unique id (8 hex digits)
    QString name;
    QString vendor;
    QString path;  // where it was when last loaded
    bool instrument = false;

    friend bool operator==(const PluginRef&, const PluginRef&) = default;
};

inline const QString kPluginKind = QStringLiteral("plugin");
inline const QString kRackKind = QStringLiteral("rack");  // a device group: chains side by side
// What a new Drum Rack is asked for by (the browser, a drop, addDevice): a rack
// with `drum` set. No device has this kind: a Drum Rack is a rack.
inline const QString kDrumRackKind = QStringLiteral("drum_rack");
inline constexpr int kDrumPadNote = 60;      // what a pad's devices hear its notes as (C3, a Sampler's root)
inline constexpr int kDrumFirstPadNote = 36; // the first pad (C1), as in Ableton
inline constexpr int kMaxRackDepth = 8;  // racks nest at most this deep (the engine's MAX_RACK_DEPTH)
inline constexpr int kDefaultMacroCount = 4;  // a new rack's macros
inline constexpr int kMaxMacroCount = 16;     // a rack has 1 to this many: its parameters macroParam(0..15)

// A rack's macro's parameter id: "macro1" for the first.
QString macroParam(int index);
// Which macro a rack's parameter is ("macro3": 2); none: no macro's.
std::optional<int> macroIndex(const QString& paramId);

// Where a sidechain takes its source's signal (Sidechain::tap), unless after
// one of its devices: as Ableton's Post Mixer, Post FX and Pre FX.
inline const QString kPostFader = QStringLiteral("post");
inline const QString kPreFader = QStringLiteral("pre");
inline const QString kPreFx = QStringLiteral("pre-fx");

// What a device's sidechain (aux) input hears: a track's signal (a group's, a
// return's: `trackId`), after its fader and pan (kPostFader), before it
// (kPreFader, after all its devices), before all its devices (kPreFx: what they
// hear; on a MIDI track, after its instrument), or after one of its devices
// (`tap`: that device's id; one in a rack's chain too, before that chain's
// fader; before the fader while that device isn't on the track). It isn't heard
// on its own, so the source's mute and solo silence it only after the fader.
struct Sidechain {
    QString trackId;
    QString tap = kPostFader;

    // The device it is taken after (none: after the fader, before it, or before the devices).
    std::optional<QString> tapDevice() const;

    friend bool operator==(const Sidechain&, const Sidechain&) = default;
};

// A rack's macro (0-based) moving a parameter of a device in the rack: the
// macro's 0..1 is the parameter's `low`..`high` (normalized, as automation is;
// `low` > `high` turns it the other way; 0.5..1 moves it over its upper half).
struct MacroMapping {
    int macro = 0;
    QString deviceId;
    QString paramId;
    double low = 0.0;
    double high = 1.0;

    // The parameter's normalized value for the macro at `value` (0..1).
    double target(double value) const;

    friend bool operator==(const MacroMapping&, const MacroMapping&) = default;
};

// A modulator moving a parameter of a device on its track (in a rack too; not
// the modulator itself): the modulator's 0..1 is the parameter's `low`..`high`
// (normalized, as automation is), or turned upside down with `invert` (1 - its
// value). A parameter follows one modulator at most; while it does, its own
// value and its automation wait.
//
// Its amount, as a modulated knob's handle shows and drags it (as Serum's and
// Ableton's do): unipolar, the modulator moves it from `low` by `high - low`;
// `bipolar`, either side of the middle of the range by half of that. Only the
// handle reads `bipolar`: the engine plays low..high either way. Set from the
// handle, `low` and `high` may reach past 0..1 (to -1..2), so the amount
// survives a range cut off at the parameter's ends (switching polarity, the
// knob near an end); what plays is held to 0..1 (`target()`), as the engine
// holds it.
struct ModulationMapping {
    QString deviceId;
    QString paramId;
    double low = 0.0;
    double high = 1.0;
    bool invert = false;
    bool bipolar = false;

    // The parameter's normalized value for the modulator at `value` (0..1).
    double target(double value) const;
    // The range it plays, held to 0..1: where it is at the modulator's 0 and 1.
    double playedLow() const;
    double playedHigh() const;
    // Its amount (-1..1: negative, the other way): high - low, or half of it when bipolar.
    double amount() const { return bipolar ? (high - low) / 2 : high - low; }
    // The range for `amount` from `base` (normalized: the parameter's own value), up from it or, bipolar, either
    // side of it (its ends unclamped: -1..2).
    void setAmount(double base, double amount);

    friend bool operator==(const ModulationMapping&, const ModulationMapping&) = default;
};

struct Chain;

// An insert device on a track: a built-in one ("synth", "utility"), a plug-in
// (kind "plugin", with `plugin` saying which), or a rack (kind "rack": its
// `chains`, its macros' `macros` mappings, and `macroNames`, one per macro).
// A modulator's `modulations` are the parameters it moves.
//
// A built-in device's state is its `params`, and `state` for what isn't a
// parameter (a sampler's sample: see DeviceState.h), base64; the engine follows
// the model. A plug-in keeps its own state; `state` holds it (base64) as last
// saved, for loading the project. Its `params` only record values changed from
// the host, for undo. A rack's params are its macros' values (macroParam()). A
// device with a sidechain (aux) input may hear a track there (`sidechain`). A
// device that plays notes (an instrument, an effect with a MIDI input, as a
// vocoder or a pitch corrector has) may take a MIDI track's notes instead of
// its own track's (`midiFrom`, that track's id; "": its own track's).
struct Device {
    QString id;
    QString kind;
    bool enabled = true;
    QMap<QString, double> params;
    std::optional<PluginRef> plugin;
    std::optional<QString> state;
    std::optional<Sidechain> sidechain;
    QString midiFrom;  // a MIDI track whose notes it plays instead of its own track's ("": its own track's)
    std::vector<Chain> chains;  // a rack's
    std::vector<MacroMapping> macros;  // a rack's
    // A rack's macros, one each: its name as the user gave it ("": "Macro N"; macroName()).
    std::vector<QString> macroNames;
    std::vector<ModulationMapping> modulations;  // a modulator's
    // A rack's own name (the preset it was saved as or loaded from); none: by its kind.
    std::optional<QString> name;
    // A Drum Rack (a rack): its chains are pads, each playing the notes of its
    // own key (Chain::receiveNote), as C3. An instrument even while empty.
    bool drum = false;

    bool isPlugin() const { return kind == kPluginKind; }
    bool isRack() const { return kind == kRackKind; }
    bool isDrumRack() const { return isRack() && drum; }

    bool operator==(const Device& other) const;
    bool operator!=(const Device& other) const { return !(*this == other); }
};

// A chain of a rack: its devices, and its mixer (volume, pan, mute, and solo:
// while any of a rack's chains is soloed, only those are heard). A Drum Rack's
// chain is a pad: it hears only the notes of its key (`receiveNote`), as C3;
// locked, swapping in similar sounds leaves its sample be.
struct Chain {
    QString id;
    QString name;
    std::vector<Device> devices;
    double volumeDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
    int receiveNote = -1;  // a pad's key (0-127); -1: every note (a chain of another rack)
    bool locked = false;   // a pad's sample stays when the rack swaps in similar sounds

    bool operator==(const Chain& other) const;
    bool operator!=(const Chain& other) const { return !(*this == other); }
};

// How many macros a rack has (0: not a rack).
int macroCount(const Device& rack);
// A rack's macro's name: the one it was given, else "Macro N".
QString macroName(const Device& rack, int index);

// A rack and one of its chains.
template <typename DeviceT, typename ChainT>
struct RackChainOf {
    DeviceT* rack;
    ChainT* chain;
};
using RackChain = RackChainOf<Device, Chain>;
using ConstRackChain = RackChainOf<const Device, const Chain>;

// Every device in a chain, depth first: each, then (a rack) those in its chains.
std::vector<Device*> iterDevices(std::vector<Device>& devices);
std::vector<const Device*> iterDevices(const std::vector<Device>& devices);
// Every rack chain in a chain (and in racks in it), with its rack, depth first.
std::vector<RackChain> iterChains(std::vector<Device>& devices);
std::vector<ConstRackChain> iterChains(const std::vector<Device>& devices);

// Where a device is: its index in the chain, or for one in a rack the rack's
// index, the chain's and its own in that chain (and so on, deeper); none: nowhere.
std::optional<std::vector<int>> devicePath(const std::vector<Device>& devices, const QString& deviceId);
Device& deviceAt(std::vector<Device>& devices, const std::vector<int>& path);
const Device& deviceAt(const std::vector<Device>& devices, const std::vector<int>& path);
// A device by id (in racks too); null: not there.
Device* findDevice(std::vector<Device>& devices, const QString& deviceId);
const Device* findDevice(const std::vector<Device>& devices, const QString& deviceId);
// A rack chain by id, in a chain or in the racks in it (depth first, as
// iterChains), with its rack; both null: not there.
RackChain findChain(std::vector<Device>& devices, const QString& chainId);
ConstRackChain findChain(const std::vector<Device>& devices, const QString& chainId);
// Where a rack's own chain of this id is among its chains (-1: none of its own).
int chainIndex(const Device& rack, const QString& chainId);
// The devices of a chain (the vector itself): `devices` (no chain: a track's
// own) or the rack chain with that id in it; null: not there.
std::vector<Device>* chainDevices(std::vector<Device>& devices, const std::optional<QString>& chain);
const std::vector<Device>* chainDevices(const std::vector<Device>& devices, const std::optional<QString>& chain);
// The chain a device is in: none for the track's own (or nowhere), else the rack chain's id.
std::optional<QString> containerOf(const std::vector<Device>& devices, const QString& deviceId);
// How many racks a chain is in (0: a track's own).
int rackDepth(const std::vector<Device>& devices, const std::optional<QString>& chain);
// New ids for a device and everything in it (its chains too), in place: a copy
// (a preset loaded, a chain duplicated) of devices that exist. Its macro and
// modulation mappings follow their devices; mappings to devices not in it go.
void refreshIds(Device& device);
// Modulation mappings whose devices got new ids (old id -> new one): those to
// devices renamed, renamed; the others gone.
std::vector<ModulationMapping> renamedModulations(const std::vector<ModulationMapping>& mappings,
                                                  const QHash<QString, QString>& renamed);
// Drops the modulation mappings a track's devices can't have: to a device not
// among them (anywhere in their racks), to the modulator itself, of a device
// that isn't a modulator (`isModulator`, by kind), or to a parameter mapped
// already (the first mapping of each parameter stays). True if any went.
bool pruneModulations(std::vector<Device>& devices, const std::function<bool(const QString&)>& isModulator);
// How deep racks nest in a device: 0 for a device, 1 for a rack without racks in it.
int rackHeight(const Device& device);

}  // namespace sub::app
