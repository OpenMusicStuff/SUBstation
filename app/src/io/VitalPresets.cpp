#include "io/VitalPresets.h"

#include "builtin/VitalPatch.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/Errors.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <exception>

namespace sub::app {

bool isVitalPreset(const QString& path) { return path.endsWith(kVitalPresetExtension, Qt::CaseInsensitive); }

Device loadVitalPreset(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw ProjectFileError(QStringLiteral("Could not read %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
    const QByteArray text = file.readAll();
    vitalpatch::Patch patch;
    try {
        patch = vitalpatch::readPreset(text.toStdString(), QFileInfo(path).completeBaseName().toStdString());
    } catch (const std::exception& error) {
        throw ProjectFileError(QStringLiteral("%1 isn't a Vital preset Vital's engine can read (%2).")
                                   .arg(QFileInfo(path).fileName(), QString::fromStdString(error.what())));
    }
    Device device = newDevice(QString::fromLatin1(vitalpatch::kKind));
    for (const auto& [id, value] : patch.params) device.params.insert(QString::fromStdString(id), value);
    deviceState::Values values;
    for (const auto& [key, value] : patch.state) values.insert(QString::fromStdString(key), QString::fromStdString(value));
    device.state = deviceState::toModel(values);
    return device;
}

QByteArray vitalPresetText(const Device& device) {
    vitalpatch::Patch patch;
    for (auto it = device.params.cbegin(); it != device.params.cend(); ++it) {
        patch.params[it.key().toStdString()] = static_cast<float>(it.value());
    }
    const deviceState::Values values = deviceState::fromModel(device.state);
    for (auto it = values.cbegin(); it != values.cend(); ++it) patch.state[it.key().toStdString()] = it.value().toStdString();
    try {
        return QByteArray::fromStdString(vitalpatch::writePreset(patch));
    } catch (const std::exception& error) {
        throw ProjectFileError(QStringLiteral("The Vital patch can't be written (%1).").arg(QString::fromStdString(error.what())));
    }
}

void saveVitalPreset(const Device& device, const QString& path) {
    const QByteArray text = vitalPresetText(device);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit()) {
        throw ProjectFileError(QStringLiteral("Could not write %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
}

}  // namespace sub::app
