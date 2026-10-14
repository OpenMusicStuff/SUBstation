#include "devices/VitalPatchInfo.h"

#include "editor/ProjectEditor.h"
#include "model/Device.h"
#include "model/Project.h"
#include "io/VitalPresets.h"
#include "model/Errors.h"
#include "model/Vital.h"

#include <QFileInfo>

#include <QVariantMap>

#include <algorithm>

namespace sub::ui {

using sub::app::Device;
using sub::app::Project;

namespace {

// [{id, name}] sorted by name (case-insensitively, then by id).
QVariantList named(const QStringList& ids, QString (*nameOf)(const QString&)) {
    std::vector<std::pair<QString, QString>> items;
    for (const QString& id : ids) items.emplace_back(nameOf(id), id);
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        const int order = a.first.compare(b.first, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : a.second < b.second;
    });
    QVariantList list;
    for (const auto& [name, id] : items) list.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}});
    return list;
}

}  // namespace

VitalPatchInfo::VitalPatchInfo(QObject* parent) : QObject(parent) {}

void VitalPatchInfo::setSession(sub::app::Session* session) {
    if (session == session_)
        return;
    session_ = session;
    for (const QMetaObject::Connection& connection : std::as_const(connections_))
        disconnect(connection);
    connections_.clear();
    if (session_) {
        Project* project = session_->project();
        auto refreshAll = [this] { refresh(); };
        connections_ << connect(project, &Project::devicesChanged, this, refreshAll);
        connections_ << connect(project, &Project::reset, this, refreshAll);
        connections_ << connect(project, &Project::deviceStateChanged, this,
                                [this](const QString& trackId, const QString& deviceId) {
                                    if (trackId == trackId_ && deviceId == deviceId_)
                                        refresh();
                                });
    }
    Q_EMIT targetChanged();
    refresh();
}

void VitalPatchInfo::setTrackId(const QString& trackId) {
    if (trackId == trackId_)
        return;
    trackId_ = trackId;
    Q_EMIT targetChanged();
    refresh();
}

void VitalPatchInfo::setDeviceId(const QString& deviceId) {
    if (deviceId == deviceId_)
        return;
    deviceId_ = deviceId;
    Q_EMIT targetChanged();
    refresh();
}

QVariantList VitalPatchInfo::sources() const {
    static const QVariantList kSources = named(sub::app::vitalSources(), &sub::app::vitalSourceName);
    return kSources;
}

QVariantList VitalPatchInfo::destinations() const {
    static const QVariantList kDestinations = named(sub::app::vitalDestinations(), &sub::app::vitalDestinationName);
    return kDestinations;
}

void VitalPatchInfo::refresh() {
    QVariantList modulations;
    int free = 0;
    QString presetName;
    QString author;
    QStringList macroNames;
    QStringList wavetableNames;
    QStringList lfoNames;
    const Device* device = session_ && !trackId_.isEmpty() ? session_->project()->findDevice(trackId_, deviceId_) : nullptr;
    if (device != nullptr && sub::app::isVital(*device)) {
        for (const sub::app::VitalModulation& slot : sub::app::vitalModulationSlots(*device)) {
            if (slot.source.isEmpty() || slot.destination.isEmpty()) {
                ++free;
                continue;
            }
            modulations.append(QVariantMap{{QStringLiteral("slot"), slot.slot},
                                           {QStringLiteral("source"), slot.source},
                                           {QStringLiteral("sourceName"), sub::app::vitalSourceName(slot.source)},
                                           {QStringLiteral("destination"), slot.destination},
                                           {QStringLiteral("destinationName"),
                                            sub::app::vitalDestinationName(slot.destination)}});
        }
        presetName = sub::app::vitalStateValue(*device, QStringLiteral("preset_name"));
        author = sub::app::vitalStateValue(*device, QStringLiteral("author"));
        wavetableNames = sub::app::vitalWavetableNames(*device);
        lfoNames = sub::app::vitalLfoNames(*device);
        for (int i = 0; i < 4; ++i) {
            const QString name = sub::app::vitalStateValue(*device, QStringLiteral("macro%1").arg(i + 1)).trimmed();
            macroNames.append(name.isEmpty() || name.startsWith(QStringLiteral("MACRO ")) ? tr("Macro %1").arg(i + 1)
                                                                                         : name);
        }
    }
    if (modulations == modulations_ && free == freeSlots_ && presetName == presetName_ && author == author_ && macroNames == macroNames_ &&
        wavetableNames == wavetableNames_ && lfoNames == lfoNames_)
        return;
    wavetableNames_ = wavetableNames;
    lfoNames_ = lfoNames;
    modulations_ = modulations;
    freeSlots_ = free;
    presetName_ = presetName;
    author_ = author;
    macroNames_ = macroNames;
    Q_EMIT changed();
}

int VitalPatchInfo::add(const QString& source, const QString& destination) {
    if (!session_)
        return -1;
    return session_->editor()->addVitalModulation(trackId_, deviceId_, source, destination);
}

bool VitalPatchInfo::change(int slot, const QString& source, const QString& destination) {
    return session_ && session_->editor()->setVitalModulation(trackId_, deviceId_, slot, source, destination);
}

bool VitalPatchInfo::remove(int slot) { return change(slot, QString(), QString()); }

QStringList VitalPatchInfo::shapeNames() const { return sub::app::vitalShapeNames(); }

QString VitalPatchInfo::loadWavetable(int oscillator, const QString& path, int style) {
    if (!session_)
        return tr("No session");
    try {
        const QString wavetable = sub::app::vitalWavetableFromFile(path, style);
        if (!session_->editor()->setVitalWavetable(trackId_, deviceId_, oscillator, wavetable,
                                                   tr("Load Wavetable %1").arg(QFileInfo(path).completeBaseName())))
            return tr("The wavetable can't go there.");
    } catch (const sub::app::ProjectFileError& error) {
        return error.message();
    }
    return {};
}

bool VitalPatchInfo::setWavetableShape(int oscillator, int shape) {
    return session_ && session_->editor()->setVitalWavetableShape(trackId_, deviceId_, oscillator, shape);
}

bool VitalPatchInfo::setLfoShape(int lfo, int shape) {
    return session_ && session_->editor()->setVitalLfoShape(trackId_, deviceId_, lfo, shape);
}

QString VitalPatchInfo::slotParam(int slot, const QString& what) const { return sub::app::vitalSlotParam(slot, what); }

}  // namespace sub::ui
