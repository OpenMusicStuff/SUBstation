#include "TransportState.h"

#include <QVariantMap>

#include <optional>

#include "audio/BridgeTypes.h"
#include "audio/EngineBridge.h"
#include "model/Keys.h"
#include "model/Project.h"
#include "model/Timebase.h"

namespace sub::ui {

TransportState::TransportState(QObject* parent) : QObject(parent) { refresh(); }

void TransportState::setSession(app::Session* session) {
    if (session == session_) return;
    for (const QMetaObject::Connection& connection : connections_) disconnect(connection);
    connections_.clear();
    session_ = session;
    if (session_) {
        app::Project* project = session_->project();
        app::EngineBridge* bridge = session_->bridge();
        connections_ << connect(project, &app::Project::settingsChanged, this, &TransportState::refresh)
                     << connect(project, &app::Project::reset, this, &TransportState::refresh)
                     << connect(bridge, &app::EngineBridge::positionChanged, this, &TransportState::showPosition)
                     << connect(bridge, &app::EngineBridge::metersUpdated, this, &TransportState::meterTick)
                     << connect(bridge, &app::EngineBridge::deviceChanged, this, &TransportState::refreshDevice)
                     << connect(bridge, &app::EngineBridge::streamingChanged, this, &TransportState::refreshDisk)
                     << connect(bridge, &app::EngineBridge::diskLateChanged, this, &TransportState::refreshDisk);
    }
    Q_EMIT sessionChanged();
    refresh();
}

int TransportState::numerator() const {
    return session_ ? session_->project()->timeSignature().numerator : 4;
}

int TransportState::denominator() const {
    return session_ ? session_->project()->timeSignature().denominator : 4;
}

QList<qreal> TransportState::denominators() const {
    QList<qreal> values;
    for (int denominator : app::kValidDenominators) values << denominator;
    return values;
}

QVariantList TransportState::keys() const {
    QVariantList keys{QVariantMap{{QStringLiteral("label"), QStringLiteral("No Key")}, {QStringLiteral("name"), QString()}}};
    for (const app::Key& key : app::allKeys())
        keys << QVariantMap{{QStringLiteral("label"), key.label()}, {QStringLiteral("name"), key.name()}};
    return keys;
}

int TransportState::keyIndex() const {
    if (!session_) return 0;
    const std::optional<app::Key>& key = session_->project()->key();
    if (!key) return 0;
    const std::vector<app::Key> all = app::allKeys();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i] == *key) return static_cast<int>(i) + 1;
    return 0;
}

QString TransportState::formatPosition(double beat, int numerator, int denominator) {
    const app::BarPosition at = app::splitPosition(beat, app::TimeSignature{numerator, denominator});
    return QStringLiteral("%1. %2. %3").arg(at.bar + 1, 3).arg(at.beat + 1).arg(at.sixteenth + 1);
}

void TransportState::showPosition(double beat) {
    QString text;
    if (session_ && !session_->project()->meter().constant()) {  // (bars as each time signature makes them)
        const app::BarPosition at = app::splitPosition(beat, session_->project()->meter());
        text = QStringLiteral("%1. %2. %3").arg(at.bar + 1, 3).arg(at.beat + 1).arg(at.sixteenth + 1);
    } else {
        text = formatPosition(beat, numerator(), denominator());
    }
    if (text == positionText_) return;  // (the playhead moves 60 times a second; the text less often)
    positionText_ = text;
    Q_EMIT positionTextChanged();
}

void TransportState::meterTick() {
    if (!cpuShown_.isValid()) cpuShown_.start();
    if (cpuShown_.elapsed() < kCpuEveryMs || !session_) return;
    cpuShown_.restart();
    const QString text =
        QStringLiteral("CPU %1%").arg(QString::number(session_->bridge()->cpuLoad() * 100.0, 'f', 0));
    if (text == cpuText_) return;
    cpuText_ = text;
    Q_EMIT cpuTextChanged();
}

void TransportState::refreshDisk() {
    const app::EngineBridge* bridge = session_ ? session_->bridge() : nullptr;
    const bool visible = bridge && bridge->streaming();
    const int late = bridge ? bridge->diskLate() : 0;
    const QString text = late > 0 ? QStringLiteral("Disk late %1×").arg(late) : QStringLiteral("Disk");
    const QString toolTip =
        late > 0 ? QStringLiteral("Long files stream from disk. It came late %1 times (%2 ms of audio played as silence): "
                                  "a faster disk, or more memory for streaming (Preferences > Audio), helps.")
                       .arg(late)
                       .arg(QString::number(bridge->diskLateMs(), 'f', 0))
                 : QStringLiteral("Long files stream from disk (Preferences > Audio). It has kept up.");
    if (visible == diskVisible_ && text == diskText_ && toolTip == diskToolTip_) return;
    diskVisible_ = visible;
    diskText_ = text;
    diskToolTip_ = toolTip;
    Q_EMIT diskChanged();
}

void TransportState::refreshDevice() {
    QString text = QStringLiteral("No audio device");
    QString toolTip = QStringLiteral("No audio output is open. Click to open Preferences.");
    if (session_) {
        const app::AudioDeviceStatus status = session_->bridge()->deviceStatus();
        if (status.open) {
            // (Counted in characters, as Python counts them, not UTF-16 units.)
            const QList<uint> name = status.name.toUcs4();
            const QString shown = name.size() <= kDeviceNameMax
                                      ? status.name
                                      : QString::fromUcs4(reinterpret_cast<const char32_t*>(name.constData()),
                                                          kDeviceNameMax - 1) +
                                            QStringLiteral("…");
            text = QStringLiteral("%1 · %2 kHz").arg(shown, QString::number(status.sampleRate / 1000.0, 'g', 6));
            toolTip = QStringLiteral("%1: %2\n%3 Hz, %4 frames, ~%5 ms output latency%6\nClick to open Preferences.")
                          .arg(status.backend, status.name)
                          .arg(status.sampleRate)
                          .arg(status.bufferFrames)
                          .arg(QString::number(status.latencyMs, 'f', 1),
                               status.exclusive ? QStringLiteral(" (exclusive)") : QString());
        }
    }
    if (text == deviceText_ && toolTip == deviceToolTip_) return;
    deviceText_ = text;
    deviceToolTip_ = toolTip;
    Q_EMIT deviceChanged();
}

void TransportState::refresh() {
    Q_EMIT settingsChanged();
    showPosition(session_ ? session_->bridge()->position() : 0.0);
    refreshDevice();
    refreshDisk();
}

}  // namespace sub::ui
