#pragma once
// Preferences › Updates, without its widgets (docs/app/updates.md,
// docs/guide/updates.md): which releases are offered (the channel: Stable,
// Nightly or Canary), whether SUBstation looks for them by itself (once a day,
// and soon after it starts), and whether one found downloads in the background
// and installs when SUBstation closes. Kept in QSettings (updates/...).
//
// A check reads the feed (GitHub's list of the repository's releases;
// SUBSTATION_UPDATE_FEED, any URL, instead) and picks the newest release the
// channel offers that is newer than this build (UpdateFeed.h). Download fetches
// the release's zip for this platform beside the settings (AppLocalDataLocation/
// updates), checking its SHA-256: one that doesn't match is thrown away. Then
// either it installs as SUBstation closes (Install updates automatically), or
// installNow() asks the UI to close SUBstation (asking about unsaved changes as
// Quit does) and it installs and starts again. applyOnQuit(), from main(), does
// the installing (UpdateInstall.h): on Windows only; elsewhere an update is
// offered with its release page, to install by hand.
//
// A build by hand (the "dev" channel: AppInfo.h) never checks by itself: Check
// Now still does.

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <QCryptographicHash>

#include <functional>
#include <memory>
#include <optional>

#include "update/UpdateFeed.h"
#include "update/UpdateInstall.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace sub::app {

class Updater : public QObject {
    Q_OBJECT
    // "stable", "nightly" or "canary".
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)
    // [{value (the channel), label ("Stable"), description ("Recommended, tested releases")}], Stable first.
    Q_PROPERTY(QVariantList channels READ channels CONSTANT)
    // Looks for updates by itself (a day after the last look, and soon after starting).
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)
    // An update found downloads at once and installs when SUBstation closes.
    Q_PROPERTY(bool autoInstall READ autoInstall WRITE setAutoInstall NOTIFY autoInstallChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    // The channel this build was released on: "dev" for a build by hand.
    Q_PROPERTY(QString buildChannel READ buildChannel CONSTANT)
    // Whether this build looks for updates by itself at all (not a "dev" build).
    Q_PROPERTY(bool automaticChecks READ automaticChecks CONSTANT)
    // Whether updates install here (Windows), or are only offered.
    Q_PROPERTY(bool canInstall READ canInstall CONSTANT)
    // "idle", "checking", "upToDate", "available", "downloading", "ready" (downloaded, checked) or "error".
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // What it is doing, or found, in words.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    // The download's progress, 0 to 1.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // The update found ("" if none), its release's title and page.
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY stateChanged)
    Q_PROPERTY(QString releaseTitle READ releaseTitle NOTIFY stateChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY stateChanged)
    // The settings in words: "Selected: Stable", and what that means.
    Q_PROPERTY(QString summaryTitle READ summaryTitle NOTIFY summaryChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)
    // When it last looked: "Last checked 14 Oct 2026 09:12", or "Never checked".
    Q_PROPERTY(QString lastChecked READ lastCheckedText NOTIFY lastCheckedChanged)
    // The update downloaded installs when SUBstation closes.
    Q_PROPERTY(bool installPending READ installPending NOTIFY stateChanged)

public:
    struct Options {
        QUrl feed;               // empty: SUBSTATION_UPDATE_FEED, else GitHub's releases of updateRepository()
        QString currentVersion;  // empty: version()
        QString buildChannel;    // empty: releaseChannel()
        QString platformSuffix;  // empty: updatePlatformSuffix()
        QString downloadFolder;  // empty: AppLocalDataLocation/updates
        QString appFolder;       // where the update goes; empty: the application's folder
        std::optional<bool> automatic;    // looks by itself; default: not a "dev" build
        std::optional<bool> installable;  // default: canInstallUpdates()
        int firstCheckDelayMs = 15000;    // after starting
        // Starts the installer (default: detached, QProcess::startDetached); false if it couldn't.
        std::function<bool(const UpdateInstallCommand&)> launch;
    };

    inline static const QString kChannelKey = QStringLiteral("updates/channel");
    inline static const QString kAutoCheckKey = QStringLiteral("updates/auto_check");
    inline static const QString kAutoInstallKey = QStringLiteral("updates/auto_install");
    inline static const QString kLastCheckKey = QStringLiteral("updates/last_check");
    static constexpr qint64 kCheckIntervalSecs = 24 * 60 * 60;

    explicit Updater(QObject* parent = nullptr);
    explicit Updater(Options options, QObject* parent = nullptr);
    ~Updater() override;

    QString channel() const { return nameOf(channel_); }
    void setChannel(const QString& channel);
    QVariantList channels() const;
    bool autoCheck() const { return autoCheck_; }
    void setAutoCheck(bool on);
    bool autoInstall() const { return autoInstall_; }
    void setAutoInstall(bool on);
    QString currentVersion() const { return currentText_; }
    QString buildChannel() const { return buildChannel_; }
    bool automaticChecks() const { return automatic_; }
    bool canInstall() const { return installable_; }
    QString state() const;
    QString status() const;
    double progress() const { return progress_; }
    QString availableVersion() const { return found_ ? found_->version.toString() : QString(); }
    QString releaseTitle() const { return found_ ? found_->title : QString(); }
    QString releaseUrl() const { return found_ ? found_->pageUrl.toString() : QString(); }
    QString summaryTitle() const;
    QString summary() const;
    QString lastCheckedText() const;
    QDateTime lastChecked() const;
    bool installPending() const;
    QUrl feed() const { return feed_; }
    QString downloadedFile() const { return downloaded_; }

    // Looks now (unless it is looking, or downloading).
    Q_INVOKABLE void checkNow();
    // Downloads the update found (and checks it).
    Q_INVOKABLE void download();
    // The update downloaded installs, and SUBstation starts again, once it closes:
    // quitRequested asks the UI to close it.
    Q_INVOKABLE void installNow();

    // As SUBstation quits (main): installs the update downloaded if Install
    // updates automatically is on or installNow() asked. True if the installer started.
    bool applyOnQuit();

    // Whether a look is due: never looked, or a day since (or a clock that went back).
    static bool isDue(const QDateTime& last, const QDateTime& now);

Q_SIGNALS:
    void channelChanged();
    void autoCheckChanged();
    void autoInstallChanged();
    void stateChanged();
    void progressChanged();
    void summaryChanged();
    void lastCheckedChanged();
    // A look found `version` (newer than this build, on the channel).
    void updateAvailable(const QString& version);
    // installNow(): the UI closes SUBstation, as Quit.
    void quitRequested();

private:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Error };

    void setState(State state, const QString& error = QString());
    void abort();
    void scheduleCheck();
    void checkIfDue();
    void finishCheck(QNetworkReply* reply);
    void fetchChecksum();
    void fetchAsset();
    void finishAsset(QNetworkReply* reply);
    QNetworkReply* get(const QUrl& url, int timeoutMs, const QByteArray& accept);
    QString folder() const;

    QNetworkAccessManager* network_ = nullptr;
    QPointer<QNetworkReply> reply_;
    QTimer timer_;  // every hour while looking by itself: is a look due?
    UpdateChannel channel_ = UpdateChannel::Stable;
    bool autoCheck_ = true;
    bool autoInstall_ = false;
    bool automatic_ = false;
    bool installable_ = false;
    bool installRequested_ = false;
    QUrl feed_;
    UpdateVersion current_;
    QString currentText_;
    QString buildChannel_;
    QString platformSuffix_;
    QString downloadFolder_;
    QString appFolder_;
    int firstCheckDelayMs_ = 15000;
    std::function<bool(const UpdateInstallCommand&)> launch_;

    State state_ = State::Idle;
    QString error_;
    double progress_ = 0.0;
    std::optional<UpdateRelease> found_;
    QByteArray expectedSha256_;
    std::unique_ptr<QFile> partial_;
    std::unique_ptr<QCryptographicHash> hash_;
    QString downloaded_;
};

}  // namespace sub::app
