// Samplers' samples and Drum Racks' pads: loading a sample (or a clip's part
// of one) into a Sampler or onto a pad, swapping in similar sounds, and the
// reference they are found from (model/SamplerState.h); pads made tracks of
// their own.

#include "editor/EditorSupport.h"
#include "editor/ProjectEditor.h"

#include "model/Commands.h"
#include "model/Devices.h"
#include "model/Ids.h"
#include "model/SamplerState.h"

#include <QFileInfo>
#include <QUndoStack>

#include <algorithm>

namespace sub::app {

using editing::frozenText;
using editing::Macro;

namespace {

QString sampleName(const QString& path) { return QFileInfo(path).completeBaseName(); }

// Whether a pad is named as it came (after its key) or after its sample: it
// follows the sample it plays then. A pad named by hand keeps its name.
bool followsSample(const Chain& pad, const QString& samplePath) {
    return pad.name.isEmpty() || pad.name == drumNoteName(pad.receiveNote) ||
           (!samplePath.isEmpty() && pad.name == sampleName(samplePath));
}

// A Sampler's Start and End for a part of its file (fractions of it); none for the whole.
OrderedMap<QString, double> partParams(double start, double end) {
    OrderedMap<QString, double> params;
    start = std::clamp(start, 0.0, 1.0);
    end = std::clamp(end, start, 1.0);
    if (start <= 0.0 && end >= 1.0) return params;
    params.insert(QStringLiteral("start"), start * 100.0);
    params.insert(QStringLiteral("end"), end * 100.0);
    return params;
}

// A new pad's Sampler playing `path`: in 1-Shot (a drum hit plays whole,
// however short the note), as Ableton's Drum Rack makes its Simplers.
Device padSamplerOf(Device sampler, const QString& path) {
    sampler.state = deviceState::toModel(samplerState::loaded(deviceState::fromModel(sampler.state), path));
    sampler.params.insert(QStringLiteral("mode"), kSamplerOneShot);
    return sampler;
}

// A built-in MIDI effect (an arpeggiator): it plays its track's notes, wherever it sits.
bool isMidiEffect(const Device& device) {
    const BuiltinDevice* builtin = device.kind == kPluginKind ? nullptr : builtinDevice(device.kind);
    return builtin != nullptr && builtin->midiEffect;
}

// A pad of `drums`' Drum Rack as a MIDI track of its own (its group and place
// are the caller's): the pad's devices, then copies of the track's MIDI
// effects; its fader; its automation on the track (its chain's faders as the
// track's); and the notes of its key on the track's clips, as its devices
// heard them (C3).
Track padTrack(const Track& drums, const Chain& pad) {
    Track track;
    track.id = newId();
    track.kind = kMidiKind;
    track.nameTemplate = pad.name.isEmpty() ? drumNoteName(pad.receiveNote) : pad.name;
    track.name = track.nameTemplate;
    track.color = drums.color;
    track.volumeDb = pad.volumeDb;
    track.pan = pad.pan;
    track.mute = pad.mute;
    track.solo = pad.solo;
    track.devices = pad.devices;
    for (const Device& device : drums.devices) {
        if (!isMidiEffect(device)) continue;
        Device copy = device;
        refreshIds(copy);
        track.devices.push_back(std::move(copy));
    }
    pruneModulations(track.devices, [](const QString& kind) { return isModulator(kind); });
    for (const Clip& clip : drums.clips) {
        if (!clip.isMidi()) continue;
        std::vector<Note> notes;
        for (Note note : clip.notes) {
            if (note.pitch != pad.receiveNote) continue;
            note.pitch = kDrumPadNote;
            notes.push_back(std::move(note));
        }
        if (notes.empty()) continue;
        Clip copy = clip;
        copy.id = newId();
        copy.notes = std::move(notes);
        track.clips.push_back(std::move(copy));
    }
    QSet<QString> ids;
    addDeviceIds(ids, pad.devices);
    for (const auto& [key, points] : drums.automation) {
        if (const auto control = automation::keyChainControl(key)) {
            if (control->chainId != pad.id) continue;
            track.automation.insert(
                control->control == automation::kChainVolume ? automation::kMixerVolume : automation::kMixerPan, points);
        } else if (const auto device = automation::keyDevice(key); device && ids.contains(*device)) {
            track.automation.insert(key, points);
        }
    }
    return track;
}

}  // namespace

int ProjectEditor::freePadNote(const Device& rack, int from) {
    // From `from` up, then down from it: as Ableton fills pads.
    for (int note = std::clamp(from, 0, 127); note <= 127; ++note) {
        if (drumPad(rack, note) == nullptr) return note;
    }
    for (int note = std::clamp(from, 0, 127) - 1; note >= 0; --note) {
        if (drumPad(rack, note) == nullptr) return note;
    }
    return -1;
}

void ProjectEditor::loadSample(const QString& trackId, const QString& deviceId, const QString& path, double start,
                               double end) {
    if (!project_->hasDevice(trackId, deviceId)) return;
    const Device& device = project_->device(trackId, deviceId);
    const deviceState::Values before = deviceState::fromModel(device.state);
    const std::optional<QString> state = deviceState::toModel(samplerState::loaded(before, path));
    const OrderedMap<QString, double> part = path.isEmpty() ? OrderedMap<QString, double>{} : partParams(start, end);
    if (state == device.state && part.isEmpty()) return;
    const QString text = path.isEmpty() ? QStringLiteral("Clear Sample") : QStringLiteral("Load Sample");
    Macro macro(undoStack_, text);
    if (state != device.state) push(std::make_unique<SetDeviceStateCommand>(project_, trackId, deviceId, device.state, state, text));
    if (!part.isEmpty()) setDeviceParams(trackId, deviceId, part, {}, text);
    renamePadsFollowing(trackId, {{deviceId, samplerState::sample(before)}}, text);
}

QString ProjectEditor::loadDrumPad(const QString& trackId, const QString& rackId, int note, const QString& path,
                                   double start, double end) {
    const Track* track = project_->findTrack(trackId);
    if (track == nullptr || path.isEmpty()) return {};
    const Device* rack = findDevice(track->devices, rackId);
    if (rack == nullptr || !rack->isDrumRack()) return {};
    if (note < 0) note = freePadNote(*rack);
    if (note < 0 || note > 127) return {};
    if (const Chain* pad = drumPad(*rack, note)) {
        const QString padId = pad->id;
        if (const Device* sampler = padSampler(*pad)) {  // its Sampler takes the sample
            loadSample(trackId, sampler->id, path, start, end);
            return padId;
        }
        // A pad without a Sampler: one goes first in it, in place of the
        // instrument there (a chain has one, as insertDevices keeps it).
        const std::vector<Device> before = track->devices;
        std::vector<Device> after = before;
        Device sampler = padSamplerOf(newDeviceOf(kSamplerKind), path);
        for (const auto& [param, value] : partParams(start, end)) sampler.params.insert(param, value);
        Chain& into = *findChain(after, padId).chain;
        std::erase_if(into.devices, [](const Device& device) { return deviceIsInstrument(device); });
        into.devices.insert(into.devices.begin(), std::move(sampler));
        if (followsSample(into, {})) into.name = sampleName(path);
        setDevices(trackId, before, std::move(after), QStringLiteral("Load Pad"));
        return padId;
    }
    const std::vector<Device> before = track->devices;
    std::vector<Device> after = before;
    Device sampler = padSamplerOf(newDeviceOf(kSamplerKind), path);
    for (const auto& [param, value] : partParams(start, end)) sampler.params.insert(param, value);
    std::vector<Device> devices;
    devices.push_back(std::move(sampler));
    Chain pad = newDrumPad(note, sampleName(path), std::move(devices));
    const QString padId = pad.id;
    Device& into = *findDevice(after, rackId);
    // In key order, as the pads sit.
    const auto at = std::find_if(into.chains.begin(), into.chains.end(),
                                 [note](const Chain& chain) { return chain.receiveNote > note; });
    into.chains.insert(at, std::move(pad));
    if (!setDevices(trackId, before, std::move(after), QStringLiteral("Load Pad"))) return {};
    return findChain(project_->track(trackId).devices, padId).chain != nullptr ? padId : QString();
}

void ProjectEditor::swapSamples(const QString& trackId, const QList<QPair<QString, QString>>& samples,
                                const QString& text) {
    if (!project_->hasOwner(trackId)) return;
    QList<QPair<QString, QString>> changing;  // (device, its sample before)
    std::vector<std::pair<QString, std::optional<QString>>> states;
    for (const auto& [deviceId, path] : samples) {
        if (!project_->hasDevice(trackId, deviceId) || path.isEmpty()) continue;
        const Device& device = project_->device(trackId, deviceId);
        const deviceState::Values before = deviceState::fromModel(device.state);
        const std::optional<QString> state = deviceState::toModel(samplerState::stepped(before, path));
        if (state == device.state) continue;
        changing.append({deviceId, samplerState::sample(before)});
        states.emplace_back(deviceId, state);
    }
    if (states.empty()) return;
    Macro macro(undoStack_, text);
    for (const auto& [deviceId, state] : states) {
        push(std::make_unique<SetDeviceStateCommand>(project_, trackId, deviceId, project_->device(trackId, deviceId).state,
                                                     state, text));
    }
    renamePadsFollowing(trackId, changing, text);
}

void ProjectEditor::setSimilarReference(const QString& trackId, const QString& deviceId) {
    if (!project_->hasDevice(trackId, deviceId)) return;
    const Device& device = project_->device(trackId, deviceId);
    const std::optional<QString> state = deviceState::toModel(samplerState::referenceSet(deviceState::fromModel(device.state)));
    if (state == device.state) return;
    push(std::make_unique<SetDeviceStateCommand>(project_, trackId, deviceId, device.state, state,
                                                 QStringLiteral("Save as Similar Reference")));
}

void ProjectEditor::renamePadsFollowing(const QString& trackId, const QList<QPair<QString, QString>>& samplersBefore,
                                        const QString& text) {
    // A pad named after the sample its Sampler played is named after the new one.
    const Track& track = project_->track(trackId);
    for (const auto& [deviceId, before] : samplersBefore) {
        const auto container = containerOf(track.devices, deviceId);
        if (!container) continue;
        const ConstRackChain rc = findChain(track.devices, *container);
        if (rc.rack == nullptr || !rc.rack->isDrumRack() || padSampler(*rc.chain) == nullptr ||
            padSampler(*rc.chain)->id != deviceId) {
            continue;
        }
        const QString now = samplerState::sample(deviceState::fromModel(project_->device(trackId, deviceId).state));
        if (now.isEmpty() || !followsSample(*rc.chain, before)) continue;
        const QString name = sampleName(now);
        if (name == rc.chain->name) continue;
        push(std::make_unique<UpdateChainCommand>(project_, trackId, rc.chain->id, ChainField::Name, rc.chain->name, name,
                                                  text));
    }
}

// --- Pads as tracks ---------------------------------------------------------------------------

void ProjectEditor::padSidechainsToTracks(const QString& trackId, const QHash<QString, QString>& padTracks,
                                          const QString& text) {
    struct Keyed {
        QString trackId, deviceId;
        Sidechain sidechain;
    };
    std::vector<Keyed> keyed;
    for (const Track* owner : project_->allTracks()) {
        if (project_->isFrozen(owner->id)) continue;  // (its devices don't play: theirs stay)
        for (const Device* device : iterDevices(owner->devices)) {
            const auto& sidechain = device->sidechain;
            if (sidechain && sidechain->trackId == trackId && padTracks.contains(sidechain->chain))
                keyed.push_back({owner->id, device->id, *sidechain});
        }
    }
    for (const Keyed& k : keyed) {
        const Sidechain moved{padTracks.value(k.sidechain.chain), k.sidechain.tap};
        push(std::make_unique<SetDeviceSidechainCommand>(project_, k.trackId, k.deviceId, k.sidechain, moved, text));
    }
}

QStringList ProjectEditor::separatePads(const QString& trackId, const QString& rackId) {
    const Project& p = *project_;
    const Track* drums = p.findTrack(trackId);
    if (drums == nullptr || !drums->isMidi()) return {};
    const auto rack = std::find_if(drums->devices.begin(), drums->devices.end(),
                                   [&](const Device& device) { return device.id == rackId; });
    if (rack == drums->devices.end() || !rack->isDrumRack() || rack->chains.empty()) return {};
    if (const auto holder = p.frozenBy(trackId)) {
        Q_EMIT refused(frozenText(p.track(*holder).name, QStringLiteral("its devices")));
        return {};
    }
    const Track before = *drums;
    std::vector<Track> pads;
    QHash<QString, QString> padTracks;  // pad -> its track
    for (const Chain& pad : rack->chains) {
        pads.push_back(padTrack(before, pad));
        showAutomationAsTheRest(pads.back());
        padTracks.insert(pad.id, pads.back().id);
    }
    // The track as a group: its mixer, sends and routing, and its devices but
    // the rack and the MIDI effects (each pad's track has those), with their automation.
    Track group;
    group.id = before.id;
    group.name = before.name;
    group.nameTemplate = before.nameTemplate;
    group.color = before.color;
    group.volumeDb = before.volumeDb;
    group.pan = before.pan;
    group.mute = before.mute;
    group.solo = before.solo;
    group.sends = before.sends;
    group.output = before.output;
    group.parent = before.parent;
    group.kind = kGroupKind;
    group.height = kDefaultGroupHeight;
    for (const Device& device : before.devices) {
        if (device.id != rackId && !isMidiEffect(device)) group.devices.push_back(device);
    }
    pruneModulations(group.devices, [](const QString& kind) { return isModulator(kind); });
    QSet<QString> kept;
    addDeviceIds(kept, group.devices);
    const auto keeps = [&kept](const QString& key) {
        const auto device = automation::keyDevice(key);
        return !automation::keyChain(key) && (!device || kept.contains(*device));
    };
    for (const auto& [key, points] : before.automation) {
        if (keeps(key)) group.automation.insert(key, points);
    }
    group.automationView = before.automationView;
    AutomationView& view = group.automationView;
    if (view.key && !keeps(*view.key)) view.key.reset();
    view.lanes.removeIf([&keeps](const QString& key) { return !keeps(key); });

    const QString text = QStringLiteral("Separate Pads into Tracks");
    Macro macro(undoStack_, text);
    dropMidiInputs({trackId}, text);  // (a group plays no notes)
    push(std::make_unique<ReplaceTrackCommand>(project_, before, group, text));
    int index = p.trackIndex(trackId) + 1;
    QStringList ids;
    for (Track& pad : pads) {
        pad.parent = trackId;
        ids << pad.id;
        push(std::make_unique<InsertTrackCommand>(project_, std::move(pad), index++, text));
    }
    padSidechainsToTracks(trackId, padTracks, text);
    return ids;
}

QString ProjectEditor::padToTrack(const QString& trackId, const QString& chainId, const QString& afterTrackId) {
    const Project& p = *project_;
    const Track* drums = p.findTrack(trackId);
    if (drums == nullptr) return {};
    const ConstRackChain rc = findChain(drums->devices, chainId);
    if (rc.rack == nullptr || !rc.rack->isDrumRack()) return {};
    if (const auto holder = p.frozenBy(trackId)) {
        Q_EMIT refused(frozenText(p.track(*holder).name, QStringLiteral("its devices")));
        return {};
    }
    const Track before = *drums;
    const int note = rc.chain->receiveNote;
    Track track = padTrack(before, *rc.chain);
    showAutomationAsTheRest(track);
    const InsertionPoint at = insertionPoint(afterTrackId);
    const QString text = QStringLiteral("Pad to Track");
    Macro macro(undoStack_, text);
    const QString id = insertTrack(std::move(track), at.index.value_or(-1), at.parent, text);
    if (id.isEmpty()) return {};
    padSidechainsToTracks(trackId, {{chainId, id}}, text);
    // The pad leaves the rack (its automation with it: setDevices), and its notes the clips.
    std::vector<Device> after = before.devices;
    RackChain into = findChain(after, chainId);
    std::erase_if(into.rack->chains, [&chainId](const Chain& chain) { return chain.id == chainId; });
    setDevices(trackId, before.devices, std::move(after), text);
    std::vector<Clip> clips = before.clips;
    bool changed = false;
    for (Clip& clip : clips) {
        if (clip.isMidi() && std::erase_if(clip.notes, [note](const Note& n) { return n.pitch == note; }) > 0)
            changed = true;
    }
    if (changed) {
        push(std::make_unique<SetClipsCommand>(project_, text, ClipLists{{trackId, before.clips}},
                                               ClipLists{{trackId, clips}}));
    }
    return id;
}

}  // namespace sub::app
