#pragma once
// Preferences › Look and Feel › Maximum display frame rate, without its
// widgets: how often the live displays refresh (the playhead, the meters, a
// recording's waveform, the scope, the devices' editors). They tick at the
// lower of the rate chosen (30, 60, 120, 144 or 240 Hz; 60 by default) and the
// screen's refresh rate (the main window's screen: followWindow(), as the
// window moves to another screen or its rate changes), every intervalMs():
// 1000 / rate ms, rounded leaning short (33, 16, 8, 7 and 4 ms). What moves on
// a display moves by the time a tick took, so it moves as fast at any rate.
//
// An application preference, not the project's: kept in QSettings
// (ui/max_display_rate), no undo. Applied at once: the bridge polls the
// playhead and the meters every intervalMs() (EngineBridge::setDisplayInterval);
// the UI's DisplayClock follows `changed`.

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include <array>

class QScreen;
class QWindow;

namespace sub::app {

class EngineBridge;

class DisplayPreferences : public QObject {
    Q_OBJECT
    // The rate chosen (Hz): one of kRates.
    Q_PROPERTY(int maxRate READ maxRate WRITE setMaxRate NOTIFY changed)
    // [{label ("60 Hz"), value (60)}], for the page's ChoiceBox; rateIndex: maxRate's.
    Q_PROPERTY(QVariantList rateChoices READ rateChoices CONSTANT)
    Q_PROPERTY(int rateIndex READ rateIndex NOTIFY changed)
    // The screen's refresh rate (Hz; 0: not known).
    Q_PROPERTY(double screenRate READ screenRate NOTIFY changed)
    // What the displays tick at: the lower of maxRate and screenRate (Hz), and its interval (ms).
    Q_PROPERTY(double rate READ rate NOTIFY changed)
    Q_PROPERTY(int intervalMs READ intervalMs NOTIFY changed)
    // "Displays refresh at 60 Hz, the screen's rate."
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    inline static const QString kKey = QStringLiteral("ui/max_display_rate");
    static constexpr int kDefaultRate = 60;
    static constexpr std::array<int, 5> kRates = {30, 60, 120, 144, 240};

    // Reads the preference and applies it to `bridge` (may be null: the tests').
    explicit DisplayPreferences(EngineBridge* bridge, QObject* parent = nullptr);

    int maxRate() const { return maxRate_; }
    // Another rate than kRates' is taken as the nearest of them. Saved.
    void setMaxRate(int hz);
    QVariantList rateChoices() const;
    int rateIndex() const;
    Q_INVOKABLE void chooseRate(int index);

    double screenRate() const { return screenRate_; }
    // The screen's rate (0 or less: not known). followWindow sets it; the tests set their own.
    void setScreenRate(double hz);
    // Follows `window`'s screen: its rate now, and as the window moves to another or the rate changes.
    void followWindow(QWindow* window);

    double rate() const;
    int intervalMs() const { return intervalFor(rate()); }
    QString status() const;

    // A tick's interval at `hz` (ms): 1000 / hz rounded leaning short, at least 1.
    static int intervalFor(double hz);

Q_SIGNALS:
    void changed();

private:
    void followScreen(QScreen* screen);
    void apply();

    EngineBridge* bridge_;
    int maxRate_ = kDefaultRate;
    double screenRate_ = 0.0;
    QPointer<QScreen> screen_;
    QMetaObject::Connection screenRateChanged_, windowScreenChanged_;
};

}  // namespace sub::app
