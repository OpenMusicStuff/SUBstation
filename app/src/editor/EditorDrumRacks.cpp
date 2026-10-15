// Samplers' samples and Drum Racks' pads: loading a sample (or a clip's part
// of one) into a Sampler or onto a pad, swapping in similar sounds, and the
// reference they are found from (model/SamplerState.h).

#include "editor/EditorSupport.h"
#include "editor/ProjectEditor.h"

#include "model/Commands.h"
#include "model/Devices.h"
#include "model/SamplerState.h"

#include <QFileInfo>
#include <QUndoStack>

#include <algorithm>

namespace sub::app {

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
        // A pad without a Sampler: one goes first in it.
        const std::vector<Device> before = track->devices;
        std::vector<Device> after = before;
        Device sampler = padSamplerOf(newDeviceOf(kSamplerKind), path);
        for (const auto& [param, value] : partParams(start, end)) sampler.params.insert(param, value);
        Chain& into = *findChain(after, padId).chain;
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

}  // namespace sub::app
