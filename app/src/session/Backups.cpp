#include "session/Backups.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>

#include "browser/PathKeys.h"
#include "io/Json.h"
#include "io/Serialization.h"
#include "model/Paths.h"
#include "model/Project.h"

namespace sub::app {

namespace {

QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return parseJsonObject(file.readAll()).value_or(QJsonObject());
}

// A folder's backups, the newest first (their names sort by when they were made).
QFileInfoList backupFiles(const QString& folder) {
    QFileInfoList files = QDir(folder).entryInfoList({QLatin1Char('*') + kProjectExtension}, QDir::Files, QDir::Name);
    std::reverse(files.begin(), files.end());
    return files;
}

}  // namespace

QString Backups::defaultFolder() { return localDataFile("SUBSTATION_BACKUPS", QStringLiteral("Backups")); }

Backups::Backups(Project* project, QString folder, QObject* parent)
    : QObject(parent),
      project_(project),
      folder_(std::move(folder)),
      sessionId_(QUuid::createUuid().toString(QUuid::Id128).left(12)) {
    timer_.setInterval(kIntervalMs);
    connect(&timer_, &QTimer::timeout, this, &Backups::tick);
}

Backups::~Backups() { finish(); }

void Backups::start() {
    if (!enabled() || started_) return;
    started_ = true;
    const QString sessions = sessionsFolder();
    QDir().mkpath(sessions);
    // The marks of sessions that ended without finish(): their locks are free.
    for (const QFileInfo& mark : QDir(sessions).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        if (mark.completeBaseName() == sessionId_) continue;
        QLockFile lock(QDir(sessions).filePath(mark.completeBaseName() + QStringLiteral(".lock")));
        lock.setStaleLockTime(0);  // (only a session that is gone lets go: not one that has run a while)
        if (!lock.tryLock(0)) continue;  // it still runs (another SUBstation)
        const QJsonObject data = readObject(mark.filePath());
        const QString backup = data.value(QStringLiteral("backup")).toString();
        if (!backup.isEmpty() && QFileInfo(backup).isFile()) {
            recoverable_.append({backup, data.value(QStringLiteral("project")).toString(),
                                 data.value(QStringLiteral("name")).toString(), QFileInfo(backup).lastModified()});
        }
        QFile::remove(mark.filePath());
        lock.unlock();  // (removes its file)
    }
    std::sort(recoverable_.begin(), recoverable_.end(), [](const Backup& a, const Backup& b) { return a.time > b.time; });
    lock_ = std::make_unique<QLockFile>(QDir(sessions).filePath(sessionId_ + QStringLiteral(".lock")));
    lock_->setStaleLockTime(0);
    lock_->tryLock(0);
    writeMark({});
    timer_.start();
}

void Backups::finish() {
    timer_.stop();
    if (!started_) return;
    started_ = false;
    QFile::remove(QDir(sessionsFolder()).filePath(sessionId_ + QStringLiteral(".json")));
    if (lock_) lock_->unlock();
    lock_.reset();
}

void Backups::saved() {
    changed_ = false;
    writeMark({});
}

void Backups::tick() {
    if (!changed_) return;
    if (unsaved_ && !unsaved_()) {  // (undone back to what was saved)
        changed_ = false;
        writeMark({});
        return;
    }
    backupNow();
}

QString Backups::projectName() const {
    if (project_->path().isEmpty()) return untitledName_ ? untitledName_() : QStringLiteral("Untitled");
    return QFileInfo(project_->path()).completeBaseName();
}

QString Backups::projectFolder() const {
    if (!enabled()) return {};
    const QString path = project_->path();
    if (path.isEmpty()) return folder_ + QStringLiteral("/Untitled ") + sessionId_;
    const QByteArray hash = QCryptographicHash::hash(pathIdentity(path).toUtf8(), QCryptographicHash::Md5).toHex();
    return folder_ + u'/' + QFileInfo(path).completeBaseName() + u' ' + QString::fromLatin1(hash.left(8));
}

QString Backups::backupNow() {
    if (!enabled()) return {};
    if (before_) before_();
    const QString folder = projectFolder();
    QDir().mkpath(folder);
    const QString stamp = QDateTime::currentDateTime().toString(kStampFormat);
    QString file = QDir(folder).filePath(stamp + kProjectExtension);
    for (int n = 2; QFileInfo::exists(file); ++n)  // (two in the same second)
        file = QDir(folder).filePath(QStringLiteral("%1 (%2)%3").arg(stamp).arg(n).arg(kProjectExtension));
    QJsonObject data = projectToJson(*project_, project_->path());
    data.insert(QStringLiteral("backup_of"), project_->path());
    data.insert(QStringLiteral("backup_name"), projectName());
    if (!writeJsonFile(file, data)) return {};
    changed_ = false;
    writeMark(file);
    prune(folder);
    Q_EMIT backedUp(file);
    return file;
}

void Backups::writeMark(const QString& backup) const {
    if (!started_) return;
    const QJsonObject mark{{QStringLiteral("backup"), backup},
                           {QStringLiteral("project"), project_->path()},
                           {QStringLiteral("name"), projectName()}};
    writeJsonFile(QDir(sessionsFolder()).filePath(sessionId_ + QStringLiteral(".json")), mark);
}

void Backups::prune(const QString& folder) const {
    const QFileInfoList files = backupFiles(folder);
    for (qsizetype i = kKeep; i < files.size(); ++i) QFile::remove(files[i].filePath());
}

QList<Backups::Backup> Backups::backupsOfProject() const {
    QList<Backup> backups;
    if (!enabled()) return backups;
    for (const QFileInfo& file : backupFiles(projectFolder())) {
        QDateTime time = QDateTime::fromString(file.completeBaseName().left(kStampFormat.size()), kStampFormat);
        if (!time.isValid()) time = file.lastModified();
        backups.append({file.filePath(), project_->path(), projectName(), time});
    }
    return backups;
}

std::optional<std::pair<QString, QString>> Backups::backupOf(const QString& path) {
    const QJsonObject data = readObject(path);
    if (data.isEmpty()) return std::nullopt;
    return std::pair{data.value(QStringLiteral("backup_of")).toString(), data.value(QStringLiteral("backup_name")).toString()};
}

}  // namespace sub::app
