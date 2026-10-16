// The Glitch's editor (ui/qml/devices/editors/GlitchEditor.qml): loaded as the
// device view loads it, over a real engine; compact (fitting the view), its
// knobs, Grid and Variation bound and undoable, the kinds' knobs in their
// colours, and its animations following the displays (DisplayValues): the
// cursor sliding to the step playing, the cells taking what each step did,
// popping and shivering while they glitch, the scan line only then, all still
// while nothing comes in. With SUBSTATION_UI_SCREENSHOTS set to a folder, it is
// saved there as a PNG.

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

class TestUiDeviceEditorsGlitch : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    double value(const char* id) { return param(track_, device_, QString::fromLatin1(id)); }

    QQuickItem* showGlitch() {
        track_ = audioTrackWith(tone(440.0, kSampleRate), QStringLiteral("tone"), 1.0);
        if (track_.isEmpty()) return nullptr;
        device_ = editor()->addDevice(track_, QStringLiteral("glitch"));
        return show(QStringLiteral("glitch"), track_, device_);
    }

    // The engine says step `at` plays, doing `kind` (one value: the step plus 16 times the kind).
    static void playing(DisplayValue* step, int at, int kind) { step->advance(true, at + 16 * kind, 0.016); }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void compactAndBound() {
        QQuickItem* view = showGlitch();
        QVERIFY(view && fitted());
        QVERIFY(view->implicitHeight() <= bodyHeight());  // fits the view
        QVERIFY(view->implicitWidth() <= 340);            // compact
        for (const char* name : {"grid", "variation", "chance", "mix", "stutter", "reverse", "tape", "gate", "steps"})
            QVERIFY2(find<QQuickItem>(view, QString::fromLatin1(name)), name);
        QCOMPARE(itemsNamed(view, QStringLiteral("cell")).size(), 16);
        // Every cell inside the strip.
        QQuickItem* strip = find(view, QStringLiteral("steps"));
        for (QQuickItem* cell : itemsNamed(view, QStringLiteral("cell"))) {
            const QRectF r = cell->mapRectToItem(strip, QRectF(0, 0, cell->width(), cell->height()));
            QVERIFY(QRectF(0, 0, strip->width(), strip->height()).contains(r));
        }
        // The kinds' knobs in their own colours, apart.
        QSet<QString> colours;
        for (const char* name : {"stutter", "reverse", "tape", "gate"})
            colours << find(view, QString::fromLatin1(name))->property("color").value<QColor>().name();
        QCOMPARE(colours.size(), 4);
        // A knob set, undoably.
        editor()->setDeviceParam(track_, device_, QStringLiteral("chance"), 80.0);
        QCOMPARE(value("chance"), 80.0);
        undo()->undo();
        QCOMPARE(value("chance"), 40.0);
    }

    void stepsLightAsTheyPlay() {
        QQuickItem* view = showGlitch();
        QVERIFY(view);
        auto* step = find<DisplayValue>(view, QStringLiteral("stepDisplay"));
        QQuickItem* cursor = find(view, QStringLiteral("cursor"));
        QQuickItem* scan = find(view, QStringLiteral("scan"));
        QVERIFY(step && cursor && scan);
        // Nothing comes in: no step playing, nothing moving.
        QCOMPARE(view->property("playingStep").toInt(), -1);
        QTRY_COMPARE(cursor->opacity(), 0.0);
        QCOMPARE(scan->opacity(), 0.0);
        const double scanAt = scan->y();
        QTest::qWait(100);
        QCOMPARE(scan->y(), scanAt);

        // Step 2 plays as it is: the cursor goes there; the cell stays dark, no scan line.
        playing(step, 2, 0);
        QCOMPARE(view->property("playingStep").toInt(), 2);
        QQuickItem* cell2 = find(view, QStringLiteral("cell2"));
        QTRY_VERIFY(std::abs(cursor->x() + 1 - cell2->x()) < 1.5);
        QTRY_VERIFY(cursor->opacity() > 0.5);
        QCOMPARE(view->property("cells").toList().at(2).toInt(), 0);
        QCOMPARE(cell2->scale(), 1.0);

        // Step 3 reverses: its cell takes Reverse's colour, pops and shivers; the scan line runs.
        playing(step, 3, 2);
        QCOMPARE(view->property("cells").toList().at(3).toInt(), 2);
        QQuickItem* cell3 = find(view, QStringLiteral("cell3"));
        QTRY_VERIFY(cell3->scale() > 1.05);
        QTRY_COMPARE(cell3->property("color").value<QColor>(), find(view, QStringLiteral("reverse"))->property("color").value<QColor>());
        bool shivered = false;
        for (int i = 0; i < 20 && !shivered; ++i) {
            QTest::qWait(15);
            shivered = cell3->property("shiver").toDouble() != 0.0;
        }
        QVERIFY(shivered);
        QTRY_VERIFY(scan->opacity() > 0.1);
        QTRY_VERIFY(scan->y() != scanAt);
        for (int i = 0; i < 6; ++i) {  // as it plays
            playing(step, 3, 2);
            QTest::qWait(30);
        }
        playing(step, 5, 1);
        playing(step, 6, 4);
        playing(step, 7, 3);
        QTest::qWait(150);
        save(grab(), QStringLiteral("glitch.png"));

        // Step 8 plays as it is: step 3 settles, the scan line goes; the cells keep what each did.
        playing(step, 8, 0);
        QTRY_COMPARE(cell3->scale(), 1.0);
        QTRY_COMPARE(cell3->property("shiver").toDouble(), 0.0);
        QTRY_COMPARE(scan->opacity(), 0.0);
        const QVariantList cells = view->property("cells").toList();
        QCOMPARE(cells.at(5).toInt(), 1);
        QCOMPARE(cells.at(6).toInt(), 4);
        QCOMPARE(cells.at(7).toInt(), 3);
        QCOMPARE(cells.at(9).toInt(), -1);  // not seen yet

        // Nothing comes in: the cursor fades.
        step->advance(false, 0.0, 1.0);
        QTRY_COMPARE(cursor->opacity(), 0.0);
    }
};

QTEST_MAIN(TestUiDeviceEditorsGlitch)
#include "test_ui_device_editors_glitch.moc"
