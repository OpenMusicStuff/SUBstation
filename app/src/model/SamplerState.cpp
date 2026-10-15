#include "model/SamplerState.h"

#include <QDir>

namespace sub::app::samplerState {

namespace {

// Paths as the model keeps them (DeviceState: cleaned, Qt's separators); the
// similarity index's may be spelt otherwise.
bool samePath(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    const QString x = QDir::cleanPath(QDir::fromNativeSeparators(a));
    const QString y = QDir::cleanPath(QDir::fromNativeSeparators(b));
#ifdef Q_OS_WIN
    return x.compare(y, Qt::CaseInsensitive) == 0;
#else
    return x == y;
#endif
}

}  // namespace

QString sample(const deviceState::Values& values) { return values.value(kSample); }

QString reference(const deviceState::Values& values) { return values.value(kReference); }

bool referenceSaved(const deviceState::Values& values) { return values.value(kReferenceSaved) == QStringLiteral("1"); }

deviceState::Values loaded(deviceState::Values values, const QString& path) {
    if (path.isEmpty()) {
        values.remove(kSample);
    } else {
        values.insert(kSample, QDir::cleanPath(path));
    }
    if (!referenceSaved(values)) {
        values.remove(kReference);
        values.remove(kReferenceSaved);
    }
    return values;
}

deviceState::Values stepped(deviceState::Values values, const QString& path) {
    if (reference(values).isEmpty() && !sample(values).isEmpty()) values.insert(kReference, sample(values));
    values.insert(kSample, QDir::cleanPath(path));
    return values;
}

deviceState::Values referenceSet(deviceState::Values values) {
    if (sample(values).isEmpty()) return values;
    values.insert(kReference, sample(values));
    values.insert(kReferenceSaved, QStringLiteral("1"));
    return values;
}

QString step(const QStringList& best, const QString& reference, const QString& current, int direction) {
    if (direction == 0) return {};
    qsizetype at = -1;  // the reference itself, or a sound not among them
    for (qsizetype i = 0; i < best.size(); ++i) {
        if (samePath(best[i], current)) {
            at = i;
            break;
        }
    }
    if (direction > 0) return at + 1 < best.size() ? best[at + 1] : QString();
    if (at < 0) return {};
    return at == 0 ? reference : best[at - 1];
}

}  // namespace sub::app::samplerState
