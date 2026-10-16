// The engine bridge's start-up and shut-down: its state, the project's signals
// it listens to, its timers and thread pools. Its parts are in the Bridge*.cpp
// files, one an area (tracks, inputs, devices, plug-ins, loading, parameters,
// sources, transport, recording, freezing, reversing, the audio device).

#include "audio/EditorEventLoop.h"
#include "audio/BridgePrivate.h"

#include "model/Project.h"

#include <QDebug>

#include <exception>

namespace sub::app {

void guarded(const char* what, const std::function<void()>& body) {
    try {
        body();
    } catch (const EditError& error) {
        qWarning("EngineBridge (%s): %s", what, qUtf8Printable(error.message()));
    } catch (const std::exception& error) {
        qWarning("EngineBridge (%s): %s", what, error.what());
    }
}

void guardedBatch(sub::Engine& engine, const char* what, const std::function<void()>& body) {
    guarded(what, [&] {
        sub::Engine::Batch batch(engine);
        body();
    });
}

EngineBridge::EngineBridge(sub::Engine& engine, Project* project, QObject* parent)
    : QObject(parent), engine_(engine), project_(project), d_(std::make_unique<Private>()) {
    d_->trackIds.insert(kMaster, sub::Engine::kMaster);
    d_->chains.insert(kMaster, engine_.trackChain(sub::Engine::kMaster));
    d_->chainOwner.insert(kMaster, kMaster);
    if (sub::editorsUseHost()) {  // plug-in editors' windows and run loops (Linux) on Qt's event loop
        d_->editorLoop = std::make_shared<EditorEventLoop>();
        sub::setEditorEventLoop(d_->editorLoop);
    }

    // What changes the engine reaches it in one batch a signal (guardedBatch): one snapshot, however many
    // engine calls the change takes.
    const auto edit = [this](const char* what, const std::function<void()>& body) { guardedBatch(engine_, what, body); };

    d_->pluginTimer.setSingleShot(true);
    connect(&d_->pluginTimer, &QTimer::timeout, this, [edit, this] { edit("loading plug-ins", [&] { loadNextPlugin(); }); });
    d_->pool.setMaxThreadCount(2);
    d_->statePool.setMaxThreadCount(1);

    connect(project, &Project::reset, this, [edit, this] { edit("reset", [&] { onReset(); }); });
    connect(project, &Project::trackInserted, this,
            [edit, this](const QString& trackId, int) { edit("trackInserted", [&] { addEngineTrack(trackId); }); });
    connect(project, &Project::trackRemoved, this,
            [edit, this](const QString& trackId, int) { edit("trackRemoved", [&] { onTrackRemoved(trackId); }); });
    connect(project, &Project::returnInserted, this,
            [edit, this](const QString& trackId, int) { edit("returnInserted", [&] { addEngineTrack(trackId); }); });
    connect(project, &Project::returnRemoved, this,
            [edit, this](const QString& trackId, int) { edit("returnRemoved", [&] { onTrackRemoved(trackId); }); });
    connect(project, &Project::trackChanged, this,
            [edit, this](const QString& trackId) { edit("trackChanged", [&] { onTrackChanged(trackId); }); });
    connect(project, &Project::tracksArranged, this, [edit, this] { edit("tracksArranged", [&] { pushRoutes(); }); });
    connect(project, &Project::clipsChanged, this,
            [edit, this](const QString& trackId) { edit("clipsChanged", [&] { pushClips(trackId); }); });
    connect(project, &Project::devicesChanged, this,
            [edit, this](const QString& trackId) { edit("devicesChanged", [&] { syncDevices(trackId); }); });
    connect(project, &Project::freezeChanged, this,
            [edit, this](const QString& trackId) { edit("freezeChanged", [&] { onFreezeChanged(trackId); }); });
    connect(project, &Project::chainChanged, this, [edit, this](const QString& trackId, const QString& chainId) {
        edit("chainChanged", [&] { onChainChanged(trackId, chainId); });
    });
    connect(project, &Project::deviceParamChanged, this,
            [edit, this](const QString& trackId, const QString& deviceId, const QString& paramId) {
                edit("deviceParamChanged", [&] { onDeviceParamChanged(trackId, deviceId, paramId); });
            });
    connect(project, &Project::deviceStateChanged, this, [edit, this](const QString& trackId, const QString& deviceId) {
        edit("deviceStateChanged", [&] { pushDeviceState(trackId, deviceId); });
    });
    connect(project, &Project::trackChanged, this,
            [this](const QString& trackId) { guarded("trackChanged", [&] { updateEditorTitles(trackId); }); });
    connect(project, &Project::settingsChanged, this, [edit, this] { edit("settingsChanged", [&] { pushSettings(); }); });
    connect(project, &Project::automationChanged, this, [edit, this](const QString& owner, const QString& key) {
        edit("automationChanged", [&] { onAutomationChanged(owner, key); });
    });

    d_->positionTimer.setInterval(kDisplayPollMs);
    d_->positionTimer.setTimerType(Qt::PreciseTimer);
    connect(&d_->positionTimer, &QTimer::timeout, this, [this] { guarded("playhead", [&] { pollPosition(); }); });
    d_->positionTimer.start();
    d_->displayTimer.setInterval(kDisplayPollMs);
    d_->displayTimer.setTimerType(Qt::PreciseTimer);
    connect(&d_->displayTimer, &QTimer::timeout, this, [this] { guarded("meters", [&] { pollDisplays(); }); });
    d_->displayTimer.start();
    d_->idleTimer.setInterval(kIdlePollMs);
    connect(&d_->idleTimer, &QTimer::timeout, this, [this] { guarded("idle", [&] { pollIdle(); }); });
    d_->idleTimer.start();

    const sub::Engine::Batch batch(engine_);
    onReset();
}

EngineBridge::~EngineBridge() {
    d_->positionTimer.stop();
    d_->displayTimer.stop();
    d_->idleTimer.stop();
    d_->pluginTimer.stop();
    // Their results come to this object, which is going: what is posted to it goes with it.
    d_->pool.waitForDone();
    d_->statePool.waitForDone();
    d_->arrivals.reset();  // (files that won't arrive now: the engine publishes what it has)
    // (Editors still open keep it, and unregister through it, until they close.)
    if (d_->editorLoop && sub::editorEventLoop() == d_->editorLoop) sub::setEditorEventLoop(nullptr);
}

void EngineBridge::shutdown() {
    stopLoadingPlugins();
    closeAllEditors();
    d_->statePool.waitForDone();  // (no plug-in is waited for: they all go)
    d_->arrivals.reset();
    removeEngineTracks();
    d_->devices.clear();
    d_->pluginIds.clear();
    for (const std::string& name : engine_.openMidiInputs()) engine_.closeMidiInput(name);
    engine_.idle(true);
}

std::optional<quint32> EngineBridge::engineTrackId(const QString& trackId) const {
    const auto it = d_->trackIds.constFind(trackId);
    if (it == d_->trackIds.constEnd()) return std::nullopt;
    return *it;
}

std::optional<quint32> EngineBridge::engineDeviceId(const QString& trackId, const QString& deviceId) const {
    const auto key = d_->where.constFind(deviceId);
    if (key == d_->where.constEnd() || d_->chainOwner.value(*key) != trackId) return std::nullopt;
    return d_->pids.value(deviceId);
}

std::optional<quint32> EngineBridge::engineChainId(const QString& chainId) const {
    if (!d_->rackOfChain.contains(chainId)) return std::nullopt;
    const auto it = d_->chains.constFind(chainId);
    if (it == d_->chains.constEnd()) return std::nullopt;
    return *it;
}

}  // namespace sub::app
