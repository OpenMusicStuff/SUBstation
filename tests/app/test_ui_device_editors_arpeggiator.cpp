// The Arpeggiator's editor (ui/qml/devices/editors/ArpeggiatorEditor.qml):
// loaded as the device view loads it, over a real engine; its buttons and what
// they set (undoably), the rate shown as a note value or in Hz, the pattern it
// draws for each style, the velocity box, the octaves and velocity under the
// timing, and the step playing lit (DisplayValue). With SUBSTATION_UI_SCREENSHOTS
// set to a folder, it is saved there as a PNG.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <cmath>

#include "EditorHarness.h"
#include "devices/DisplayValue.h"
#include "editor/ProjectEditor.h"
#include "model/Project.h"

using namespace sub::app;
using namespace sub::ui;

class TestUiDeviceEditorsArpeggiator : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    QQuickItem* showArpeggiator() {
        track_ = editor()->addMidiTrack(-1, {}, QStringLiteral("synth"));
        device_ = editor()->addDevice(track_, QStringLiteral("arpeggiator"));
        return show(QStringLiteral("arpeggiator"), track_, device_);
    }

    double value(const char* id) { return param(track_, device_, QString::fromLatin1(id)); }
    void set(const char* id, double v) { editor()->setDeviceParam(track_, device_, QString::fromLatin1(id), v); }

    QQuickItem* button(QQuickItem* view, const QString& name) {
        QQuickItem* item = find(view, name);
        return item ? qvariant_cast<QQuickItem*>(item->property("button")) : nullptr;
    }

    // The pattern's steps' rows (or -1 for none).
    static QList<int> rows(QQuickItem* view) {
        QList<int> out;
        for (const QVariant& step : view->property("pattern").toList())
            out << step.toMap().value(QStringLiteral("row")).toInt();
        return out;
    }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void itGoesAfterTheInstrument() {
        QQuickItem* view = showArpeggiator();
        QVERIFY(view);
        const auto& devices = project()->track(track_).devices;
        QCOMPARE(devices.size(), size_t(2));
        QCOMPARE(devices[1].kind, QStringLiteral("arpeggiator"));
        QVERIFY(view->implicitHeight() <= bodyHeight());  // fits the view
        // Compact: the octaves and the velocity under the timing's controls.
        const auto rect = [&](const char* name) {
            QQuickItem* item = find(view, QString::fromLatin1(name));
            return item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height()));
        };
        QVERIFY(rect("octaves1").top() >= rect("gate").bottom());
        QVERIFY(rect("velocityPlayed").top() >= rect("octaves1").bottom());
        QVERIFY(rect("fixedVelocity").right() <= view->implicitWidth());
    }

    void playingStepLights() {
        QQuickItem* view = showArpeggiator();
        QVERIFY(view);
        auto* playing = find<DisplayValue>(view, QStringLiteral("stepDisplay"));
        QQuickItem* glow = find(view, QStringLiteral("playingGlow"));
        QVERIFY(playing && glow);
        QCOMPARE(view->property("playingStep").toInt(), -1);
        QTRY_COMPARE(glow->opacity(), 0.0);
        // The engine says step 5 sounds: the glow slides there and shows.
        playing->advance(true, 5.0, 0.016);
        QCOMPARE(view->property("playingStep").toInt(), 5);
        const double stepWidth = find(view, QStringLiteral("patternView"))->property("stepWidth").toDouble();
        QTRY_VERIFY(std::abs(glow->x() - (1 + 5 * stepWidth)) < 0.5);
        QTRY_VERIFY(glow->opacity() > 0.1);
        // Nothing sounds (-1), or nothing comes: it fades.
        playing->advance(true, -1.0, 0.016);
        QCOMPARE(view->property("playingStep").toInt(), -1);
        QTRY_COMPARE(glow->opacity(), 0.0);
    }

    void buttonsSetTheirParameters() {
        QQuickItem* view = showArpeggiator();
        QVERIFY(view && fitted());
        // Octaves: one of four, undoably.
        QVERIFY(find(view, QStringLiteral("octaves1"))->property("lit").toBool());
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(button(view, QStringLiteral("octaves3"))));
        QCOMPARE(value("octaves"), 3.0);
        QVERIFY(find(view, QStringLiteral("octaves3"))->property("lit").toBool());
        undo()->undo();
        QCOMPARE(value("octaves"), 1.0);

        // Hold switches.
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(button(view, QStringLiteral("hold"))));
        QCOMPARE(value("hold"), 1.0);

        // Fixed velocity: its box is live only then.
        auto* fixed = find(view, QStringLiteral("fixedVelocity"));
        QVERIFY(fixed && !fixed->isEnabled());
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                          centerOf(button(view, QStringLiteral("velocityFixed"))));
        QCOMPARE(value("velocity"), 1.0);
        QVERIFY(fixed->isEnabled());
    }

    void rateAsANoteValueOrInHz() {
        QQuickItem* view = showArpeggiator();
        QVERIFY(view);
        QVERIFY(find(view, QStringLiteral("synced"))->isVisible());
        QVERIFY(!find(view, QStringLiteral("rate"))->isVisible());
        set("sync", 0.0);
        QVERIFY(!find(view, QStringLiteral("synced"))->isVisible());
        QVERIFY(find(view, QStringLiteral("rate"))->isVisible());
    }

    void patternFollowsTheStyle() {
        QQuickItem* view = showArpeggiator();
        QVERIFY(view);
        // Up over a three-note chord.
        QCOMPARE(rows(view).mid(0, 6), (QList<int>{0, 1, 2, 0, 1, 2}));
        set("style", 1.0);  // Down
        QCOMPARE(rows(view).mid(0, 4), (QList<int>{2, 1, 0, 2}));
        set("style", 2.0);  // Up-Down
        QCOMPARE(rows(view).mid(0, 6), (QList<int>{0, 1, 2, 1, 0, 1}));
        set("style", 4.0);  // Converge, over two octaves
        set("octaves", 2.0);
        QCOMPARE(rows(view).mid(0, 6), (QList<int>{0, 5, 1, 4, 2, 3}));
        set("style", 8.0);  // Chord: an octave up each step
        QCOMPARE(rows(view).mid(0, 4), (QList<int>{0, 3, 0, 3}));
        QCOMPARE(view->property("pattern").toList().size(), 16);
        set("style", 0.0);
        set("octaves", 1.0);
        set("swing", 30.0);
        set("gate", 80.0);
        auto* playing = find<DisplayValue>(view, QStringLiteral("stepDisplay"));
        QVERIFY(playing);
        for (int i = 0; i < 10; ++i) {  // as it plays: the seventh step lit
            playing->advance(true, 6.0, 0.016);
            QTest::qWait(30);
        }
        save(grab(), QStringLiteral("arpeggiator.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsArpeggiator)
#include "test_ui_device_editors_arpeggiator.moc"
