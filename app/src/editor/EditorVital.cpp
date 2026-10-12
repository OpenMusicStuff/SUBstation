// Editing a Vital device's modulation routing: its state's modulations, slot
// by slot, and the slot's parameters with them, as one undo step.

#include "editor/EditorSupport.h"
#include "editor/ProjectEditor.h"

#include "builtin/VitalPatch.h"
#include "model/Commands.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/Vital.h"

#include <QUndoStack>

#include <exception>

namespace sub::app {

using editing::Macro;

bool ProjectEditor::setVitalModulation(const QString& trackId, const QString& deviceId, int slot,
                                       const QString& source, const QString& destination) {
    const Device* device = project_->findDevice(trackId, deviceId);
    if (device == nullptr || !isVital(*device) || slot < 0 || slot >= kVitalModulationSlots) return false;
    const bool clearing = source.isEmpty() && destination.isEmpty();
    if (!clearing && (!vitalSources().contains(source) || !vitalDestinations().contains(destination))) return false;

    deviceState::Values values = deviceState::fromModel(device->state);
    const QString key = QString::fromLatin1(vitalpatch::kModulations);
    std::vector<vitalpatch::Modulation> modulations;
    try {
        if (!values.value(key).isEmpty()) modulations = vitalpatch::readModulations(values.value(key).toStdString());
    } catch (const std::exception&) {  // (unreadable: none, as the engine has it)
    }
    modulations.resize(std::max<size_t>(modulations.size(), kVitalModulationSlots));
    vitalpatch::Modulation& modulation = modulations[static_cast<size_t>(slot)];
    const QString oldSource = QString::fromStdString(modulation.source);
    if (oldSource == source && QString::fromStdString(modulation.destination) == destination) return true;
    modulation.source = source.toStdString();
    modulation.destination = destination.toStdString();
    if (clearing) modulation.lineMapping.clear();
    values.insert(key, QString::fromStdString(vitalpatch::writeModulations(modulations)));

    // Its settings: back to their defaults when it is cleared; a new source's Bipolar as Vital sets it.
    QMap<DeviceParam, double> nw;
    QMap<DeviceParam, double> old;
    const auto set = [&](const QString& what, double value) {
        const QString id = vitalSlotParam(slot, what);
        const sub::ParamInfo* info = builtinParamInfo(device->kind, id);
        if (info == nullptr) return;
        old.insert({deviceId, id}, device->params.value(id, info->defaultValue));
        nw.insert({deviceId, id}, value);
    };
    if (clearing) {
        for (const QString& what : {QStringLiteral("amount"), QStringLiteral("bipolar"), QStringLiteral("power"),
                                    QStringLiteral("stereo"), QStringLiteral("bypass")}) {
            if (const sub::ParamInfo* info = builtinParamInfo(device->kind, vitalSlotParam(slot, what))) {
                set(what, info->defaultValue);
            }
        }
    } else if (source != oldSource) {
        set(QStringLiteral("bipolar"), vitalpatch::sourceBipolar(source.toStdString()) ? 1.0 : 0.0);
    }

    const QString text = clearing ? QStringLiteral("Remove Modulation") : QStringLiteral("Change Modulation");
    const std::optional<QString> oldState = device->state;  // (the device may move as the commands run)
    Macro macro(undoStack_, text);
    if (old != nw) push(std::make_unique<SetDeviceParamsCommand>(project_, trackId, old, nw, text));
    push(std::make_unique<SetDeviceStateCommand>(project_, trackId, deviceId, oldState, deviceState::toModel(values), text));
    return true;
}

int ProjectEditor::addVitalModulation(const QString& trackId, const QString& deviceId, const QString& source,
                                      const QString& destination) {
    const Device* device = project_->findDevice(trackId, deviceId);
    if (device == nullptr || !isVital(*device) || source.isEmpty() || destination.isEmpty()) return -1;
    int free = -1;
    for (const VitalModulation& slot : vitalModulationSlots(*device)) {
        if (slot.source == source && slot.destination == destination) return slot.slot;
        if (free < 0 && slot.source.isEmpty() && slot.destination.isEmpty()) free = slot.slot;
    }
    if (free < 0 || !setVitalModulation(trackId, deviceId, free, source, destination)) return -1;
    return free;
}

}  // namespace sub::app
