#pragma once

// A Drum Rack as its body shows it: sixteen pads, four by four, the lowest key
// at the bottom left (as Ableton's), from `firstNote` (C1 at first; the
// arrows beside them, or the wheel, move the pads a row of four keys at a
// time), and an overview of all 128 keys, lit where a pad has a sample. Each
// pad shown, as `pads` lists them (left to right, top row first): its key and
// its name, and the chain playing it (none: an empty pad), muted, soloed,
// locked (similar sounds leave its sample be), selected (the device view shows
// its devices beside the rack). What its pads do goes through the editor:
// load a sample (a drop), mute, solo, lock, delete; a click selects the pad
// and plays its key (previewNote).
//
//   DrumRack { id: drums; session: Session; trackId: ...; rackId: ... }
//   Repeater { model: drums.pads; DrumPadItem { drumRack: drums; note: modelData.note } }

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "session/Session.h"

namespace sub::ui {

class DrumRack : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(sub::app::Session* session READ session WRITE setSession NOTIFY targetChanged)
    Q_PROPERTY(QString trackId READ trackId WRITE setTrackId NOTIFY targetChanged)
    Q_PROPERTY(QString rackId READ rackId WRITE setRackId NOTIFY targetChanged)
    // The key of the bottom left pad (a multiple of 4, 0 to 112).
    Q_PROPERTY(int firstNote READ firstNote WRITE setFirstNote NOTIFY firstNoteChanged)
    // The sixteen pads shown, top row first: {note, noteName, chainId, name, sample, mute, solo, locked, selected}.
    Q_PROPERTY(QVariantList pads READ pads NOTIFY changed)
    // Every key, 0 to 127: whether a pad has it (the overview).
    Q_PROPERTY(QVariantList filled READ filled NOTIFY changed)
    // Its pads with a sample, and how many of those are locked.
    Q_PROPERTY(int padCount READ padCount NOTIFY changed)
    Q_PROPERTY(int lockedCount READ lockedCount NOTIFY changed)

public:
    static constexpr int kColumns = 4;
    static constexpr int kPads = 16;
    static constexpr int kLastFirstNote = 128 - kPads;

    explicit DrumRack(QObject* parent = nullptr);

    sub::app::Session* session() const { return session_; }
    void setSession(sub::app::Session* session);
    QString trackId() const { return trackId_; }
    void setTrackId(const QString& trackId);
    QString rackId() const { return rackId_; }
    void setRackId(const QString& rackId);
    int firstNote() const { return firstNote_; }
    void setFirstNote(int note);
    QVariantList pads() const { return pads_; }
    QVariantList filled() const { return filled_; }
    int padCount() const { return padCount_; }
    int lockedCount() const { return lockedCount_; }

    // A pad's chain ("": an empty pad).
    Q_INVOKABLE QString chainOf(int note) const;
    // Loads a sample (a part of one: fractions of the file) onto a pad. Its chain.
    Q_INVOKABLE QString loadSample(int note, const QString& path, double start = 0.0, double end = 1.0);
    // A click: the pad's devices show beside the rack (if it has a chain), and its key plays (`velocity` 0: released).
    Q_INVOKABLE void select(int note);
    Q_INVOKABLE void play(int note, int velocity);
    Q_INVOKABLE void toggleMute(int note);
    Q_INVOKABLE void toggleSolo(int note);
    Q_INVOKABLE void toggleLock(int note);
    Q_INVOKABLE void remove(int note);
    // The pad's Sampler ("": none), for its similar sounds.
    Q_INVOKABLE QString samplerOf(int note) const;
    // The pads up (+1) or down (-1) a row of keys.
    Q_INVOKABLE void scroll(int rows);
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void targetChanged();
    void firstNoteChanged();
    void changed();

private:
    const sub::app::Device* rack() const;

    QPointer<sub::app::Session> session_;
    QString trackId_;
    QString rackId_;
    int firstNote_ = 36;
    QVariantList pads_;
    QVariantList filled_;
    int padCount_ = 0;
    int lockedCount_ = 0;
    QList<QMetaObject::Connection> connections_;
};

// A pad's drop target: an audio file (from the browser, the desktop) or a
// clip dragged out of the arrangement dropped on it loads onto its key. Other
// drags (devices, presets) go on to the device view (onto a pad with a chain:
// into its chain, as onto a chain's row: QML gives it the "chainRow" role then).
class DrumPadItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(sub::ui::DrumRack* drumRack READ drumRack WRITE setDrumRack NOTIFY drumRackChanged)
    Q_PROPERTY(int note READ note WRITE setNote NOTIFY noteChanged)
    Q_PROPERTY(bool dropping READ dropping NOTIFY droppingChanged)  // an audio drag is over it

public:
    explicit DrumPadItem(QQuickItem* parent = nullptr);

    DrumRack* drumRack() const { return drumRack_; }
    void setDrumRack(DrumRack* rack);
    int note() const { return note_; }
    void setNote(int note);
    bool dropping() const { return dropping_; }

Q_SIGNALS:
    void drumRackChanged();
    void noteChanged();
    void droppingChanged();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setDropping(bool dropping);

    QPointer<DrumRack> drumRack_;
    int note_ = 0;
    bool dropping_ = false;
};

}  // namespace sub::ui
