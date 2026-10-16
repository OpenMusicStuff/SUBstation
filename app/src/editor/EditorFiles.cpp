// The files clips, take lanes' clips and devices play: one put in place of
// another (the File Manager's Replace, a hot swap), and files found somewhere
// else (missing ones located).

#include "editor/ProjectEditor.h"

#include "model/Commands.h"
#include "model/Edits.h"

#include <QSet>
#include <QUndoStack>

namespace sub::app {

bool ProjectEditor::replaceFile(const FileUses& uses, const QString& path, double seconds, const QString& text,
                                const QString& mergeKey) {
    const Project& p = *project_;
    if (path.isEmpty() || seconds <= 0.0) return false;
    QMap<QString, QSet<QString>> clipIds;
    for (const ClipRef& ref : uses.clips) {
        if (p.findClip(ref.trackId, ref.clipId)) clipIds[ref.trackId].insert(ref.clipId);
    }
    ClipLists current;
    for (auto it = clipIds.constBegin(); it != clipIds.constEnd(); ++it) {
        current.insert(it.key(), p.track(it.key()).clips);
    }
    // Take lanes' clips: by track, (lane id, clip id).
    QMap<QString, QSet<std::pair<QString, QString>>> takeIds;
    for (const TakeClipRef& ref : uses.takeClips) {
        if (p.findTrack(ref.trackId)) takeIds[ref.trackId].insert({ref.laneId, ref.clipId});
    }
    TakeLists takesCurrent;
    for (auto it = takeIds.constBegin(); it != takeIds.constEnd(); ++it) {
        takesCurrent.insert(it.key(), p.track(it.key()).takes);
    }
    DeviceStates statesCurrent;
    for (const DeviceRef& ref : uses.devices) {
        const Device* device = p.hasOwner(ref.trackId) ? p.findDevice(ref.trackId, ref.deviceId) : nullptr;
        if (device) statesCurrent.insert({ref.trackId, ref.deviceId}, device->state);
    }
    if (current.isEmpty() && takesCurrent.isEmpty() && statesCurrent.isEmpty()) return false;
    // Within one hot swap, from what was there before it began.
    ClipLists baseline = current;
    TakeLists takesBaseline = takesCurrent;
    DeviceStates statesBaseline = statesCurrent;
    const int index = undoStack_->index();
    const auto* last = index > 0 ? dynamic_cast<const ReplaceFilesCommand*>(undoStack_->command(index - 1)) : nullptr;
    if (!mergeKey.isEmpty() && last != nullptr && last->mergeKey() == mergeKey &&
        last->before().keys() == current.keys() && last->takesBefore().keys() == takesCurrent.keys() &&
        last->statesBefore().keys() == statesCurrent.keys()) {
        baseline = last->before();
        takesBaseline = last->takesBefore();
        statesBaseline = last->statesBefore();
    }
    const Tempo& tempo = p.tempoMap();
    ClipLists after;
    for (auto it = baseline.constBegin(); it != baseline.constEnd(); ++it) {
        const QSet<QString> replacing = clipIds.value(it.key());
        std::vector<Clip> clips;
        for (const Clip& clip : it.value()) {
            clips.push_back(replacing.contains(clip.id) ? edits::replaceFile(clip, path, seconds) : clip);
        }
        after.insert(it.key(), edits::fitToTempo(clips, tempo));
    }
    TakeLists takesAfter;
    for (auto it = takesBaseline.constBegin(); it != takesBaseline.constEnd(); ++it) {
        const QSet<std::pair<QString, QString>> replacing = takeIds.value(it.key());
        std::vector<TakeLane> lanes = it.value();
        for (TakeLane& lane : lanes) {
            for (Clip& clip : lane.clips) {
                if (replacing.contains({lane.id, clip.id})) clip = edits::replaceFile(clip, path, seconds);
            }
            lane.clips = edits::fitToTempo(lane.clips, tempo);
        }
        takesAfter.insert(it.key(), std::move(lanes));
    }
    DeviceStates statesAfter;
    for (auto it = statesBaseline.constBegin(); it != statesBaseline.constEnd(); ++it) {
        statesAfter.insert(it.key(), withDeviceFile(it.value(), path));
    }
    if (after == current && takesAfter == takesCurrent && statesAfter == statesCurrent) return false;
    return push(std::make_unique<ReplaceFilesCommand>(project_, text, current, after, statesCurrent, statesAfter,
                                                      takesCurrent, takesAfter, mergeKey));
}

bool ProjectEditor::revertReplaceFiles(const QString& mergeKey) {
    const int index = undoStack_->index();
    const auto* last = index > 0 ? dynamic_cast<const ReplaceFilesCommand*>(undoStack_->command(index - 1)) : nullptr;
    if (mergeKey.isEmpty() || last == nullptr || last->mergeKey() != mergeKey) return false;
    // (Back where it began: the merge leaves the step obsolete, and the stack drops it.)
    return push(std::make_unique<ReplaceFilesCommand>(project_, last->text(), last->after(), last->before(),
                                                      last->statesAfter(), last->statesBefore(), last->takesAfter(),
                                                      last->takesBefore(), mergeKey, last->relink()));
}

bool ProjectEditor::relinkFiles(const QMap<QString, QString>& moved, const QString& text) {
    const Project& p = *project_;
    if (moved.isEmpty()) return false;
    // A clip playing a file that moved, made to play it where it is now: whether it was.
    const auto relink = [&moved](Clip& clip) {
        bool changed = false;
        for (auto it = moved.constBegin(); it != moved.constEnd(); ++it) {
            const Clip relinked = edits::relinkFile(clip, it.key(), it.value());
            if (relinked == clip) continue;
            clip = relinked;
            changed = true;
        }
        return changed;
    };
    ClipLists before;
    ClipLists after;
    TakeLists takesBefore;
    TakeLists takesAfter;
    for (const Track* track : p.allTracks()) {
        if (!track->isAudio()) continue;
        std::vector<Clip> clips = track->clips;
        bool changed = false;
        for (Clip& clip : clips) changed |= relink(clip);
        if (changed) {
            before.insert(track->id, track->clips);
            after.insert(track->id, clips);
        }
        std::vector<TakeLane> lanes = track->takes;
        bool lanesChanged = false;
        for (TakeLane& lane : lanes) {
            for (Clip& clip : lane.clips) lanesChanged |= relink(clip);
        }
        if (lanesChanged) {
            takesBefore.insert(track->id, track->takes);
            takesAfter.insert(track->id, std::move(lanes));
        }
    }
    DeviceStates statesBefore;
    DeviceStates statesAfter;
    for (auto it = moved.constBegin(); it != moved.constEnd(); ++it) {
        for (const DeviceRef& ref : fileUses(p, it.key()).devices) {
            const std::pair<QString, QString> key{ref.trackId, ref.deviceId};
            const std::optional<QString> state = p.device(ref.trackId, ref.deviceId).state;
            if (!statesBefore.contains(key)) statesBefore.insert(key, state);
            statesAfter.insert(key, withDeviceFile(statesAfter.value(key, state), it.value()));
        }
    }
    if (after.isEmpty() && takesAfter.isEmpty() && statesAfter.isEmpty()) return false;
    return push(std::make_unique<ReplaceFilesCommand>(project_, text, before, after, statesBefore, statesAfter,
                                                      takesBefore, takesAfter, QString(), true));
}

}  // namespace sub::app
