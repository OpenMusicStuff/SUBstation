#pragma once

// Hue's spectrum, its editor's face: what goes in and what comes out, 30 Hz to
// 12 kHz, the band it processes between Low Cut and High Cut (its parameters
// Start and End) as two handles to drag, and Color as a drag up and down.
// - The output as a fill coloured by pitch: each frequency in the hue of the
//   note it is (HueWheel::noteColor, blended between notes), as vivid as Color
//   makes the sound (grey at 0 %, glowing past 100 %), so the partials pulled
//   into key stand out in their notes' colours; outside the band it is grey. A
//   glowing line along its top; the input as a faint line behind.
// - Inside the band, the notes in use (the key's, or the MIDI notes once they
//   choose them: the display "notes") as thin lines in their hues at every
//   octave (the comb the sound is pulled onto), each brighter as it sounds.
// - The handles, at the bottom: a line each with a grip and its frequency; drag
//   one across (Shift: finely), one undo step a drag; a double-click resets it.
//   Outside the band the view is shaded.
// - Anywhere else, drag up for more Color, down for less (Shift: finely); a
//   double-click resets it. A gauge at the right edge shows it, and its value
//   while hovered or dragged.
// The editor lays its buttons over the top edge, clear of all this. The analyzer
// is the EQ's (analysis::EqAnalyzer: tilted 4.5 dB/octave, rising fast, falling
// slowly). It repaints only while something moves.

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
    Q_PROPERTY(QString hovered READ hovered NOTIFY hoveredChanged)  // "start", "end", "color" or ""
    Q_PROPERTY(bool midi READ midi NOTIFY midiChanged)  // MIDI notes choose the notes (the display "notes" says)

public:
    static constexpr int kMinimumWidth = 200;
    static constexpr int kMinimumHeight = 96;
    static constexpr double kLowHz = 30.0;
    static constexpr double kHighHz = 12000.0;
    static constexpr double kGrabPixels = 6.0;          // a handle's reach either side
    static constexpr double kColorPerPixel = 1.2;      // % dragged up
    static constexpr double kHandleBand = 34.0;         // the handles' grips and frequencies: this far from the bottom
    static constexpr int kFromMidi = 1 << 12;          // in the display "notes": the notes are MIDI's

    explicit HueSpectrum(QQuickItem* parent = nullptr);

    bool animating() const { return animating_; }
    bool dragging() const { return !gesture_.isEmpty(); }
    QString hovered() const { return hovered_; }
    bool midi() const { return midi_; }
    // Where a frequency is across the view, and the frequency at a point across.
    Q_INVOKABLE double xOf(double hz) const;
    Q_INVOKABLE double hzAt(double x) const;
    // What a point would grab: "start", "end", "color" or "" (outside the view).
    Q_INVOKABLE QString partAt(QPointF point) const;
    // The output's level shown at a frequency (dB, tilted as drawn).
    Q_INVOKABLE double levelDbAt(double hz) const;

Q_SIGNALS:
    void animatingChanged();
    void draggingChanged();
    void hoveredChanged();
    void midiChanged();

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
    double start_ = 240.0, end_ = 2400.0, color_ = 100.0;
    int buttons_ = 0;          // the note buttons
    int shownNotes_ = -1;      // the display's (bits, and kFromMidi), -1 before any
    bool midi_ = false;        // the notes are MIDI's

    sub::app::analysis::EqAnalyzer analyzer_;
    Eased startShown_, endShown_, colorShown_;  // easing to a value set elsewhere (snapping while dragged)
    bool animating_ = false;
    bool started_ = false;
    double glow_ = 0.0;  // the handles' and the gauge's highlight while hovered or dragged (0..1)

    // Dragging.
    QString gesture_;
    QString part_;  // what is dragged
    double lastX_ = 0.0, lastY_ = 0.0;
    double dragValue_ = 0.0;  // the dragged parameter, unclamped while past its ends
    QString hovered_;
};

}  // namespace sub::ui
