#pragma once

// Hue's display: the twelve pitch classes round a colour wheel (C at the top,
// clockwise by semitone, each note in its own hue), as the device treats them:
// - the enabled notes lit in their hues (brighter as Color rises), the notes
//   taken out dark; a note switched on pops out and sends a ring off;
// - a note moved onto an enabled neighbour shades into that one's hue, an arrow
//   inside the ring bends onto it, and while it sounds sparks flow along the
//   arrow (steady in High Quality; wavering a little in Low Latency, as its
//   shifter does);
// - inside, the sound's chromagram between Start and End (analysis::Chroma): the
//   output as petals in each note's hue, the input as a faint outline behind
//   them, so what Hue pulled into key shows against what came in; in the middle
//   a bloom in the colour the sound is now (its notes' hues, as loud as each).
// Once MIDI notes choose the notes it shows them (the device's display "notes").
// Click a note to switch it on or off (one undo step); hover shows it.
//
// Everything moves on in refreshDisplays() by the time since the last tick; it
// repaints only while something moves, and rests once the sound has died away.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include "analysis/Chroma.h"

#include <QColor>
#include <QtQml/qqmlregistration.h>

#include <array>

namespace sub::ui {

class HueWheel : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int notes READ notes NOTIFY notesChanged)  // the notes shown (bits, C the lowest)
    Q_PROPERTY(bool midi READ midi NOTIFY notesChanged)
    Q_PROPERTY(bool highQuality READ highQuality NOTIFY notesChanged)
    Q_PROPERTY(QString caption READ caption WRITE setCaption NOTIFY captionChanged)  // in the middle: the key
    Q_PROPERTY(int hoveredNote READ hoveredNote NOTIFY hoveredNoteChanged)           // -1: none
    Q_PROPERTY(bool animating READ animating NOTIFY animatingChanged)

public:
    static constexpr int kMinimumSize = 110;
    static constexpr int kClasses = 12;
    static constexpr double kFloorDb = -66.0;  // the chromagram's floor: quieter is dark
    static constexpr int kFromMidi = 1 << 12;  // in the display "notes": the notes are MIDI's

    explicit HueWheel(QQuickItem* parent = nullptr);

    int notes() const { return notes_; }
    bool midi() const { return midi_; }
    bool highQuality() const { return highQuality_; }
    QString caption() const { return caption_; }
    void setCaption(const QString& caption);
    int hoveredNote() const { return hovered_; }
    bool animating() const { return animating_; }

    // A pitch class's colour: its hue round the wheel.
    Q_INVOKABLE static QColor noteColor(int pitchClass);
    // A pitch class's name ("C#").
    Q_INVOKABLE static QString noteName(int pitchClass);
    // The parameter of a pitch class's button ("note_cs").
    Q_INVOKABLE static QString noteParam(int pitchClass);
    // Where a note set moves a pitch class: +1 up, -1 down (onto the lower neighbour when both are on), 0 none.
    Q_INVOKABLE static int moveOf(int notes, int pitchClass);
    // Sets the twelve note buttons to `notes` (bits), one undo step named `text`.
    Q_INVOKABLE void setNotes(int notes, const QString& text = QStringLiteral("Change Notes"));
    // Switches a note's button (not while MIDI notes choose them).
    Q_INVOKABLE void toggleNote(int pitchClass);
    // The chromagram as shown (dB): the output's and the input's.
    Q_INVOKABLE double levelDb(int pitchClass) const;
    Q_INVOKABLE double inputLevelDb(int pitchClass) const;
    // The pitch class under a point (-1: none: the middle, or outside the ring).
    Q_INVOKABLE int noteAt(QPointF point) const;

    QPointF center() const;
    double radius() const;  // the ring's outer edge

Q_SIGNALS:
    void notesChanged();
    void captionChanged();
    void hoveredNoteChanged();
    void animatingChanged();

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    void showNotes(int notes);   // what is drawn follows a new set of notes
    bool advance(double dt);     // moves every animation on: whether anything moved
    void setHovered(int note);
    QPointF pointAt(double angle, double radius) const;  // angle: in pitch classes, clockwise from the top

    // The parameters (sync()).
    int buttons_ = 0;  // the note buttons
    int held_ = 0;     // the MIDI notes held (the display "notes")
    int notes_ = 0;    // what is shown: one or the other
    bool midi_ = false;  // the notes are MIDI's (the display says)
    bool highQuality_ = false;
    double color_ = 100.0, start_ = 240.0, end_ = 2400.0;
    QString caption_;

    // What eases (refreshDisplays()).
    std::array<Eased, kClasses> on_{};     // lit (0..1)
    std::array<Eased, kClasses> moved_{};  // moved onto a neighbour (0..1)
    std::array<int, kClasses> direction_{};  // where it moves (kept while it fades out)
    std::array<double, kClasses> pulse_{};   // a note just switched on: 1, falling to 0
    std::array<double, kClasses> flow_{};    // the sparks' place along a moved note's arrow (0..1)
    std::array<Eased, kClasses> out_{}, in_{};  // the chromagram's brightness (0..1)
    Eased quality_;  // 1: High Quality (the arrows steady)
    Eased colorShown_;
    double time_ = 0.0;
    double sinceAnalysis_ = 0.0;
    sub::app::analysis::Chroma chroma_;
    int hovered_ = -1;
    bool animating_ = false;
    bool started_ = false;
};

}  // namespace sub::ui
