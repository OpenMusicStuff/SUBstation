#pragma once
// Finding a track by what it is called or what it is: Session.trackSearch, the
// arrangement's find bar (Q).
//
// A query matches a track when every word typed is in what is searched, in any
// case. The best first: tracks whose name has the words at the start of its
// words, then those whose label has them so ("Orchestral Horn"), then those
// whose name has them anywhere ("Thorn"), then those whose label, name and what
// the label was made from (its instrument and preset, its files, its effects:
// TrackLabels' details, and its family, role and traits) have them, at the
// start of their words, then anywhere. Within each, the arrangement's order:
// its tracks top to bottom (those folded away in groups too), the returns, the
// master. So "horn" finds a track called Horns first, then BBC Symphony
// Orchestra with its Horns preset.
//
// Started from the clip view, the search has an area: the beats the clips it
// shows span (setArea()). A match with a clip there opens in the clip view
// (clipInArea()), the others in the arrangement.
//
// The matches are worked out again whenever the labels change (a rename, a
// preset picked: TrackLabels' changed) and before stepping to the next one,
// staying on the track they were on.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <optional>
#include <utility>

namespace sub::app {

class Project;
class TrackLabels;

class TrackSearch : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    // The tracks that match (ids), the best first.
    Q_PROPERTY(QStringList matches READ matches NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    // The match shown (an index in matches; -1: none), and its track ("": none).
    Q_PROPERTY(int current READ current NOTIFY changed)
    Q_PROPERTY(QString currentTrackId READ currentTrackId NOTIFY changed)

public:
    TrackSearch(Project* project, TrackLabels* labels, QObject* parent = nullptr);

    QString query() const { return query_; }
    // Searches again: the best match is the current one.
    void setQuery(const QString& query);
    QStringList matches() const { return matches_; }
    int count() const { return static_cast<int>(matches_.size()); }
    int current() const { return current_; }
    QString currentTrackId() const { return current_ >= 0 ? matches_.at(current_) : QString(); }

    // Enter: the next match (after the last, the first again); Shift+Enter: the
    // one before. The track it is ("": none matches).
    Q_INVOKABLE QString next();
    Q_INVOKABLE QString previous();

    // The area: from the earliest start to the latest end of these clips (as
    // the clip view is given them: ids on `trackId`, or {trackId, clipId}
    // maps); none if none of them is there.
    Q_INVOKABLE void setArea(const QString& trackId, const QVariantList& clipIds);
    Q_INVOKABLE void clearArea() { area_.reset(); }
    std::optional<std::pair<double, double>> area() const { return area_; }
    // The clip of a track that overlaps the area most (the earliest of those
    // overlapping as much); "" if none does, or there is no area.
    Q_INVOKABLE QString clipInArea(const QString& trackId) const;

    // The tracks of `project` that match `query`, the best first.
    static QStringList find(const Project& project, const TrackLabels& labels, const QString& query);

Q_SIGNALS:
    void changed();

private:
    // Searches again, staying on the current match's track if it still matches (else the best).
    void refresh();
    QString step(int by);

    QPointer<Project> project_;
    QPointer<TrackLabels> labels_;
    QString query_;
    QStringList matches_;
    int current_ = -1;
    std::optional<std::pair<double, double>> area_;
};

}  // namespace sub::app
