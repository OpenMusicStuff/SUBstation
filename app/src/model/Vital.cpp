#include "model/Vital.h"

#include "builtin/VitalPatch.h"
#include "model/DeviceState.h"

#include <QHash>

#include <exception>

namespace sub::app {

QString vitalKind() { return QString::fromLatin1(vitalpatch::kKind); }

bool isVital(const Device& device) { return device.kind == QLatin1String(vitalpatch::kKind); }

std::vector<VitalModulation> vitalModulationSlots(const Device& device) {
    std::vector<VitalModulation> slots(kVitalModulationSlots);
    for (int i = 0; i < kVitalModulationSlots; ++i) slots[static_cast<size_t>(i)].slot = i;
    const QString value = deviceState::fromModel(device.state).value(QString::fromLatin1(vitalpatch::kModulations));
    if (value.isEmpty()) return slots;
    try {
        const std::vector<vitalpatch::Modulation> read = vitalpatch::readModulations(value.toStdString());
        for (size_t i = 0; i < read.size() && i < slots.size(); ++i) {
            slots[i].source = QString::fromStdString(read[i].source);
            slots[i].destination = QString::fromStdString(read[i].destination);
        }
    } catch (const std::exception&) {  // (unreadable: none, as the engine has it)
    }
    return slots;
}

std::vector<VitalModulation> vitalModulations(const Device& device) {
    std::vector<VitalModulation> used;
    for (const VitalModulation& slot : vitalModulationSlots(device)) {
        if (!slot.source.isEmpty() && !slot.destination.isEmpty()) used.push_back(slot);
    }
    return used;
}

namespace {

QStringList listOf(const std::vector<std::string>& names) {
    QStringList list;
    for (const std::string& name : names) list.append(QString::fromStdString(name));
    return list;
}

}  // namespace

QStringList vitalSources() {
    static const QStringList kSources = listOf(vitalpatch::modulationSources());
    return kSources;
}

QStringList vitalDestinations() {
    static const QStringList kDestinations = listOf(vitalpatch::modulationDestinations());
    return kDestinations;
}

QString vitalSourceName(const QString& source) {
    return QString::fromStdString(vitalpatch::sourceName(source.toStdString()));
}

QString vitalDestinationName(const QString& destination) {
    const int index = vitalpatch::paramIndex(destination.toStdString());
    if (index < 0) return destination;
    return QString::fromStdString(vitalpatch::params()[static_cast<size_t>(index)].name);
}

QString vitalSlotParam(int slot, const QString& what) {
    return QStringLiteral("modulation_%1_%2").arg(slot + 1).arg(what);
}

namespace {

QStringList listOfNames(const std::vector<std::string>& names) {
    QStringList list;
    for (const std::string& name : names) list.append(QString::fromStdString(name));
    return list;
}

}  // namespace

QStringList vitalWavetableNames(const Device& device) {
    return listOfNames(vitalpatch::wavetableNames(
        deviceState::fromModel(device.state).value(QString::fromLatin1(vitalpatch::kWavetables)).toStdString()));
}

QStringList vitalLfoNames(const Device& device) {
    return listOfNames(
        vitalpatch::lfoNames(deviceState::fromModel(device.state).value(QString::fromLatin1(vitalpatch::kLfos)).toStdString()));
}

QStringList vitalShapeNames() {
    QStringList names;
    for (int i = 0; i < vitalpatch::kNumShapes; ++i)
        names.append(QString::fromStdString(vitalpatch::shapeName(static_cast<vitalpatch::Shape>(i))));
    return names;
}

QString vitalStateValue(const Device& device, const QString& key) {
    return deviceState::fromModel(device.state).value(key);
}

}  // namespace sub::app
