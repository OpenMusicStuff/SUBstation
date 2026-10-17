// Take lanes: comping from them, deleting them, showing them (Track::takes).
// Recording makes them (ProjectEditor::addRecordings, EditorTracks.cpp).

#include "editor/EditorSupport.h"
#include "editor/ProjectEditor.h"

#include "model/Commands.h"
#include "model/Edits.h"

#include <QSet>

#include <algorithm>
#include <utility>

namespace sub::app {

using editing::Macro;

namespace {

const TakeLane* findLane(const Track& track, const QString& laneId) {
    const auto lane = std::find_if(track.takes.begin(), track.takes.end(),
                                   [&](const TakeLane& l) { return l.id == laneId; });
    return lane == track.takes.end() ? nullptr : &*lane;
}

}  // namespace

bool ProjectEditor::compTake(const QString& trackId, const QString& laneId, double startBeat, double endBeat) {
    const Project& p = *project_;
    const Track* track = p.findTrack(trackId);
    const TakeLane* lane = track == nullptr ? nullptr : findLane(*track, laneId);
    startBeat = std::max(0.0, startBeat);
    if (lane == nullptr || endBeat <= startBeat + edits::kEps) return false;
    const Tempo& tempo = p.tempoMap();
    // What plays there now goes; the take's clips there (its silence too) take its place.
    std::vector<Clip> clips = edits::removeRange(track->clips, startBeat, endBeat, tempo);
    QSet<QString> chosen;
    for (Clip& part : edits::sliceRange(lane->clips, startBeat, endBeat, tempo)) {
        chosen.insert(part.id);
        clips.push_back(std::move(part));
    }
    // (Comping what the comp plays already changes nothing: the same clips, but for their ids.)
    std::vector<Clip> after = edits::resolveOverlaps(clips, chosen, tempo);
    edits::sortByStart(after);
    const auto same = [&](const Clip& a, const Clip& b) {
        Clip renamed = a;
        renamed.id = b.id;
        return renamed == b;
    };
    if (std::equal(after.begin(), after.end(), track->clips.begin(), track->clips.end(), same)) return false;
    return commitClips(QStringLiteral("Comp Take"), {{trackId, after}});
}

void ProjectEditor::deleteTakeLane(const QString& trackId, const QString& laneId) {
    const Track* track = project_->findTrack(trackId);
    if (track == nullptr || findLane(*track, laneId) == nullptr) return;
    std::vector<TakeLane> lanes = track->takes;
    std::erase_if(lanes, [&](const TakeLane& l) { return l.id == laneId; });
    push(std::make_unique<SetTakeLanesCommand>(project_, trackId, track->takes, std::move(lanes),
                                               QStringLiteral("Delete Take Lane")));
}

void ProjectEditor::deleteTakeLanes(const QString& trackId) {
    const Track* track = project_->findTrack(trackId);
    if (track == nullptr || track->takes.empty()) return;
    push(std::make_unique<SetTakeLanesCommand>(project_, trackId, track->takes, std::vector<TakeLane>{},
                                               QStringLiteral("Delete Take Lanes")));
}

void ProjectEditor::setTakesShown(const QString& trackId, bool shown) {
    const Track* track = project_->findTrack(trackId);
    if (track != nullptr && track->hasClips() && track->takesShown != shown)
        project_->updateTrack(trackId, TrackField::TakesShown, shown);
}

}  // namespace sub::app
