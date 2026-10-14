// Updates (update/): versions and their order, the channels, reading GitHub's
// list of releases and picking one, and the Updater on a feed in files: its
// settings (kept; the defaults by the build's channel) and what they add up to,
// a look finding an update or none, a download checked by its SHA-256 (GitHub's
// digest, or a .sha256 beside it; one that doesn't match thrown away), Install
// updates automatically downloading at once, looking by itself (and only when
// due), and the installer started as SUBstation quits.

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>
#include <vector>

#include "TestSupport.h"
#include "update/UpdateFeed.h"
#include "update/UpdateInstall.h"
#include "update/Updater.h"

using sub::app::UpdateChannel;
using sub::app::UpdateRelease;
using sub::app::Updater;
using sub::app::UpdateVersion;

namespace {

UpdateVersion v(const char* text) { return *UpdateVersion::parse(QString::fromLatin1(text)); }

const QString kSuffix = QStringLiteral("-test-x64.zip");

}  // namespace

class TestUpdates : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> dir_;
    std::vector<sub::app::UpdateInstallCommand> launched_;

    QString path(const QString& name) const { return dir_->filePath(name); }

    // A release in GitHub's form: its zip (`zip` bytes, written beside the
    // feed) with GitHub's digest or a .sha256 asset, or neither.
    enum class Checksum { Digest, File, WrongDigest, None };
    QJsonObject release(const QString& version, const QByteArray& zip, Checksum checksum = Checksum::Digest,
                        bool draft = false) {
        const QString name = QStringLiteral("SUBstation-") + version + kSuffix;
        QFile file(path(name));
        if (!file.open(QIODevice::WriteOnly)) qFatal("can't write the zip");
        file.write(zip);
        file.close();
        const QByteArray sha = QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex();
        QJsonObject asset{{QStringLiteral("name"), name},
                          {QStringLiteral("size"), qint64(zip.size())},
                          {QStringLiteral("browser_download_url"), QUrl::fromLocalFile(path(name)).toString()}};
        if (checksum == Checksum::Digest) asset[QStringLiteral("digest")] = QStringLiteral("sha256:") + QString::fromLatin1(sha);
        if (checksum == Checksum::WrongDigest) asset[QStringLiteral("digest")] = QStringLiteral("sha256:") + QString(64, QLatin1Char('0'));
        QJsonArray assets{asset};
        if (checksum == Checksum::File) {
            QFile sums(path(name + QStringLiteral(".sha256")));
            if (!sums.open(QIODevice::WriteOnly)) qFatal("can't write the checksum");
            sums.write(sha + "  " + name.toLatin1() + "\n");
            sums.close();
            assets.append(QJsonObject{{QStringLiteral("name"), name + QStringLiteral(".sha256")},
                                      {QStringLiteral("browser_download_url"),
                                       QUrl::fromLocalFile(path(name + QStringLiteral(".sha256"))).toString()}});
        }
        return {{QStringLiteral("tag_name"), QStringLiteral("v") + version},
                {QStringLiteral("name"), QStringLiteral("SUBstation ") + version},
                {QStringLiteral("draft"), draft},
                {QStringLiteral("prerelease"), version.contains(QLatin1Char('-'))},
                {QStringLiteral("html_url"), QStringLiteral("https://example.invalid/releases/v") + version},
                {QStringLiteral("published_at"), QStringLiteral("2026-10-14T07:00:00Z")},
                {QStringLiteral("body"), QStringLiteral("Notes")},
                {QStringLiteral("assets"), assets}};
    }
    QUrl feed(const QJsonArray& releases) {
        QFile file(path(QStringLiteral("releases.json")));
        if (!file.open(QIODevice::WriteOnly)) qFatal("can't write the feed");
        file.write(QJsonDocument(releases).toJson());
        file.close();
        return QUrl::fromLocalFile(file.fileName());
    }
    std::unique_ptr<Updater> updater(const QUrl& feed, bool automatic = false, int firstCheckDelayMs = 60000) {
        Updater::Options options;
        options.feed = feed;
        options.currentVersion = QStringLiteral("0.2.0");
        options.buildChannel = QStringLiteral("stable");
        options.platformSuffix = kSuffix;
        options.downloadFolder = path(QStringLiteral("downloads"));
        options.appFolder = path(QStringLiteral("app"));
        options.automatic = automatic;
        options.installable = true;
        options.firstCheckDelayMs = firstCheckDelayMs;
        options.launch = [this](const sub::app::UpdateInstallCommand& command) {
            launched_.push_back(command);
            return true;
        };
        return std::make_unique<Updater>(options);
    }
    static QString argument(const sub::app::UpdateInstallCommand& command, const QString& name) {
        const qsizetype at = command.arguments.indexOf(name);
        return at >= 0 && at + 1 < command.arguments.size() ? command.arguments[at + 1] : QString();
    }

private Q_SLOTS:
    void initTestCase() { sub::app::test::prepareApplication(); }

    void init() {
        dir_ = std::make_unique<QTemporaryDir>();
        QVERIFY(dir_->isValid());
        QDir().mkpath(path(QStringLiteral("app")));
        launched_.clear();
        QSettings().clear();
    }

    // Versions and tags read, written back, and ordered: the numbers, then a
    // release above the builds that led to it, then the builds' stamps.
    void versions() {
        const UpdateVersion stable = v("0.3.0");
        QCOMPARE(stable.channel, UpdateChannel::Stable);
        QCOMPARE(stable.toString(), QStringLiteral("0.3.0"));
        const UpdateVersion nightly = v("v0.3.0-nightly.202610140700");
        QCOMPARE(nightly.channel, UpdateChannel::Nightly);
        QCOMPARE(nightly.build, int64_t{202610140700});
        QCOMPARE(nightly.toString(), QStringLiteral("0.3.0-nightly.202610140700"));
        const UpdateVersion canary = v("0.3.0-canary.202610141532");
        QCOMPARE(canary.channel, UpdateChannel::Canary);

        QVERIFY(v("0.3.0") > v("0.2.9"));
        QVERIFY(v("0.10.0") > v("0.9.0"));
        QVERIFY(v("1.0.0") > v("0.99.99"));
        QVERIFY(stable > nightly);
        QVERIFY(stable > canary);
        QVERIFY(canary > nightly);  // (a later stamp, whatever the channel)
        QVERIFY(v("0.3.1-nightly.1") > stable);
        QVERIFY(v("0.3.0") == v("v0.3.0"));

        for (const char* bad : {"", "0.3", "0.3.0.1", "v", "0.3.0-beta.1", "0.3.0-nightly", "nightly-20261014", "x0.3.0"})
            QVERIFY2(!UpdateVersion::parse(QString::fromLatin1(bad)), bad);
    }

    // Stable is offered stable releases; Nightly those and nightlies; Canary everything.
    void channels() {
        using sub::app::channelOffers;
        QVERIFY(channelOffers(UpdateChannel::Stable, UpdateChannel::Stable));
        QVERIFY(!channelOffers(UpdateChannel::Stable, UpdateChannel::Nightly));
        QVERIFY(!channelOffers(UpdateChannel::Stable, UpdateChannel::Canary));
        QVERIFY(channelOffers(UpdateChannel::Nightly, UpdateChannel::Stable));
        QVERIFY(channelOffers(UpdateChannel::Nightly, UpdateChannel::Nightly));
        QVERIFY(!channelOffers(UpdateChannel::Nightly, UpdateChannel::Canary));
        QVERIFY(channelOffers(UpdateChannel::Canary, UpdateChannel::Nightly));
        QCOMPARE(sub::app::updateChannelNamed(QStringLiteral("canary")), std::optional(UpdateChannel::Canary));
        QVERIFY(!sub::app::updateChannelNamed(QStringLiteral("dev")));
        QCOMPARE(sub::app::nameOf(UpdateChannel::Nightly), QStringLiteral("nightly"));
    }

    // GitHub's list read: drafts and other tags left out, the platform's zip
    // and its checksum found; the newest offered on each channel picked.
    void feed() {
        QJsonArray releases{release(QStringLiteral("0.4.0"), "draft", Checksum::Digest, true),
                            release(QStringLiteral("0.3.1-canary.202610141532"), "canary"),
                            release(QStringLiteral("0.3.1-nightly.202610140700"), "nightly", Checksum::File),
                            release(QStringLiteral("0.3.0"), "stable"),
                            release(QStringLiteral("0.2.0"), "old")};
        releases.append(QJsonObject{{QStringLiteral("tag_name"), QStringLiteral("not-a-version")}});
        QFile file(feed(releases).toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString error;
        const std::vector<UpdateRelease> list = sub::app::parseUpdateReleases(file.readAll(), kSuffix, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(list.size(), size_t(4));
        const UpdateRelease& nightly = list[1];
        QCOMPARE(nightly.assetName, QStringLiteral("SUBstation-0.3.1-nightly.202610140700") + kSuffix);
        QVERIFY(nightly.sha256.isEmpty());
        QVERIFY(nightly.sha256Url.toString().endsWith(QStringLiteral(".sha256")));
        QCOMPARE(list[2].sha256, QCryptographicHash::hash("stable", QCryptographicHash::Sha256).toHex());
        QCOMPARE(list[2].pageUrl, QUrl(QStringLiteral("https://example.invalid/releases/v0.3.0")));
        QCOMPARE(list[2].title, QStringLiteral("SUBstation 0.3.0"));
        QCOMPARE(list[2].assetSize, qint64(6));
        // Another platform's: no download.
        QVERIFY(sub::app::parseUpdateReleases(QJsonDocument(releases).toJson(), QStringLiteral("-other.zip"))[2]
                    .assetUrl.isEmpty());

        const auto pick = [&](UpdateChannel channel, const char* current) {
            const auto found = sub::app::pickUpdate(list, channel, v(current));
            return found ? found->version.toString() : QString();
        };
        QCOMPARE(pick(UpdateChannel::Stable, "0.2.0"), QStringLiteral("0.3.0"));
        QCOMPARE(pick(UpdateChannel::Nightly, "0.2.0"), QStringLiteral("0.3.1-nightly.202610140700"));
        QCOMPARE(pick(UpdateChannel::Canary, "0.2.0"), QStringLiteral("0.3.1-canary.202610141532"));
        QCOMPARE(pick(UpdateChannel::Stable, "0.3.0"), QString());
        QCOMPARE(pick(UpdateChannel::Canary, "0.3.1-canary.202610141532"), QString());
        // A nightly user on the build before a release is offered the release.
        QCOMPARE(pick(UpdateChannel::Nightly, "0.3.0-nightly.202610010700"), QStringLiteral("0.3.1-nightly.202610140700"));
        QCOMPARE(pick(UpdateChannel::Stable, "0.3.0-nightly.202610010700"), QStringLiteral("0.3.0"));

        QVERIFY(sub::app::parseUpdateReleases("{", kSuffix, &error).empty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(sub::app::parseUpdateReleases("{}", kSuffix, &error).empty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(sub::app::parseUpdateReleases("[]", kSuffix, &error).empty());
        QVERIFY(error.isEmpty());
    }

    void sha256Files() {
        const QByteArray sha(64, 'a');
        QCOMPARE(sub::app::parseSha256File(sha + "  SUBstation.zip\n"), sha);
        QCOMPARE(sub::app::parseSha256File(sha.toUpper() + "\tSUBstation.zip"), sha);
        QCOMPARE(sub::app::parseSha256File("  " + sha + "\r\n"), sha);
        QVERIFY(sub::app::parseSha256File("abc  SUBstation.zip").isEmpty());
        QVERIFY(sub::app::parseSha256File(QByteArray(64, 'g')).isEmpty());
    }

    // A look is due a day after the last, or with none, or with a clock gone back.
    void due() {
        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-10-14T12:00:00Z"), Qt::ISODate);
        QVERIFY(Updater::isDue(QDateTime(), now));
        QVERIFY(!Updater::isDue(now.addSecs(-3600), now));
        QVERIFY(Updater::isDue(now.addDays(-1), now));
        QVERIFY(Updater::isDue(now.addDays(1), now));
    }

    // The settings: kept, the channel's default the build's, and in words.
    void settings() {
        {
            Updater::Options options;
            options.feed = feed({});
            options.buildChannel = QStringLiteral("nightly");
            options.automatic = false;
            options.installable = true;
            Updater nightlyBuild(options);
            QCOMPARE(nightlyBuild.channel(), QStringLiteral("nightly"));
            options.buildChannel = QStringLiteral("dev");
            Updater devBuild(options);
            QCOMPARE(devBuild.channel(), QStringLiteral("stable"));
        }
        auto updates = updater(feed({}));
        QCOMPARE(updates->channel(), QStringLiteral("stable"));
        QVERIFY(updates->autoCheck());
        QVERIFY(!updates->autoInstall());
        QCOMPARE(updates->channels().size(), 3);
        QCOMPARE(updates->channels()[0].toMap()[QStringLiteral("label")].toString(), QStringLiteral("Stable"));
        QCOMPARE(updates->channels()[0].toMap()[QStringLiteral("description")].toString(),
                 QStringLiteral("Recommended, tested releases"));
        QCOMPARE(updates->summaryTitle(), QStringLiteral("Selected: Stable"));
        QCOMPARE(updates->summary(), QStringLiteral("Only tested releases are offered. This is a development build: it "
                                                    "checks only when asked. Installation requires confirmation."));
        QCOMPARE(updates->lastCheckedText(), QStringLiteral("Never checked."));

        QSignalSpy channelChanged(updates.get(), &Updater::channelChanged);
        updates->setChannel(QStringLiteral("canary"));
        updates->setChannel(QStringLiteral("nonsense"));
        QCOMPARE(channelChanged.count(), 1);
        updates->setAutoCheck(false);
        updates->setAutoInstall(true);
        QCOMPARE(updates->summaryTitle(), QStringLiteral("Selected: Canary"));
        QVERIFY(updates->summary().endsWith(QStringLiteral("install when SUBstation closes.")));
        QCOMPARE(QSettings().value(Updater::kChannelKey).toString(), QStringLiteral("canary"));

        auto again = updater(feed({}));
        QCOMPARE(again->channel(), QStringLiteral("canary"));
        QVERIFY(!again->autoCheck());
        QVERIFY(again->autoInstall());

        Updater::Options automatic;
        automatic.feed = feed({});
        automatic.automatic = true;
        automatic.installable = false;
        QSettings().clear();
        Updater elsewhere(automatic);
        QCOMPARE(elsewhere.summary(), QStringLiteral("Only tested releases are offered. Automatic checks are enabled. "
                                                     "Updates are installed by hand on this system."));
    }

    // Check Now: none newer, then one; a download checked, ready; it installs
    // as SUBstation quits only once asked (Restart to Install: and starts again).
    void checkAndInstall() {
        auto updates = updater(feed({release(QStringLiteral("0.2.0"), "same")}));
        updates->checkNow();
        QCOMPARE(updates->state(), QStringLiteral("checking"));
        QTRY_COMPARE(updates->state(), QStringLiteral("upToDate"));
        QCOMPARE(updates->status(), QStringLiteral("SUBstation 0.2.0 is up to date."));
        QVERIFY(updates->lastCheckedText().startsWith(QStringLiteral("Last checked")));
        QVERIFY(QSettings().value(Updater::kLastCheckKey).toDateTime().isValid());

        const QByteArray zip(200000, 'z');
        feed({release(QStringLiteral("0.3.0"), zip), release(QStringLiteral("0.2.0"), "same")});
        QSignalSpy available(updates.get(), &Updater::updateAvailable);
        updates->checkNow();
        QTRY_COMPARE(updates->state(), QStringLiteral("available"));
        QCOMPARE(available.count(), 1);
        QCOMPARE(available[0][0].toString(), QStringLiteral("0.3.0"));
        QCOMPARE(updates->availableVersion(), QStringLiteral("0.3.0"));
        QCOMPARE(updates->releaseUrl(), QStringLiteral("https://example.invalid/releases/v0.3.0"));
        QCOMPARE(updates->status(), QStringLiteral("SUBstation 0.3.0 is available."));
        QVERIFY(!updates->applyOnQuit());  // (nothing downloaded)

        // An old download goes.
        QDir().mkpath(path(QStringLiteral("downloads")));
        QFile old(path(QStringLiteral("downloads/SUBstation-0.1.0") + kSuffix));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.close();
        updates->download();
        QCOMPARE(updates->state(), QStringLiteral("downloading"));
        QTRY_COMPARE(updates->state(), QStringLiteral("ready"));
        QCOMPARE(updates->progress(), 1.0);
        QVERIFY(!old.exists());
        QCOMPARE(updates->downloadedFile(), path(QStringLiteral("downloads/SUBstation-0.3.0") + kSuffix));
        QCOMPARE(sub::app::sha256OfFile(updates->downloadedFile()),
                 QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex());
        QVERIFY(!QFile::exists(updates->downloadedFile() + QStringLiteral(".part")));
        QVERIFY(!updates->installPending());
        QCOMPARE(updates->status(), QStringLiteral("SUBstation 0.3.0 is downloaded: restart SUBstation to install it."));
        updates->checkNow();  // (one is downloaded: nothing to look for)
        QCOMPARE(updates->state(), QStringLiteral("ready"));
        QVERIFY(!updates->applyOnQuit());
        QVERIFY(launched_.empty());

        QSignalSpy quit(updates.get(), &Updater::quitRequested);
        updates->installNow();
        QCOMPARE(quit.count(), 1);
        QVERIFY(updates->installPending());
        QVERIFY(updates->applyOnQuit());
        QCOMPARE(launched_.size(), size_t(1));
        const sub::app::UpdateInstallCommand& command = launched_[0];
        QCOMPARE(command.program, QStringLiteral("powershell.exe"));
        const QString script = argument(command, QStringLiteral("-File"));
        QCOMPARE(QDir::fromNativeSeparators(script), path(QStringLiteral("downloads/install-update.ps1")));
        QFile scriptFile(script);
        QVERIFY(scriptFile.open(QIODevice::ReadOnly));
        QCOMPARE(scriptFile.readAll(), sub::app::updateInstallScript());
        QCOMPARE(QDir::fromNativeSeparators(argument(command, QStringLiteral("-Package"))), updates->downloadedFile());
        QCOMPARE(QDir::fromNativeSeparators(argument(command, QStringLiteral("-AppDir"))), path(QStringLiteral("app")));
        QCOMPARE(argument(command, QStringLiteral("-ProcessId")), QString::number(QCoreApplication::applicationPid()));
        QCOMPARE(argument(command, QStringLiteral("-Restart")), QStringLiteral("1"));

        // A download changed on disk meanwhile isn't installed.
        QFile tampered(updates->downloadedFile());
        QVERIFY(tampered.open(QIODevice::Append));
        tampered.write("x");
        tampered.close();
        QVERIFY(!updates->applyOnQuit());
    }

    // Install updates automatically: an update found downloads at once, and
    // installs as SUBstation quits, without starting it again.
    void autoInstall() {
        auto updates = updater(feed({release(QStringLiteral("0.3.0"), "zip", Checksum::File)}));
        updates->setAutoInstall(true);
        updates->checkNow();
        QTRY_COMPARE(updates->state(), QStringLiteral("ready"));
        QVERIFY(updates->installPending());
        QCOMPARE(updates->status(), QStringLiteral("SUBstation 0.3.0 is downloaded: it installs when SUBstation closes."));
        QVERIFY(updates->applyOnQuit());
        QCOMPARE(launched_.size(), size_t(1));
        QCOMPARE(argument(launched_[0], QStringLiteral("-Restart")), QStringLiteral("0"));

        // Turned on with an update found already: it downloads then.
        QSettings().clear();
        auto later = updater(feed({release(QStringLiteral("0.3.0"), "zip")}));
        later->checkNow();
        QTRY_COMPARE(later->state(), QStringLiteral("available"));
        later->setAutoInstall(true);
        QTRY_COMPARE(later->state(), QStringLiteral("ready"));
    }

    // A download that doesn't match its checksum is thrown away; a release
    // without a checksum isn't downloaded; a feed that isn't there says so.
    void failures() {
        auto updates = updater(feed({release(QStringLiteral("0.3.0"), "zip", Checksum::WrongDigest)}));
        updates->checkNow();
        QTRY_COMPARE(updates->state(), QStringLiteral("available"));
        updates->download();
        QTRY_COMPARE(updates->state(), QStringLiteral("error"));
        QCOMPARE(updates->status(), QStringLiteral("The update's download doesn't match its checksum: it was thrown away."));
        QVERIFY(QDir(path(QStringLiteral("downloads"))).entryList(QDir::Files).isEmpty());
        QVERIFY(!updates->applyOnQuit());

        feed({release(QStringLiteral("0.3.0"), "zip", Checksum::None)});
        updates->checkNow();
        QTRY_COMPARE(updates->state(), QStringLiteral("available"));
        updates->download();
        QCOMPARE(updates->state(), QStringLiteral("error"));
        QVERIFY(updates->status().contains(QStringLiteral("no checksum")));

        QSettings().remove(Updater::kLastCheckKey);
        auto nowhere = updater(QUrl::fromLocalFile(path(QStringLiteral("missing.json"))));
        nowhere->checkNow();
        QTRY_COMPARE(nowhere->state(), QStringLiteral("error"));
        QVERIFY(nowhere->status().startsWith(QStringLiteral("Couldn't check for updates: ")));
        QCOMPARE(nowhere->lastCheckedText(), QStringLiteral("Never checked."));

        QFile junk(path(QStringLiteral("junk.json")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("<html>");
        junk.close();
        auto notJson = updater(QUrl::fromLocalFile(junk.fileName()));
        notJson->checkNow();
        QTRY_COMPARE(notJson->state(), QStringLiteral("error"));
    }

    // Looking by itself: soon after starting when due, not when it looked lately,
    // nor with Automatically check off; a channel changed looks again.
    void automatic() {
        const QUrl releases = feed({release(QStringLiteral("0.3.0-nightly.202610140700"), "zip"),
                                    release(QStringLiteral("0.3.0-canary.202610141532"), "zip")});
        {
            auto updates = updater(releases, true, 0);
            QTRY_COMPARE(updates->state(), QStringLiteral("upToDate"));  // (Stable: no nightly)
            updates->setChannel(QStringLiteral("nightly"));
            QTRY_COMPARE(updates->state(), QStringLiteral("available"));
            QCOMPARE(updates->availableVersion(), QStringLiteral("0.3.0-nightly.202610140700"));
            updates->setChannel(QStringLiteral("canary"));
            QTRY_COMPARE(updates->state(), QStringLiteral("available"));
            QCOMPARE(updates->availableVersion(), QStringLiteral("0.3.0-canary.202610141532"));
        }
        {
            auto updates = updater(releases, true, 0);  // (looked a moment ago)
            QTest::qWait(100);
            QCOMPARE(updates->state(), QStringLiteral("idle"));
        }
        QSettings().remove(Updater::kLastCheckKey);
        QSettings().setValue(Updater::kAutoCheckKey, false);
        {
            auto updates = updater(releases, true, 0);
            QTest::qWait(100);
            QCOMPARE(updates->state(), QStringLiteral("idle"));
            updates->setAutoCheck(true);
            QTRY_COMPARE(updates->state(), QStringLiteral("available"));
        }
        QSettings().remove(Updater::kLastCheckKey);
        {
            auto updates = updater(releases, false, 0);  // (a dev build)
            QTest::qWait(100);
            QCOMPARE(updates->state(), QStringLiteral("idle"));
        }
    }

    // Installed by the installer (its uninstaller in the folder) or unzipped by
    // hand: which download it takes; an older installer goes as a new one comes.
    void installedOrUnzipped() {
        const QString app = path(QStringLiteral("app"));
        QVERIFY(!sub::app::isInstalledCopy(app));
        QVERIFY(!updater(feed({}))->installed());
        QFile uninstaller(app + QStringLiteral("/unins000.exe"));
        QVERIFY(uninstaller.open(QIODevice::WriteOnly));
        uninstaller.close();
        QVERIFY(sub::app::isInstalledCopy(app));
        QVERIFY(updater(feed({}))->installed());
#ifdef _WIN32
        QCOMPARE(sub::app::updatePlatformSuffix(true), QStringLiteral("-windows-x64-setup.exe"));
        QCOMPARE(sub::app::updatePlatformSuffix(false), QStringLiteral("-windows-x64.zip"));
#endif

        // A release's installer, downloaded and checked as a zip is.
        Updater::Options options;
        options.feed = feed({});
        options.currentVersion = QStringLiteral("0.2.0");
        options.platformSuffix = QStringLiteral("-test-setup.exe");
        options.downloadFolder = path(QStringLiteral("downloads"));
        options.appFolder = app;
        options.automatic = false;
        options.installable = true;
        options.launch = [this](const sub::app::UpdateInstallCommand& command) {
            launched_.push_back(command);
            return true;
        };
        QDir().mkpath(path(QStringLiteral("downloads")));
        QFile old(path(QStringLiteral("downloads/SUBstation-0.1.0-test-setup.exe")));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.close();
        QJsonObject setup = release(QStringLiteral("0.3.0"), "setup");
        // (release() names the zip: the same asset as the installer.)
        QJsonArray assets = setup[QStringLiteral("assets")].toArray();
        QJsonObject asset = assets[0].toObject();
        const QString zipName = asset[QStringLiteral("name")].toString();
        const QString setupName = QStringLiteral("SUBstation-0.3.0-test-setup.exe");
        QVERIFY(QFile::rename(path(zipName), path(setupName)));
        asset[QStringLiteral("name")] = setupName;
        asset[QStringLiteral("browser_download_url")] = QUrl::fromLocalFile(path(setupName)).toString();
        setup[QStringLiteral("assets")] = QJsonArray{asset};
        options.feed = feed({setup});
        Updater updates(options);
        QVERIFY(updates.installed());
        updates.checkNow();
        QTRY_COMPARE(updates.state(), QStringLiteral("available"));
        updates.download();
        QTRY_COMPARE(updates.state(), QStringLiteral("ready"));
        QVERIFY(!old.exists());
        QCOMPARE(updates.downloadedFile(), path(QStringLiteral("downloads/") + setupName));
        updates.installNow();
        QVERIFY(updates.applyOnQuit());
        QCOMPARE(QDir::fromNativeSeparators(argument(launched_.back(), QStringLiteral("-Package"))),
                 updates.downloadedFile());
    }

    // The installer's command line, and its script's parameters.
    void installCommand() {
        const auto command = sub::app::updateInstallCommand(QStringLiteral("C:/u/install-update.ps1"),
                                                            QStringLiteral("C:/u/S.zip"), QStringLiteral("C:/S"), 42, false);
        QCOMPARE(command.program, QStringLiteral("powershell.exe"));
        QVERIFY(command.arguments.contains(QStringLiteral("Bypass")));
        QCOMPARE(argument(command, QStringLiteral("-ProcessId")), QStringLiteral("42"));
        QCOMPARE(argument(command, QStringLiteral("-Restart")), QStringLiteral("0"));
        QVERIFY(QDir::fromNativeSeparators(argument(command, QStringLiteral("-Log"))).endsWith(QStringLiteral("/u/install-update.log")));
        const QByteArray script = sub::app::updateInstallScript();
        for (const char* parameter : {"$Package", "$AppDir", "$ProcessId", "$Restart", "$Log", "robocopy", "Wait-Process", "/VERYSILENT",
                                      "Assert-SameSigner", "Get-AuthenticodeSignature"})
            QVERIFY2(script.contains(parameter), parameter);
    }
};

QTEST_GUILESS_MAIN(TestUpdates)
#include "test_updates.moc"
