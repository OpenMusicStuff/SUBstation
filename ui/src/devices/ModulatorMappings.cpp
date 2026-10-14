#include "devices/ModulatorMappings.h"

#include "audio/EngineBridge.h"
#include "editor/ProjectEditor.h"
#include "model/Automation.h"
#include "model/Devices.h"
#include "model/Project.h"
#include "session/ModulationLearn.h"

#include <QVariantMap>

namespace sub::ui {

using sub::app::Project;

ModulatorMappings::ModulatorMappings(QObject* parent) : QObject(parent) {}

void ModulatorMappings::setSession(sub::app::Session* session) {
    if (session == session_)
        return;
    session_ = session;
    connectSession();
    Q_EMIT targetChanged();
    Q_EMIT learningChanged();
    refresh();
}

void ModulatorMappings::setTrackId(const QString& trackId) {
    if (trackId == trackId_)
        return;
    trackId_ = trackId;
    Q_EMIT targetChanged();
    refresh();
}

void ModulatorMappings::setDeviceId(const QString& deviceId) {
    if (deviceId == deviceId_)
        return;
    deviceId_ = deviceId;
    Q_EMIT targetChanged();
    Q_EMIT learningChanged();
    refresh();
}

void ModulatorMappings::connectSession() {
    for (const QMetaObject::Connection& connection : std::as_const(connections_))
        disconnect(connection);
    connections_.clear();
    if (!session_)
        return;
    Project* project = session_->project();
    connections_ << connect(project, &Project::devicesChanged, this, [this](const QString& trackId) {
        if (trackId == trackId_)
            refresh();
    });
    connections_ << connect(project, &Project::reset, this, &ModulatorMappings::refresh);
    connections_ << connect(session_->bridge(), &sub::app::EngineBridge::devicesLoaded, this,
                            [this](const QString& trackId) {
                                if (trackId == trackId_)
                                    refresh();  // (a plug-in's parameters' names, once it loads)
                            });
    connections_ << connect(session_->modulationLearn(), &sub::app::ModulationLearn::changed, this,
                            &ModulatorMappings::learningChanged);
}

void ModulatorMappings::refresh() {
    QVariantList rows;
    const sub::app::Device* modulator =
        session_ && !deviceId_.isEmpty() ? session_->project()->findDevice(trackId_, deviceId_) : nullptr;
    if (modulator != nullptr) {
        for (const sub::app::ModulationMapping& m : modulator->modulations) {
            const sub::app::Device* target = session_->project()->findDevice(trackId_, m.deviceId);
            const auto spec = session_->bridge()->paramSpec(trackId_, sub::app::automation::deviceKey(m.deviceId, m.paramId));
            rows.append(QVariantMap{
                {QStringLiteral("deviceId"), m.deviceId},
                {QStringLiteral("paramId"), m.paramId},
                {QStringLiteral("device"), target ? sub::app::deviceName(*target) : QString()},
                {QStringLiteral("param"), spec && !spec->name.isEmpty() ? spec->name : m.paramId},
                // (The range it plays; its ends as set, maybe past 0..1 from a knob's handle, for where it is now.)
                {QStringLiteral("low"), m.playedLow()},
                {QStringLiteral("high"), m.playedHigh()},
                {QStringLiteral("setLow"), m.low},
                {QStringLiteral("setHigh"), m.high},
                {QStringLiteral("invert"), m.invert},
                {QStringLiteral("lowText"), spec ? spec->formatNormalized(m.playedLow()) : QString()},
                {QStringLiteral("highText"), spec ? spec->formatNormalized(m.playedHigh()) : QString()},
            });
        }
    }
    if (rows == rows_)
        return;
    rows_ = rows;
    Q_EMIT rowsChanged();
}

bool ModulatorMappings::learning() const {
    return session_ && session_->modulationLearn()->isLearning(deviceId_);
}

void ModulatorMappings::toggleLearning() {
    if (session_)
        session_->modulationLearn()->toggle(trackId_, deviceId_);
}

void ModulatorMappings::stopLearning() {
    if (learning())
        session_->modulationLearn()->stop();
}

void ModulatorMappings::setRange(const QString& deviceId, const QString& paramId, double low, double high,
                                 const QString& mergeKey) {
    if (session_)
        session_->editor()->setModulationRange(trackId_, deviceId_, deviceId, paramId, low, high, mergeKey);
}

void ModulatorMappings::setInverted(const QString& deviceId, const QString& paramId, bool inverted) {
    if (session_)
        session_->editor()->setModulationInverted(trackId_, deviceId_, deviceId, paramId, inverted);
}

void ModulatorMappings::remove(const QString& deviceId, const QString& paramId) {
    if (session_)
        session_->editor()->unmapModulation(trackId_, deviceId, paramId);
}

void ModulatorMappings::clear() {
    if (session_)
        session_->editor()->clearModulations(trackId_, deviceId_);
}

QString ModulatorMappings::valueText(const QString& deviceId, const QString& paramId, double normalized) const {
    if (!session_)
        return {};
    const auto spec = session_->bridge()->paramSpec(trackId_, sub::app::automation::deviceKey(deviceId, paramId));
    return spec ? spec->formatNormalized(std::clamp(normalized, 0.0, 1.0)) : QString();
}

}  // namespace sub::ui
