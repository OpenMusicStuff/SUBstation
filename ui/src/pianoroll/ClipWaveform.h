#pragma once

// The clip view's waveforms: each open audio clip's source file across the
// width (the whole of it at first), in its track's colour and scaled by its
// gain, the part the clip plays tinted and the rest dimmed, with S and E flags
// at its start and end, as in Ableton's sample editor. One clip gets a time
// ruler (steps at least 70 px apart); several are stacked in bands of at least
// 40 px, labelled, with "+N more" when they don't fit.
//
// It zooms and scrolls as the arrangement and the piano roll do: Ctrl+wheel
// zooms around the mouse, the wheel (Shift, or a horizontal one) scrolls
// sideways, Ctrl+Alt drags it, and a drag on the ruler scrolls (sideways) or
// zooms (up and down) as their rulers do. What it shows is a share of each
// file (`viewFrom` to `viewTo`, 0..1), so several clips, each across the width,
// zoom together. Opening clips shows the whole of each again (zoomToFit()).
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

public:
    static constexpr int kRulerHeight = 20;
    static constexpr int kMinBandHeight = 40;  // each clip's waveform when several are open
    static constexpr int kMarkerTab = 8;        // a warp marker's tab, and how near the mouse picks it
    static constexpr double kMaxPixelsPerFrame = 8.0;  // as far as it zooms in (the first clip's file)
    static constexpr double kWheelZoom = 1.2;          // a notch of Ctrl+wheel
    static constexpr double kWheelScroll = 80.0;       // pixels a notch of the wheel scrolls

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
    // Zoom by `factor` (more than 1: in) around `x`, the moment there staying put.
    Q_INVOKABLE void zoomAt(double x, double factor);
    // Show from `from` (a share of each file) on, the zoom staying.
    Q_INVOKABLE void scrollTo(double from);
    // The whole of each file.
    Q_INVOKABLE void zoomToFit();

    // Where each clip shown is drawn: its waveform's area, its file's length in
    // seconds (0: not loaded), and the part of it shown (from, length), top
    // first (several: those that fit).
    struct Placed {
        QRectF area;
        double totalSec = 0.0;
        double fromSec = 0.0;  // the file's second at the left edge
        double spanSec = 0.0;  // ... to the right edge's
        // A warped clip, laid out in its own beats (Clip::warpBeatAt): from `fromBeat` at the left edge, `spanBeat` across.
        std::optional<app::Clip> warped;
        double fromBeat = 0.0;
        double spanBeat = 0.0;
    };
    std::vector<Placed> placed() const;
    // The ruler shows (one clip open).
    bool rulerShown() const { return bands_.size() == 1; }
    // The playhead in each clip's file (seconds from its start), while playing inside that clip; by band.
    const std::vector<std::optional<double>>& playheadSecs() const { return playheadSecs_; }
    // The x of `sec` into a band's file, and back.
    static double xAt(const Placed& placed, double sec) {
        if (placed.warped) return xAtBeat(placed, placed.warped->warpBeatAt(sec));
        return placed.area.left() + (sec - placed.fromSec) / placed.spanSec * placed.area.width();
    }
    static double secAt(const Placed& placed, double x) {
        if (placed.warped) return placed.warped->warpSecondsAt(beatAt(placed, x));
        return placed.fromSec + (x - placed.area.left()) / placed.area.width() * placed.spanSec;
    }
    // A warped band's: the x of its own beat `beat`, and back.
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

private:
    // A clip as it is drawn: its track's colour, its source (null while it
    // loads) and why it couldn't be loaded. Worked out on the GUI thread.
    struct Band {
        app::Clip clip;
        QColor color;
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
    // The clip of band `index` in its place; its file's length in seconds (0: not loaded).
    double drawBand(SgPainter& painter, size_t index, const Placed& placed) const;
    // A warped clip's song beats and bars where it plays them in its file (under its waveform).
    void drawWarpedGrid(SgPainter& painter, const app::Clip& clip, const Placed& placed) const;
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
