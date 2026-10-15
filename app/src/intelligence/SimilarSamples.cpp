#include "intelligence/SimilarSamples.h"

#include <QDir>
#include <QFileInfo>

#include "editor/ProjectEditor.h"
#include "intelligence/SoundSimilarity.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/Project.h"
#include "model/SamplerState.h"

namespace sub::app {

SimilarSamples::SimilarSamples(Project* project, ProjectEditor* editor, SoundSimilarity* similarity, QObject* parent)
    : QObject(parent), project_(project), editor_(editor), similarity_(similarity) {
    if (similarity_) connect(similarity_, &SoundSimilarity::found, this, &SimilarSamples::onFound);
    // (Its devices gone, a request still runs: run() skips what isn't there.)
}

QString SimilarSamples::key(const QString& path) {
    QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    cleaned = cleaned.toLower();
#endif
    return cleaned;
}

QString SimilarSamples::sampleOf(const QString& trackId, const QString& deviceId) const {
    if (!project_->hasDevice(trackId, deviceId)) return {};
    return samplerState::sample(deviceState::fromModel(project_->device(trackId, deviceId).state));
}

QString SimilarSamples::referenceOf(const QString& trackId, const QString& deviceId) const {
    if (!project_->hasDevice(trackId, deviceId)) return {};
    const deviceState::Values values = deviceState::fromModel(project_->device(trackId, deviceId).state);
    const QString reference = samplerState::reference(values);
    return reference.isEmpty() ? samplerState::sample(values) : reference;
}

QStringList SimilarSamples::rackSamplers(const QString& trackId, const QString& rackId) const {
    QStringList samplers;
    if (!project_->hasDevice(trackId, rackId)) return samplers;
    const Device& rack = project_->device(trackId, rackId);
    if (!rack.isDrumRack()) return samplers;
    for (const Chain& pad : rack.chains) {
        const Device* sampler = padSampler(pad);
        if (pad.locked || sampler == nullptr || sampleOf(trackId, sampler->id).isEmpty()) continue;
        samplers.append(sampler->id);
    }
    return samplers;
}

void SimilarSamples::step(const QString& trackId, const QString& deviceId, int direction) {
    if (sampleOf(trackId, deviceId).isEmpty()) {
        Q_EMIT statusMessage(QStringLiteral("Load a sample first: its similar sounds come from it."));
        return;
    }
    enqueue({trackId, {deviceId}, direction, QStringLiteral("Swap Similar Sample")});
}

void SimilarSamples::stepRack(const QString& trackId, const QString& rackId, int direction) {
    const QStringList samplers = rackSamplers(trackId, rackId);
    if (samplers.isEmpty()) {
        Q_EMIT statusMessage(QStringLiteral("No pad to swap: every pad with a sample is locked, or none has one."));
        return;
    }
    enqueue({trackId, samplers, direction, QStringLiteral("Swap Similar Samples")});
}

void SimilarSamples::saveReference(const QString& trackId, const QString& deviceId) {
    editor_->setSimilarReference(trackId, deviceId);
}

void SimilarSamples::returnToReference(const QString& trackId, const QString& deviceId) {
    if (!canReturn(trackId, deviceId)) return;
    editor_->swapSamples(trackId, {{deviceId, referenceOf(trackId, deviceId)}}, QStringLiteral("Return to Reference"));
}

void SimilarSamples::saveRackReferences(const QString& trackId, const QString& rackId) {
    const QStringList samplers = rackSamplers(trackId, rackId);
    for (const QString& sampler : samplers) editor_->setSimilarReference(trackId, sampler);
}

void SimilarSamples::returnRackToReferences(const QString& trackId, const QString& rackId) {
    QList<QPair<QString, QString>> back;
    for (const QString& sampler : rackSamplers(trackId, rackId)) {
        if (canReturn(trackId, sampler)) back.append({sampler, referenceOf(trackId, sampler)});
    }
    editor_->swapSamples(trackId, back, QStringLiteral("Return to References"));
}

bool SimilarSamples::canReturn(const QString& trackId, const QString& deviceId) const {
    const QString sample = sampleOf(trackId, deviceId);
    const QString reference = referenceOf(trackId, deviceId);
    return !sample.isEmpty() && !reference.isEmpty() && key(sample) != key(reference);
}

bool SimilarSamples::rackCanReturn(const QString& trackId, const QString& rackId) const {
    const QStringList samplers = rackSamplers(trackId, rackId);
    return std::any_of(samplers.begin(), samplers.end(),
                       [&](const QString& sampler) { return canReturn(trackId, sampler); });
}

void SimilarSamples::enqueue(Request request) {
    if (!similarity_) {
        Q_EMIT statusMessage(QStringLiteral("Similar sounds aren't available."));
        return;
    }
    requests_.push_back(std::move(request));
    pump();
}

void SimilarSamples::pump() {
    if (searching_ || !similarity_) return;
    // What the index found before, while it has analysed no more files since.
    if (similarity_->analysedFiles() != foundWhenAnalysed_) {
        found_.clear();
        foundWhenAnalysed_ = similarity_->analysedFiles();
    }
    while (!requests_.empty()) {
        const Request& request = requests_.front();
        for (const QString& device : request.devices) {
            const QString reference = referenceOf(request.trackId, device);
            if (reference.isEmpty() || found_.contains(key(reference))) continue;
            searching_ = Search{reference, similarity_->find(reference), 1};
            Q_EMIT searchingChanged();
            return;
        }
        const Request ready = request;
        requests_.pop_front();
        run(ready);
    }
}

void SimilarSamples::onFound(const SimilarSounds& result) {
    if (!searching_) return;
    if (result.generation() != searching_->generation) {
        // Another search took this one's place (the browser's): ask again, but not forever.
        if (result.generation() < searching_->generation) return;  // (an older one's, handed out late)
        if (searching_->asks >= kRetries) {
            found_.insert(key(searching_->path), {});
        } else {
            searching_->generation = similarity_->find(searching_->path);
            ++searching_->asks;
            return;
        }
    } else {
        QStringList best;
        for (const auto& [path, similarity] : result.best(kCount)) best.append(QDir::cleanPath(path));
        found_.insert(key(searching_->path), best);
        if (!result.error().isEmpty()) {
            Q_EMIT statusMessage(QStringLiteral("%1 couldn't be analysed: %2")
                                     .arg(QFileInfo(searching_->path).fileName(), result.error()));
        }
    }
    searching_.reset();
    Q_EMIT searchingChanged();
    pump();
}

void SimilarSamples::run(const Request& request) {
    QList<QPair<QString, QString>> swaps;
    bool anyFound = false;
    for (const QString& device : request.devices) {
        const QString sample = sampleOf(request.trackId, device);
        const QString reference = referenceOf(request.trackId, device);
        if (sample.isEmpty() || reference.isEmpty()) continue;
        const QStringList best = found_.value(key(reference));
        anyFound = anyFound || !best.isEmpty();
        const QString next = samplerState::step(best, reference, sample, request.direction);
        if (!next.isEmpty() && key(next) != key(sample)) swaps.append({device, next});
    }
    if (swaps.isEmpty()) {
        if (!anyFound) {
            Q_EMIT statusMessage(similarity_ && similarity_->analysing()
                                     ? QStringLiteral("No similar sounds yet: the browser's files are still being analysed.")
                                     : QStringLiteral("No similar sounds: add folders of samples to the browser's places."));
        } else {
            Q_EMIT statusMessage(request.direction > 0 ? QStringLiteral("No more similar sounds.")
                                                       : QStringLiteral("Back at the reference."));
        }
        return;
    }
    editor_->swapSamples(request.trackId, swaps, request.text);
    if (swaps.size() == 1) {
        Q_EMIT statusMessage(QStringLiteral("Swapped in %1").arg(QFileInfo(swaps.front().second).fileName()));
    } else {
        Q_EMIT statusMessage(QStringLiteral("Swapped in %1 similar sounds").arg(swaps.size()));
    }
}

}  // namespace sub::app
