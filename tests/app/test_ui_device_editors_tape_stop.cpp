// The Tape Stop's editor (ui/qml/devices/editors/TapeStopEditor.qml): loaded as
// the device view loads it, over a real engine; compact (fitting the view, its
// knobs at their small size), its Stop button switching the parameter undoably,
// the curve drawn as the engine bends it, and the reels turning at the speed the
// engine says the tape goes (DisplayValue), still while it is stopped. With
// SUBSTATION_UI_SCREENSHOTS set to a folder, it is saved there as a PNG.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <cmath>

#include "EditorHarness.h"
#include "devices/DisplayValue.h"
#include "editor/ProjectEditor.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsTapeStop : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    double value(const char* id) { return param(track_, device_, QString::fromLatin1(id)); }
    void set(const char* id, double v) { editor()->setDeviceParam(track_, device_, QString::fromLatin1(id), v); }

    QQuickItem* showTapeStop() {
        track_ = audioTrackWith(tone(440.0, kSampleRate), QStringLiteral("tone"), 1.0);
        if (track_.isEmpty()) return nullptr;
        device_ = editor()->addDevice(track_, QStringLiteral("tape_stop"));
        return show(QStringLiteral("tape_stop"), track_, device_);
    }

    static double speedAt(QQuickItem* view, double progress, double curve) {
        QVariant out;
        QMetaObject::invokeMethod(view, "speedAt", Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, progress),
                                  Q_ARG(QVariant, curve));
        return out.toDouble();
    }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void compactAndBound() {
        QQuickItem* view = showTapeStop();
        QVERIFY(view && fitted());
        QVERIFY(view->implicitHeight() <= bodyHeight());  // fits the view
        QVERIFY(view->implicitWidth() <= 310);            // compact
        for (const char* name : {"stop_time", "start_time", "curve", "tone", "mix", "stop", "deck", "reels", "curveGraph"})
            QVERIFY2(find<QQuickItem>(view, QString::fromLatin1(name)), name);

        // Stop: pressed, the tape stops (lit); undone, it runs.
        QQuickItem* stop = find(view, QStringLiteral("stop"));
        QVERIFY(!stop->property("lit").toBool());
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                          centerOf(qvariant_cast<QQuickItem*>(stop->property("button"))));
        QCOMPARE(value("stop"), 1.0);
        QVERIFY(stop->property("lit").toBool());
        undo()->undo();
        QCOMPARE(value("stop"), 0.0);
    }

    void curveAsTheEngineBendsIt() {
        QQuickItem* view = showTapeStop();
        QVERIFY(view);
        QCOMPARE(speedAt(view, 0.25, 0.0), 0.75);
        QVERIFY(speedAt(view, 0.25, 100.0) > 0.99);  // 1 - 0.25^5.66
        QVERIFY(std::abs(speedAt(view, 0.25, -100.0) - std::pow(0.75, std::exp2(2.5))) < 1e-9);
        QCOMPARE(speedAt(view, 0.0, -40.0), 1.0);
        QCOMPARE(speedAt(view, 1.0, 40.0), 0.0);
        // Its inverse puts the dot back where the speed was.
        for (const double curve : {-80.0, 0.0, 65.0}) {
            QVariant progress;
            QMetaObject::invokeMethod(view, "progressAt", Q_RETURN_ARG(QVariant, progress), Q_ARG(QVariant, 0.4),
                                      Q_ARG(QVariant, curve));
            QVERIFY(std::abs(speedAt(view, progress.toDouble(), curve) - 0.4) < 1e-9);
        }
    }

    void reelsTurnAtTheSpeed() {
        QQuickItem* view = showTapeStop();
        QVERIFY(view);
        auto* speed = find<DisplayValue>(view, QStringLiteral("speedDisplay"));
        QVERIFY(speed);
        // Nothing coming in: still.
        const double still = view->property("angle").toDouble();
        QTest::qWait(100);
        QCOMPARE(view->property("angle").toDouble(), still);
        // Running at full speed: turning.
        speed->advance(true, 1.0, 0.016);
        QVERIFY(view->property("running").toBool());
        QTRY_VERIFY(view->property("angle").toDouble() != still);
        // Stopped (speed 0): still, and dimmed.
        speed->advance(true, 0.0, 0.016);
        QVERIFY(!view->property("running").toBool());
        const double stopped = view->property("angle").toDouble();
        QTest::qWait(100);
        QCOMPARE(view->property("angle").toDouble(), stopped);
        QTRY_VERIFY(find(view, QStringLiteral("reels"))->opacity() < 0.5);
        // Half way up: the dot half way down the curve.
        set("curve", 0.0);
        speed->advance(true, 0.5, 0.016);
        QQuickItem* dot = find(view, QStringLiteral("speedDot"));
        QQuickItem* graph = find(view, QStringLiteral("curveGraph"));
        QTRY_VERIFY(std::abs(dot->x() - 0.5 * (graph->width() - dot->width())) < 0.5);
        set("curve", -30.0);
        for (int i = 0; i < 10; ++i) {
            speed->advance(true, 0.6, 0.016);
            QTest::qWait(30);
        }
        save(grab(), QStringLiteral("tape-stop.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsTapeStop)
#include "test_ui_device_editors_tape_stop.moc"
