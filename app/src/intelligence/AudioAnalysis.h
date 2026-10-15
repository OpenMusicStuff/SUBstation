#pragma once
// Tempo, key and chords of audio files (the intelligence module's audio/,
// sub_intelligence) on the application thread's side: Session.audioAnalysis.
//
// Each file is analysed once, after the engine bridge has decoded it (its
// samples, at the engine's rate), on a thread of its own at background
// priority, one file at a time: a loop in a few tens of milliseconds, a song in
// a few hundred. Results are kept by file (pathIdentity) for as long as the
// session lives; nothing is saved: a project opened again is analysed again as
// its files decode. What asks for a file's analysis (request()) hears of it on
// analysed(), on the application's thread, or at once when it is known.
//
// What it is for:
// - audio dragged into the arrangement whose name gives no tempo is warped at
//   the tempo it is heard to have (ArrangementActions: the clips as they were
//   added, one undo step "Detect Tempo"): a long file, or a short one as long as
//   whole bars at that tempo (a loop); a one-shot stays as it is;
// - an audio clip's chords: drawn along it in the arrangement, transposed as
//   it plays (clipChords), and heard in the song's chords (Harmony);
// - the clip view shows a clip's file's tempo and key.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "harmony/Chords.h"
#include "model/Keys.h"

namespace sub::app {

class EngineBridge;
struct Clip;

class AudioAnalysis : public QObject {
    Q_OBJECT

public:
    // A chord the file holds, in seconds of the file.
    struct Chord {
        double start = 0.0;
        double end = 0.0;
        intelligence::harmony::Chord chord;
    };
    // What a file was heard to hold.
    struct Result {
        std::optional<double> bpm;  // none: no steady beat
        double tempoConfidence = 0.0;
        double firstBeatSec = 0.0;  // where its beats fall: this, and every beat from it
        std::optional<Key> key;     // none: too little pitched sound to tell
        std::vector<Chord> chords;  // by time
    };
    // A chord along a clip: where it plays on the timeline, and its name as the
    // clip plays it (transposed).
    struct ClipChord {
        double startBeat = 0.0;
        double endBeat = 0.0;
        intelligence::harmony::Chord chord;

        friend bool operator==(const ClipChord&, const ClipChord&) = default;
    };

    explicit AudioAnalysis(EngineBridge* bridge, QObject* parent = nullptr);
    ~AudioAnalysis() override;

    // A file's analysis: once it is decoded and analysed, analysed(path) (at
    // once, if it was already), and `then` with its result (none: it couldn't be
    // decoded, or this goes first).
    void request(const QString& path, std::function<void(const Result*)> then = {});
    // What a file was heard to hold; null until it is analysed.
    const Result* result(const QString& path) const;
    // Whether it is being analysed (or decoded for it).
    bool isPending(const QString& path) const;

    // A clip's chords on the timeline: its file's chords in the part of it the
    // clip plays, where they play at `tempo`, transposed as the clip plays (its
    // transposition; warped in Re-Pitch, by how much faster it plays).
    static std::vector<ClipChord> clipChords(const Result& result, const Clip& clip, double tempo);
    // How many semitones a clip plays its audio up (Transpose, or a Re-Pitch
    // clip's speed), rounded to a semitone.
    static int clipShift(const Clip& clip, double tempo);
    // What a clip's file was heard to hold, for the clip view: {bpm (0: none),
    // key ("A Minor"; "": none), analysed, pending}.
    Q_INVOKABLE QVariantMap fileInfo(const QString& path) const;

    // For tests: analyse every file with this instead (on the same thread).
    using Analyser = std::function<Result(const QString& path)>;
    void setAnalyser(Analyser analyser) { analyser_ = std::move(analyser); }
    // Waits for the analyses under way (tests).
    void waitForDone();
    // Off: nothing is analysed (every request hears of none at once).
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

Q_SIGNALS:
    void analysed(const QString& path);

private:
    void start(const QString& path);
    void finish(const QString& key, std::optional<Result> result);

    QPointer<EngineBridge> bridge_;
    QThreadPool pool_;
    Analyser analyser_;
    bool enabled_ = true;
    QHash<QString, Result> results_;   // by pathIdentity
    QSet<QString> failed_;             // couldn't be decoded or analysed
    QHash<QString, std::vector<std::function<void(const Result*)>>> waiting_;  // pending, by pathIdentity
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace sub::app
