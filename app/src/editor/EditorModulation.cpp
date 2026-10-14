// Editing modulation: mapping a modulator (an LFO, an Envelope) to parameters
// of devices on its track, over which range and which way up, and unmapping
// them.

#include "editor/ProjectEditor.h"

#include "model/Commands.h"
#include "model/Devices.h"
#include "model/Errors.h"

#include "editor/EditorSupport.h"

#include <algorithm>

namespace sub::app {

namespace {

// The mapping of a parameter in a modulator's list (end: none).
auto mappingOf(std::vector<ModulationMapping>& mappings, const QString& deviceId, const QString& paramId) {
    return std::find_if(mappings.begin(), mappings.end(),
                        [&](const ModulationMapping& m) { return m.deviceId == deviceId && m.paramId == paramId; });
}

}  // namespace

void ProjectEditor::mapModulation(const QString& trackId, const QString& modulatorId, const QString& deviceId,
                                  const QString& paramId, double low, double high, bool invert) {
    const Track& track = project_->track(trackId);
    const Device* modulator = findDevice(track.devices, modulatorId);
    if (modulator == nullptr || !isModulator(modulator->kind)) {
        throw EditError(QStringLiteral("Only a modulator (an LFO, an Envelope) moves other devices' parameters"));
    }
    const Device* target = findDevice(track.devices, deviceId);
    if (target == nullptr || deviceId == modulatorId) {
        throw EditError(QStringLiteral("A modulator moves parameters of the other devices on its track"));
    }
    if (target->isRack()) {
        throw EditError(QStringLiteral("A rack's macros can't be modulated: modulate the parameters they move"));
    }
    if (!canModulate(trackId, deviceId, paramId)) throw EditError(QStringLiteral("That parameter can't be modulated"));
    const std::vector<Device> before = track.devices;
    std::vector<Device> after = before;
    for (Device* d : iterDevices(after)) {  // (it follows one modulator at a time)
        auto& mappings = d->modulations;
        mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
                                      [&](const ModulationMapping& m) {
                                          return m.deviceId == deviceId && m.paramId == paramId;
                                      }),
                       mappings.end());
    }
    findDevice(after, modulatorId)
        ->modulations.push_back({deviceId, paramId, std::clamp(low, 0.0, 1.0), std::clamp(high, 0.0, 1.0), invert});
    if (after != before) setDevices(trackId, before, std::move(after), QStringLiteral("Map Modulation"));
}

bool ProjectEditor::tryMapModulation(const QString& trackId, const QString& modulatorId, const QString& deviceId,
                                     const QString& paramId) {
    if (!project_->hasDevice(trackId, modulatorId) || !project_->hasDevice(trackId, deviceId)) return false;
    // Unipolar from wherever the parameter is now to its maximum (the knob's own value stays the base).
    double base = 0.0;
    if (const auto info = paramInfo(trackId, deviceId, paramId)) {
        base = std::clamp(info->toNormalized(ownParamValue(trackId, deviceId, paramId, info->defaultValue)), 0.0, 1.0);
    }
    return reportRefusal([&] { mapModulation(trackId, modulatorId, deviceId, paramId, base, 1.0); });
}

bool ProjectEditor::pushModulatedParams(const QString& trackId, const QMap<DeviceParam, double>& nw,
                                        const QMap<DeviceParam, double>& old, const QString& text,
                                        const QString& mergeKey) {
    struct Change {
        std::vector<ModulationMapping> mappings;
        QMap<DeviceParam, double> oldValues, newValues;
    };
    QMap<QString, Change> changes;  // by modulator
    QMap<DeviceParam, double> unmoved;
    for (auto it = nw.constBegin(); it != nw.constEnd(); ++it) {
        const auto& [deviceId, paramId] = it.key();
        const QString modulatorId = modulatorOf(trackId, deviceId, paramId);
        const auto info = modulatorId.isEmpty() ? std::nullopt : paramInfo(trackId, deviceId, paramId);
        if (!info) {
            unmoved.insert(it.key(), it.value());
            continue;
        }
        auto change = changes.find(modulatorId);
        if (change == changes.end()) {
            change = changes.insert(modulatorId, {project_->device(trackId, modulatorId).modulations, {}, {}});
        }
        const auto mapping = mappingOf(change->mappings, deviceId, paramId);
        mapping->setAmount(info->toNormalized(it.value()), mapping->amount());
        change->newValues.insert(it.key(), it.value());
        change->oldValues.insert(it.key(), old.value(it.key(), it.value()));
    }
    if (changes.isEmpty()) return false;
    // The parameters nothing moves go with the first modulator's command, so a drag of several still merges.
    for (auto it = nw.constBegin(); it != nw.constEnd(); ++it) {
        bool claimed = false;
        for (const Change& change : std::as_const(changes)) claimed = claimed || change.newValues.contains(it.key());
        if (claimed) continue;
        changes.begin()->newValues.insert(it.key(), it.value());
        changes.begin()->oldValues.insert(it.key(), old.value(it.key(), it.value()));
    }
    std::optional<editing::Macro> macro;
    if (changes.size() > 1) macro.emplace(undoStack_, text);
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        push(std::make_unique<SetModulationsCommand>(project_, trackId, it.key(),
                                                     project_->device(trackId, it.key()).modulations, it->mappings,
                                                     text, mergeKey, it->oldValues, it->newValues));
    }
    return true;
}

bool ProjectEditor::canModulate(const QString& trackId, const QString& deviceId, const QString& paramId) const {
    const Device* device = project_->findDevice(trackId, deviceId);
    if (device == nullptr || device->isRack()) return false;
    if (!device->isPlugin()) {
        const sub::ParamInfo* info = builtinParamInfo(device->kind, paramId);
        return info != nullptr && info->automatable && !info->hidden && !info->readOnly;
    }
    return paramInfo(trackId, deviceId, paramId).has_value();
}

void ProjectEditor::setModulationRange(const QString& trackId, const QString& modulatorId, const QString& deviceId,
                                       const QString& paramId, double low, double high, const QString& mergeKey) {
    const Device* modulator = project_->findDevice(trackId, modulatorId);
    if (modulator == nullptr) return;
    std::vector<ModulationMapping> nw = modulator->modulations;
    const auto mapping = mappingOf(nw, deviceId, paramId);
    if (mapping == nw.end()) return;
    mapping->low = std::clamp(low, 0.0, 1.0);
    mapping->high = std::clamp(high, 0.0, 1.0);
    if (nw == modulator->modulations) return;
    push(std::make_unique<SetModulationsCommand>(project_, trackId, modulatorId, modulator->modulations, nw,
                                                 QStringLiteral("Change Modulation Range"), mergeKey));
}

void ProjectEditor::setModulationInverted(const QString& trackId, const QString& modulatorId, const QString& deviceId,
                                          const QString& paramId, bool invert) {
    const Device* modulator = project_->findDevice(trackId, modulatorId);
    if (modulator == nullptr) return;
    std::vector<ModulationMapping> nw = modulator->modulations;
    const auto mapping = mappingOf(nw, deviceId, paramId);
    if (mapping == nw.end() || mapping->invert == invert) return;
    mapping->invert = invert;
    push(std::make_unique<SetModulationsCommand>(project_, trackId, modulatorId, modulator->modulations, nw,
                                                 invert ? QStringLiteral("Invert Modulation")
                                                        : QStringLiteral("Uninvert Modulation")));
}

void ProjectEditor::setModulationAmount(const QString& trackId, const QString& modulatorId, const QString& deviceId,
                                        const QString& paramId, double amount, double base, const QString& mergeKey) {
    const Device* modulator = project_->findDevice(trackId, modulatorId);
    if (modulator == nullptr) return;
    std::vector<ModulationMapping> nw = modulator->modulations;
    const auto mapping = mappingOf(nw, deviceId, paramId);
    if (mapping == nw.end()) return;
    mapping->setAmount(base, amount);
    if (nw == modulator->modulations) return;
    push(std::make_unique<SetModulationsCommand>(project_, trackId, modulatorId, modulator->modulations, nw,
                                                 QStringLiteral("Change Modulation Amount"), mergeKey));
}

void ProjectEditor::setModulationBipolar(const QString& trackId, const QString& modulatorId, const QString& deviceId,
                                         const QString& paramId, bool bipolar, double base) {
    const Device* modulator = project_->findDevice(trackId, modulatorId);
    if (modulator == nullptr) return;
    std::vector<ModulationMapping> nw = modulator->modulations;
    const auto mapping = mappingOf(nw, deviceId, paramId);
    if (mapping == nw.end() || mapping->bipolar == bipolar) return;
    const double amount = mapping->amount();
    mapping->bipolar = bipolar;
    mapping->setAmount(base, amount);
    push(std::make_unique<SetModulationsCommand>(project_, trackId, modulatorId, modulator->modulations, nw,
                                                 bipolar ? QStringLiteral("Bipolar Modulation")
                                                         : QStringLiteral("Unipolar Modulation")));
}

void ProjectEditor::unmapModulation(const QString& trackId, const QString& deviceId, const QString& paramId) {
    const QString modulatorId = modulatorOf(trackId, deviceId, paramId);
    if (modulatorId.isEmpty()) return;
    const Device& modulator = project_->device(trackId, modulatorId);
    std::vector<ModulationMapping> kept = modulator.modulations;
    kept.erase(mappingOf(kept, deviceId, paramId));
    push(std::make_unique<SetModulationsCommand>(project_, trackId, modulatorId, modulator.modulations, kept,
                                                 QStringLiteral("Remove Modulation")));
}

void ProjectEditor::clearModulations(const QString& trackId, const QString& modulatorId) {
    const Device* modulator = project_->findDevice(trackId, modulatorId);
    if (modulator == nullptr || modulator->modulations.empty()) return;
    push(std::make_unique<SetModulationsCommand>(project_, trackId, modulatorId, modulator->modulations,
                                                 std::vector<ModulationMapping>{},
                                                 QStringLiteral("Remove All Modulations")));
}

QString ProjectEditor::modulatorOf(const QString& trackId, const QString& deviceId, const QString& paramId) const {
    if (!project_->hasOwner(trackId)) return {};
    for (const Device* d : iterDevices(project_->track(trackId).devices)) {
        for (const ModulationMapping& m : d->modulations) {
            if (m.deviceId == deviceId && m.paramId == paramId) return d->id;
        }
    }
    return {};
}

QStringList ProjectEditor::modulators(const QString& trackId) const {
    QStringList ids;
    if (!project_->hasOwner(trackId)) return ids;
    for (const Device* d : iterDevices(project_->track(trackId).devices)) {
        if (isModulator(d->kind)) ids.append(d->id);
    }
    return ids;
}

}  // namespace sub::app
