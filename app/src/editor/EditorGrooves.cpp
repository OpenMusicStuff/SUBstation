// Editing grooves (model/Grooves.h): the project's groove library, clips'
// grooves, Apply Groove, the Groove device's source and the default groove.

#include "editor/EditorSupport.h"
#include "editor/ProjectEditor.h"

#include "model/Commands.h"
#include "model/DeviceState.h"
#include "model/Edits.h"
#include "model/Errors.h"
#include "model/Grooves.h"
#include "model/Ids.h"
#include "model/Notes.h"

#include <QUndoStack>

#include <algorithm>
#include <cmath>

namespace sub::app {

namespace {

// Every Groove device in the project: (track id, device).
std::vector<std::pair<QString, const Device*>> grooveDevices(const Project& p) {
    std::vector<std::pair<QString, const Device*>> found;
    for (const Track* track : p.allTracks()) {
        for (const Device* device : iterDevices(track->devices)) {
            if (device->kind == grooves::kDeviceKind) found.emplace_back(track->id, device);
        }
    }
    return found;
}

// The groove a Groove device plays, and at what (its parameters), in this project; none: none.
std::optional<std::pair<groove::Groove, groove::Amounts>> devicePlays(const Project& p, const Device& device) {
    GrooveSource source = grooves::deviceSource(device.state);
    if (source.kind == GrooveSource::Kind::Swing) {
        const int step = std::clamp(static_cast<int>(std::lround(device.params.value(QStringLiteral("step"),
                                                                                        groove::kDefaultStep))),
                                    0, static_cast<int>(groove::kSteps.size()) - 1);
        source = grooves::swingSource(device.params.value(QStringLiteral("swing"), groove::kMinSwing),
                                      groove::kSteps[static_cast<size_t>(step)].beats);
    } else if (source.kind == GrooveSource::Kind::Library) {
        // (What it plays: the copy in its state, as the library's is.)
        const deviceState::Values values = deviceState::fromModel(device.state);
        if (auto copy = groove::fromText(values.value(QStringLiteral("groove")).toStdString())) {
            source.kind = GrooveSource::Kind::Own;
            source.own = std::move(copy);
        }
    }
    const std::optional<groove::Groove> plays = grooves::resolve(source, p.grooves(), p.defaultGroove());
    if (!plays) return std::nullopt;
    groove::Amounts amounts;
    amounts.timing = device.params.value(QStringLiteral("timing"), 100.0) / 100.0;
    amounts.velocity = device.params.value(QStringLiteral("velocity"), 100.0) / 100.0;
    amounts.length = device.params.value(QStringLiteral("length"), 0.0) / 100.0;
    amounts.random = device.params.value(QStringLiteral("random"), 0.0) / 100.0;
    amounts.quantize = device.params.value(QStringLiteral("quantize"), 0.0) / 100.0;
    amounts.seed = grooves::deviceSeed(device.state);
    return std::pair{*plays, amounts};
}

}  // namespace

void ProjectEditor::setTransients(TransientSource transients) { transients_ = std::move(transients); }

std::vector<std::pair<double, float>> ProjectEditor::transientsOf(const QString& path) const {
    return transients_ ? transients_(path) : std::vector<std::pair<double, float>>{};
}

QString ProjectEditor::uniqueGrooveName(const QString& base) const {
    const QString stem = base.trimmed().isEmpty() ? QStringLiteral("Groove") : base.trimmed();
    const auto taken = [this](const QString& name) {
        const auto& all = project_->grooves();
        return std::any_of(all.begin(), all.end(), [&](const LibraryGroove& g) { return g.name == name; });
    };
    if (!taken(stem)) return stem;
    for (int n = 2;; ++n) {
        const QString name = QStringLiteral("%1 %2").arg(stem).arg(n);
        if (!taken(name)) return name;
    }
}

void ProjectEditor::commitGrooves(std::vector<LibraryGroove> after, const QString& text, const QString& grooveId,
                                  const QString& mergeKey, const QString& detach) {
    const Project& p = *project_;
    GroovesState before{p.grooves(), {}, {}, {}, std::nullopt};
    GroovesState next{std::move(after), {}, {}, {}, std::nullopt};
    // Within one drag, from the library as it was when the drag began (the devices' states too).
    const int index = undoStack_->index();
    const auto* last = index > 0 ? dynamic_cast<const SetGroovesCommand*>(undoStack_->command(index - 1)) : nullptr;
    const bool merging = !mergeKey.isEmpty() && last != nullptr && last->mergeKey() == mergeKey;
    // The Groove devices playing a library groove: their copies as the library has it now (or, the
    // groove going, their own: the id dropped, the copy kept).
    for (const auto& [trackId, device] : grooveDevices(p)) {
        const GrooveSource source = grooves::deviceSource(device->state);
        if (source.kind != GrooveSource::Kind::Library) continue;
        GrooveSource wanted = source;
        if (source.libraryId == detach) {
            const LibraryGroove* going = p.findGroove(detach);
            if (!going) continue;
            wanted = grooves::detached(source, detach, going->groove);
        }
        const std::optional<QString> state = grooves::deviceState(device->state, wanted, next.library, device->id);
        if (state == device->state) continue;
        const std::pair<QString, QString> key{trackId, device->id};
        before.devices.insert(key, device->state);
        next.devices.insert(key, state);
    }
    if (!detach.isEmpty()) {
        if (const LibraryGroove* going = p.findGroove(detach)) {
            // Clips and their takes playing it, and the default groove: their own copies.
            const auto detachAll = [&](std::vector<Clip>& clips) {
                bool changed = false;
                for (Clip& clip : clips) {
                    if (!clip.groove) continue;
                    const GrooveSource own = grooves::detached(clip.groove->source, detach, going->groove);
                    if (own == clip.groove->source) continue;
                    clip.groove->source = own;
                    changed = true;
                }
                return changed;
            };
            for (const Track* track : p.allTracks()) {
                std::vector<Clip> clips = track->clips;
                if (detachAll(clips)) {
                    before.clips.insert(track->id, track->clips);
                    next.clips.insert(track->id, std::move(clips));
                }
                std::vector<TakeLane> takes = track->takes;
                bool tookAny = false;
                for (TakeLane& lane : takes) tookAny = detachAll(lane.clips) || tookAny;
                if (tookAny) {
                    before.takes.insert(track->id, track->takes);
                    next.takes.insert(track->id, std::move(takes));
                }
            }
            if (p.defaultGroove()) {
                const GrooveSource own = grooves::detached(*p.defaultGroove(), detach, going->groove);
                if (own != *p.defaultGroove()) {
                    before.defaultGroove = p.defaultGroove();
                    next.defaultGroove = std::optional<GrooveSource>(own);
                }
            }
        }
    }
    if (merging) {
        // (The devices the drag changed first keep their state before it.)
        for (auto it = last->oldValue().devices.constBegin(); it != last->oldValue().devices.constEnd(); ++it) {
            before.devices.insert(it.key(), it.value());
        }
        before.library = last->oldValue().library;
    }
    push(std::make_unique<SetGroovesCommand>(project_, std::move(before), std::move(next), text, grooveId, mergeKey));
}

QString ProjectEditor::addGroove(const QString& name, const groove::Groove& groove,
                                const std::vector<groove::NoteTime>& notes) {
    std::vector<LibraryGroove> after = project_->grooves();
    LibraryGroove added{newId(), uniqueGrooveName(name), groove::normalized(groove), notes};
    const QString id = added.id;
    after.push_back(std::move(added));
    commitGrooves(std::move(after), QStringLiteral("Add Groove"), id, {});
    return id;
}

QString ProjectEditor::addBuiltinGroove(const QString& builtinId) {
    const LibraryGroove* builtin = grooves::builtin(builtinId);
    return builtin ? addGroove(builtin->name, builtin->groove, builtin->notes) : QString();
}

QString ProjectEditor::duplicateGroove(const QString& grooveId) {
    const LibraryGroove* groove = project_->findGroove(grooveId);
    return groove ? addGroove(groove->name, groove->groove, groove->notes) : QString();
}

void ProjectEditor::setGroove(const QString& grooveId, const groove::Groove& groove, const QString& mergeKey) {
    std::vector<LibraryGroove> after = project_->grooves();
    const auto it = std::find_if(after.begin(), after.end(), [&](const LibraryGroove& g) { return g.id == grooveId; });
    if (it == after.end()) return;
    const groove::Groove changed = groove::normalized(groove);
    if (it->groove == changed) return;
    it->groove = changed;
    commitGrooves(std::move(after), QStringLiteral("Edit Groove"), grooveId, mergeKey);
}

void ProjectEditor::renameGroove(const QString& grooveId, const QString& name) {
    std::vector<LibraryGroove> after = project_->grooves();
    const auto it = std::find_if(after.begin(), after.end(), [&](const LibraryGroove& g) { return g.id == grooveId; });
    if (it == after.end() || name.trimmed().isEmpty() || it->name == name.trimmed()) return;
    it->name = uniqueGrooveName(name);
    commitGrooves(std::move(after), QStringLiteral("Rename Groove"), grooveId, {});
}

void ProjectEditor::removeGroove(const QString& grooveId) {
    std::vector<LibraryGroove> after = project_->grooves();
    const auto it = std::find_if(after.begin(), after.end(), [&](const LibraryGroove& g) { return g.id == grooveId; });
    if (it == after.end()) return;
    after.erase(it);
    commitGrooves(std::move(after), QStringLiteral("Remove Groove"), grooveId, {}, grooveId);
}

QString ProjectEditor::extractGroove(const QString& trackId, const QString& clipId, double stepBeats, int bars,
                                     bool perKeyLanes) {
    const Clip* clip = project_->findClip(trackId, clipId);
    if (!clip) return {};
    groove::Groove made;
    std::vector<groove::NoteTime> notes;
    if (clip->isMidi()) {
        if (clip->heardNotes().empty()) return {};
        made = grooves::fromMidiClip(*clip, stepBeats, bars, perKeyLanes, &notes);
    } else {
        const std::vector<std::pair<double, float>> transients = transientsOf(clip->path);
        if (transients.empty()) return {};
        made = grooves::fromAudioClip(*clip, transients, project_->tempoMap().bpmAt(clip->startBeat), stepBeats, bars,
                                      &notes);
    }
    const Track& track = project_->track(trackId);
    return addGroove(QStringLiteral("%1 Groove").arg(track.name), made, notes);
}

void ProjectEditor::setClipGroove(const ClipRefs& refs, const std::optional<ClipGroove>& groove,
                                  const QString& mergeKey) {
    const Project& p = *project_;
    ClipRefs taking;
    for (const ClipRef& ref : refs) {
        const Clip* clip = p.findClip(ref.trackId, ref.clipId);
        if (clip && grooves::takesGroove(*clip) && clip->groove != groove) taking.append(ref);
    }
    if (taking.isEmpty()) return;
    const Tempo& tempo = p.tempoMap();
    updateClips(
        taking,
        [&](const Clip& clip) {
            Clip changed = groove ? grooves::warpedForGroove(clip, tempo.bpmAt(clip.startBeat)) : clip;
            changed.groove = groove;
            return changed;
        },
        groove ? QStringLiteral("Set Clip Groove") : QStringLiteral("Remove Clip Groove"), mergeKey);
}

void ProjectEditor::applyClipGrooves(const ClipRefs& refs) {
    const Project& p = *project_;
    ClipRefs grooved;
    for (const ClipRef& ref : refs) {
        const Clip* clip = p.findClip(ref.trackId, ref.clipId);
        if (clip && clip->groove) grooved.append(ref);
    }
    if (grooved.isEmpty()) return;
    const Tempo& tempo = p.tempoMap();
    updateClips(
        grooved,
        [&](const Clip& clip) {
            const std::optional<groove::Groove> plays = grooves::resolve(clip.groove->source, p.grooves(), p.defaultGroove());
            std::vector<double> seconds;
            if (clip.isAudio()) {
                for (const auto& [sec, strength] : transientsOf(clip.path)) seconds.push_back(sec);
            }
            return grooves::applied(clip, plays, seconds, tempo);
        },
        QStringLiteral("Apply Groove"));
}

void ProjectEditor::setDeviceGrooveSource(const QString& trackId, const QString& deviceId, const QString& kind,
                                          const QString& grooveId) {
    const Project& p = *project_;
    const Device* device = p.findDevice(trackId, deviceId);
    const std::optional<GrooveSource::Kind> known = grooves::kindFromName(kind);
    if (!device || device->kind != grooves::kDeviceKind || !known) return;
    GrooveSource source;
    source.kind = *known;
    if (*known == GrooveSource::Kind::Library) {
        if (!p.findGroove(grooveId)) return;
        source.libraryId = grooveId;
    } else if (*known == GrooveSource::Kind::Own) {
        // What it plays now, apart from the library.
        const auto plays = devicePlays(p, *device);
        if (!plays) return;
        source.own = plays->first;
    }
    const std::optional<QString> state = grooves::deviceState(device->state, source, p.grooves(), deviceId);
    if (state == device->state) return;
    setDeviceState(trackId, deviceId, device->state, state, QStringLiteral("Change Groove"));
}

bool ProjectEditor::applyDeviceGroove(const QString& trackId, const QString& deviceId) {
    const Project& p = *project_;
    const Device* device = p.findDevice(trackId, deviceId);
    if (!device || device->kind != grooves::kDeviceKind || !p.hasTrack(trackId)) return false;
    const auto plays = devicePlays(p, *device);
    if (!plays) return false;
    const auto& [groove, amounts] = *plays;
    const Track& track = p.track(trackId);
    // On a Drum Rack's pad: that pad's key's notes, which it hears as C3.
    int padKey = -1;
    if (const std::optional<QString> chainId = containerOf(track.devices, deviceId)) {
        const ConstRackChain found = findChain(track.devices, *chainId);
        if (found.chain && found.rack && found.rack->isDrumRack()) padKey = found.chain->receiveNote;
    }
    // (Allow Early, or Latency: half a step either way.)
    const bool early = device->params.value(QStringLiteral("early"), 1.0) >= 0.5 ||
                       device->params.value(QStringLiteral("latency"), 0.0) >= 0.5;
    const double earliest = early ? 0.5 * groove.stepBeats : 0.0;
    const Tempo& tempo = p.tempoMap();
    std::vector<Clip> clips = track.clips;
    bool changed = false;
    for (Clip& clip : clips) {
        if (!clip.isMidi()) continue;
        std::vector<Note> notes = clip.notes;
        for (Note& note : notes) {
            if (padKey >= 0 && note.pitch != padKey) continue;
            const double at = clip.toTimeline(note.start);
            const groove::Placed placed =
                groove::place(groove, at, padKey >= 0 ? 60 : note.pitch, note.velocity, note.length, tempo.map(), amounts);
            const double moved = std::max(placed.beat, at - earliest) - at;
            note.start = std::max(0.0, note.start + moved);
            note.length = std::max(notes::kMinNoteBeats, placed.lengthBeats);
            note.velocity = static_cast<int>(std::clamp(std::lround(placed.velocity), 1L, 127L));
        }
        notes = notes::normalize(notes::untangle(notes));
        if (notes == clip.notes) continue;
        clip.notes = std::move(notes);
        changed = true;
    }
    if (!changed && !device->enabled) return false;
    const editing::Macro macro(undoStack_, QStringLiteral("Apply Groove"));
    if (changed) commitClips(QStringLiteral("Apply Groove"), {{trackId, clips}});
    if (device->enabled) setDeviceEnabled(trackId, deviceId, false);
    return true;
}

void ProjectEditor::setDefaultGroove(const std::optional<GrooveSource>& source, const QString& mergeKey) {
    std::optional<GrooveSource> wanted = source;
    if (wanted && wanted->kind == GrooveSource::Kind::Project) wanted.reset();
    if (wanted && wanted->kind == GrooveSource::Kind::Swing && wanted->swing <= groove::kMinSwing) wanted.reset();
    if (wanted && wanted->kind == GrooveSource::Kind::Library && !project_->findGroove(wanted->libraryId)) return;
    if (wanted == project_->defaultGroove()) return;
    push(std::make_unique<SetDefaultGrooveCommand>(project_, project_->defaultGroove(), wanted, mergeKey));
}

void ProjectEditor::setDefaultSwing(double percent, const QString& mergeKey) {
    const std::optional<GrooveSource>& current = project_->defaultGroove();
    const double step = current && current->kind == GrooveSource::Kind::Swing ? current->stepBeats : 0.25;
    setDefaultGroove(grooves::swingSource(percent, step), mergeKey);
}

void ProjectEditor::setDefaultGrooveId(const QString& grooveId) {
    setDefaultGroove(grooveId.isEmpty() ? std::nullopt : std::optional(grooves::librarySource(grooveId)));
}

}  // namespace sub::app
