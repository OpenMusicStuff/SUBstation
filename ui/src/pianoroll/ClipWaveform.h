#pragma once

// The clip view's waveforms: each open audio clip's whole source file fitted to
// the width, in its track's colour and scaled by its gain, the part the clip
// plays tinted and the rest dimmed, with S and E flags at its start and end, as
// in Ableton's sample editor. One clip gets a time ruler (steps at least 70 px
// apart); several are stacked in bands of at least 40 px, labelled, with
// "+N more" when they don't fit.
//
// A clip's warp markers stand on its waveform, a tab at the top of each. With
// one warped clip open they are edited there: double-clicking the waveform adds
// one (on the beat the audio plays there now: nothing moves), dragging one moves
// that moment of the file (its beat kept: one undo step a drag), double-clicking
// one removes it (ClipViewController::addWarpMarker and the rest). Its file's
// transients show then as short ticks under the ruler, as in Live; a marker
// added by double-clicking near one goes on it.

#include "audio/Waveform.h"
#include "model/Clip.h"
#include "pianoroll/ClipViewController.h"
#include "sg/SgCanvas.h"
#include "session/Session.h"

#include <QColor>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace sub::ui {

class ClipWaveform : public SgCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(sub::app::Session* session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(sub::ui::ClipViewController* controller READ controller WRITE setController NOTIFY controllerChanged)

public:
    static constexpr int kRulerHeight = 20;
    static constexpr int kMinBandHeight = 40;  // each clip's waveform when several are open
    static constexpr int kMarkerTab = 8;        // a warp marker's tab, and how near the mouse picks it

    explicit ClipWaveform(QQuickItem* parent = nullptr);

    app::Session* session() const { return session_; }
    void setSession(app::Session* session);
    ClipViewController* controller() const { return controller_; }
    void setController(ClipViewController* controller);

    // A time on the ruler: seconds (minutes:seconds from a minute), with as
    // many decimals as the step needs.
    static QString formatTime(double seconds, double step);

Q_SIGNALS:
    void sessionChanged();
    void controllerChanged();

protected:
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
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
    // One clip's source in `area`; its length in seconds (0: not loaded).
    double drawBand(SgPainter& painter, const Band& band, const QRectF& area) const;
    void drawLabel(SgPainter& painter, const QRectF& band, const QString& name) const;
    void drawRuler(SgPainter& painter, double totalSec) const;
    // The one clip open's source's length when its warp markers can be edited (0: they can't).
    double editableSec() const;
    double secAt(double x) const;
    // The warp marker under `x` (-1: none).
    int markerAt(double x) const;
    // Where a marker added at `x` goes: the transient near it, else there.
    double snappedSecAt(double x) const;

    QPointer<app::Session> session_;
    QPointer<ClipViewController> controller_;
    std::vector<Band> bands_;
    struct Drag {
        int index = -1;
        QString key;
    };
    std::optional<Drag> drag_;  // a warp marker dragged
};

}  // namespace sub::ui
