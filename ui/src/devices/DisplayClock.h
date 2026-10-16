#pragma once

// What paces the live displays (the device editors' graphs, the EQ's spectrum,
// the Sampler's playhead, the modulators' values, the scope): a tick every
// intervalMs(), the preferences' display rate (follow(): Session's
// DisplayPreferences, the lower of the rate chosen and the screen's; 16 ms,
// kDisplayRefreshMs, until told). What moves on a display moves by the time
// since its last tick (elapsed()), not by a share a tick, so it moves as fast
// however often the clock ticks. One for the whole application, running from
// when a display first listens; the tests tick it by hand.

#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>
#include <QTimer>

namespace sub::app {
class DisplayPreferences;
}

namespace sub::ui {

inline constexpr int kDisplayRefreshMs = 16;

class DisplayClock : public QObject {
    Q_OBJECT

public:
    static DisplayClock* instance();

    // A tick's interval (ms) and length (s).
    int intervalMs() const { return timer_.interval(); }
    double tickSeconds() const { return timer_.interval() / 1000.0; }
    void setIntervalMs(int ms);
    // Ticks at `preferences`' interval, now and as it changes (null: as it is).
    void follow(sub::app::DisplayPreferences* preferences);
    // The seconds since `since` last restarted (restarting it): at least a tick (displays ticked
    // by hand, no time between, move as the clock would), at most 0.1 s (a busy UI doesn't make
    // them jump); the first time, or after more than a second (hidden meanwhile), a tick.
    double elapsed(QElapsedTimer& since) const;
    // For the tests: elapsed() counts every call a tick, however long it was (ticks by hand on a busy machine).
    void setTicksByHand(bool on) { byHand_ = on; }

Q_SIGNALS:
    void tick();
    void intervalChanged();

protected:
    void connectNotify(const QMetaMethod& signal) override;

private:
    DisplayClock();

    QTimer timer_;
    QMetaObject::Connection following_;
    bool byHand_ = false;
};

}  // namespace sub::ui
