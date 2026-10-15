// The Vocal Rider's editor (ui/qml/devices/editors/VocalRiderEditor.qml): loaded as the device view loads it, over a
// real engine; every control there and fitting the view; what the engine renders reaching its display
// (VocalRiderGraph: the ride as the device made it); and the target line dragged (one undo step), reset by a
// double-click. With SUBSTATION_UI_SCREENSHOTS set to a folder, it is saved there as a PNG.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QStyleHints>
#include <QUndoStack>

#include <cmath>

#include "EditorHarness.h"
#include "devices/VocalRiderGraph.h"
#include "editor/ProjectEditor.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsVocalRider : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    void set(const char* id, double v) { editor()->setDeviceParam(track_, device_, QString::fromLatin1(id), v); }

    void tick(int count, int ms = 16) {
        for (int i = 0; i < count; ++i) {
            QTest::qWait(ms);
            refreshDisplays();
        }
    }

    QQuickItem* shown(const std::vector<float>& mono, double seconds) {
        track_ = audioTrackWith(mono, QStringLiteral("vocal"), seconds);
        if (track_.isEmpty()) return nullptr;
        device_ = editor()->addDevice(track_, QStringLiteral("vocal_rider"));
        return show(QStringLiteral("vocal_rider"), track_, device_);
    }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void controls() {
        QQuickItem* view = shown(tone(1000.0, kSampleRate), 1.0);
        QVERIFY(view);
        QVERIFY2(view->implicitHeight() <= bodyHeight(),
                 qPrintable(QStringLiteral("%1 > %2").arg(view->implicitHeight()).arg(bodyHeight())));
        for (const char* id : {"target", "min_gain", "max_gain", "speed", "sensitivity", "music_sens", "output"})
            QVERIFY2(find<QQuickItem>(view, QStringLiteral("param_") + QString::fromLatin1(id)), id);
        for (const char* name : {"weighting0", "weighting1", "vocalRiderGraph", "rideReadout"})
            QVERIFY2(find<QQuickItem>(view, QString::fromLatin1(name)), name);
        QQuickItem* target = find<QQuickItem>(view, QStringLiteral("param_target"));
        const QString problem = boxTextsProblem(target);
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }

    void displaysReachTheGraph() {
        // A quiet vocal (-30 dB RMS), flat: ridden up 12 dB to the target.
        QQuickItem* view = shown(tone(1000.0, 2 * kSampleRate, 0.0447), 2.0);
        QVERIFY(view);
        set("weighting", 1.0);
        auto* graph = find<VocalRiderGraph>(view, QStringLiteral("vocalRiderGraph"));
        QVERIFY(graph);
        tick(2);
        engine()->renderOffline(0.0, 2 * kSampleRate);
        tick(10);
        QVERIFY2(std::abs(graph->gain() - 12.0) < 0.6, qPrintable(QString::number(graph->gain())));
        QVERIFY2(std::abs(graph->level() + 30.0) < 0.6, qPrintable(QString::number(graph->level())));
        save(grab(), QStringLiteral("vocal-rider.png"));
    }

    void targetDragged() {
        QQuickItem* view = shown(tone(1000.0, kSampleRate), 1.0);
        QVERIFY(view);
        auto* graph = find<VocalRiderGraph>(view, QStringLiteral("vocalRiderGraph"));
        QVERIFY(graph);
        const int before = undo()->count();
        const QPoint at = scenePoint(graph, QPointF(graph->width() / 2, graph->targetY()));
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, at);
        QVERIFY(graph->dragging());
        for (int step = 1; step <= 4; ++step) dragTo(at + QPoint(0, -5 * step));
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, at + QPoint(0, -20));
        QVERIFY(!graph->dragging());
        const double raised = param(track_, device_, QStringLiteral("target"));
        QVERIFY2(raised > -18.0 + 3.0, qPrintable(QString::number(raised)));
        QCOMPARE(undo()->count(), before + 1);
        // A double-click: back to its default.
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        const QPoint line = scenePoint(graph, QPointF(graph->width() / 2, graph->targetY()));
        QTest::mouseDClick(window_, Qt::LeftButton, Qt::NoModifier, line);
        QTRY_COMPARE(param(track_, device_, QStringLiteral("target")), -18.0);
    }
};

QTEST_MAIN(TestUiDeviceEditorsVocalRider)
#include "test_ui_device_editors_vocal_rider.moc"
