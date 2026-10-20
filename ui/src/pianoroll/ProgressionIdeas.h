#pragma once

// The clip view's progression ideas (Generate ▾ › Progressions…, the panel
// beside the piano roll): chord progressions in a mood to try in the lead
// clip, made by the intelligence module (harmony/Progressions.h: the mood's
// own progressions, free-midi-chords', and drawn ones, from how chords follow
// each other in the Billboard songs).
//
// It holds a few ideas (kIdeas), each a chord a bar, with a lock on each chord
// and a star for the idea (kept). New Ideas keeps the kept ones as they are,
// draws again what isn't locked in those with locked chords, and puts fresh
// ideas in place of the rest; More Like This keeps the kept ones and that one,
// and puts ideas one or two chords away from it in place of the rest. A chord
// can be swapped (alternatives(): its colours, then the chords that best lead
// from the one before it into the one after). They are made in the song's key
// (the project's, else the one its MIDI is most likely in, else A minor),
// which each idea keeps: its Roman numerals are in it. With Sevenths, each
// idea's triads show and play with their sevenths as the key has them.
//
// Try writes an idea into the lead clip (PianoRoll::writeProgression), in
// place of the one tried in it before: a run of tries is one undo step, each
// written over the clip as it was before the first (the base). If the clip
// changed since its last try (an edit, an undo), the base is the clip as it is
// now, without the notes that try wrote. Each clip's last try is kept (tries in
// another clip in between don't count). The idea the clip plays is tried(); an
// idea changed while it is (a chord swapped, Sevenths, Bass) is written again.
// The arrow keys step through the ideas, trying each.
//
// It is a list model of the ideas, for QML: "chords" (each {"name", "numeral",
// "locked"}), "kept", "tried" and "source" (where it came from, for a
// tooltip). A change sends only the rows it changed (publish()): the others'
// delegates stay as they are.

#include "harmony/Progressions.h"
#include "pianoroll/PianoRoll.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace sub::ui {

class ProgressionIdeas : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(sub::ui::PianoRoll* roll READ roll WRITE setRoll NOTIFY rollChanged)
    Q_PROPERTY(QStringList moods READ moods CONSTANT)
    Q_PROPERTY(QList<int> lengths READ lengths CONSTANT)
    // The mood (an index of moods()) and the length (chords, a bar each) new ideas are made in.
    Q_PROPERTY(int mood READ mood WRITE setMood NOTIFY settingsChanged)
    Q_PROPERTY(int length READ length WRITE setLength NOTIFY settingsChanged)
    // Triads with their sevenths; Try writes a bass line too.
    Q_PROPERTY(bool sevenths READ sevenths WRITE setSevenths NOTIFY settingsChanged)
    Q_PROPERTY(bool bass READ bass WRITE setBass NOTIFY settingsChanged)
    Q_PROPERTY(int count READ count NOTIFY ideasChanged)
    // The idea the arrow keys are on, and the one the clip plays (-1: none).
    Q_PROPERTY(int current READ current NOTIFY ideasChanged)
    Q_PROPERTY(int tried READ tried NOTIFY ideasChanged)
    // The key the newest ideas are in ("A minor"), and whether it was the song's MIDI's guess (none set: inferred)
    // or A minor for want of one (assumed).
    Q_PROPERTY(QString keyName READ keyName NOTIFY ideasChanged)
    Q_PROPERTY(QString keySource READ keySource NOTIFY ideasChanged)

public:
    enum Role {
        ChordsRole = Qt::UserRole + 1,
        KeptRole,
        TriedRole,
        SourceRole,
    };

    static constexpr int kIdeas = 6;
    static constexpr int kAlternatives = 8;
    // A run of tries in one clip is one undo step: this and the clip's id (another clip's are its own).
    inline static const QString kMergeKey = QStringLiteral("progression-ideas:");

    explicit ProgressionIdeas(QObject* parent = nullptr);

    PianoRoll* roll() const { return roll_; }
    void setRoll(PianoRoll* roll);

    static QStringList moods();
    static QList<int> lengths() { return {4, 8}; }
    int mood() const { return static_cast<int>(mood_); }
    void setMood(int mood);  // new ideas in it
    int length() const { return length_; }
    void setLength(int length);  // new ideas of it
    bool sevenths() const { return sevenths_; }
    void setSevenths(bool on);
    bool bass() const { return bass_; }
    void setBass(bool on);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(rows_.size()); }
    int current() const { return current_; }
    int tried() const { return tried_; }
    QString keyName() const;
    QString keySource() const { return keySource_; }

    // The chords an idea plays (with Sevenths: triads with their sevenths).
    std::vector<intelligence::harmony::Chord> played(int row) const;
    // An idea's chords as made (none: no such row).
    std::vector<intelligence::harmony::Chord> chords(int row) const;
    bool isLocked(int row, int chord) const;
    bool isKept(int row) const;

    // New ideas: kept ones stay, locked chords stay (the rest of their idea drawn again), the rest fresh.
    // With none yet, as many fresh ones as there are rows.
    Q_INVOKABLE void newIdeas();
    // Ideas no more than one or two chords away from `row`'s in place of all but it and the kept ones.
    Q_INVOKABLE void moreLike(int row);
    Q_INVOKABLE void toggleLock(int row, int chord);
    Q_INVOKABLE void toggleKept(int row);
    // What a chord could be swapped for: its colours ({"name", "numeral", "colour": true}), then the chords
    // that lead best from the one before it into the one after ("colour": false), up to kAlternatives.
    Q_INVOKABLE QVariantList alternatives(int row, int chord) const;
    // Swap a chord for alternatives(row, chord)[index].
    Q_INVOKABLE void swap(int row, int chord, int index);
    // Write an idea into the lead clip (see the top); it becomes current.
    Q_INVOKABLE void tryIdea(int row);
    // The arrow keys: the idea `delta` on from the current one (none yet: the first), tried.
    Q_INVOKABLE void step(int delta);
    // No ideas (open() makes new ones), and what was tried forgotten.
    Q_INVOKABLE void clear();
    // New ideas' randomness starts over from `seed` (for tests).
    Q_INVOKABLE void seedRandom(quint64 seed) { seed_ = seed; }

Q_SIGNALS:
    void rollChanged();
    void settingsChanged();
    void ideasChanged();
    // Something couldn't be done, and why.
    void statusMessage(const QString& message);

private:
    struct Row {
        intelligence::harmony::Idea idea;
        intelligence::harmony::Key key;  // the key it was made in
        intelligence::harmony::Mood mood = intelligence::harmony::Mood::Sad;  // ... and the mood
        std::vector<bool> locked;        // by chord
        bool kept = false;
    };

    // The settings new ideas are made with: the song's key now (keySource_ says where it came from).
    intelligence::harmony::ProgressionSettings settings();
    std::vector<std::vector<intelligence::harmony::Chord>> chordsBut(int row) const;
    Row rowOf(const intelligence::harmony::Idea& idea, const intelligence::harmony::Key& key) const;
    // tried() is -1 once that idea no longer plays what was written.
    void refreshTried();
    // The alternatives' chords, as alternatives() lists them.
    std::vector<std::pair<intelligence::harmony::Chord, bool>> alternativeChords(int row, int chord) const;
    QString sourceText(const Row& row) const;
    // An idea as the model hands it to QML (its roles, by name).
    QVariantMap rowData(int row) const;
    // Tell QML what changed since the last time: the rows whose data did, rows
    // added or taken away at the end; and ideasChanged().
    void publish();
    // The tried idea changed: write it again.
    void retry();
    // The clip changed: is it still what the last try left?
    void clipChanged();

    QPointer<PianoRoll> roll_;
    intelligence::harmony::Mood mood_ = intelligence::harmony::Mood::Sad;
    int length_ = 4;
    bool sevenths_ = false;
    bool bass_ = false;
    std::vector<Row> rows_;
    std::vector<QVariantMap> published_;  // each row's data as QML last had it
    int current_ = -1;
    int tried_ = -1;
    quint64 seed_ = 0;
    std::optional<intelligence::harmony::Key> key_;  // the newest ideas'
    QString keySource_;
    // Each clip's last try (by clipKey()): what it wrote over, and what it left.
    struct Tried {
        PianoRoll::ClipBody base;
        PianoRoll::ClipBody written;
    };
    QHash<QString, Tried> tries_;
    // The lead clip's key in tries_ ("" without one).
    QString clipKey() const;
    // The clip tried() was tried in, and the chords it wrote.
    QString triedClip_;
    std::vector<intelligence::harmony::Chord> triedChords_;
    bool triedBass_ = false;
    bool writing_ = false;
};

}  // namespace sub::ui
