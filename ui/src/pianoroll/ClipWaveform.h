#pragma once

// The clip view's waveforms: each open audio clip's source file, in its
// track's colour and scaled by its gain, the part the clip plays tinted and the
// rest dimmed, with S and E flags at its start and end, as in Ableton's sample
// editor. One clip shows across the width (the whole of its file at first).
// Several are stacked in bands of at least 40 px, labelled, on one axis: the
// song's beats, each where it plays in the arrangement, under a ruler of bars
// and beats; when they don't fit they scroll up and down (`scrollY`). Clicking
// one of several selects it and its track (as a click in the arrangement
// would) and asks for it alone (clipClicked).
//
// It zooms and scrolls as the arrangement and the piano roll do: Ctrl+wheel
// zooms around the mouse, the wheel (Shift, or a horizontal one) scrolls
// sideways (with several clips that don't fit, the wheel scrolls up and down,
// as the arrangement's), Ctrl+Alt drags it, and a drag on the ruler scrolls
// (sideways) or zooms (up and down) as their rulers do. What it shows is a share
// (`viewFrom` to `viewTo`, 0..1) of its extent: one clip's file, or the beats
// several clips' files span. Opening clips shows the whole of one's file, or
// the stretch several clips play (zoomToFit()).
//
// A warped clip is laid out as Live lays it out: in its own beats (Placed's
// `warped`), its waveform drawn stretch by stretch as fast or slow as it plays
// (arrangement::linearPieces), over the song's beats and bars, numbered by bar
// along the bottom, with a ruler of bars and beats. An unwarped one is laid out
// in seconds of its file. Its fades show over its waveform as the arrangement
// draws them.
//
// A clip's warp markers stand on its waveform, a tab at the top of each. With
// one warped clip open they are edited there, as in Live: double-clicking the
// waveform adds one (on the beat the audio plays there now: nothing moves),
// dragging one moves it to another beat, its moment of the audio with it, the
// waveform stretching either side (snapped to the grid's lines near it; Alt:
// freely); Shift-dragging one moves the waveform under it instead (its beat
// kept); double-clicking one removes it. A drag is one undo step, and the view
// holds still while it lasts. Its file's transients show then as short ticks
// under the ruler, as in Live; a marker added by double-clicking near one goes
// on it.
//
// Where the song is in each clip's file shows as in the arrangement: the start
// marker (the arrangement's insert marker, while it is in the part a clip
// plays) as a line, and the playhead while playing inside a clip, drawn by a
// ClipPlayhead over this item (it moves every frame; the waveforms stay). With
// one clip open, a click on the ruler plays from that moment of its file
// (locateRequested; outside the part it plays, from its start or its end).

#include "audio/Waveform.h"
#include "model/Clip.h"
#include "music/TempoMap.h"
#include "pianoroll/ClipViewController.h"
#include "sg/SgCanvas.h"
#include "session/Session.h"

#include <QColor>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QWheelEvent>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <utility>
#include <vector>

namespace sub::ui {

class ClipWaveform : public SgCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(sub::app::Session* session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(sub::ui::ClipViewController* controller READ controller WRITE setController NOTIFY controllerChanged)
    // The share of each clip's file shown (0..1).
    Q_PROPERTY(double viewFrom READ viewFrom NOTIFY viewChanged)
    Q_PROPERTY(double viewTo READ viewTo NOTIFY viewChanged)
    // Several clips: how far their bands are scrolled up, and how tall they are together.
    Q_PROPERTY(double scrollY READ scrollY WRITE setScrollY NOTIFY viewChanged)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY viewChanged)
    Q_PROPERTY(int rulerHeight READ rulerHeight CONSTANT)

public:
    static constexpr int kRulerHeight = 20;
    static constexpr int kMinBandHeight = 40;  // each clip's waveform when several are open
    static constexpr int kMarkerTab = 8;        // a warp marker's tab, and how near the mouse picks it
    static constexpr double kMaxPixelsPerFrame = 8.0;  // as far as it zooms in (the first clip's file)
    static constexpr double kWheelZoom = 1.2;          // a notch of Ctrl+wheel
    static constexpr double kWheelScroll = 80.0;       // pixels a notch of the wheel scrolls
    static constexpr double kWheelScrollY = 40.0;      // ... up and down (several clips)

    explicit ClipWaveform(QQuickItem* parent = nullptr);

    app::Session* session() const { return session_; }
    void setSession(app::Session* session);
    ClipViewController* controller() const { return controller_; }
    void setController(ClipViewController* controller);

    // A time on the ruler: seconds (minutes:seconds from a minute), with as
    // many decimals as the step needs.
    static QString formatTime(double seconds, double step);
    // A song beat as the beat ruler names it: bars and beats from 1 ("5", "5.2", "5.2.3" for sixteenths).
    static QString formatBeat(const MeterMap& meter, double song);

    double viewFrom() const { return viewFrom_; }
    double viewTo() const { return viewTo_; }
    double scrollY() const { return scrollY_; }
    static int rulerHeight() { return kRulerHeight; }
    void setScrollY(double y);
    double contentHeight() const;
    // Several clips shown, on the song's beats.
    bool shared() const { return bands_.size() > 1; }
    // Zoom by `factor` (more than 1: in) around `x`, the moment there staying put.
    Q_INVOKABLE void zoomAt(double x, double factor);
    // Show from `from` (a share of each file) on, the zoom staying.
    Q_INVOKABLE void scrollTo(double from);
    // The whole of each file.
    Q_INVOKABLE void zoomToFit();

    // Where each clip shown is drawn: its waveform's area, its file's length in
    // seconds (0: not loaded), and the part of it shown (from, length), top
    // first (several: each, scrolled; some may be out of sight).
    struct Placed {
        QRectF area;
        double totalSec = 0.0;
        double fromSec = 0.0;  // the file's second at the left edge
        double spanSec = 0.0;  // ... to the right edge's
        // Laid out in the song's beats (a warped clip, or several clips): `clip` placing its file on them
        // (as it plays in the arrangement, at `tempo`), from `fromBeat` at the left edge, `spanBeat` across.
        std::optional<app::Clip> clip;
        app::Tempo tempo;
        double fromBeat = 0.0;
        double spanBeat = 0.0;
    };
    std::vector<Placed> placed() const;
    // The ruler shows (some clip open).
    bool rulerShown() const { return !bands_.empty(); }
    // The playhead in each clip's file (seconds from its start), while playing inside that clip; by band.
    const std::vector<std::optional<double>>& playheadSecs() const { return playheadSecs_; }
    // The x of `sec` into a band's file, and back.
    static double xAt(const Placed& placed, double sec) {
        if (placed.clip) return xAtBeat(placed, songAt(*placed.clip, sec, placed.tempo));
        return placed.area.left() + (sec - placed.fromSec) / placed.spanSec * placed.area.width();
    }
    static double secAt(const Placed& placed, double x) {
        if (placed.clip) return secAtSong(*placed.clip, beatAt(placed, x), placed.tempo);
        return placed.fromSec + (x - placed.area.left()) / placed.area.width() * placed.spanSec;
    }
    // The song beat at which `clip` plays `sec` of its file (before or after the part it plays too), and back.
    static double songAt(const app::Clip& clip, double sec, const app::Tempo& tempo) {
        return clip.startBeat + clip.sourceToBeats(sec - clip.offsetSec, tempo);
    }
    static double secAtSong(const app::Clip& clip, double song, const app::Tempo& tempo) {
        return clip.offsetSec + clip.beatsToSource(song - clip.startBeat, tempo);
    }
    // A band laid out in beats: the x of song beat `beat`, and back.
    static double xAtBeat(const Placed& placed, double beat) {
        return placed.area.left() + (beat - placed.fromBeat) / placed.spanBeat * placed.area.width();
    }
    static double beatAt(const Placed& placed, double x) {
        return placed.fromBeat + (x - placed.area.left()) / placed.area.width() * placed.spanBeat;
    }
    // A warped clip's song beat at its own beat `beat` (the beat it plays at in the arrangement), and back.
    static double songBeat(const app::Clip& clip, double beat) {
        return clip.startBeat + beat - clip.warpBeatAt(clip.offsetSec);
    }
    static double ownBeat(const app::Clip& clip, double song) {
        return song - clip.startBeat + clip.warpBeatAt(clip.offsetSec);
    }

Q_SIGNALS:
    void sessionChanged();
    void controllerChanged();
    void viewChanged();
    // The playhead moved (only ClipPlayhead draws it again).
    void playheadChanged();
    // A click on the ruler: play from this arrangement beat.
    void locateRequested(double beat);
    // One of several clips clicked (it and its track selected): show it alone.
    void clipClicked(const QString& trackId, const QString& clipId);

protected:
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverEnterEvent(QHoverEvent* event) override { hoverMoveEvent(event); }
    void hoverMoveEvent(QHoverEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    // A clip as it is drawn: its track's colour, its source (null while it
    // loads) and why it couldn't be loaded. Worked out on the GUI thread.
    struct Band {
        app::Clip clip;
        QColor color;
        QString trackId;
        app::Waveform source;
        QString loadError;
        std::vector<double> transients;  // seconds: while its warp markers can be edited
    };

    void refreshBands();
    // Where the playhead and the start marker are in each band's file.
    void onPosition();
    void onInsert();
    // The second of `clip`'s file that plays at arrangement beat `beat`, if the clip plays there.
    std::optional<double> secAtBeat(const app::Clip& clip, double beat, bool endIncluded) const;
    // Shows `from` to `to` of each file, held to 0..1 and to the closest zoom.
    void setView(double from, double to);
    // The shortest share of the files shown: kMaxPixelsPerFrame of the first one's frames.
    double minSpan() const;
    // Several clips: the song beats their files span, and those they play (none while none is loaded).
    std::optional<std::pair<double, double>> sharedExtent(bool played) const;
    // A band's height with several clips (at least kMinBandHeight), and the band at `y` (-1: none).
    double bandHeight() const;
    int bandAt(double y) const;
    // The clip of band `index` in its place; its file's length in seconds (0: not loaded).
    double drawBand(SgPainter& painter, size_t index, const Placed& placed) const;
    // A band laid out in beats: the song's beats and bars across it (under its waveform).
    void drawWarpedGrid(SgPainter& painter, const Placed& placed) const;
    // Its fades over its waveform.
    void drawFades(SgPainter& painter, const app::Clip& clip, const Placed& placed) const;
    void drawLabel(SgPainter& painter, const QRectF& band, const QString& name) const;
    void drawRuler(SgPainter& painter, const Placed& placed) const;
    void drawBeatRuler(SgPainter& painter, const Placed& placed) const;
    void drawTimeRuler(SgPainter& painter, const Placed& placed) const;
    // The one clip open's source's length when its warp markers can be edited (0: they can't).
    double editableSec() const;
    // The second of the one clip's file at `x`, held to the file.
    double editableSecAt(double x) const;
    // The warp marker under `x` (-1: none).
    int markerAt(double x) const;
    // Where a marker added at `x` goes: the transient near it, else there.
    double snappedSecAt(double x) const;
    // A marker dragged to `x` of `placed`: the own beat there, on the grid's line near it unless `free`.
    double snappedBeatAt(const Placed& placed, double x, bool free) const;
    // A warped band's song beats on its grid: every `step` song beats while
    // they are at least kMinGridPixels apart (0.25, 0.5, 1, then bars).
    double gridStep(const Placed& placed) const;
    // A grid of bars (`step` over a beat): every how many bars a line is drawn (and a dragged marker snaps to).
    int64_t barsPerLine(const Placed& placed, double step) const;

    QPointer<app::Session> session_;
    QPointer<ClipViewController> controller_;
    std::vector<Band> bands_;
    double viewFrom_ = 0.0;
    double viewTo_ = 1.0;
    std::vector<std::optional<double>> playheadSecs_;  // by band
    std::vector<std::optional<double>> startSecs_;     // the start marker, by band
    struct Drag {
        int index = -1;
        QString key;
        Placed at;            // the lead band as it was at the press (the view holds still)
        double pressX = 0.0;
        double sec = 0.0;     // the marker's second at the press
    };
    std::optional<Drag> drag_;  // a warp marker dragged
    // While one is: the lead band's beats from its file's start to its end, as they were at the press.
    std::optional<std::pair<double, double>> heldBeats_;
    double scrollY_ = 0.0;
    // A press on one of several clips' bands: a click (released within 3 px) selects that clip.
    std::optional<std::pair<QPointF, int>> clickPress_;
    bool justChosen_ = false;  // (the double-click that a click choosing a clip began isn't the clip's)
    // Ctrl+Alt dragging the view, or a press on the ruler (a click plays from there, a drag scrolls or zooms).
    struct ViewDrag {
        QPointF press;
        double lastY = 0.0;
        double from = 0.0;  // viewFrom_ at the press
        bool ruler = false;
        bool moved = false;
        bool zoom = false;  // a ruler drag up and down
    };
    std::optional<ViewDrag> viewDrag_;
};

}  // namespace sub::ui
