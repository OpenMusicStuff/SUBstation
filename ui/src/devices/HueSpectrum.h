#pragma once

// Hue's spectrum: what goes in and what comes out, 30 Hz to 12 kHz, with the
// band it processes between Low Cut and High Cut (its parameters Start and
// End) as two handles to drag.
// - The output as a fill coloured by pitch: each frequency in the hue of the
//   note it is (HueWheel::noteColor, blended between notes), so the partials
//   pulled into key stand out in their notes' colours; outside the band it is
//   grey. A glowing line along its top; the input as a faint line behind.
// - Inside the band, the enabled notes as thin lines in their hues at every
//   octave (the comb the sound is pulled onto), each glowing as it sounds.
// - The handles: a line each with a grip and its frequency; drag one across
//   (Shift: finely), or the band between them to move both, one undo step a
//   drag; a double-click on a handle resets it, elsewhere in the band both.
//   Outside the band the view is shaded.
// The analyzer is the EQ's (analysis::EqAnalyzer: tilted 4.5 dB/octave, rising
// fast, falling slowly). It repaints only while something moves.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include "analysis/Spectrum.h"

#include <QtQml/qqmlregistration.h>

namespace sub::ui {

class HueSpectrum : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool animating READ animating NOTIFY animatingChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(QString hovered READ hovered NOTIFY hoveredChanged)  // "start", "end", "band" or ""

public:
    static constexpr int kMinimumWidth = 160;
    static constexpr int kMinimumHeight = 96;
    static constexpr double kLowHz = 30.0;
    static constexpr double kHighHz = 12000.0;
    static constexpr double kGrabPixels = 6.0;  // a handle's reach either side

    explicit HueSpectrum(QQuickItem* parent = nullptr);

    bool animating() const { return animating_; }
    bool dragging() const { return !gesture_.isEmpty(); }
    QString hovered() const { return hovered_; }
    // Where a frequency is across the view, and the frequency at a point across.
    Q_INVOKABLE double xOf(double hz) const;
    Q_INVOKABLE double hzAt(double x) const;
    // What a point would grab: "start", "end", "band" or "".
    Q_INVOKABLE QString partAt(QPointF point) const;
    // The output's level shown at a frequency (dB, tilted as drawn).
    Q_INVOKABLE double levelDbAt(double hz) const;

Q_SIGNALS:
    void animatingChanged();
    void draggingChanged();
    void hoveredChanged();

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    LogAxis axis() const;
    QRectF plot() const;
    double yOf(double db) const;
    void setHovered(const QString& part);
    void endDrag();

    // The parameters (sync()).
    double start_ = 240.0, end_ = 2400.0;
    int notes_ = 0;
    bool midi_ = false;
    int held_ = 0;

    sub::app::analysis::EqAnalyzer analyzer_;
    Eased startShown_, endShown_;  // the handles, easing to a value set elsewhere (snapping while dragged)
    bool animating_ = false;
    bool started_ = false;
    double glow_ = 0.0;  // the handles' highlight while hovered or dragged (0..1)

    // Dragging.
    QString gesture_;
    QString part_;  // what is dragged
    double pressX_ = 0.0;
    double dragStart_ = 0.0, dragEnd_ = 0.0;  // at the press
    QString hovered_;
};

}  // namespace sub::ui
