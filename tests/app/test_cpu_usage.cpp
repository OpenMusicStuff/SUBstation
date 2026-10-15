// View › CPU Usage's model (CpuUsage): its rows are the project's tracks in
// arrangement order (then the returns and the master), each followed by its
// devices (a rack's after it, named after their chain), and it measures only
// while active. What the engine measures is tested with the engine (test_midi_input.cpp).

#include "BridgeTestSupport.h"
#include "TestSupport.h"

#include "model/Automation.h"
#include "session/CpuUsage.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace sub::app;
using sub::app::test::Studio;

namespace {

QStringList column(const CpuUsage& usage, CpuUsage::Role role) {
    QStringList out;
    for (int row = 0; row < usage.rowCount(); ++row) out << usage.data(usage.index(row), role).toString();
    return out;
}

}  // namespace

class TestCpuUsage : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void tracksThenTheirDevices() {
        Studio studio;
        const QString drums = studio.edit.addAudioTrack(QStringLiteral("Drums"));
        const QString eq = studio.edit.addDevice(drums, QStringLiteral("eq"));
        const QString utility = studio.edit.addDevice(drums, QStringLiteral("utility"));
        const QString bass = studio.edit.addAudioTrack(QStringLiteral("Bass"));
        const QString group = studio.edit.groupTracks({bass});
        const QString verb = studio.edit.addReturnTrack(QStringLiteral("Verb"));
        CpuUsage usage(&studio.project, studio.bridge.get());
        QAbstractItemModelTester tester(&usage, QAbstractItemModelTester::FailureReportingMode::QtTest);
        usage.update();
        QCOMPARE(column(usage, CpuUsage::RowIdRole), (QStringList{drums, eq, utility, group, bass, verb, kMaster}));
        QCOMPARE(column(usage, CpuUsage::IsTrackRole),
                 (QStringList{"true", "false", "false", "true", "true", "true", "true"}));
        QCOMPARE(column(usage, CpuUsage::DepthRole), (QStringList{"0", "1", "1", "0", "1", "0", "0"}));  // Bass is in the group
        QCOMPARE(column(usage, CpuUsage::TrackIdRole), (QStringList{drums, drums, drums, group, bass, verb, kMaster}));
        QCOMPARE(usage.data(usage.index(0), CpuUsage::NameRole).toString(), QStringLiteral("Drums"));
        QCOMPARE(usage.data(usage.index(6), CpuUsage::NameRole).toString(), QStringLiteral("Main"));
        // Nothing measured: nothing costs anything.
        for (int row = 0; row < usage.rowCount(); ++row) QCOMPARE(usage.data(usage.index(row), CpuUsage::CpuRole).toDouble(), 0.0);
        QCOMPARE(usage.total(), 0.0);
    }

    void aRacksDevicesFollowIt() {
        Studio studio;
        const QString track = studio.edit.addAudioTrack(QStringLiteral("Keys"));
        const QString a = studio.edit.addDevice(track, QStringLiteral("utility"));
        const QString b = studio.edit.addDevice(track, QStringLiteral("eq"));
        const QString rack = studio.edit.groupDevices(track, {a, b});
        CpuUsage usage(&studio.project, studio.bridge.get());
        usage.update();
        const QStringList ids = column(usage, CpuUsage::RowIdRole);
        QCOMPARE(ids.mid(0, 4), (QStringList{track, rack, a, b}));
        QCOMPARE(column(usage, CpuUsage::DepthRole).mid(0, 4), (QStringList{"0", "1", "2", "2"}));
        QVERIFY(usage.data(usage.index(2), CpuUsage::NameRole).toString().contains(QStringLiteral(" › ")));
    }

    void rowsFollowTheProject() {
        Studio studio;
        const QString first = studio.edit.addAudioTrack(QStringLiteral("One"));
        CpuUsage usage(&studio.project, studio.bridge.get());
        usage.update();
        QCOMPARE(usage.rowCount(), 2);  // and the master
        studio.edit.addDevice(first, QStringLiteral("utility"));
        usage.update();
        QCOMPARE(usage.rowCount(), 3);
    }

    void theSameRowsReorderedAreMovedNotReset() {
        // (As the costliest-first order changes: a list scrolled stays where it is.)
        Studio studio;
        const QString a = studio.edit.addAudioTrack(QStringLiteral("A"));
        const QString aEq = studio.edit.addDevice(a, QStringLiteral("eq"));
        const QString b = studio.edit.addAudioTrack(QStringLiteral("B"));
        const QString bUtility = studio.edit.addDevice(b, QStringLiteral("utility"));
        CpuUsage usage(&studio.project, studio.bridge.get());
        QAbstractItemModelTester tester(&usage, QAbstractItemModelTester::FailureReportingMode::QtTest);
        usage.update();
        QCOMPARE(column(usage, CpuUsage::RowIdRole), (QStringList{a, aEq, b, bUtility, kMaster}));
        QSignalSpy resets(&usage, &QAbstractItemModel::modelReset);
        QSignalSpy moves(&usage, &QAbstractItemModel::rowsMoved);
        studio.edit.moveTracks({b}, 0, std::nullopt);
        usage.update();
        QCOMPARE(column(usage, CpuUsage::RowIdRole), (QStringList{b, bUtility, a, aEq, kMaster}));
        QCOMPARE(resets.count(), 0);
        QVERIFY(moves.count() > 0);
    }

    void measuresOnlyWhileActive() {
        Studio studio;
        studio.edit.addAudioTrack();
        CpuUsage usage(&studio.project, studio.bridge.get());
        QVERIFY(!usage.active());
        usage.setActive(true);
        QVERIFY(usage.active());
        QCOMPARE(usage.rowCount(), 2);  // (updated at once)
        usage.setSortByCpu(true);
        QCOMPARE(usage.rowCount(), 2);
        usage.setActive(false);
        QVERIFY(!usage.active());
    }
};

QTEST_GUILESS_MAIN(TestCpuUsage)
#include "test_cpu_usage.moc"
