#include "session/GrooveLibrary.h"

#include "editor/EditorSupport.h"
#include "editor/ProjectEditor.h"
#include "io/GrooveFiles.h"
#include "io/Presets.h"
#include "model/DeviceState.h"
#include "model/Errors.h"
#include "model/Grooves.h"
#include "model/Project.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace sub::app {

namespace {

// The steps' choices (groove::kSteps), by index (held to them).
double stepBeatsAt(int index) {
    const int last = static_cast<int>(groove::kSteps.size()) - 1;
    return groove::kSteps[static_cast<size_t>(std::clamp(index, 0, last))].beats;
}

int stepIndexOf(double beats) {
    for (size_t i = 0; i < groove::kSteps.size(); ++i) {
        if (std::abs(groove::kSteps[i].beats - beats) < 1e-9) return static_cast<int>(i);
    }
    return -1;
}

std::vector<int> keysOf(const QVariantList& keys) {
    std::vector<int> out;
    for (const QVariant& key : keys) {
        bool ok = false;
        const int k = key.toInt(&ok);
        if (ok && k >= 0 && k <= 127) out.push_back(k);
    }
    return out;
}

QVariantMap sourceMap(const GrooveSource& source, const std::vector<LibraryGroove>& library) {
    QVariantMap map{{QStringLiteral("kind"), grooves::kindName(source.kind)},
                    {QStringLiteral("grooveId"), source.libraryId},
                    {QStringLiteral("name"), grooves::describe(source, library)}};
    return map;
}

}  // namespace

QString grooveFilesDir(const QString& root) {
    return (root.isEmpty() ? libraryDir() : root) + QStringLiteral("/Grooves");
}

QVariantMap grooveToMap(const groove::Groove& groove, const std::vector<groove::NoteTime>& notes) {
    QVariantList lanes;
    for (const groove::Lane& lane : groove.lanes) {
        QVariantList keys;
        for (const int key : lane.keys) keys.append(key);
        QVariantList steps;
        for (const groove::Step& step : lane.steps) {
            steps.append(QVariantMap{{QStringLiteral("timing"), step.timing},
                                     {QStringLiteral("ms"), step.timingMs},
                                     {QStringLiteral("velocity"), step.velocity},
                                     {QStringLiteral("length"), step.length}});
        }
        lanes.append(QVariantMap{{QStringLiteral("keys"), keys}, {QStringLiteral("steps"), steps}});
    }
    QVariantList heard;
    for (const groove::NoteTime& note : notes) {
        heard.append(QVariantMap{{QStringLiteral("beat"), note.beat},
                                 {QStringLiteral("key"), note.key},
                                 {QStringLiteral("velocity"), note.velocity}});
    }
    return {{QStringLiteral("step"), groove.stepBeats},
            {QStringLiteral("stepIndex"), stepIndexOf(groove.stepBeats)},
            {QStringLiteral("bars"), groove.bars},
            {QStringLiteral("base"), groove.base},
            {QStringLiteral("steps"), groove::stepsPerCycle(groove)},
            {QStringLiteral("lanes"), lanes},
            {QStringLiteral("notes"), heard}};
}

GrooveLibrary::GrooveLibrary(Project* project, ProjectEditor* editor, QObject* parent, const QString& root)
    : QObject(parent), project_(project), editor_(editor), root_(root) {
    connect(project_, &Project::groovesChanged, this, &GrooveLibrary::groovesChanged);
    connect(project_, &Project::defaultGrooveChanged, this, &GrooveLibrary::defaultChanged);
    // (A groove of the library renamed or removed: how the default shows.)
    connect(project_, &Project::groovesChanged, this, &GrooveLibrary::defaultChanged);
    connect(project_, &Project::devicesChanged, this, &GrooveLibrary::bump);
    connect(project_, &Project::clipsChanged, this, &GrooveLibrary::bump);
    connect(project_, &Project::deviceStateChanged, this, &GrooveLibrary::bump);
    connect(project_, &Project::deviceParamChanged, this, &GrooveLibrary::bump);
    connect(project_, &Project::groovesChanged, this, &GrooveLibrary::bump);
    connect(project_, &Project::defaultGrooveChanged, this, &GrooveLibrary::bump);
    connect(project_, &Project::reset, this, &GrooveLibrary::bump);
    refreshFiles();
}

void GrooveLibrary::bump() {
    ++revision_;
    Q_EMIT revisionChanged();
}

bool GrooveLibrary::tried(const std::function<void()>& change) {
    try {
        change();
        return true;
    } catch (const EditError& error) {
        Q_EMIT statusMessage(error.message());
    }
    return false;
}

QVariantList GrooveLibrary::grooves() const {
    QVariantList list;
    for (const LibraryGroove& g : project_->grooves()) {
        list.append(QVariantMap{{QStringLiteral("id"), g.id}, {QStringLiteral("name"), g.name}});
    }
    return list;
}

QVariantList GrooveLibrary::builtins() {
    QVariantList list;
    for (const LibraryGroove& g : grooves::builtins()) {
        list.append(QVariantMap{{QStringLiteral("id"), g.id}, {QStringLiteral("name"), g.name}});
    }
    return list;
}

QStringList GrooveLibrary::stepNames() {
    QStringList names;
    for (const groove::StepChoice& step : groove::kSteps) names.append(QString::fromLatin1(step.name));
    return names;
}

QString GrooveLibrary::defaultKind() const {
    const std::optional<GrooveSource>& chosen = project_->defaultGroove();
    return chosen ? grooves::kindName(chosen->kind) : QString();
}

double GrooveLibrary::defaultSwing() const {
    const std::optional<GrooveSource>& chosen = project_->defaultGroove();
    return chosen && chosen->kind == GrooveSource::Kind::Swing ? chosen->swing : groove::kMinSwing;
}

QString GrooveLibrary::defaultGrooveId() const {
    const std::optional<GrooveSource>& chosen = project_->defaultGroove();
    return chosen && chosen->kind == GrooveSource::Kind::Library ? chosen->libraryId : QString();
}

QString GrooveLibrary::defaultName() const {
    const std::optional<GrooveSource>& chosen = project_->defaultGroove();
    return chosen ? grooves::describe(*chosen, project_->grooves()) : QStringLiteral("None");
}

QVariantMap GrooveLibrary::groove(const QString& grooveId) const {
    const LibraryGroove* found = project_->findGroove(grooveId);
    if (!found) return {};
    QVariantMap map = grooveToMap(groove::normalized(found->groove), found->notes);
    map.insert(QStringLiteral("id"), found->id);
    map.insert(QStringLiteral("name"), found->name);
    return map;
}

QVariantMap GrooveLibrary::builtinGroove(const QString& builtinId) {
    const LibraryGroove* found = grooves::builtin(builtinId);
    if (!found) return {};
    QVariantMap map = grooveToMap(found->groove);
    map.insert(QStringLiteral("id"), found->id);
    map.insert(QStringLiteral("name"), found->name);
    return map;
}

QVariantMap GrooveLibrary::swingGroove(double percent, int stepIndex) {
    QVariantMap map = grooveToMap(groove::swing(percent, stepBeatsAt(stepIndex)));
    map.insert(QStringLiteral("name"), grooves::describe(grooves::swingSource(percent, stepBeatsAt(stepIndex)), {}));
    return map;
}

QVariantMap GrooveLibrary::deviceGroove(const QString& trackId, const QString& deviceId) const {
    const Device* device = project_->findDevice(trackId, deviceId);
    if (!device || device->kind != grooves::kDeviceKind) return {};
    GrooveSource source = grooves::deviceSource(device->state);
    QVariantMap map = sourceMap(source, project_->grooves());
    std::optional<groove::Groove> plays;
    if (source.kind == GrooveSource::Kind::Swing) {
        const int step = static_cast<int>(std::lround(device->params.value(QStringLiteral("step"), groove::kDefaultStep)));
        plays = groove::swing(device->params.value(QStringLiteral("swing"), groove::kMinSwing), stepBeatsAt(step));
    } else if (source.kind == GrooveSource::Kind::Library || source.kind == GrooveSource::Kind::Own) {
        // What it plays: the copy in its state.
        plays = groove::fromText(deviceState::fromModel(device->state).value(QStringLiteral("groove")).toStdString());
        if (source.kind == GrooveSource::Kind::Own) map.insert(QStringLiteral("name"), QStringLiteral("Its own groove"));
    } else {
        plays = grooves::resolve(source, project_->grooves(), project_->defaultGroove());
    }
    map.insert(QStringLiteral("groove"), plays ? grooveToMap(*plays) : QVariantMap());
    return map;
}

QVariantMap GrooveLibrary::clipGroove(const QString& trackId, const QString& clipId) const {
    const Clip* clip = project_->findClip(trackId, clipId);
    if (!clip || !clip->groove) return {{QStringLiteral("kind"), QString()}};
    const ClipGroove& setting = *clip->groove;
    QVariantMap map = sourceMap(setting.source, project_->grooves());
    map.insert(QStringLiteral("swing"), setting.source.swing);
    map.insert(QStringLiteral("stepIndex"), groove::stepIndex(setting.source.stepBeats));
    map.insert(QStringLiteral("timing"), setting.amounts.timing);
    map.insert(QStringLiteral("velocity"), setting.amounts.velocity);
    map.insert(QStringLiteral("random"), setting.amounts.random);
    map.insert(QStringLiteral("quantize"), setting.amounts.quantize);
    const std::optional<groove::Groove> plays =
        grooves::resolve(setting.source, project_->grooves(), project_->defaultGroove());
    map.insert(QStringLiteral("groove"), plays ? grooveToMap(*plays) : QVariantMap());
    return map;
}

QString GrooveLibrary::find(const QString& idOrName) const {
    for (const LibraryGroove& g : project_->grooves()) {
        if (g.id == idOrName) return g.id;
    }
    for (const LibraryGroove& g : project_->grooves()) {
        if (g.name.compare(idOrName, Qt::CaseInsensitive) == 0) return g.id;
    }
    return {};
}

void GrooveLibrary::setDeviceSource(const QString& trackId, const QString& deviceId, const QString& kind,
                                    const QString& grooveId) {
    tried([&] { editor_->setDeviceGrooveSource(trackId, deviceId, kind, grooveId); });
}

bool GrooveLibrary::applyDevice(const QString& trackId, const QString& deviceId) {
    bool applied = false;
    tried([&] { applied = editor_->applyDeviceGroove(trackId, deviceId); });
    if (!applied) Q_EMIT statusMessage(QStringLiteral("The Groove device has no notes to move on its track"));
    return applied;
}

QString GrooveLibrary::addBuiltin(const QString& builtinId) { return editor_->addBuiltinGroove(builtinId); }

QString GrooveLibrary::addFile(const QString& path) {
    try {
        const LibraryGroove read = loadGrooveFile(path);
        return editor_->addGroove(read.name, read.groove, read.notes);
    } catch (const ProjectFileError& error) {
        Q_EMIT statusMessage(error.message());
        return {};
    }
}

QString GrooveLibrary::addSwing(double percent, int stepIndex) {
    const GrooveSource source = grooves::swingSource(percent, stepBeatsAt(stepIndex));
    return editor_->addGroove(grooves::describe(source, {}), groove::swing(source.swing, source.stepBeats));
}

QString GrooveLibrary::extract(const QString& trackId, const QString& clipId, int stepIndex, int bars,
                               bool perKeyLanes) {
    const QString id = editor_->extractGroove(trackId, clipId, stepBeatsAt(stepIndex), bars, perKeyLanes);
    if (id.isEmpty()) {
        Q_EMIT statusMessage(QStringLiteral("Nothing to take a groove from there (no notes; or the clip's audio "
                                            "isn't decoded yet, or has no transients)"));
    }
    return id;
}

QString GrooveLibrary::addFromDevice(const QString& trackId, const QString& deviceId) {
    const QVariantMap plays = deviceGroove(trackId, deviceId);
    const Device* device = project_->findDevice(trackId, deviceId);
    if (!device || plays.value(QStringLiteral("groove")).toMap().isEmpty()) return {};
    std::optional<groove::Groove> made;
    const GrooveSource source = grooves::deviceSource(device->state);
    if (source.kind == GrooveSource::Kind::Swing) {
        const int step = static_cast<int>(std::lround(device->params.value(QStringLiteral("step"), groove::kDefaultStep)));
        made = groove::swing(device->params.value(QStringLiteral("swing"), groove::kMinSwing), stepBeatsAt(step));
    } else {
        made = groove::fromText(deviceState::fromModel(device->state).value(QStringLiteral("groove")).toStdString());
        if (!made) made = grooves::resolve(source, project_->grooves(), project_->defaultGroove());
    }
    if (!made) return {};
    const editing::Macro macro(editor_->undoStack(), QStringLiteral("Add to Grooves"));
    const QString id = editor_->addGroove(QStringLiteral("%1 Groove").arg(project_->track(trackId).name), *made);
    editor_->setDeviceGrooveSource(trackId, deviceId, QStringLiteral("library"), id);
    return id;
}

QString GrooveLibrary::duplicate(const QString& grooveId) { return editor_->duplicateGroove(grooveId); }

void GrooveLibrary::rename(const QString& grooveId, const QString& name) { editor_->renameGroove(grooveId, name); }

void GrooveLibrary::remove(const QString& grooveId) { editor_->removeGroove(grooveId); }

QString GrooveLibrary::saveFile(const QString& grooveId) {
    const LibraryGroove* found = project_->findGroove(grooveId);
    if (!found) return {};
    try {
        const QString dir = grooveFilesDir(root_);
        QDir().mkpath(dir);
        const QString path = dir + u'/' + presetFileName(found->name) + kGrooveExtension;
        saveGrooveFile(*found, path);
        refreshFiles();
        Q_EMIT statusMessage(QStringLiteral("Saved %1 to the library's Grooves").arg(found->name));
        return path;
    } catch (const ProjectFileError& error) {
        Q_EMIT statusMessage(error.message());
    } catch (const EditError& error) {
        Q_EMIT statusMessage(error.message());
    }
    return {};
}

void GrooveLibrary::refreshFiles() {
    QVariantList listed;
    const QDir dir(grooveFilesDir(root_));
    QFileInfoList entries = dir.entryInfoList({QStringLiteral("*") + kGrooveExtension, QStringLiteral("*") + kAgrExtension},
                                              QDir::Files);
    std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.completeBaseName().toCaseFolded() < b.completeBaseName().toCaseFolded();
    });
    for (const QFileInfo& entry : entries) {
        listed.append(QVariantMap{{QStringLiteral("path"), entry.filePath()},
                                  {QStringLiteral("name"), entry.completeBaseName()}});
    }
    if (listed == files_) return;
    files_ = listed;
    Q_EMIT filesChanged();
}

void GrooveLibrary::edit(const QString& grooveId, const std::function<void(groove::Groove&)>& change,
                         const QString& mergeKey) {
    const LibraryGroove* found = project_->findGroove(grooveId);
    if (!found) return;
    groove::Groove changed = groove::normalized(found->groove);
    change(changed);
    editor_->setGroove(grooveId, changed, mergeKey);
}

void GrooveLibrary::setStep(const QString& grooveId, int lane, int step, const QString& field, double value,
                            const QString& mergeKey) {
    edit(
        grooveId,
        [&](groove::Groove& g) {
            if (lane < 0 || lane >= static_cast<int>(g.lanes.size())) return;
            std::vector<groove::Step>& steps = g.lanes[static_cast<size_t>(lane)].steps;
            if (step < 0 || step >= static_cast<int>(steps.size())) return;
            groove::Step& s = steps[static_cast<size_t>(step)];
            if (field == u"timing") {
                s.timing = value;
            } else if (field == u"ms") {
                s.timingMs = value;
            } else if (field == u"velocity") {
                s.velocity = value;
            } else if (field == u"length") {
                s.length = value;
            }
        },
        mergeKey);
}

void GrooveLibrary::addLane(const QString& grooveId, const QVariantList& keys) {
    const std::vector<int> wanted = keysOf(keys);
    if (wanted.empty()) return;
    edit(grooveId, [&](groove::Groove& g) {
        groove::Lane lane{wanted, g.lanes.front().steps};
        g.lanes.push_back(std::move(lane));
    });
}

void GrooveLibrary::setLaneKeys(const QString& grooveId, int lane, const QVariantList& keys) {
    const std::vector<int> wanted = keysOf(keys);
    if (wanted.empty() || lane < 1) return;
    edit(grooveId, [&](groove::Groove& g) {
        if (lane >= static_cast<int>(g.lanes.size())) return;
        // (Taken from the other lanes first: a key is one lane's.)
        for (groove::Lane& other : g.lanes) {
            std::erase_if(other.keys, [&](int k) { return std::find(wanted.begin(), wanted.end(), k) != wanted.end(); });
        }
        g.lanes[static_cast<size_t>(lane)].keys = wanted;
    });
}

void GrooveLibrary::removeLane(const QString& grooveId, int lane) {
    if (lane < 1) return;  // (the default lane stays)
    edit(grooveId, [&](groove::Groove& g) {
        if (lane < static_cast<int>(g.lanes.size())) g.lanes.erase(g.lanes.begin() + lane);
    });
}

void GrooveLibrary::resetLane(const QString& grooveId, int lane) {
    edit(grooveId, [&](groove::Groove& g) {
        if (lane < 0 || lane >= static_cast<int>(g.lanes.size())) return;
        for (groove::Step& step : g.lanes[static_cast<size_t>(lane)].steps) step = groove::Step{};
    });
}

void GrooveLibrary::setBars(const QString& grooveId, int bars) {
    edit(grooveId, [&](groove::Groove& g) {
        const size_t had = g.lanes.front().steps.size();
        groove::Groove resized = g;
        resized.bars = bars;
        resized = groove::normalized(std::move(resized));
        // The steps there were, repeated (or cut).
        for (size_t l = 0; l < resized.lanes.size() && l < g.lanes.size(); ++l) {
            std::vector<groove::Step>& steps = resized.lanes[l].steps;
            for (size_t i = 0; i < steps.size(); ++i) steps[i] = g.lanes[l].steps[i % std::max<size_t>(1, had)];
        }
        g = std::move(resized);
    });
}

void GrooveLibrary::setBase(const QString& grooveId, double baseBeats) {
    if (!(baseBeats > 0.0)) return;
    edit(grooveId, [&](groove::Groove& g) { g.base = baseBeats; });
}

void GrooveLibrary::setDefaultSwing(double percent, const QString& mergeKey) { editor_->setDefaultSwing(percent, mergeKey); }

void GrooveLibrary::setDefaultGroove(const QString& grooveId) { editor_->setDefaultGrooveId(grooveId); }

void GrooveLibrary::clearDefault() { editor_->setDefaultGroove(std::nullopt); }

}  // namespace sub::app
