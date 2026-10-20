// The Groove device's editor (ui/qml/devices/editors/GrooveEditor.qml): loaded
// as the device view loads it, over a real engine; its source chooser (Swing,
// Project, the library's grooves) and what it sets (undoably), the Swing knobs
// live only for Swing, the steps it draws following the groove, the step playing
// lit (DisplayValue), Add to Grooves and Apply. With SUBSTATION_UI_SCREENSHOTS
// set to a folder, it is saved there as a PNG.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <cmath>

#include "EditorHarness.h"
#include "devices/DisplayValue.h"
#include "editor/ProjectEditor.h"
#include "model/Grooves.h"
#include "model/Project.h"
#include "session/GrooveLibrary.h"

using namespace sub::app;
using namespace sub::ui;

class TestUiDeviceEditorsGroove : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    QQuickItem* showGroove() {
        track_ = editor()->addMidiTrack(-1, {}, QStringLiteral("synth"));
        device_ = editor()->addDevice(track_, QStringLiteral("groove"));
        return show(QStringLiteral("groove"), track_, device_);
    }

    double value(const char* id) { return param(track_, device_, QString::fromLatin1(id)); }
    void set(const char* id, double v) { editor()->setDeviceParam(track_, device_, QString::fromLatin1(id), v); }

    GrooveSource source() const {
        return grooves::deviceSource(project()->findDevice(track_, device_)->state);
    }

    // The steps drawn: each one's timing.
    QList<double> timings(QQuickItem* view) {
        QList<double> out;
        for (const QVariant& step : find(view, QStringLiteral("stepsView"))->property("steps").toList())
            out << step.toMap().value(QStringLiteral("timing")).toDouble();
        return out;
    }

    // Chooses the source chooser's entry `index`, as a click on it would.
    void choose(QQuickItem* view, int index) {
        QQuickItem* box = find(view, QStringLiteral("source"));
        QVERIFY(box);
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, index));
    }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void itFitsTheView() {
        QQuickItem* view = showGroove();
        QVERIFY(view);
        QVERIFY(view->implicitHeight() <= bodyHeight());
        const auto rect = [&](const char* name) {
            QQuickItem* item = find(view, QString::fromLatin1(name));
            return item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height()));
        };
        QVERIFY(rect("length").top() >= rect("swing").bottom());
        QVERIFY(rect("latency").right() <= view->implicitWidth());
        QVERIFY(rect("addToGrooves").bottom() <= view->implicitHeight());
        save(grab(), QStringLiteral("groove.png"));
    }

    void stepsFollowTheSwing() {
        QQuickItem* view = showGroove();
        QVERIFY(view);
        QCOMPARE(view->property("kind").toString(), QStringLiteral("swing"));
        // Straight: sixteen steps, none moved.
        QCOMPARE(timings(view).size(), 16);
        QCOMPARE(timings(view).value(1), 0.0);
        // At 66.7 % every other 16th is a third of a step late.
        set("swing", 200.0 / 3.0);
        QCOMPARE(timings(view).size(), 16);
        QVERIFY(std::abs(timings(view).value(1) - 1.0 / 3.0) < 1e-6);
        QCOMPARE(timings(view).value(2), 0.0);
        // On 8ths: eight steps.
        set("step", 0.0);
        QCOMPARE(timings(view).size(), 8);
    }

    void sourceChoosesAGroove() {
        QQuickItem* view = showGroove();
        QVERIFY(view);
        const QString id = session()->grooves()->addBuiltin(QStringLiteral("builtin:shuffle-16t"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(view->property("sources").toList().size(), 3);  // Swing, Project, the groove
        auto* swing = find(view, QStringLiteral("swing"));
        QVERIFY(swing->isEnabled());

        choose(view, 2);
        QCOMPARE(source().kind, GrooveSource::Kind::Library);
        QCOMPARE(source().libraryId, id);
        QCOMPARE(view->property("kind").toString(), QStringLiteral("library"));
        QCOMPARE(view->property("sourceIndex").toInt(), 2);
        QVERIFY(!swing->isEnabled());  // (Swing's knobs are Swing's)
        QVERIFY(std::abs(timings(view).value(1) - 1.0 / 3.0) < 0.01);

        undo()->undo();
        QCOMPARE(source().kind, GrooveSource::Kind::Swing);
        QCOMPARE(view->property("sourceIndex").toInt(), 0);

        // The project's default.
        choose(view, 1);
        QCOMPARE(source().kind, GrooveSource::Kind::Project);
    }

    void playingStepLights() {
        QQuickItem* view = showGroove();
        QVERIFY(view);
        auto* playing = find<DisplayValue>(view, QStringLiteral("stepDisplay"));
        QQuickItem* glow = find(view, QStringLiteral("playingGlow"));
        QVERIFY(playing && glow);
        QCOMPARE(view->property("playingStep").toInt(), -1);
        QTRY_COMPARE(glow->opacity(), 0.0);
        playing->advance(true, 3.0, 0.016);
        QCOMPARE(view->property("playingStep").toInt(), 3);
        const double stepWidth = find(view, QStringLiteral("stepsView"))->property("stepWidth").toDouble();
        QTRY_VERIFY(std::abs(glow->x() - (1 + 3 * stepWidth)) < 0.5);
        QTRY_VERIFY(glow->opacity() > 0.1);
        playing->advance(true, -1.0, 0.016);
        QTRY_COMPARE(glow->opacity(), 0.0);
    }

    void addToGroovesAndApply() {
        QQuickItem* view = showGroove();
        QVERIFY(view && fitted());
        set("swing", 60.0);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(find(view, QStringLiteral("addToGrooves"))));
        QCOMPARE(project()->grooves().size(), size_t(1));
        QCOMPARE(source().kind, GrooveSource::Kind::Library);
        QCOMPARE(source().libraryId, project()->grooves().front().id);

        // Apply: the track's notes moved, the device off.
        const QString clip = editor()->addMidiClip(track_, 0.0, 4.0)->clipId;
        editor()->setClipNotes({track_, clip}, {Note{60, 0.0, 0.25, 100}, Note{62, 0.25, 0.25, 100}},
                               QStringLiteral("Notes"));
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(find(view, QStringLiteral("apply"))));
        const Clip* applied = project()->findClip(track_, clip);
        QVERIFY(applied);
        QCOMPARE(applied->notes.size(), size_t(2));
        QVERIFY(std::abs(applied->notes[1].start - 0.3) < 1e-6);  // 60 % of the way through its pair of 16ths
        QVERIFY(!project()->findDevice(track_, device_)->enabled);
        undo()->undo();
        QVERIFY(project()->findDevice(track_, device_)->enabled);
        QCOMPARE(project()->findClip(track_, clip)->notes[1].start, 0.25);
    }
};

QTEST_MAIN(TestUiDeviceEditorsGroove)
#include "test_ui_device_editors_groove.moc"
