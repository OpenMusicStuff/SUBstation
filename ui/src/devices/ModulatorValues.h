#pragma once

// The modulators' values as they play (an LFO's or an Envelope's "value"
// display: 0..1), for what draws the parameters they move: a knob mapped to
// one shows where its parameter is now, a device editor's graph moves with
// it. One for the application: whatever watches a modulator (watch() and
// unwatch(), counted) has its value on each DisplayClock tick (ticked()),
// played back at the audio's pace (DisplayPlayback: the engine publishes a
// block of values at a time, the screen ticks every 16 ms).

#include "devices/DisplayPlayback.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

namespace sub::app {
class Session;
}

namespace sub::ui {

class ModulatorValues : public QObject {
    Q_OBJECT

public:
    static ModulatorValues* instance();

    void watch(sub::app::Session* session, const QString& trackId, const QString& modulatorId);
    void unwatch(const QString& modulatorId);
    // unwatch() on the instance, if there still is one (a watcher going after the application).
    static void release(const QString& modulatorId);
    // The modulator's value now (0..1); none: nobody watches it, or nothing came from it yet.
    std::optional<double> value(const QString& modulatorId) const;
    // Reads every watched modulator's values since, and moves on by `seconds` (DisplayClock's tick; the tests').
    void tick(double seconds);

Q_SIGNALS:
    void ticked();

private:
    ModulatorValues();

    struct Watched {
        QPointer<sub::app::Session> session;
        QString trackId;
        int count = 0;
        quint32 processor = 0;  // whose display it reads (a new one starts again)
        int display = -1;       // its "value" display's index
        int samplesPerValue = 64;
        quint64 position = 0;
        DisplayPlayback<1> playback;
    };
    void read(const QString& modulatorId, Watched& watched);

    QHash<QString, Watched> watched_;  // modulator (device) id ->
    QElapsedTimer clock_;
    QMetaObject::Connection ticking_;
};

}  // namespace sub::ui
