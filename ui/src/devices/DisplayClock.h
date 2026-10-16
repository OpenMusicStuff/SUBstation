#pragma once

// What paces the live displays (the device editors' graphs, the EQ's spectrum,
// the Sampler's playhead, the modulators' values): a tick every
// kDisplayRefreshMs, about 60 times a second. What moves on a display moves by
// the time since its last tick (elapsed()), not by a share a tick, so it moves
// as fast however often the clock ticks. One for the whole application,
// running from when a display first listens; the tests tick it by hand.

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace sub::ui {

inline constexpr int kDisplayRefreshMs = 16;

class DisplayClock : public QObject {
    Q_OBJECT

public:
    static DisplayClock* instance();

    // A tick's length (s).
    double tickSeconds() const { return timer_.interval() / 1000.0; }
    // The seconds since `since` last restarted (restarting it): at least a tick (displays ticked
    // by hand, no time between, move as the clock would), at most 0.1 s (a busy UI doesn't make
    // them jump); the first time, or after more than a second (hidden meanwhile), a tick.
    double elapsed(QElapsedTimer& since) const;

Q_SIGNALS:
    void tick();

protected:
    void connectNotify(const QMetaMethod& signal) override;

private:
    DisplayClock();

    QTimer timer_;
};

}  // namespace sub::ui
