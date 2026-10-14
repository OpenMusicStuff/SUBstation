#include "intelligence/SoundSuggestions.h"

#include <QSet>
#include <QStringList>

#include <atomic>
#include <vector>

#include "intelligence/Harmony.h"
#include "intelligence/SoundSimilarity.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/Project.h"

namespace sub::app {

namespace tagging = intelligence::tagging;

SoundSuggestions::SoundSuggestions(Project* project, Harmony* harmony, SoundSimilarity* similarity, QObject* parent)
    : QObject(parent), project_(project), harmony_(harmony), similarity_(similarity),
      fit_(std::make_shared<const tagging::ProjectFit>()) {
    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMs);
    connect(&settle_, &QTimer::timeout, this, &SoundSuggestions::update);
    const auto stale = [this] { invalidate(); };
    if (project) {
        // What it plays: its tracks' clips and devices (a sampler's sample).
        connect(project, &Project::trackInserted, this, stale);
        connect(project, &Project::trackRemoved, this, stale);
        connect(project, &Project::clipsChanged, this, stale);
        connect(project, &Project::devicesChanged, this, stale);
        connect(project, &Project::deviceStateChanged, this, stale);
        connect(project, &Project::reset, this, stale);
    }
    if (harmony) connect(harmony, &Harmony::changed, this, stale);
    if (similarity) connect(similarity, &SoundSimilarity::tagsChanged, this, stale);
    invalidate();
}

void SoundSuggestions::invalidate() { settle_.start(); }

void SoundSuggestions::update() {
    settle_.stop();
    tags_ = similarity_ ? similarity_->tags() : nullptr;
    // The project's sounds, each file once: its audio clips' (the file a
    // reversed clip came from), then its samplers' samples.
    std::vector<tagging::SoundTags> sounds;
    if (project_ && tags_ && similarity_) {
        QSet<QString> seen;
        const auto add = [&](const QString& path) {
            if (path.isEmpty() || seen.contains(path)) return;
            seen.insert(path);
            if (const auto t = similarity_->tagsOf(path)) sounds.push_back(*t);
        };
        for (const Track& track : project_->tracks()) {
            for (const Clip& clip : track.clips)
                if (clip.isAudio()) add(clip.reversedFrom.isEmpty() ? clip.path : clip.reversedFrom);
            for (const Device* device : iterDevices(track.devices))
                if (device->kind == QStringLiteral("sampler"))
                    add(deviceState::fromModel(device->state).value(QStringLiteral("sample")));
        }
    }
    std::optional<tagging::ProjectKey> key;
    if (harmony_)
        if (const auto k = harmony_->key()) key = tagging::ProjectKey{k->tonic, k->minor};
    fit_ = std::make_shared<const tagging::ProjectFit>(sounds, key);
    fitFunction_ = makeFit();
    QStringList parts;
    QStringList words;
    for (const std::string& w : fit_->words()) words << QString::fromStdString(w);
    if (!words.isEmpty()) parts << words.join(QStringLiteral(", "));
    if (harmony_ && key) parts << harmony_->keyLabel();
    const QString summary = parts.join(QStringLiteral(" · "));
    summary_ = summary;
    Q_EMIT changed();
}

std::function<float(uint64_t)> SoundSuggestions::fit() const { return fitFunction_; }

std::function<float(uint64_t)> SoundSuggestions::makeFit() const {
    if (!tags_ || fit_->empty()) return {};
    // Each file's fit, worked out the first time a search asks for it and kept
    // while the tags and the project stay the same (a list of every sample is
    // searched again and again: as you type, as files come).
    auto known = std::make_shared<std::vector<std::atomic<float>>>(tags_->size());
    for (auto& value : *known) value.store(-1.f, std::memory_order_relaxed);
    return [fit = fit_, tags = tags_, known](uint64_t hash) {
        const tagging::SoundTags* t = tags->find(hash);
        if (!t) return 0.5f;
        std::atomic<float>& slot = (*known)[tags->indexOf(t)];
        float value = slot.load(std::memory_order_relaxed);
        if (value < 0.f) {
            value = fit->fit(*t);
            slot.store(value, std::memory_order_relaxed);
        }
        return value;
    };
}

}  // namespace sub::app
