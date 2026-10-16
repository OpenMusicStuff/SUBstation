// Freezing: rendering a track's signal before its fader into its frozen audio,
// and a frozen track in the engine (its frozen audio as its clips, its
// devices' processors gone until it is unfrozen). And exporting the
// arrangement, the other render in the background.

#include "audio/AudioFiles.h"
#include "audio/BridgePrivate.h"

#include "AudioSource.h"

#include "model/Devices.h"
#include "model/Paths.h"
#include "model/Project.h"

#include <QDateTime>
#include <QFile>

namespace sub::app {

void EngineBridge::pushFrozen(const QString& trackId) {
    const auto engineId = engineTrackId(trackId);
    if (!engineId || trackId == kMaster) return;
    const bool frozen = project_->track(trackId).frozen.has_value();
    if (frozen == d_->frozen.contains(trackId)) return;
    engine_.setTrackFrozen(*engineId, frozen);
    if (frozen) {
        d_->frozen.insert(trackId);
    } else {
        d_->frozen.remove(trackId);
    }
}

// Frozen: it plays its frozen audio, and its devices go (their states kept);
// unfrozen, they come back.
void EngineBridge::onFreezeChanged(const QString& trackId) {
    const auto engineId = engineTrackId(trackId);
    if (!engineId) return;
    const Track& track = project_->track(trackId);
    const bool frozen = track.frozen.has_value();
    const bool midi = track.isMidi();
    // Frozen, its plug-ins go: their states into the model, to be saved while it is frozen.
    if (frozen) storePluginStates(deviceIdsOfList(track.devices));
    pushFrozen(trackId);
    if (!frozen && midi) engine_.setTrackClips(*engineId, {});  // (its frozen audio: it plays its notes again)
    pushClips(trackId);
    syncDevices(trackId);
    pushRoutes();
}

Freeze EngineBridge::renderFreeze(const QString& trackId) {
    waitForDeviceStates();  // plug-ins and samples still loading
    const std::unique_ptr<FreezeRender> render = startFreeze(trackId);
    const std::optional<Freeze> freeze = finishFreeze(*render);
    if (!freeze) throw EditError(QStringLiteral("The render was cancelled"));
    return *freeze;
}

std::unique_ptr<FreezeRender> EngineBridge::startFreeze(const QStringList& trackIds) {
    const double end = project_->endBeat();
    std::vector<sub::TrackFile> files;
    QStringList paths;
    const QString folder = freezeFolder(*project_);
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString& trackId : trackIds) {
        if (trackIds.count(trackId) > 1) throw EditError(QStringLiteral("A track is frozen once at a time"));
        const Track* track = project_->findTrack(trackId);
        if (track == nullptr) throw EditError(QStringLiteral("That track can't be frozen"));
        const QString name = track->name;
        const auto engineId = engineTrackId(trackId);
        if (!engineId || trackId == kMaster) throw EditError(name + QStringLiteral(" can't be frozen"));
        if (end <= 0) throw EditError(QStringLiteral("There is nothing to freeze yet: the arrangement is empty"));
        if (paths.isEmpty()) {
            if (const auto problem = makeFolder(folder, QStringLiteral("the freeze folder"))) throw EditError(*problem);
        }
        const QString path = takePath(folder, name + QStringLiteral(" Freeze"), now, paths);
        paths.append(path);
        files.push_back({*engineId, path.toStdString()});
    }
    if (files.empty()) throw EditError(QStringLiteral("There is no track to freeze"));
    if (isPlaying()) stop();
    try {
        std::shared_ptr<sub::RenderJob> job = engine_.startTracksRender(files, 0.0, end, kFreezeTailSeconds);
        return std::make_unique<FreezeRender>(trackIds, std::move(job), project_->tempo());
    } catch (const std::exception& error) {
        throw EditError(QString::fromStdString(error.what()));
    }
}

std::optional<OrderedMap<QString, Freeze>> EngineBridge::finishFreezes(FreezeRender& render) {
    if (!render.finish()) return std::nullopt;
    const QStringList paths = render.paths();
    const QList<qint64> written = render.written();
    OrderedMap<QString, Freeze> freezes;
    for (qsizetype i = 0; i < paths.size(); ++i) {
        Freeze freeze;
        freeze.path = paths[i];
        freeze.durationSec = static_cast<double>(written.value(i)) / engine_.sampleRate();
        freeze.tempo = render.tempo();
        freezes.insert(render.trackIds()[i], freeze);
    }
    // Its samples at hand at once, so that each plays as soon as its track is frozen
    // (no gap while it loads): those the render kept, as decoding the file gives
    // them (the window doesn't wait for a decode), else decoded now.
    const std::vector<std::shared_ptr<sub::AudioSource>> rendered = render.sources();
    qsizetype i = 0;
    for (const auto& [trackId, freeze] : freezes) {
        try {
            std::shared_ptr<sub::AudioSource> source = i < static_cast<qsizetype>(rendered.size()) ? rendered[i] : nullptr;
            ++i;
            if (source && source->sampleRate() == static_cast<uint32_t>(engine_.sampleRate())) {
                engine_.cacheSource(source);
            } else {
                source = engine_.loadSource(freeze.path.toStdString());
            }
            d_->sources.insert(pathIdentity(freeze.path), std::move(source));
        } catch (const std::exception& error) {
            for (const auto& [id, made] : freezes) discardFreeze(made);
            throw EditError(QString::fromStdString(error.what()));
        }
        Q_EMIT sourceReady(freeze.path);
    }
    return freezes;
}

std::optional<Freeze> EngineBridge::finishFreeze(FreezeRender& render) {
    const std::optional<OrderedMap<QString, Freeze>> freezes = finishFreezes(render);
    if (!freezes || freezes->isEmpty()) return std::nullopt;
    return freezes->begin()->second;
}

void EngineBridge::discardFreeze(const Freeze& freeze) {
    d_->sources.remove(pathIdentity(freeze.path));
    engine_.releaseUnusedSources();
    // (which also lets go of others no track plays: those are decoded again when asked for)
    for (auto it = d_->sources.begin(); it != d_->sources.end();) {
        it = engine_.cachedSource((*it)->path()) != nullptr ? std::next(it) : d_->sources.erase(it);
    }
    QFile::remove(freeze.path);
}

std::unique_ptr<EngineRender> EngineBridge::startExport(const QString& path, double startBeat, double endBeat,
                                                        int bitDepth) {
    return startExport(path, startBeat, endBeat, AudioExportFormat{false, bitDepth});
}

std::unique_ptr<EngineRender> EngineBridge::startExport(const QString& path, double startBeat, double endBeat,
                                                        const AudioExportFormat& format) {
    const sub::ExportFormat engineFormat{format.mp3 ? sub::ExportFormat::Kind::Mp3 : sub::ExportFormat::Kind::Wav,
                                         format.bitDepth, format.bitrate};
    try {
        return std::make_unique<EngineRender>(engine_.startExport(path.toStdString(), startBeat, endBeat, engineFormat));
    } catch (const std::exception& error) {
        throw EditError(QString::fromStdString(error.what()));
    }
}

std::optional<qint64> EngineBridge::finishExport(EngineRender& render) { return render.finish(); }

}  // namespace sub::app
