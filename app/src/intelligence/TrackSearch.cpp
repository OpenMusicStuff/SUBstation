#include "intelligence/TrackSearch.h"

#include "intelligence/TrackLabels.h"
#include "model/Project.h"

#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <vector>

namespace sub::app {

namespace labels = intelligence::labels;

namespace {

// How well a track matches, the best first.
enum Rank { NameWords, NameText, LabelWords, AllWords, AllText, None };

// The words of a text, in lower case: split at what isn't a letter or a digit,
// between a lower-case letter and a capital ("BellStab") and between letters
// and digits ("Kick808").
QStringList wordsOf(const QString& text) {
    static const QRegularExpression camel(QStringLiteral("(?<=\\p{Ll})(?=\\p{Lu})|(?<=\\p{L})(?=\\d)|(?<=\\d)(?=\\p{L})"));
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\d]+"));
    QString spaced = text;
    spaced.replace(camel, QStringLiteral(" "));
    QStringList words = spaced.toCaseFolded().split(separators, Qt::SkipEmptyParts);
    return words;
}

// Every query word starts one of the text's words.
bool startsWords(const QStringList& query, const QStringList& words) {
    return std::all_of(query.begin(), query.end(), [&](const QString& q) {
        return std::any_of(words.begin(), words.end(), [&](const QString& w) { return w.startsWith(q); });
    });
}

// Every query word is somewhere in the text (case folded).
bool contains(const QStringList& query, const QString& folded) {
    return std::all_of(query.begin(), query.end(), [&](const QString& q) { return folded.contains(q); });
}

Rank rank(const QStringList& query, const Track& track, const labels::TrackLabel* label) {
    if (startsWords(query, wordsOf(track.name))) return NameWords;
    const QString name = track.name.toCaseFolded();
    if (contains(query, name)) return NameText;
    if (!label) return None;
    const QString labelText = QString::fromStdString(label->label);
    if (startsWords(query, wordsOf(labelText))) return LabelWords;
    QStringList all{track.name, labelText, QString::fromStdString(label->family), QString::fromStdString(label->role)};
    for (const std::string& trait : label->traits) all << QString::fromStdString(trait);
    for (const std::string& detail : label->details) all << QString::fromStdString(detail);
    const QString text = all.join(u'\n');
    if (startsWords(query, wordsOf(text))) return AllWords;
    if (contains(query, text.toCaseFolded())) return AllText;
    return None;
}

}  // namespace

TrackSearch::TrackSearch(Project* project, TrackLabels* labels, QObject* parent)
    : QObject(parent), project_(project), labels_(labels) {
    if (labels_) connect(labels_, &TrackLabels::changed, this, &TrackSearch::refresh);
    if (project_) connect(project_, &Project::reset, this, &TrackSearch::refresh);
}

QStringList TrackSearch::find(const Project& project, const TrackLabels& labels, const QString& query) {
    // The query's words, as typed (its separators aside: "pluck-arp" is "pluck arp").
    const QStringList words = query.toCaseFolded().split(QRegularExpression(QStringLiteral("[^\\p{L}\\d]+")),
                                                         Qt::SkipEmptyParts);
    if (words.isEmpty()) return {};
    std::vector<const Track*> tracks;
    for (const Track& track : project.tracks()) tracks.push_back(&track);
    for (const Track& track : project.returns()) tracks.push_back(&track);
    tracks.push_back(&project.master());
    std::array<QStringList, None> ranked;
    for (const Track* track : tracks) {
        const Rank r = rank(words, *track, labels.find(track->id));
        if (r != None) ranked[r] << track->id;
    }
    QStringList out;
    for (const QStringList& ids : ranked) out << ids;
    return out;
}

void TrackSearch::setQuery(const QString& query) {
    if (query == query_) return;
    query_ = query;
    matches_ = project_ && labels_ ? find(*project_, *labels_, query_) : QStringList();
    current_ = matches_.isEmpty() ? -1 : 0;
    Q_EMIT changed();
}

void TrackSearch::refresh() {
    const QString was = currentTrackId();
    const QStringList matches = project_ && labels_ ? find(*project_, *labels_, query_) : QStringList();
    const int current = matches.isEmpty() ? -1 : std::max(0, static_cast<int>(matches.indexOf(was)));
    if (matches == matches_ && current == current_) return;
    matches_ = matches;
    current_ = current;
    Q_EMIT changed();
}

QString TrackSearch::step(int by) {
    const QString was = currentTrackId();
    refresh();
    if (matches_.isEmpty()) return {};
    if (currentTrackId() != was) return currentTrackId();  // (its track no longer matches: the best one)
    const int n = count();
    current_ = ((current_ + by) % n + n) % n;
    Q_EMIT changed();
    return currentTrackId();
}

QString TrackSearch::next() { return step(1); }

QString TrackSearch::previous() { return step(-1); }

}  // namespace sub::app
