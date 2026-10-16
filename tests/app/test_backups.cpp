// Backups (session/Backups.h): unsaved changes written to a folder of the
// project's own, the newest kept; a crashed session's latest backup offered
// when the next one starts (its lock free), not a running one's; and the
// session recovering a backup as the project it was of, unsaved.

#include "SessionFixture.h"
#include "TestSupport.h"

#include "io/Json.h"
#include "io/Serialization.h"
#include "model/Project.h"
#include "session/Backups.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLockFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

using namespace sub::app;
using sub::app::test::SessionFixture;
using sub::app::test::TempDir;

namespace {

QStringList backupNames(const QString& folder) {
    return QDir(folder).entryList({QStringLiteral("*.gilproj")}, QDir::Files, QDir::Name);
}

}  // namespace

class TestBackups : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }
    void init() { QSettings().clear(); }

    // Off without a folder: nothing written.
    void offWithoutAFolder() {
        Project project;
        Backups backups(&project, QString());
        QVERIFY(!backups.enabled());
        backups.start();
        QCOMPARE(backups.backupNow(), QString());
        QVERIFY(backups.backupsOfProject().isEmpty());
    }

    // A backup is the project as Save writes it, in its own folder, saying
    // what it is of; the newest kKeep stay.
    void backupsAreKeptPerProject() {
        TempDir dir;
        SessionFixture f;
        f.editor().setTempo(97.0);
        Backups backups(&f.project(), dir.path(QStringLiteral("Backups")));
        backups.setUntitledName([] { return QStringLiteral("Imported Set"); });
        const QString untitled = backups.backupNow();
        QVERIFY(QFileInfo(untitled).isFile());
        QVERIFY(QFileInfo(untitled).absolutePath().contains(QStringLiteral("/Untitled ")));
        QCOMPARE(Backups::backupOf(untitled)->first, QString());
        QCOMPARE(Backups::backupOf(untitled)->second, QStringLiteral("Imported Set"));

        QVERIFY(f.s().saveProjectAs(dir.path(QStringLiteral("song.gilproj"))));
        const QString first = backups.backupNow();
        QVERIFY(QFileInfo(first).absolutePath().contains(QStringLiteral("/song ")));
        QCOMPARE(Backups::backupOf(first)->first, f.project().path());
        QCOMPARE(Backups::backupOf(first)->second, QStringLiteral("song"));
        Project read;
        loadProject(read, first);
        QCOMPARE(read.tempo(), 97.0);
        // Many: only the newest are kept (two in a second: numbered).
        for (int i = 0; i < Backups::kKeep + 3; ++i) QVERIFY(!backups.backupNow().isEmpty());
        const QString folder = backups.projectFolder();
        QCOMPARE(backupNames(folder).size(), Backups::kKeep);
        const QList<Backups::Backup> listed = backups.backupsOfProject();
        QCOMPARE(listed.size(), Backups::kKeep);
        QVERIFY(listed.first().path > listed.last().path || listed.first().time >= listed.last().time);  // (newest first)
        QCOMPARE(listed.first().name, QStringLiteral("song"));
    }

    // Changed and unsaved: backed up at the interval; undone back to what was saved, not.
    void onlyUnsavedChangesAreBackedUp() {
        TempDir dir;
        Project project;
        Backups backups(&project, dir.path(QStringLiteral("Backups")));
        bool unsaved = false;
        backups.setUnsaved([&] { return unsaved; });
        backups.start();
        backups.changed();
        QMetaObject::invokeMethod(&backups, "tick");  // (private; the timer's)
        QVERIFY(backups.backupsOfProject().isEmpty());
        unsaved = true;
        backups.changed();
        QSignalSpy backedUp(&backups, &Backups::backedUp);
        QMetaObject::invokeMethod(&backups, "tick");
        QCOMPARE(backedUp.count(), 1);
        QMetaObject::invokeMethod(&backups, "tick");  // (nothing changed since)
        QCOMPARE(backedUp.count(), 1);
        backups.finish();
    }

    // A session that ended without finish() (it crashed): its latest backup is
    // offered by the next start; one that still runs (its lock held), not; one
    // that saved, not. Their marks go; the backups stay.
    void whatACrashLeftIsOffered() {
        TempDir dir;
        const QString folder = dir.path(QStringLiteral("Backups"));
        Project project;
        {
            Backups crashed(&project, folder);
            crashed.start();
            crashed.changed();
            QVERIFY(!crashed.backupNow().isEmpty());
        }
        QVERIFY(QDir(folder + QStringLiteral("/sessions")).entryList({QStringLiteral("*.json")}).isEmpty());  // (finished)

        // A mark left as a crash leaves it: its backup, no live lock.
        const QString backup = Backups(&project, folder).backupNow();
        QVERIFY(!backup.isEmpty());
        QVERIFY(writeJsonFile(folder + QStringLiteral("/sessions/gone.json"),
                              {{QStringLiteral("backup"), backup}, {QStringLiteral("project"), QString()},
                               {QStringLiteral("name"), QStringLiteral("Lost Song")}}));
        QVERIFY(writeJsonFile(folder + QStringLiteral("/sessions/saved.json"),
                              {{QStringLiteral("backup"), QString()}, {QStringLiteral("name"), QStringLiteral("Saved")}}));
        // One that still runs: its lock held (by this process, as another SUBstation would).
        QVERIFY(writeJsonFile(folder + QStringLiteral("/sessions/running.json"),
                              {{QStringLiteral("backup"), backup}, {QStringLiteral("name"), QStringLiteral("Running")}}));
        QLockFile running(folder + QStringLiteral("/sessions/running.lock"));
        QVERIFY(running.tryLock(0));

        Backups next(&project, folder);
        next.start();
        QCOMPARE(next.recoverable().size(), 1);
        QCOMPARE(next.recoverable().first().name, QStringLiteral("Lost Song"));
        QCOMPARE(next.recoverable().first().path, backup);
        QVERIFY(!QFileInfo::exists(folder + QStringLiteral("/sessions/gone.json")));
        QVERIFY(!QFileInfo::exists(folder + QStringLiteral("/sessions/saved.json")));
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/sessions/running.json")));
        QVERIFY(QFileInfo::exists(backup));
        next.forgetRecoverable();
        QVERIFY(next.recoverable().isEmpty());
        next.finish();
    }

    // Recovered: the project it was of, with that project's file, unsaved.
    void theSessionRecoversABackup() {
        TempDir dir;
        SessionFixture f;
        Session& s = f.s();
        f.editor().addAudioTrack();
        const QString path = dir.path(QStringLiteral("song.gilproj"));
        QVERIFY(s.saveProjectAs(path));
        f.editor().setTempo(133.0);
        f.editor().addMidiTrack(-1, {}, QStringLiteral("synth"));
        Backups backups(&f.project(), dir.path(QStringLiteral("Backups")));
        const QString backup = backups.backupNow();
        QVERIFY(!backup.isEmpty());

        s.newProject();
        QVERIFY(f.project().tracks().empty());
        QSignalSpy opened(&s, &Session::projectOpened);
        QVERIFY(s.recoverBackup(backup));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(f.project().tempo(), 133.0);
        QCOMPARE(f.project().tracks().size(), size_t{2});
        QCOMPARE(f.project().path(), path);  // (Save writes the project's own file)
        QVERIFY(!s.clean());
        QCOMPARE(s.title(), QStringLiteral("song* - SUBstation"));
        QVERIFY(f.lastMessage().startsWith(QStringLiteral("Recovered song from its backup of ")));

        // One that can't be read: a warning, the project as it was.
        QSignalSpy warnings(&s, &Session::warning);
        QVERIFY(!s.recoverBackup(dir.path(QStringLiteral("nothing.gilproj"))));
        QCOMPARE(warnings.count(), 1);
        QCOMPARE(f.project().tempo(), 133.0);
    }
};

QTEST_GUILESS_MAIN(TestBackups)
#include "test_backups.moc"
