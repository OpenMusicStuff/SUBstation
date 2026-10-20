#include "pianoroll/ProgressionIdeas.h"

#include "intelligence/Harmony.h"

#include <QRandomGenerator>
#include <QVariantMap>

#include <algorithm>

namespace sub::ui {

namespace harmony = intelligence::harmony;

namespace {

const harmony::Key kAssumedKey{9, true};  // A minor: for want of the song's

}  // namespace

ProgressionIdeas::ProgressionIdeas(QObject* parent)
    : QAbstractListModel(parent), seed_(QRandomGenerator::global()->generate64()) {}

int ProgressionIdeas::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant ProgressionIdeas::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(published_.size())) return {};
    const QHash<int, QByteArray> names = roleNames();
    return published_[static_cast<size_t>(index.row())].value(QString::fromLatin1(names.value(role)));
}

QHash<int, QByteArray> ProgressionIdeas::roleNames() const {
    return {{ChordsRole, "chords"}, {KeptRole, "kept"}, {TriedRole, "tried"}, {SourceRole, "source"}};
}

void ProgressionIdeas::publish() {
    std::vector<QVariantMap> now;
    for (int i = 0; i < count(); ++i) now.push_back(rowData(i));
    const int before = static_cast<int>(published_.size()), after = static_cast<int>(now.size());
    if (after < before) {
        beginRemoveRows(QModelIndex(), after, before - 1);
        published_.resize(static_cast<size_t>(after));
        endRemoveRows();
    }
    for (int i = 0; i < std::min(before, after); ++i) {
        if (published_[static_cast<size_t>(i)] == now[static_cast<size_t>(i)]) continue;
        published_[static_cast<size_t>(i)] = now[static_cast<size_t>(i)];
        Q_EMIT dataChanged(index(i), index(i));
    }
    if (after > before) {
        beginInsertRows(QModelIndex(), before, after - 1);
        published_.insert(published_.end(), now.begin() + before, now.end());
        endInsertRows();
    }
    Q_EMIT ideasChanged();
}

void ProgressionIdeas::setRoll(PianoRoll* roll) {
    if (roll == roll_) return;
    if (roll_) disconnect(roll_, nullptr, this, nullptr);
    roll_ = roll;
    if (roll_) {
        connect(roll_, &PianoRoll::contentChanged, this, &ProgressionIdeas::clipChanged);
        connect(roll_, &PianoRoll::clipChanged, this, &ProgressionIdeas::clipChanged);
    }
    Q_EMIT rollChanged();
}

QStringList ProgressionIdeas::moods() {
    QStringList names;
    for (int m = 0; m < harmony::kMoods; ++m) names.append(QString::fromLatin1(harmony::moodName(static_cast<harmony::Mood>(m))));
    return names;
}

void ProgressionIdeas::setMood(int mood) {
    if (mood < 0 || mood >= harmony::kMoods || mood == static_cast<int>(mood_)) return;
    mood_ = static_cast<harmony::Mood>(mood);
    Q_EMIT settingsChanged();
    if (!rows_.empty()) newIdeas();
}

void ProgressionIdeas::setLength(int length) {
    if (!lengths().contains(length) || length == length_) return;
    length_ = length;
    Q_EMIT settingsChanged();
    if (!rows_.empty()) newIdeas();
}

void ProgressionIdeas::setSevenths(bool on) {
    if (on == sevenths_) return;
    sevenths_ = on;
    Q_EMIT settingsChanged();
    retry();
    refreshTried();
    publish();
}

void ProgressionIdeas::setBass(bool on) {
    if (on == bass_) return;
    bass_ = on;
    Q_EMIT settingsChanged();
    retry();
    refreshTried();
    publish();
}

QString ProgressionIdeas::keyName() const { return key_ ? QString::fromStdString(key_->name()) : QString(); }

std::vector<harmony::Chord> ProgressionIdeas::chords(int row) const {
    if (row < 0 || row >= count()) return {};
    return rows_[static_cast<size_t>(row)].idea.chords;
}

std::vector<harmony::Chord> ProgressionIdeas::played(int row) const {
    std::vector<harmony::Chord> result = chords(row);
    if (sevenths_) {
        for (harmony::Chord& c : result) c = harmony::withSeventh(c, rows_[static_cast<size_t>(row)].key);
    }
    return result;
}

bool ProgressionIdeas::isLocked(int row, int chord) const {
    if (row < 0 || row >= count()) return false;
    const std::vector<bool>& locked = rows_[static_cast<size_t>(row)].locked;
    return chord >= 0 && chord < static_cast<int>(locked.size()) && locked[static_cast<size_t>(chord)];
}

bool ProgressionIdeas::isKept(int row) const { return row >= 0 && row < count() && rows_[static_cast<size_t>(row)].kept; }

QVariantMap ProgressionIdeas::rowData(int i) const {
    const Row& row = rows_[static_cast<size_t>(i)];
    const std::vector<harmony::Chord> chords = played(i);
    QVariantList chordList;
    for (size_t k = 0; k < chords.size(); ++k) {
        chordList.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(chords[k].name())},
                                     {QStringLiteral("numeral"), QString::fromStdString(harmony::romanNumeral(chords[k], row.key))},
                                     {QStringLiteral("locked"), static_cast<bool>(row.locked[k])}});
    }
    return {{QStringLiteral("chords"), chordList},
            {QStringLiteral("kept"), row.kept},
            {QStringLiteral("tried"), i == tried_},
            {QStringLiteral("source"), sourceText(row)}};
}

QString ProgressionIdeas::sourceText(const Row& row) const {
    const QString key = QString::fromStdString(row.key.name());
    if (row.idea.seed >= 0) {
        const harmony::SeedInfo info = harmony::seedInfo(row.idea.seed);
        QString tags = QString::fromLatin1(info.tags);
        tags.replace(QLatin1Char(' '), QStringLiteral(", "));
        return tr("free-midi-chords' %1 (%2), in %3").arg(QString::fromLatin1(info.numerals), tags, key);
    }
    return tr("Drawn in %1: each chord as chords follow each other in the Billboard songs\nand in %2 progressions")
        .arg(key, QString::fromLatin1(harmony::moodName(row.mood)).toLower());
}

harmony::ProgressionSettings ProgressionIdeas::settings() {
    const app::Harmony* h = roll_ ? roll_->harmony() : nullptr;
    const std::optional<app::Key> key = h ? h->key() : std::nullopt;
    if (key) {
        key_ = app::Harmony::toHarmony(*key);
        keySource_ = h->keyInferred() ? QStringLiteral("inferred") : QStringLiteral("project");
    } else {
        key_ = kAssumedKey;
        keySource_ = QStringLiteral("assumed");
    }
    return {*key_, mood_, length_};
}

std::vector<std::vector<harmony::Chord>> ProgressionIdeas::chordsBut(int row) const {
    std::vector<std::vector<harmony::Chord>> all;
    for (int i = 0; i < count(); ++i) {
        if (i != row) all.push_back(rows_[static_cast<size_t>(i)].idea.chords);
    }
    return all;
}

ProgressionIdeas::Row ProgressionIdeas::rowOf(const harmony::Idea& idea, const harmony::Key& key) const {
    return {idea, key, mood_, std::vector<bool>(idea.chords.size(), false), false};
}

void ProgressionIdeas::newIdeas() {
    const harmony::ProgressionSettings fresh = settings();
    std::vector<int> replaced;
    for (int i = 0; i < count(); ++i) {
        Row& row = rows_[static_cast<size_t>(i)];
        if (row.kept) continue;
        if (std::find(row.locked.begin(), row.locked.end(), true) == row.locked.end()) {
            replaced.push_back(i);
            continue;
        }
        // Its locked chords stay where they are, in its own key; the rest are drawn again.
        harmony::ProgressionSettings own = fresh;
        own.key = row.key;
        row.idea = harmony::redrawIdea(row.idea.chords, row.locked, own, seed_++, chordsBut(i));
        row.mood = mood_;
    }
    for (int i = count(); i < kIdeas; ++i) replaced.push_back(i);
    std::vector<std::vector<harmony::Chord>> staying;
    for (int i = 0; i < count(); ++i) {
        if (std::find(replaced.begin(), replaced.end(), i) == replaced.end()) staying.push_back(rows_[static_cast<size_t>(i)].idea.chords);
    }
    const std::vector<harmony::Idea> made =
        harmony::progressionIdeas(fresh, static_cast<int>(replaced.size()), seed_++, staying);
    for (size_t k = 0; k < replaced.size() && k < made.size(); ++k) {
        const auto i = static_cast<size_t>(replaced[k]);
        if (i < rows_.size()) rows_[i] = rowOf(made[k], fresh.key);
        else rows_.push_back(rowOf(made[k], fresh.key));
    }
    if (current_ < 0 || current_ >= count()) current_ = count() > 0 ? 0 : -1;
    refreshTried();
    publish();
}

void ProgressionIdeas::clear() {
    rows_.clear();
    current_ = tried_ = -1;
    triedClip_.clear();
    base_ = written_ = {};
    triedChords_.clear();
    publish();
}

void ProgressionIdeas::moreLike(int row) {
    if (row < 0 || row >= count()) return;
    const Row source = rows_[static_cast<size_t>(row)];
    std::vector<int> targets;
    for (int i = 0; i < count(); ++i) {
        if (i != row && !rows_[static_cast<size_t>(i)].kept) targets.push_back(i);
    }
    if (targets.empty()) {
        Q_EMIT statusMessage(tr("Every other idea is kept: unkeep one to make room"));
        return;
    }
    harmony::ProgressionSettings settings{source.key, mood_, static_cast<int>(source.idea.chords.size())};
    const std::vector<harmony::Idea> variations = harmony::ideaVariations(
        source.idea.chords, source.locked, settings, static_cast<int>(targets.size()), seed_++, chordsBut(-1));
    if (variations.empty()) {
        Q_EMIT statusMessage(tr("Every chord of it is locked: unlock one to vary it"));
        return;
    }
    for (size_t k = 0; k < variations.size(); ++k) {
        Row varied = rowOf(variations[k], source.key);
        varied.locked = source.locked;  // (they keep its locked chords)
        rows_[static_cast<size_t>(targets[k])] = std::move(varied);
    }
    current_ = row;
    refreshTried();
    publish();
}

void ProgressionIdeas::toggleLock(int row, int chord) {
    if (row < 0 || row >= count()) return;
    std::vector<bool>& locked = rows_[static_cast<size_t>(row)].locked;
    if (chord < 0 || chord >= static_cast<int>(locked.size())) return;
    locked[static_cast<size_t>(chord)] = !locked[static_cast<size_t>(chord)];
    publish();
}

void ProgressionIdeas::toggleKept(int row) {
    if (row < 0 || row >= count()) return;
    rows_[static_cast<size_t>(row)].kept = !rows_[static_cast<size_t>(row)].kept;
    publish();
}

std::vector<std::pair<harmony::Chord, bool>> ProgressionIdeas::alternativeChords(int row, int chord) const {
    if (row < 0 || row >= count()) return {};
    const Row& r = rows_[static_cast<size_t>(row)];
    if (chord < 0 || chord >= static_cast<int>(r.idea.chords.size())) return {};
    const harmony::Chord& current = r.idea.chords[static_cast<size_t>(chord)];
    const harmony::ProgressionSettings settings{r.key, mood_, static_cast<int>(r.idea.chords.size())};
    std::vector<std::pair<harmony::Chord, bool>> all;
    for (const harmony::Chord& c : harmony::chordColours(current, r.key)) all.emplace_back(c, true);
    for (const harmony::Chord& c : harmony::chordAlternatives(r.idea.chords, chord, settings, kAlternatives))
        all.emplace_back(c, false);
    // As they would play: with Sevenths, a triad and its seventh are one choice.
    const auto shown = [&](const harmony::Chord& c) { return sevenths_ ? harmony::withSeventh(c, r.key) : c; };
    std::vector<std::pair<harmony::Chord, bool>> result;
    std::vector<harmony::Chord> seen{shown(current)};
    for (const auto& [c, colour] : all) {
        if (std::find(seen.begin(), seen.end(), shown(c)) != seen.end()) continue;
        seen.push_back(shown(c));
        result.emplace_back(c, colour);
    }
    return result;
}

QVariantList ProgressionIdeas::alternatives(int row, int chord) const {
    QVariantList list;
    if (row < 0 || row >= count()) return list;
    const harmony::Key& key = rows_[static_cast<size_t>(row)].key;
    for (const auto& [c, colour] : alternativeChords(row, chord)) {
        const harmony::Chord shown = sevenths_ ? harmony::withSeventh(c, key) : c;
        list.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(shown.name())},
                                {QStringLiteral("numeral"), QString::fromStdString(harmony::romanNumeral(shown, key))},
                                {QStringLiteral("colour"), colour}});
    }
    return list;
}

void ProgressionIdeas::swap(int row, int chord, int index) {
    const auto all = alternativeChords(row, chord);
    if (index < 0 || index >= static_cast<int>(all.size())) return;
    Row& r = rows_[static_cast<size_t>(row)];
    r.idea.chords[static_cast<size_t>(chord)] = all[static_cast<size_t>(index)].first;
    r.idea.seed = -1;
    if (row == tried_) tryIdea(row);
    publish();
}

void ProgressionIdeas::tryIdea(int row) {
    if (!roll_ || row < 0 || row >= count()) return;
    const std::optional<PianoRoll::ClipBody> body = roll_->leadBody();
    if (!body) {
        Q_EMIT statusMessage(tr("Open a MIDI clip to try progressions in"));
        return;
    }
    const QString clipId = roll_->clipId();
    PianoRoll::ClipBody base;
    if (clipId == triedClip_ && *body == written_) {
        base = base_;  // as the last try left it: over what it wrote over
    } else {
        base = *body;
        if (clipId == triedClip_) {
            // Changed since: without the notes the last try wrote, where they still are.
            std::vector<app::Note> wrote = written_.notes;
            for (const app::Note& n : base_.notes) {
                const auto it = std::find(wrote.begin(), wrote.end(), n);
                if (it != wrote.end()) wrote.erase(it);
            }
            for (const app::Note& n : wrote) {
                const auto it = std::find(base.notes.begin(), base.notes.end(), n);
                if (it != base.notes.end()) base.notes.erase(it);
            }
        }
    }
    const std::vector<harmony::Chord> chords = played(row);
    writing_ = true;
    const std::optional<PianoRoll::ClipBody> result = roll_->writeProgression(chords, bass_, base, kMergeKey + clipId);
    writing_ = false;
    if (!result) return;
    triedClip_ = clipId;
    base_ = std::move(base);
    written_ = *result;
    triedChords_ = chords;
    triedBass_ = bass_;
    tried_ = current_ = row;
    publish();
}

void ProgressionIdeas::step(int delta) {
    if (count() == 0) return;
    const int row = current_ < 0 ? 0 : std::clamp(current_ + delta, 0, count() - 1);
    tryIdea(row);
}

void ProgressionIdeas::retry() {
    if (tried_ >= 0 && tried_ < count()) tryIdea(tried_);
}

void ProgressionIdeas::refreshTried() {
    if (tried_ < 0) return;
    if (tried_ >= count() || played(tried_) != triedChords_ || bass_ != triedBass_) tried_ = -1;
}

void ProgressionIdeas::clipChanged() {
    if (writing_ || tried_ < 0) return;
    const std::optional<PianoRoll::ClipBody> body = roll_ ? roll_->leadBody() : std::nullopt;
    if (body && roll_->clipId() == triedClip_ && *body == written_) return;
    tried_ = -1;
    publish();
}

}  // namespace sub::ui
