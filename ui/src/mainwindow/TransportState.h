#pragma once

// What the transport bar shows that needs working out: the position as
// "bar. beat. sixteenth", changed only when its text does; the time
// signature's two numbers and the denominators it may have; the project key's
// choices; the CPU load, read every kCpuEveryMs (on a meter update), with the
// last kCpuHistory readings for the meter's graph; the disk, while files
// stream from it (how often it came late); and the audio device's name and
// rate, with a tooltip. The bar's controls call the session's editor and
// bridge themselves.

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "session/Session.h"

namespace sub::ui {

class TransportState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(sub::app::Session* session READ session WRITE setSession NOTIFY sessionChanged)
    // "  1. 1. 1": the bar right-aligned in three places, one-based.
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionTextChanged)
    Q_PROPERTY(int numerator READ numerator NOTIFY settingsChanged)
    Q_PROPERTY(int denominator READ denominator NOTIFY settingsChanged)
    // The denominators a time signature may have (the value box's choices).
    Q_PROPERTY(QList<qreal> denominators READ denominators CONSTANT)
    // The key chooser's entries: [{label, name}], "No Key" (name "") first,
    // then every key ("C Major", "C Minor", ...; name as projects save it).
    Q_PROPERTY(QVariantList keys READ keys CONSTANT)
    // The project's key among them (0: none).
    Q_PROPERTY(int keyIndex READ keyIndex NOTIFY settingsChanged)
    Q_PROPERTY(QString cpuText READ cpuText NOTIFY cpuChanged)  // "CPU 12%"
    // The same as a fraction (more than 1 when late), and the last readings, oldest first.
    Q_PROPERTY(qreal cpuLoad READ cpuLoad NOTIFY cpuChanged)
    Q_PROPERTY(QList<qreal> cpuHistory READ cpuHistory NOTIFY cpuChanged)
    // While files stream from disk: "Disk", or "Disk late 3×" once it has come late; and why.
    Q_PROPERTY(bool diskVisible READ diskVisible NOTIFY diskChanged)
    Q_PROPERTY(QString diskText READ diskText NOTIFY diskChanged)
    Q_PROPERTY(QString diskToolTip READ diskToolTip NOTIFY diskChanged)
    Q_PROPERTY(int diskLate READ diskLate NOTIFY diskChanged)  // times it came late
    // "Speakers · 48 kHz" (a long name cut to 28 characters), or "No audio device".
    Q_PROPERTY(QString deviceText READ deviceText NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceToolTip READ deviceToolTip NOTIFY deviceChanged)
    // Its two parts: the name as shown ("" with none) and the rate ("48 kHz"; "" with none).
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceRate READ deviceRate NOTIFY deviceChanged)

public:
    static constexpr int kCpuEveryMs = 500;    // a CPU reading shown this often (on a meter update)
    static constexpr int kCpuHistory = 24;     // readings kept for the graph (12 s)
    static constexpr int kDeviceNameMax = 28;  // characters of the device's name shown

    explicit TransportState(QObject* parent = nullptr);

    app::Session* session() const { return session_; }
    void setSession(app::Session* session);

    QString positionText() const { return positionText_; }
    int numerator() const;
    int denominator() const;
    QList<qreal> denominators() const;
    QVariantList keys() const;
    int keyIndex() const;
    QString cpuText() const { return cpuText_; }
    qreal cpuLoad() const { return cpuHistory_.isEmpty() ? 0.0 : cpuHistory_.last(); }
    QList<qreal> cpuHistory() const { return cpuHistory_; }
    bool diskVisible() const { return diskVisible_; }
    QString diskText() const { return diskText_; }
    QString diskToolTip() const { return diskToolTip_; }
    int diskLate() const { return diskLate_; }
    QString deviceText() const { return deviceText_; }
    QString deviceToolTip() const { return deviceToolTip_; }
    QString deviceName() const { return deviceName_; }
    QString deviceRate() const { return deviceRate_; }

    // The position's text for a beat in a time signature.
    static QString formatPosition(double beat, int numerator, int denominator);

Q_SIGNALS:
    void sessionChanged();
    void positionTextChanged();
    void settingsChanged();
    void cpuChanged();
    void diskChanged();
    void deviceChanged();

private:
    void showPosition(double beat);
    void meterTick();
    void refreshDevice();
    void refreshDisk();
    void refresh();

    QPointer<app::Session> session_;
    QString positionText_;
    QString cpuText_ = QStringLiteral("CPU 0%");
    QList<qreal> cpuHistory_;
    bool diskVisible_ = false;
    QString diskText_;
    QString diskToolTip_;
    int diskLate_ = 0;
    QString deviceText_;
    QString deviceToolTip_;
    QString deviceName_;
    QString deviceRate_;
    QElapsedTimer cpuShown_;  // since the CPU reading was last shown
    QList<QMetaObject::Connection> connections_;
};

}  // namespace sub::ui
