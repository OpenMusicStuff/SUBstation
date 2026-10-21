#pragma once
// Backups of the open project, so a crash loses at most a minute of work.
//
// While the project has unsaved changes, it is written every kIntervalMs to a
// folder of its own under the backups folder (localDataDir()/Backups, or
// SUBSTATION_BACKUPS): "<name> <8 hex digits of its path>" for a saved
// project, "Untitled <session id>" for one never saved. Each backup is a
// .gilproj named by when it was made ("2026-10-16 14-03-22.gilproj"), its
// files by their absolute paths (and relative to the project's own file, as
// Save writes them), plus "backup_of" (the project's file, "" for none) and
// "backup_name". The newest kKeep of a folder are kept: a history of the work
// between saves.
//
// Crashes are told by a mark each running session keeps: sessions/<id>.lock
// (a QLockFile, held while it runs) and sessions/<id>.json, which says the
// session's latest backup while its changes are unsaved ({"backup": path,
// "project": path, "name": text}; "backup" is "" once saved). start() looks
// for the marks of sessions whose program is gone (their lock is free: they
// ended without finish()): those with a backup are recoverable(), the newest
// first; their marks are removed. finish() removes this session's own mark.
//
// The timer's backups don't hold up the window: the plug-ins' states are
// gathered first without waiting for them (setGatherBeforeBackup(): those in
// sandboxes all at once, as they answer; those unchanged since they were last
// taken not again), then the project is turned into JSON here, and written,
// and the oldest pruned, on a thread of its own. One at a time; a backup under
// way when the project is saved (or this session finishes) is let go of.
//
// A folder of "" turns it all off (the tests' sessions).

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QLockFile>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace sub::app {

class Project;

class Backups : public QObject {
    Q_OBJECT

public:
    static constexpr int kIntervalMs = 60'000;  // how often unsaved changes are backed up
    static constexpr int kKeep = 20;            // backups kept per project
    inline static const QString kStampFormat = QStringLiteral("yyyy-MM-dd HH-mm-ss");

    // A backup: its file, the project it was of ("" for one never saved), the
    // project's name, and when it was made.
    struct Backup {
        QString path;
        QString project;
        QString name;
        QDateTime time;
    };

    // localDataDir()/Backups, or SUBSTATION_BACKUPS.
    static QString defaultFolder();

    Backups(Project* project, QString folder, QObject* parent = nullptr);
    ~Backups() override;

    bool enabled() const { return !folder_.isEmpty(); }
    const QString& folder() const { return folder_; }

    // What a backup needs first (the plug-ins' states stored in the project):
    // done now (backupNow()), or (the timer's backups) done meanwhile, `then`
    // called once it is (without it, setBeforeBackup()'s is done first). And
    // whether there are changes to back up (the session isn't clean).
    void setBeforeBackup(std::function<void()> before) { before_ = std::move(before); }
    void setGatherBeforeBackup(std::function<void(std::function<void()> then)> gather) { gather_ = std::move(gather); }
    void setUnsaved(std::function<bool()> unsaved) { unsaved_ = std::move(unsaved); }
    // The project's name for a project never saved ("Untitled").
    void setUntitledName(std::function<QString()> name) { untitledName_ = std::move(name); }

    // Finds what crashed sessions left (recoverable()), marks this one as
    // running, and backs up from now on.
    void start();
    // This session ends as it should: its mark goes (its backups stay).
    void finish();

    // The project changed: backed up at the next interval (if unsaved then).
    void changed() { changed_ = true; }
    // The project is as its file holds it (saved, opened, new): nothing to back
    // up or recover until it changes again.
    void saved();

    // Writes a backup now: its path, or "" if it couldn't (or isn't enabled).
    QString backupNow();
    // Starts one as the timer does (if there are unsaved changes, and none is
    // under way); backedUp() says when it is written.
    void backUpInBackground();
    bool backingUp() const { return underWay_.has_value(); }

    // The backups crashed sessions left, the newest first.
    const QList<Backup>& recoverable() const { return recoverable_; }
    void forgetRecoverable() { recoverable_.clear(); }

    // The backups of the open project (by its file; this session's for one
    // never saved), the newest first.
    QList<Backup> backupsOfProject() const;
    // The folder those are in (it may not exist yet).
    QString projectFolder() const;

    // What a backup file says it was of: {project, name}; none if it can't be read.
    static std::optional<std::pair<QString, QString>> backupOf(const QString& path);

Q_SIGNALS:
    void backedUp(const QString& path);

private:
    QString sessionsFolder() const { return folder_ + QStringLiteral("/sessions"); }
    QString projectName() const;
    // A new backup's file in `folder` (made), named by when it is made, and what it holds.
    QString newFile(const QString& folder) const;
    QJsonObject backupData() const;
    void writeMark(const QString& backup) const;
    static void prune(const QString& folder);
    void write(quint64 backup);
    void written(const QString& file, bool ok, quint64 backup);

private Q_SLOTS:
    void tick();  // (the timer's: a backup, if there are unsaved changes)

private:

    Project* project_;
    QString folder_;
    QString sessionId_;
    std::unique_ptr<QLockFile> lock_;
    QTimer timer_;
    bool changed_ = false;
    bool started_ = false;
    // The backup under way (gathering, or being written; saved() and finish() let go of it), the
    // last one's number, and the file being written.
    std::optional<quint64> underWay_;
    quint64 backups_ = 0;
    QString writing_;
    QThreadPool writer_;  // writes them, one at a time
    std::function<void()> before_;
    std::function<void(std::function<void()>)> gather_;
    std::function<bool()> unsaved_;
    std::function<QString()> untitledName_;
    QList<Backup> recoverable_;
};

}  // namespace sub::app
