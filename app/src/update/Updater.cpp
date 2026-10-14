#include "update/Updater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>

#include "AppInfo.h"

namespace sub::app {

namespace {

constexpr int kFeedTimeoutMs = 30000;
constexpr int kDownloadTimeoutMs = 60000;  // (without a byte for this long)
constexpr int kTimerMs = 60 * 60 * 1000;

struct ChannelText {
    UpdateChannel channel;
    const char* label;
    const char* description;
    const char* offers;  // the summary's first sentence
};

constexpr ChannelText kChannelTexts[] = {
    {UpdateChannel::Stable, "Stable", "Recommended, tested releases", "Only tested releases are offered."},
    {UpdateChannel::Nightly, "Nightly", "Newest daily features",
     "A build of the day's changes is offered each day, and tested releases."},
    {UpdateChannel::Canary, "Canary", "Experimental builds from recent commits",
     "A build of every change is offered as it is made: the newest, and the least tested."},
};

const ChannelText& textOf(UpdateChannel channel) {
    for (const ChannelText& text : kChannelTexts) {
        if (text.channel == channel) return text;
    }
    return kChannelTexts[0];
}

QUrl defaultFeed() {
    const QString overridden = qEnvironmentVariable("SUBSTATION_UPDATE_FEED");
    if (!overridden.isEmpty()) return QUrl::fromUserInput(overridden);
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=50").arg(updateRepository()));
}

bool launchDetached(const UpdateInstallCommand& command) {
    return QProcess::startDetached(command.program, command.arguments);
}

QString replyError(QNetworkReply* reply) {
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (http >= 400) return QStringLiteral("the server answered %1").arg(http);
    return reply->errorString();
}

}  // namespace

Updater::Updater(QObject* parent) : Updater(Options{}, parent) {}

Updater::Updater(Options options, QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
    feed_ = options.feed.isEmpty() ? defaultFeed() : options.feed;
    currentText_ = options.currentVersion.isEmpty() ? version() : options.currentVersion;
    current_ = UpdateVersion::parse(currentText_).value_or(UpdateVersion{});
    buildChannel_ = options.buildChannel.isEmpty() ? releaseChannel() : options.buildChannel;
    platformSuffix_ = options.platformSuffix.isEmpty() ? updatePlatformSuffix() : options.platformSuffix;
    downloadFolder_ = options.downloadFolder;
    appFolder_ = options.appFolder;
    automatic_ = options.automatic.value_or(buildChannel_ != QLatin1String("dev"));
    installable_ = options.installable.value_or(canInstallUpdates());
    firstCheckDelayMs_ = options.firstCheckDelayMs;
    launch_ = options.launch ? std::move(options.launch) : launchDetached;

    // Without a choice: the channel this build came from (a nightly's is Nightly).
    const QSettings settings;
    const QString saved = settings.value(kChannelKey).toString();
    channel_ = updateChannelNamed(saved).value_or(updateChannelNamed(buildChannel_).value_or(UpdateChannel::Stable));
    autoCheck_ = settings.value(kAutoCheckKey, true).toBool();
    autoInstall_ = settings.value(kAutoInstallKey, false).toBool();

    timer_.setInterval(kTimerMs);
    connect(&timer_, &QTimer::timeout, this, &Updater::checkIfDue);
    scheduleCheck();
}

Updater::~Updater() { abort(); }

QString Updater::folder() const {
    if (!downloadFolder_.isEmpty()) return downloadFolder_;
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/updates");
}

void Updater::setChannel(const QString& name) {
    const std::optional<UpdateChannel> channel = updateChannelNamed(name);
    if (!channel || *channel == channel_) return;
    channel_ = *channel;
    QSettings().setValue(kChannelKey, name);
    Q_EMIT channelChanged();
    Q_EMIT summaryChanged();
    // What was found may not be offered now: look again.
    abort();
    found_.reset();
    installRequested_ = false;
    setState(State::Idle);
    if (automatic_ && autoCheck_) checkNow();
}

QVariantList Updater::channels() const {
    QVariantList list;
    for (const ChannelText& text : kChannelTexts) {
        list.append(QVariantMap{{QStringLiteral("value"), nameOf(text.channel)},
                                {QStringLiteral("label"), QString::fromLatin1(text.label)},
                                {QStringLiteral("description"), QString::fromLatin1(text.description)}});
    }
    return list;
}

void Updater::setAutoCheck(bool on) {
    if (on == autoCheck_) return;
    autoCheck_ = on;
    QSettings().setValue(kAutoCheckKey, on);
    Q_EMIT autoCheckChanged();
    Q_EMIT summaryChanged();
    scheduleCheck();
}

void Updater::setAutoInstall(bool on) {
    if (on == autoInstall_) return;
    autoInstall_ = on;
    QSettings().setValue(kAutoInstallKey, on);
    Q_EMIT autoInstallChanged();
    Q_EMIT summaryChanged();
    if (on && state_ == State::Available) download();
    Q_EMIT stateChanged();  // (installPending, and the status saying when it installs)
}

QString Updater::state() const {
    switch (state_) {
        case State::Idle: return QStringLiteral("idle");
        case State::Checking: return QStringLiteral("checking");
        case State::UpToDate: return QStringLiteral("upToDate");
        case State::Available: return QStringLiteral("available");
        case State::Downloading: return QStringLiteral("downloading");
        case State::Ready: return QStringLiteral("ready");
        case State::Error: return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

QString Updater::status() const {
    const QString found = found_ ? QStringLiteral("SUBstation ") + found_->version.toString() : QString();
    switch (state_) {
        case State::Idle: return QStringLiteral("SUBstation %1.").arg(currentText_);
        case State::Checking: return QStringLiteral("Checking for updates…");
        case State::UpToDate: return QStringLiteral("SUBstation %1 is up to date.").arg(currentText_);
        case State::Available:
            if (!installable_) return found + QStringLiteral(" is available: download it from its release page.");
            if (found_->assetUrl.isEmpty())
                return found + QStringLiteral(" is available, without a download for this system: see its release page.");
            return found + QStringLiteral(" is available.");
        case State::Downloading: return QStringLiteral("Downloading %1…").arg(found);
        case State::Ready:
            if (installPending()) return found + QStringLiteral(" is downloaded: it installs when SUBstation closes.");
            return found + QStringLiteral(" is downloaded: restart SUBstation to install it.");
        case State::Error: return error_;
    }
    return QString();
}

QString Updater::summaryTitle() const {
    return QStringLiteral("Selected: %1").arg(QString::fromLatin1(textOf(channel_).label));
}

QString Updater::summary() const {
    QString text = QString::fromLatin1(textOf(channel_).offers) + QLatin1Char(' ');
    if (!automatic_) text += QStringLiteral("This is a development build: it checks only when asked. ");
    else if (autoCheck_) text += QStringLiteral("Automatic checks are enabled. ");
    else text += QStringLiteral("Automatic checks are off. ");
    if (!installable_) text += QStringLiteral("Updates are installed by hand on this system.");
    else if (autoInstall_) text += QStringLiteral("Updates download in the background and install when SUBstation closes.");
    else text += QStringLiteral("Installation requires confirmation.");
    return text;
}

QDateTime Updater::lastChecked() const { return QSettings().value(kLastCheckKey).toDateTime(); }

QString Updater::lastCheckedText() const {
    const QDateTime last = lastChecked();
    if (!last.isValid()) return QStringLiteral("Never checked.");
    return QStringLiteral("Last checked %1.").arg(QLocale().toString(last.toLocalTime(), QLocale::ShortFormat));
}

bool Updater::installPending() const {
    return state_ == State::Ready && installable_ && (autoInstall_ || installRequested_);
}

bool Updater::isDue(const QDateTime& last, const QDateTime& now) {
    if (!last.isValid()) return true;
    const qint64 since = last.secsTo(now);
    return since < 0 || since >= kCheckIntervalSecs;
}

void Updater::scheduleCheck() {
    if (!automatic_ || !autoCheck_) {
        timer_.stop();
        return;
    }
    timer_.start();
    QTimer::singleShot(firstCheckDelayMs_, this, &Updater::checkIfDue);
}

void Updater::checkIfDue() {
    if (!automatic_ || !autoCheck_) return;
    if (state_ == State::Checking || state_ == State::Downloading || state_ == State::Ready) return;
    if (isDue(lastChecked(), QDateTime::currentDateTimeUtc())) checkNow();
}

void Updater::setState(State state, const QString& error) {
    state_ = state;
    error_ = error;
    Q_EMIT stateChanged();
}

void Updater::abort() {
    if (reply_) {
        QNetworkReply* reply = reply_;
        reply_ = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (partial_) {
        partial_->remove();
        partial_.reset();
    }
    hash_.reset();
}

QNetworkReply* Updater::get(const QUrl& url, int timeoutMs, const QByteArray& accept) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SUBstation/%1").arg(currentText_));
    request.setRawHeader("Accept", accept);
    request.setTransferTimeout(timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return network_->get(request);
}

void Updater::checkNow() {
    if (state_ == State::Checking || state_ == State::Downloading) return;
    if (state_ == State::Ready) return;  // (one is downloaded already: it installs, or waits to)
    found_.reset();
    setState(State::Checking);
    reply_ = get(feed_, kFeedTimeoutMs, "application/vnd.github+json");
    QNetworkReply* reply = reply_;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishCheck(reply); });
}

void Updater::finishCheck(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply != reply_) return;
    reply_ = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        setState(State::Error, QStringLiteral("Couldn't check for updates: %1.").arg(replyError(reply)));
        return;
    }
    QString error;
    const std::vector<UpdateRelease> releases = parseUpdateReleases(reply->readAll(), platformSuffix_, &error);
    if (!error.isEmpty()) {
        setState(State::Error, QStringLiteral("Couldn't check for updates: ") + error);
        return;
    }
    QSettings().setValue(kLastCheckKey, QDateTime::currentDateTimeUtc());
    Q_EMIT lastCheckedChanged();
    found_ = pickUpdate(releases, channel_, current_);
    if (!found_) {
        setState(State::UpToDate);
        return;
    }
    setState(State::Available);
    Q_EMIT updateAvailable(found_->version.toString());
    if (autoInstall_ && installable_) download();
}

void Updater::download() {
    if (state_ != State::Available && !(state_ == State::Error && found_)) return;
    if (!found_ || found_->assetUrl.isEmpty() || !installable_) return;
    if (found_->sha256.isEmpty() && found_->sha256Url.isEmpty()) {
        setState(State::Error, QStringLiteral("SUBstation %1 has no checksum to check its download by: download it "
                                              "from its release page.")
                                   .arg(found_->version.toString()));
        return;
    }
    progress_ = 0.0;
    Q_EMIT progressChanged();
    setState(State::Downloading);
    expectedSha256_ = found_->sha256;
    if (expectedSha256_.isEmpty()) fetchChecksum();
    else fetchAsset();
}

void Updater::fetchChecksum() {
    reply_ = get(found_->sha256Url, kFeedTimeoutMs, "*/*");
    QNetworkReply* reply = reply_;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != reply_) return;
        reply_ = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            setState(State::Error, QStringLiteral("Couldn't download the update's checksum: %1.").arg(replyError(reply)));
            return;
        }
        expectedSha256_ = parseSha256File(reply->readAll());
        if (expectedSha256_.isEmpty()) {
            setState(State::Error, QStringLiteral("The update's checksum isn't one."));
            return;
        }
        fetchAsset();
    });
}

void Updater::fetchAsset() {
    const QString dir = folder();
    QDir().mkpath(dir);
    // (Earlier downloads go: only the newest is kept.)
    for (const QFileInfo& old : QDir(dir).entryInfoList({QStringLiteral("*.zip"), QStringLiteral("*.part")}, QDir::Files))
        QFile::remove(old.absoluteFilePath());
    partial_ = std::make_unique<QFile>(dir + QLatin1Char('/') + found_->assetName + QStringLiteral(".part"));
    if (!partial_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString error = partial_->errorString();
        partial_.reset();
        setState(State::Error, QStringLiteral("Couldn't save the update: %1.").arg(error));
        return;
    }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    reply_ = get(found_->assetUrl, kDownloadTimeoutMs, "application/octet-stream");
    QNetworkReply* reply = reply_;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != reply_ || !partial_) return;
        const QByteArray data = reply->readAll();
        hash_->addData(data);
        if (partial_->write(data) != data.size()) {
            const QString error = partial_->errorString();
            abort();
            setState(State::Error, QStringLiteral("Couldn't save the update: %1.").arg(error));
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
        if (reply != reply_) return;
        if (total <= 0) total = found_ ? found_->assetSize : 0;
        if (total <= 0) return;
        progress_ = std::clamp(double(received) / double(total), 0.0, 1.0);
        Q_EMIT progressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishAsset(reply); });
}

void Updater::finishAsset(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply != reply_ || !partial_) return;
    reply_ = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        const QString error = replyError(reply);
        abort();
        setState(State::Error, QStringLiteral("Couldn't download the update: %1.").arg(error));
        return;
    }
    const QByteArray rest = reply->readAll();
    hash_->addData(rest);
    partial_->write(rest);
    partial_->close();
    const QByteArray sha256 = hash_->result().toHex();
    hash_.reset();
    if (sha256 != expectedSha256_) {
        partial_->remove();
        partial_.reset();
        setState(State::Error, QStringLiteral("The update's download doesn't match its checksum: it was thrown away."));
        return;
    }
    const QString path = partial_->fileName().chopped(int(QStringLiteral(".part").size()));
    QFile::remove(path);
    if (!partial_->rename(path)) {
        const QString error = partial_->errorString();
        partial_->remove();
        partial_.reset();
        setState(State::Error, QStringLiteral("Couldn't save the update: %1.").arg(error));
        return;
    }
    partial_.reset();
    downloaded_ = path;
    progress_ = 1.0;
    Q_EMIT progressChanged();
    setState(State::Ready);
}

void Updater::installNow() {
    if (state_ != State::Ready || !installable_) return;
    installRequested_ = true;
    Q_EMIT stateChanged();
    Q_EMIT quitRequested();
}

bool Updater::applyOnQuit() {
    if (!installPending() || downloaded_.isEmpty() || !QFileInfo::exists(downloaded_)) return false;
    // (Checked again: the file waited on disk meanwhile.)
    if (sha256OfFile(downloaded_) != expectedSha256_) return false;
    const QString script = folder() + QStringLiteral("/install-update.ps1");
    {
        QSaveFile file(script);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(updateInstallScript());
        if (!file.commit()) return false;
    }
    const QString appDir = appFolder_.isEmpty() ? QCoreApplication::applicationDirPath() : appFolder_;
    return launch_(updateInstallCommand(script, downloaded_, appDir, QCoreApplication::applicationPid(),
                                        installRequested_));
}

}  // namespace sub::app
