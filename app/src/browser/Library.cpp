#include "browser/Library.h"

#include <QDateTime>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

#include "browser/PathKeys.h"
#include "io/Json.h"

namespace sub::app {

double Library::systemClock() { return static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0; }

QString Library::defaultPath() { return localDataFile("SUBSTATION_LIBRARY", QStringLiteral("library.json")); }

Library::Library(QString path, Clock clock)
    : path_(path.isEmpty() ? defaultPath() : std::move(path)), clock_(clock ? std::move(clock) : Clock(&systemClock)) {
    records_ = load();
}

std::optional<double> Library::number(const QJsonValue& value) {
    if (value.isDouble()) return value.toDouble();
    if (value.isBool()) return value.toBool() ? 1.0 : 0.0;
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if (ok) return parsed;
    }
    return std::nullopt;
}

namespace {

// A record's older score (from before uses were timed): halved every half-life since it was last used.
double olderScore(const QJsonObject& record, double now) {
    if (!truthy(record.value(QStringLiteral("score")))) return 0.0;
    const double last = Library::number(record.value(QStringLiteral("last_used"))).value_or(now);
    const double days = std::max(0.0, now - last) / 86400.0;
    return Library::number(record.value(QStringLiteral("score"))).value_or(0.0) *
           std::pow(0.5, days / Library::kHalfLifeDays);
}

}  // namespace

void Library::recordUse(const QStringList& keys) {
    const double time = clock_();
    for (const QString& key : keys) {
        QJsonObject record = records_.value(key).toObject();
        record.insert(QStringLiteral("uses"), static_cast<qint64>(number(record.value(QStringLiteral("uses"))).value_or(0)) + 1);
        // Its uses in the window, and this one.
        QJsonArray times;
        for (const QJsonValue& value : record.value(QStringLiteral("times")).toArray()) {
            const auto at = number(value);
            if (at && time - *at < kWindowDays * 86400.0) times.append(*at);
        }
        times.append(time);
        record.insert(QStringLiteral("times"), times);
        // An older score fades from now on, as it would have (it counts no more uses).
        if (truthy(record.value(QStringLiteral("score")))) record.insert(QStringLiteral("score"), olderScore(record, time));
        record.insert(QStringLiteral("last_used"), time);
        records_.insert(key, record);
    }
    if (!keys.isEmpty()) save();
}

int Library::uses(const QString& key) const {
    const QJsonValue record = records_.value(key);
    if (!record.isObject()) return 0;
    return static_cast<int>(number(record.toObject().value(QStringLiteral("uses"))).value_or(0));
}

double Library::rank(const QString& key, std::optional<double> now) const {
    const QJsonObject record = records_.value(key).toObject();
    if (record.isEmpty()) return 0.0;
    const double time = now ? *now : clock_();
    // (As the browser's backend counts them: browser::Usage::rank.)
    double uses = 0.0;
    for (const QJsonValue& value : record.value(QStringLiteral("times")).toArray()) {
        const auto at = number(value);
        if (at && time - *at < kWindowDays * 86400.0) uses += 1.0;
    }
    uses += olderScore(record, time);
    return uses / (kWindowDays / 7.0);
}

bool Library::hidden(const QString& key) const {
    return truthy(records_.value(key).toObject().value(QStringLiteral("hidden")));
}

void Library::setHidden(const QStringList& keys, bool hidden) {
    bool changed = false;
    for (const QString& key : keys) {
        if (this->hidden(key) == hidden) continue;
        QJsonObject record = records_.value(key).toObject();
        if (hidden)
            record.insert(QStringLiteral("hidden"), true);
        else
            record.remove(QStringLiteral("hidden"));
        records_.insert(key, record);
        changed = true;
    }
    if (changed) save();
}

int Library::hiddenCount() const {
    int count = 0;
    for (auto it = records_.begin(); it != records_.end(); ++it)
        if (truthy(it.value().toObject().value(QStringLiteral("hidden")))) ++count;
    return count;
}

QJsonObject Library::load() const {
    QJsonObject records;
    const QJsonObject all = readVersionedObject(path_, kVersion, QStringLiteral("items"));
    for (auto it = all.begin(); it != all.end(); ++it)
        if (it.value().isObject()) records.insert(it.key(), it.value());
    return records;
}

void Library::save() const { writeVersionedObject(path_, kVersion, QStringLiteral("items"), records_); }

}  // namespace sub::app
