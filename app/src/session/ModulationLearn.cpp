#include "session/ModulationLearn.h"

#include "editor/ProjectEditor.h"
#include "model/Automation.h"
#include "model/Devices.h"
#include "model/Project.h"

namespace sub::app {

ModulationLearn::ModulationLearn(Project* project, ProjectEditor* editor, QObject* parent)
    : QObject(parent), project_(project), editor_(editor) {
    connect(editor_, &ProjectEditor::parameterTouched, this, &ModulationLearn::touched);
    connect(project_, &Project::devicesChanged, this, &ModulationLearn::followDevices);
    connect(project_, &Project::trackRemoved, this, [this](const QString& trackId) { followDevices(trackId); });
    connect(project_, &Project::reset, this, &ModulationLearn::stop);
}

void ModulationLearn::start(const QString& trackId, const QString& modulatorId) {
    const Device* modulator = project_->findDevice(trackId, modulatorId);
    if (modulator == nullptr || !isModulator(modulator->kind)) return;
    if (trackId == trackId_ && modulatorId == modulatorId_) return;
    trackId_ = trackId;
    modulatorId_ = modulatorId;
    Q_EMIT changed();
    Q_EMIT statusMessage(QStringLiteral("%1: touch a parameter on this track to modulate it (Esc to cancel)")
                             .arg(deviceName(*modulator)));
}

void ModulationLearn::stop() {
    if (!active()) return;
    trackId_.clear();
    modulatorId_.clear();
    Q_EMIT changed();
}

void ModulationLearn::toggle(const QString& trackId, const QString& modulatorId) {
    if (isLearning(modulatorId)) {
        stop();
    } else {
        start(trackId, modulatorId);
    }
}

void ModulationLearn::touched(const QString& owner, const QString& key) {
    if (!active()) return;
    const auto deviceId = automation::keyDevice(key);
    const auto target = automation::parseKey(key);
    if (!deviceId || !target || target->param == automation::kDeviceOn) {
        Q_EMIT statusMessage(QStringLiteral("A modulator moves devices' parameters: touch one of them"));
        return;
    }
    if (*deviceId == modulatorId_) return;  // (adjusting the modulator itself)
    if (owner != trackId_) {
        Q_EMIT statusMessage(QStringLiteral("A modulator moves parameters on its own track"));
        return;
    }
    if (!editor_->canModulate(owner, *deviceId, target->param)) {
        Q_EMIT statusMessage(QStringLiteral("That parameter can't be modulated"));
        return;
    }
    const QString trackId = trackId_, modulatorId = modulatorId_;
    stop();
    if (!editor_->tryMapModulation(trackId, modulatorId, *deviceId, target->param)) return;
    const Device* modulator = project_->findDevice(trackId, modulatorId);
    const Device* device = project_->findDevice(trackId, *deviceId);
    const auto spec = editor_->paramInfo(trackId, *deviceId, target->param);
    if (modulator != nullptr && device != nullptr) {
        Q_EMIT statusMessage(QStringLiteral("%1 modulates %2: %3")
                                 .arg(deviceName(*modulator), deviceName(*device),
                                      spec && !spec->name.isEmpty() ? spec->name : target->param));
    }
}

void ModulationLearn::followDevices(const QString& trackId) {
    if (active() && trackId == trackId_ && project_->findDevice(trackId_, modulatorId_) == nullptr) stop();
}

}  // namespace sub::app
