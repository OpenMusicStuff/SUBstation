#include "arrangement/Arrangement.h"

#include "audio/EngineBridge.h"
#include "editor/ProjectEditor.h"
#include "model/Numbers.h"
#include "model/Project.h"
#include "session/ArrangementActions.h"
#include "session/Selection.h"

#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace sub::ui {

using namespace arrangement;

namespace {

constexpr int kNameRow = 22;  // a header's name row (Ctrl+R scrolls it into view)

}  // namespace

Arrangement::Arrangement(QObject* parent)
    : QObject(parent), rows_(new TrackRowModel(this)), returns_(new ReturnRowModel(this)) {
    elapsed_.start();
    clock_ = [this] { return static_cast<double>(elapsed_.nsecsElapsed()) / 1e9; };
}

Arrangement::~Arrangement() = default;

app::Project* Arrangement::project() const { return session_ ? session_->project() : nullptr; }
app::ProjectEditor* Arrangement::editor() const { return session_ ? session_->editor() : nullptr; }
app::EngineBridge* Arrangement::bridge() const { return session_ ? session_->bridge() : nullptr; }
app::Selection* Arrangement::selection() const { return session_ ? session_->selection() : nullptr; }

void Arrangement::setHeaderSelected(const QStringList& trackIds) {
    if (trackIds == headerSelected_) return;
    headerSelected_ = trackIds;
    Q_EMIT headerSelectionChanged();
}

void Arrangement::endHeaderClick() {
    headerClick_ = false;
    const app::Selection* s = selection();
    if (!s) return;
    QStringList ids = s->trackIds();
    if (s->trackId() == app::kMaster && !ids.contains(app::kMaster)) ids << app::kMaster;
    setHeaderSelected(ids);
}

void Arrangement::setSession(app::Session* session) {
    if (session == session_) return;
    if (session_) {
        disconnect(session_, nullptr, this, nullptr);
        disconnect(session_->project(), nullptr, this, nullptr);
        disconnect(session_->bridge(), nullptr, this, nullptr);
        disconnect(session_->selection(), nullptr, this, nullptr);
    }
    session_ = session;
    setHeaderSelected({});
    view_.setProject(session ? session->project() : nullptr, true);  // (the song's beats: its bars)
    if (session) connectSession();
    Q_EMIT sessionChanged();
    onReset();
}

void Arrangement::connectSession() {
    app::Project* p = session_->project();
    connect(session_->selection(), &app::Selection::changed, this, [this] {
        if (!headerClick_) setHeaderSelected({});  // (a header's click sets them when it is done)
    });
    for (auto signal : {&app::Project::trackInserted, &app::Project::trackRemoved}) {
        connect(p, signal, this, [this] {
            invalidateGraph();
            rebuild();
        });
    }
    connect(p, &app::Project::tracksArranged, this, [this] {
        invalidateGraph();
        rebuild();
    });
    for (auto signal : {&app::Project::returnInserted, &app::Project::returnRemoved}) {
        // A return came or went: its rows, and how tall a track's lane is while its automation shows.
        connect(p, signal, this, [this] {
            invalidateGraph();
            updateReturnRows();
            rebuild();
        });
    }
    connect(p, &app::Project::reset, this, &Arrangement::onReset);
    connect(p, &app::Project::trackChanged, this, &Arrangement::onTrackChanged);
    // (A sidechain is a routing edge too.)
    connect(p, &app::Project::devicesChanged, this, &Arrangement::invalidateGraph);
    connect(p, &app::Project::automationViewChanged, this, &Arrangement::onAutomationViewChanged);
    connect(p, &app::Project::clipsChanged, this, &Arrangement::updateHBar);
    // Take lanes coming or going: its row, if they show (a row as onTrackChanged's).
    connect(p, &app::Project::takesChanged, this, &Arrangement::onTrackChanged);
    connect(p, &app::Project::settingsChanged, this, [this] {
        updateHBar();
        Q_EMIT gridChanged();  // (the time signature's bars)
    });
    app::EngineBridge* bridge = session_->bridge();
    connect(bridge, &app::EngineBridge::positionChanged, this, &Arrangement::onPosition);
    connect(bridge, &app::EngineBridge::transportChanged, this, [this] {
        if (followPaused_) {  // stopping or starting playback follows again
            followPaused_ = false;
            Q_EMIT followChanged();
        }
        onPosition(this->bridge()->position());
    });
    // A project opened shows all of it.
    connect(session_, &app::Session::projectOpened, this, &Arrangement::zoomToArrangement);
}

// --- Model changes -------------------------------------------------------------------------

void Arrangement::onReset() {
    view_.setScrollBeats(0.0);
    view_.setScrollY(0);
    invalidateGraph();
    updateReturnRows();
    rebuild();
    updateMasterRows();
    moved();
    vscrolled();
}

void Arrangement::rebuild() {
    if (app::Project* p = project())
        layout_.rebuild(*p);
    else
        layout_ = TrackLayout();
    rows_->update(layout_.rows());
    Q_EMIT layoutChanged();
    Q_EMIT automationModeChanged();  // (tracks added or gone)
    updateVBar();
}

bool Arrangement::automationMode() const {
    const app::Project* p = project();
    if (!p) return false;
    const QStringList owners = p->owners();
    return !owners.isEmpty() &&
           std::all_of(owners.begin(), owners.end(), [p](const QString& o) { return p->automationView(o).shown; });
}

void Arrangement::onTrackChanged(const QString& trackId) {
    invalidateGraph();  // its sends or its input: which sends would close a cycle
    app::Project* p = project();
    if (!p) return;
    if (p->hasReturn(trackId)) return;
    if (trackId == app::kMaster) {
        // Its height or folding: its rows, if they changed.
        const AutomationRows rows = arrangement::masterRows(*p);
        if (rows.mainHeight != masterRows_.mainHeight || rows.lanes != masterRows_.lanes) updateMasterRows();
        return;
    }
    const int oldHeight = layout_.totalHeight();
    const Row* old = layout_.rowFor(trackId);
    const std::optional<Row> oldRow = old ? std::optional<Row>(*old) : std::nullopt;
    TrackLayout fresh;
    fresh.rebuild(*p);
    const Row* now = fresh.rowFor(trackId);
    const std::optional<Row> newRow = now ? std::optional<Row>(*now) : std::nullopt;
    if (fresh.totalHeight() != oldHeight || newRow != oldRow) rebuild();
}

void Arrangement::onAutomationViewChanged(const QString& owner) {
    app::Project* p = project();
    if (!p) return;
    Q_EMIT automationModeChanged();
    if (owner == app::kMaster)
        updateMasterRows();
    else if (p->hasReturn(owner))
        updateReturnRows();
    else if (p->hasTrack(owner))
        rebuild();
}

void Arrangement::updateMasterRows() {
    if (app::Project* p = project()) masterRows_ = arrangement::masterRows(*p);
    Q_EMIT masterRowsChanged();
}

void Arrangement::updateReturnRows() {
    std::vector<ReturnRowModel::Item> items;
    if (app::Project* p = project()) {
        for (const app::Track& ret : p->returns()) {
            const AutomationRows rows = arrangement::returnRows(*p, ret.id);
            items.push_back({ret.id, rows.mainHeight, rows.height()});
        }
    }
    returns_->update(items);
    Q_EMIT returnRowsChanged();
}

int Arrangement::returnsHeight() const {
    int height = 0;
    for (const ReturnRowModel::Item& item : returns_->items()) height += item.height;
    return height;
}

std::optional<AutomationRows> Arrangement::returnRows(const QString& returnId) const {
    app::Project* p = project();
    if (!p || !p->hasReturn(returnId)) return std::nullopt;
    return arrangement::returnRows(*p, returnId);
}

QVariantList Arrangement::lanesOf(const QString& owner) const {
    std::vector<LaneRow> lanes;
    int top = 0;
    if (owner == app::kMaster) {
        lanes = masterRows_.lanes;
    } else if (const auto rows = returnRows(owner)) {
        lanes = rows->lanes;
    } else if (const Row* row = layout_.rowFor(owner)) {
        lanes = row->lanes;
        top = row->top;
    }
    QVariantList list;
    for (const LaneRow& lane : lanes) {
        list.append(QVariantMap{{QStringLiteral("index"), lane.index},
                                {QStringLiteral("key"), lane.key},
                                {QStringLiteral("top"), lane.top - top},
                                {QStringLiteral("height"), lane.height}});
    }
    return list;
}

const app::RoutingGraph& Arrangement::routingGraph() const {
    if (graphDirty_) {
        graph_ = project() ? app::routingGraph(project()->tracks(), project()->returns()) : app::RoutingGraph();
        graphDirty_ = false;
    }
    return graph_;
}

// --- The time axis -----------------------------------------------------------------------------

void Arrangement::moved() {
    Q_EMIT viewChanged();
    Q_EMIT gridChanged();  // (the grid's step follows the zoom)
    updateHBar();
}

void Arrangement::vscrolled() {
    Q_EMIT vscrollChanged();
    Q_EMIT scrollBarsChanged();
}

void Arrangement::setSnap(bool snap) {
    if (view_.setSnap(snap)) Q_EMIT gridChanged();
}

void Arrangement::setFollow(bool follow) {
    if (follow == follow_) return;
    follow_ = follow;
    Q_EMIT followChanged();
}

void Arrangement::setGridLevel(int level) {
    if (view_.setGridLevel(level)) Q_EMIT gridChanged();
}

QString Arrangement::gridLabel() const {
    const double step = view_.gridStep();
    const double bar = view_.timeSignature().beatsPerBar();
    QString label;
    if (step >= bar) {
        label = QStringLiteral("%1 Bar").arg(qRound(step / bar)) + (step > bar ? QStringLiteral("s") : QString());
    } else {
        label = QStringLiteral("1/%1").arg(qRound(4.0 / step));
    }
    return QStringLiteral("Grid ") + label + (view_.snap() ? QString() : QStringLiteral(" (off)"));
}

void Arrangement::setScrollBeats(double beats) {
    if (view_.setScrollBeats(beats)) moved();
}

void Arrangement::scrollByHand(double beats) {
    if (!followPaused_) {
        followPaused_ = true;
        Q_EMIT followChanged();
    }
    setScrollBeats(beats);
}

void Arrangement::setScrollY(double y) {
    if (view_.setScrollY(y)) vscrolled();
}

void Arrangement::zoomAt(double x, double factor) {
    if (view_.zoomAt(x, factor)) moved();
}

void Arrangement::setPxPerBeat(double pxPerBeat) {
    if (view_.setPxPerBeat(pxPerBeat)) moved();
}

void Arrangement::zoom(double factor) {
    const double width = lanesWidth_;
    const double playheadX = bridge() ? view_.beatToX(bridge()->position()) : -1.0;
    const double anchor = 0 <= playheadX && playheadX <= width ? playheadX : width / 2;
    zoomAt(anchor, factor);
}

void Arrangement::zoomToArrangement() {
    app::Project* p = project();
    if (!p) return;
    const double end = std::max(p->endBeat(), p->timeSignature().beatsPerBar() * 8);
    if (view_.zoomToFit(0.0, end * 1.05, lanesWidth_)) moved();
}

void Arrangement::narrowGrid() { setGridLevel(view_.gridLevel() - 1); }

void Arrangement::widenGrid() { setGridLevel(view_.gridLevel() + 1); }

void Arrangement::openClipView() {
    app::Selection* s = selection();
    if (!s || s->clips().isEmpty()) return;
    session_->arrangement()->requestClipView(app::ClipRefs(s->clips().begin(), s->clips().end()), app::ClipRef());
}

void Arrangement::openClips(const QString& leadTrackId, const QString& leadClipId) {
    app::Selection* s = selection();
    if (!s) return;
    session_->arrangement()->requestClipView(app::ClipRefs(s->clips().begin(), s->clips().end()),
                                             app::ClipRef{leadTrackId, leadClipId});
}

void Arrangement::setLanesWidth(double width) {
    if (width == lanesWidth_) return;
    lanesWidth_ = width;
    Q_EMIT lanesSizeChanged();
    updateHBar();
}

void Arrangement::setLanesHeight(double height) {
    if (height == lanesHeight_) return;
    lanesHeight_ = height;
    Q_EMIT lanesSizeChanged();
    updateVBar();
}

// --- Scroll bars -----------------------------------------------------------------------------

void Arrangement::updateHBar() {
    app::Project* p = project();
    const double width = std::max(1.0, std::floor(lanesWidth_));
    const double ppb = view_.pxPerBeat();
    const double bar = view_.timeSignature().beatsPerBar();
    const double contentEnd =
        std::max({p ? p->endBeat() : 0.0, p ? p->loopEnd() : 0.0, view_.xToBeat(width)}) + 16 * bar;
    const double range = std::max(0.0, std::trunc(contentEnd * ppb - width));
    hTotal_ = range + width;
    hValue_ = std::trunc(view_.scrollBeats() * ppb);
    Q_EMIT scrollBarsChanged();
}

void Arrangement::updateVBar() {
    const int viewport = std::max(1, static_cast<int>(lanesHeight_));
    const int maximum = std::max(0, layout_.totalHeight() + kDropZone - viewport);
    view_.setMaxScrollY(maximum);
    if (view_.scrollY() > maximum) {
        view_.setScrollY(maximum);
        Q_EMIT vscrollChanged();
    }
    Q_EMIT scrollBarsChanged();
}

void Arrangement::scrollToX(double pixels) { scrollByHand(pixels / view_.pxPerBeat()); }

void Arrangement::scrollToY(double pixels) { setScrollY(pixels); }

// --- The playhead -------------------------------------------------------------------------------

void Arrangement::onPosition(double beat) {
    app::EngineBridge* b = bridge();
    const bool playing = b && b->isPlaying();
    const std::optional<double> shown = playing ? std::optional<double>(beat) : std::nullopt;
    if (shown != playhead_) {
        playhead_ = shown;
        Q_EMIT playheadChanged();
    }
    if (follow_ && !followPaused_ && playing) {
        const double x = view_.beatToX(beat);
        const double width = lanesWidth_;
        if (x > width * (1.0 - kFollowMargin) || x < 0) setScrollBeats(beat - width * kFollowMargin / view_.pxPerBeat());
    }
}

// --- Tracks -----------------------------------------------------------------------------------

bool Arrangement::renameTrack(const QString& trackId) {
    app::Project* p = project();
    if (!p) return false;
    if (p->hasReturn(trackId)) {
        Q_EMIT renameRequested(trackId);
        return true;
    }
    const Row* row = layout_.rowFor(trackId);
    if (!row || row->hidden) return false;  // the master, or a track folded away in its group
    const int scroll = view_.scrollY();
    if (!(scroll <= row->top && row->top <= scroll + lanesHeight_ - kNameRow)) setScrollY(row->top);
    Q_EMIT renameRequested(trackId);
    return true;
}

bool Arrangement::revealTrack(const QString& trackId) {
    app::Project* p = project();
    app::Selection* s = selection();
    if (!p || !s || !(trackId == app::kMaster || p->hasTrack(trackId) || p->hasReturn(trackId))) return false;
    // Folded itself (a thin row, or a group hiding its tracks): unfolded, its lanes shown.
    if (const app::Track* track = p->findTrack(trackId); track && track->folded) editor()->setFolded(trackId, false);
    if (p->hasTrack(trackId)) {
        for (auto parent = p->track(trackId).parent; parent && p->hasTrack(*parent); parent = p->track(*parent).parent) {
            if (p->track(*parent).folded) editor()->setFolded(*parent, false);  // (the layout follows at once)
        }
        if (const Row* row = layout_.rowFor(trackId); row && !row->hidden) {
            // (The whole row, its automation lanes too; one taller than the lanes at its top.)
            const int scroll = view_.scrollY();
            const int height = row->height();
            if (row->top < scroll || row->top + height > scroll + lanesHeight_) {
                setScrollY(row->top - std::max(0.0, (lanesHeight_ - height) / 2.0));
            }
        }
    }
    beginHeaderClick();
    s->selectTrack(trackId, true);
    endHeaderClick();
    return true;
}

bool Arrangement::selectTrackContent(const QString& trackId) {
    app::Project* p = project();
    app::Selection* s = selection();
    if (!p || !s || !editor() || !p->hasTrack(trackId)) return false;
    QStringList tracks{trackId};
    for (const app::Track* track : p->descendants(trackId)) tracks.append(track->id);
    const app::Tempo& tempo = p->tempoMap();
    std::optional<std::pair<double, double>> span;
    const auto take = [&span](double start, double end) {
        span = span ? std::make_pair(std::min(span->first, start), std::max(span->second, end)) : std::make_pair(start, end);
    };
    for (const QString& id : tracks) {
        for (const app::Clip& clip : p->track(id).clips) take(clip.startBeat, clip.endBeat(tempo));
        for (const auto& [key, points] : p->automation(id)) {
            if (!points.empty()) take(points.front().beat, points.back().beat);
        }
    }
    if (!span) return false;
    // (A lone breakpoint spans no time: a beat past it, so the selection holds it.)
    const double start = span->first, end = std::max(span->second, start + 1.0);
    s->setTimeRange(start, end, tracks, editor()->clipsInRange(start, end, tracks), {}, {trackId});
    s->setInsert(start);
    return true;
}

QString Arrangement::revealNearestClip(const QString& trackId) {
    app::Project* p = project();
    app::Selection* s = selection();
    if (!p || !s || !editor() || !p->hasTrack(trackId)) return {};
    const double beat = playhead_ ? *playhead_ : s->insertBeat();
    const app::Tempo& tempo = p->tempoMap();
    QString clipId;
    double start = 0.0, end = 0.0, nearest = 0.0;
    for (const app::Clip& clip : p->track(trackId).clips) {  // (sorted by start: the earliest wins a tie)
        const double distance = std::max({0.0, clip.startBeat - beat, beat - clip.endBeat(tempo)});
        if (clipId.isEmpty() || distance < nearest) {
            clipId = clip.id;
            nearest = distance;
            start = clip.startBeat;
            end = clip.endBeat(tempo);
        }
    }
    if (clipId.isEmpty()) return {};
    s->selectClips(*editor(), {{trackId, clipId}});
    const double shown = lanesWidth_ / view_.pxPerBeat();
    if (shown > 0 && (start < view_.scrollBeats() || end > view_.scrollBeats() + shown)) {
        setScrollBeats(std::max(0.0, end - start <= shown ? (start + end - shown) / 2 : start - 0.05 * shown));
    }
    return clipId;
}

bool Arrangement::onTitleBar(const QString& trackId, double y) const {
    const app::Project* p = project();
    if (!p || !resizable(trackId)) return false;
    return y < (p->track(trackId).isGroup() ? kFoldedGroupHeight : kFoldedHeight);
}

bool Arrangement::resizable(const QString& trackId) const {
    const app::Project* p = project();
    return p && (p->hasTrack(trackId) || trackId == app::kMaster);
}

void Arrangement::wheelResize(const QString& trackId, double y, int delta) {
    app::Project* p = project();
    if (!p) return;
    const double now = clock_();
    const bool turning = lastWheel_ && now - lastWheel_->second < kWheelGesture;
    if (!turning) {
        wheelFolds_ = onTitleBar(trackId, y);
        wheelSum_ = 0;
    }
    if (wheelFolds_) {
        // The track under the mouse, whichever it is now: a sweep folds (or unfolds) one after another.
        lastWheel_ = std::make_pair(trackId, now);
        if (!delta || !resizable(trackId)) return;
        if ((wheelSum_ < 0) != (delta < 0)) wheelSum_ = 0;  // (turned back)
        wheelSum_ += delta;
        if (std::abs(wheelSum_) < 120) return;
        const bool fold = wheelSum_ < 0;
        wheelSum_ = 0;
        editor()->setFolded(trackId, fold);  // (unfolded at the height it had)
        return;
    }
    QString id = trackId;
    if (turning && resizable(lastWheel_->first)) id = lastWheel_->first;
    lastWheel_ = std::make_pair(id, now);
    if (!delta || !resizable(id)) return;
    const app::Track& track = p->track(id);
    if (track.folded) return;  // (folded meanwhile: its title bar folds and unfolds it)
    const int height = track.height + static_cast<int>(app::roundHalfEven(delta / 120.0 * kHeightStep));
    editor()->setTrackHeight(id, std::clamp(height, app::kMinTrackHeight, app::kMaxTrackHeight));
}

std::optional<Arrangement::DropTarget> Arrangement::dropTarget(const QStringList& trackIds, double y) const {
    app::Project* p = project();
    if (!p) return std::nullopt;
    const double contentY = y + view_.scrollY();
    std::optional<DropTarget> target;
    const auto& rows = layout_.rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        if (row.hidden || contentY >= row.bottom()) continue;
        const int index = static_cast<int>(i);
        const app::Track& track = p->tracks()[i];
        const double offset = contentY - row.top;
        if (track.isGroup() && row.mainHeight * 0.25 <= offset && offset <= row.mainHeight * 0.75) {
            target = DropTarget{p->subtreeEnd(index), track.id, track.id, row.top};
        } else if (offset < row.mainHeight / 2.0) {
            target = DropTarget{index, track.parent.value_or(QString()), QString(), row.top};
        } else {
            const int after = track.isGroup() && track.folded ? p->subtreeEnd(index) : index + 1;
            target = DropTarget{after, p->parentAt(after).value_or(QString()), QString(), row.bottom()};
        }
        break;
    }
    if (!target)  // below the tracks: last, in no group
        target = DropTarget{static_cast<int>(p->tracks().size()), QString(), QString(), layout_.totalHeight()};
    if (!editor()->canMoveTracks(trackIds, target->index, target->parent)) return std::nullopt;
    return target;
}

void Arrangement::dragTracks(const QStringList& trackIds, double y) {
    const auto target = dropTarget(trackIds, y);
    std::optional<Marker> marker;
    if (target) {
        if (!target->into.isEmpty()) {
            if (const Row* row = layout_.rowFor(target->into))
                marker = Marker{true, static_cast<double>(row->top - view_.scrollY()), static_cast<double>(row->mainHeight)};
        } else {
            marker = Marker{false, static_cast<double>(target->line - view_.scrollY() - 1), 3.0};
        }
    }
    const bool changed = marker.has_value() != marker_.has_value() ||
                         (marker && (marker->into != marker_->into || marker->y != marker_->y ||
                                     marker->height != marker_->height));
    marker_ = marker;
    if (changed) Q_EMIT dropMarkerChanged();
}

void Arrangement::dropTracks(const QStringList& trackIds, double y) {
    if (marker_) {
        marker_.reset();
        Q_EMIT dropMarkerChanged();
    }
    if (const auto target = dropTarget(trackIds, y)) editor()->moveTracks(trackIds, target->index, target->parent);
}

}  // namespace sub::ui
