#include "session/Backups.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>
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
    writer_.setMaxThreadCount(1);
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
    underWay_.reset();  // (one under way is let go of)
    writer_.waitForDone();
    if (!started_) return;
    started_ = false;
    QFile::remove(QDir(sessionsFolder()).filePath(sessionId_ + QStringLiteral(".json")));
    if (lock_) lock_->unlock();
    lock_.reset();
}

void Backups::saved() {
    changed_ = false;
    underWay_.reset();  // (one under way is of what was before)
    writeMark({});
}

void Backups::tick() { backUpInBackground(); }

void Backups::backUpInBackground() {
    if (!enabled() || !changed_ || underWay_) return;
    if (unsaved_ && !unsaved_()) {  // (undone back to what was saved)
        changed_ = false;
        writeMark({});
        return;
    }
    const quint64 backup = ++backups_;
    underWay_ = backup;
    if (!gather_) {
        if (before_) before_();
        write(backup);
        return;
    }
    gather_([self = QPointer<Backups>(this), backup] {
        if (self) self->write(backup);
    });
}

// The plug-ins' states are in the project: it is turned into JSON here, written on the writer's thread.
void Backups::write(quint64 backup) {
    if (underWay_ != backup) return;  // (saved, or finished, meanwhile)
    const QString folder = projectFolder();
    const QString file = newFile(folder);
    const QJsonObject data = backupData();
    changed_ = false;
    writing_ = file;
    // (finish() waits for it: this outlives it.)
    writer_.start([this, folder, file, data, backup] {
        const bool ok = writeJsonFile(file, data);
        if (ok) prune(folder);
        QMetaObject::invokeMethod(this, [this, file, ok, backup] { written(file, ok, backup); }, Qt::QueuedConnection);
    });
}

void Backups::written(const QString& file, bool ok, quint64 backup) {
    if (writing_ == file) writing_.clear();
    if (underWay_ != backup) return;  // (saved, or finished, meanwhile: nothing to recover from it)
    underWay_.reset();
    if (!ok) {
        changed_ = true;  // (tried again at the next interval)
        return;
    }
    writeMark(file);
    Q_EMIT backedUp(file);
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

QString Backups::newFile(const QString& folder) const {
    QDir().mkpath(folder);
    const QString stamp = QDateTime::currentDateTime().toString(kStampFormat);
    QString file = QDir(folder).filePath(stamp + kProjectExtension);
    for (int n = 2; QFileInfo::exists(file) || file == writing_; ++n)  // (two in the same second)
        file = QDir(folder).filePath(QStringLiteral("%1 (%2)%3").arg(stamp).arg(n).arg(kProjectExtension));
    return file;
}

QJsonObject Backups::backupData() const {
    QJsonObject data = projectToJson(*project_, project_->path());
    data.insert(QStringLiteral("backup_of"), project_->path());
    data.insert(QStringLiteral("backup_name"), projectName());
    return data;
}

QString Backups::backupNow() {
    if (!enabled()) return {};
    writer_.waitForDone();  // (one being written takes its name first)
    if (before_) before_();
    const QString folder = projectFolder();
    const QString file = newFile(folder);
    if (!writeJsonFile(file, backupData())) return {};
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

void Backups::prune(const QString& folder) {
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
