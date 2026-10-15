#pragma once
// Swapping in similar sounds: the Sampler's arrows under its waveform (and
// each of a Drum Rack's pads, its Sampler's) step through the sounds most like
// its reference (SoundSimilarity's best, best first), and a Drum Rack's arrows
// step every pad but the locked ones at once. The reference is the sample the
// stepping started from, unless one was saved (model/SamplerState.h): the
// right-click menus save the sample playing as it, and go back to it.
// Session.similarSamples.
//
// The index searches one sound at a time and hands out only the latest
// search's result (its `found`), which the browser's Find Similar shares: the
// searches a step needs go one after another, each re-asked if another search
// took its place, and their results are kept (until the library's analysis
// moves on), so stepping back and forth asks no more.

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <deque>
#include <optional>

namespace sub::app {

class Project;
class ProjectEditor;
class SimilarSounds;
class SoundSimilarity;

class SimilarSamples : public QObject {
    Q_OBJECT
    // A step waits for its searches.
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)

public:
    static constexpr int kCount = 50;    // the similar sounds a reference steps through
    static constexpr int kRetries = 3;   // asks of a search another search took the place of

    SimilarSamples(Project* project, ProjectEditor* editor, SoundSimilarity* similarity, QObject* parent = nullptr);

    // A Sampler steps to the next (+1) or previous (-1) sound like its
    // reference; past the first, back to the reference itself. One undo step.
    Q_INVOKABLE void step(const QString& trackId, const QString& deviceId, int direction);
    // Every pad of a Drum Rack but the locked ones steps (each its Sampler, from its own reference). One undo step.
    Q_INVOKABLE void stepRack(const QString& trackId, const QString& rackId, int direction);
    // The Sampler's sample becomes its reference (Save as Similar Reference).
    Q_INVOKABLE void saveReference(const QString& trackId, const QString& deviceId);
    // The Sampler plays its reference again (Return to Reference). One undo step.
    Q_INVOKABLE void returnToReference(const QString& trackId, const QString& deviceId);
    // The same for every pad of a Drum Rack but the locked ones (one undo step for them all).
    Q_INVOKABLE void saveRackReferences(const QString& trackId, const QString& rackId);
    Q_INVOKABLE void returnRackToReferences(const QString& trackId, const QString& rackId);
    // Whether the Sampler has a reference other than the sample it plays (Return to Reference is enabled).
    Q_INVOKABLE bool canReturn(const QString& trackId, const QString& deviceId) const;
    // Whether any of a Drum Rack's unlocked pads can.
    Q_INVOKABLE bool rackCanReturn(const QString& trackId, const QString& rackId) const;
    // A Drum Rack's unlocked pads' Samplers with a sample (the devices its arrows step).
    QStringList rackSamplers(const QString& trackId, const QString& rackId) const;

    bool searching() const { return searching_.has_value(); }
    // Drops what was found (the library changed); tests.
    void forget() { found_.clear(); }

Q_SIGNALS:
    void searchingChanged();
    void statusMessage(const QString& message);

private:
    struct Request {
        QString trackId;
        QStringList devices;  // Samplers
        int direction = 1;
        QString text;          // its undo step's
    };
    struct Search {
        QString path;
        uint64_t generation = 0;
        int asks = 1;
    };

    void enqueue(Request request);
    void pump();  // runs the requests whose sounds are found; searches for the next missing one
    void onFound(const SimilarSounds& result);
    void run(const Request& request);
    QString referenceOf(const QString& trackId, const QString& deviceId) const;
    QString sampleOf(const QString& trackId, const QString& deviceId) const;
    static QString key(const QString& path);

    Project* project_;
    ProjectEditor* editor_;
    QPointer<SoundSimilarity> similarity_;
    std::deque<Request> requests_;
    std::optional<Search> searching_;
    QHash<QString, QStringList> found_;  // by key(): the reference's similar sounds, best first
    int foundWhenAnalysed_ = -1;         // the library's analysed files when found_ was filled
};

}  // namespace sub::app
