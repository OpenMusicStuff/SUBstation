#pragma once
// What the browser remembers about its items, beyond the files themselves.
//
// One record per item, keyed by BrowserItem::key(), in a JSON file next to the
// plug-in cache: {"version": 1, "items": {key: record}}. A record counts how
// often the item was used (added to the project from the browser: "uses"),
// when it was used lately ("times": search results are ranked by how often a
// week, on average), and whether it is hidden from search ("hidden").
// Records are plain JSON objects, and fields this version does not know are
// kept, so later versions can add their own (sound features...) without
// losing anything. A file that can't be read or has another version counts as
// empty; a failed save is ignored (losing a use count is not worth an error).

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace sub::app {

class Library {
public:
    static constexpr int kVersion = 1;
    static constexpr double kWindowDays = 28.0;    // the rank: uses a week, on average over this long
    static constexpr double kHalfLifeDays = 30.0;  // a record's older "score" counts half as much after this long

    // Seconds since 1970, as Python's time.time() (tests give their own).
    using Clock = std::function<double()>;
    static double systemClock();

    // library.json in localDataDir(), or SUBSTATION_LIBRARY if set.
    static QString defaultPath();

    explicit Library(QString path = {}, Clock clock = {});

    const QString& path() const { return path_; }
    double now() const { return clock_(); }

    // The items were used (added to the project) just now: each gets one more
    // use, and the time is kept (with those of its uses in the last
    // kWindowDays). Saved at once.
    void recordUse(const QStringList& keys);
    int uses(const QString& key) const;
    // How often the item is used: the times a week, on average over the last
    // kWindowDays (a rolling average, so what you use now soon outranks what
    // you used a lot long ago). A record from before uses were timed counts
    // its "score" as that many uses in them, halved every kHalfLifeDays since
    // it was last used. 0 if never used.
    double rank(const QString& key, std::optional<double> now = std::nullopt) const;

    // Hidden from search (the browser's right-click Hide from Search): its
    // record's "hidden". Saved at once.
    bool hidden(const QString& key) const;
    void setHidden(const QStringList& keys, bool hidden);
    int hiddenCount() const;

    // The records by key ("uses", "score", "last_used", and what else they hold).
    const QJsonObject& records() const { return records_; }
    void setRecord(const QString& key, const QJsonObject& record) { records_.insert(key, record); }

    void save() const;

    // Python's float() of a record's field: a number, or a string of one;
    // nothing for anything else (null, missing, a list...).
    static std::optional<double> number(const QJsonValue& value);

private:
    QJsonObject load() const;

    QString path_;
    Clock clock_;
    QJsonObject records_;
};

}  // namespace sub::app
