#pragma once

// GigaTune's display, as a tuner's: the note the voice is corrected to (its name, large; under it the note that
// comes out, while Pitch moves it), a cents scale (-50 to +50 of that note) with the pitch heard as a needle and
// where the correction takes it as a lit dot, the stretch between them the correction being made, a band round
// 0 that glows while the result is in tune, and along the bottom the twelve notes, those the key's scale corrects
// to lit and the one being corrected to glowing. It reads the displays `pitch`, `corrected` and `note` (one
// value per 256 samples, as the output plays them), and the parameters Key, Scale and Pitch (with the song's key,
// `songTonic` and `songMinor`, for Song).
//
// Its animation, in refreshDisplays() by tickSeconds(): the needle and the dot ease to the newest values (snapping
// when the note changes, or the voice comes in), the note's name slides and fades into the next, the notes along
// the bottom light at once and fade, and everything but the scale fades out while no voice is heard (nothing
// came for kSilentSeconds counts as none). It repaints only while something moves.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QtQml/qqmlregistration.h>

#include <array>

namespace sub::ui {

class GigaTuneMeter : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    // The song's key, for Key or Scale at Song: its tonic (0 = C; -1 none known) and whether it is minor.
    Q_PROPERTY(int songTonic READ songTonic WRITE setSongTonic NOTIFY songKeyChanged)
    Q_PROPERTY(bool songMinor READ songMinor WRITE setSongMinor NOTIFY songKeyChanged)
    Q_PROPERTY(int notes READ notes NOTIFY notesChanged)        // the notes corrected to (bit c: pitch class c)
    Q_PROPERTY(int note READ note NOTIFY heardChanged)          // the note corrected to (MIDI; -1 none)
    Q_PROPERTY(bool voiced READ voiced NOTIFY heardChanged)

public:
    static constexpr int kWidth = 156;
    static constexpr int kMinimumHeight = 104;
    static constexpr double kRangeCents = 50.0;      // either side of the note
    static constexpr double kInTuneCents = 5.0;      // the band round 0
    static constexpr double kRecentSeconds = 0.02;   // the newest values read each tick
    static constexpr double kSilentSeconds = 0.25;   // nothing came for this long: no voice

    explicit GigaTuneMeter(QQuickItem* parent = nullptr);

    int songTonic() const { return songTonic_; }
    void setSongTonic(int tonic);
    bool songMinor() const { return songMinor_; }
    void setSongMinor(bool minor);
    int notes() const { return notes_; }
    int note() const { return note_; }
    bool voiced() const { return voiced_; }
    // Eased, for tests: the needle's and the dot's cents, how present the voice is (0..1).
    double needleCents() const { return needle_.value; }
    double dotCents() const { return dot_.value; }
    double presence() const { return presence_.value; }

    // The newest values the displays reported (pitch 0: unvoiced), and `dt` seconds on: the targets move, then
    // everything eases. True while anything moves.
    bool advance(bool came, float pitch, float corrected, float note, double dt);

Q_SIGNALS:
    void songKeyChanged();
    void notesChanged();
    void heardChanged();

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;

private:
    void readNotes();

    int songTonic_ = -1;
    bool songMinor_ = false;
    int key_ = 0, scale_ = 0, pitchShift_ = 0;
    int notes_ = 0xfff;
    bool voiced_ = false;
    int note_ = -1;          // the note corrected to (the last while none is heard: it fades)
    int previousNote_ = -1;  // the one it slides away from
    Eased slide_;            // 0..1: from previousNote_ to note_
    double heardCents_ = 0.0, correctedCents_ = 0.0;  // of note_, the newest
    Eased needle_, dot_;     // the cents shown
    Eased presence_;         // 0..1: a voice heard
    Eased inTune_;           // 0..1: the band's glow
    std::array<Eased, 12> glow_{};  // the notes along the bottom
    double silent_ = 0.0;    // seconds since a value came
};

}  // namespace sub::ui
