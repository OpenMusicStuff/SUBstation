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
    QStringList parts;
    QStringList words;
    for (const std::string& w : fit_->words()) words << QString::fromStdString(w);
    if (!words.isEmpty()) parts << words.join(QStringLiteral(", "));
    if (harmony_ && key) parts << harmony_->keyLabel();
    const QString summary = parts.join(QStringLiteral(" · "));
    summary_ = summary;
    Q_EMIT changed();
}

}  // namespace sub::app
