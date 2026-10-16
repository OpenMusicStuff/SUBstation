// The Tape Stop's editor (ui/qml/devices/editors/TapeStopEditor.qml): loaded as
// the device view loads it, over a real engine; laid out as Kilohearts' (the deck
// over a row of knobs, Stop Time and Start Time at its ends, the Stop button in
// the deck's middle), compact and fitting the view; the button switching the
// parameter undoably, its glyph a triangle while the tape runs and a square while
// it is stopped; and the reels turning at the speed the engine says the tape
// goes (DisplayValue), still while it is stopped. With SUBSTATION_UI_SCREENSHOTS
// set to a folder, it is saved there as a PNG.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <cmath>
#include <functional>

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

    QRectF rectOf(QQuickItem* view, const char* name) {
        QQuickItem* item = find(view, QString::fromLatin1(name));
        return item ? item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height())) : QRectF();
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
        for (const char* name : {"stop_time", "start_time", "curve", "tone", "mix", "stop", "deck", "reels"})
            QVERIFY2(find<QQuickItem>(view, QString::fromLatin1(name)), name);

        // Kilohearts' layout: the deck over the knobs, in one row from Stop Time to Start Time, the button in
        // the deck's middle.
        const QRectF deck = rectOf(view, "deck"), stop = rectOf(view, "stop");
        QVERIFY(deck.contains(stop));
        QVERIFY(std::abs(stop.center().x() - deck.center().x()) < 1.0);
        QVERIFY(std::abs(stop.center().y() - deck.center().y()) < 1.0);
        double lastX = -1;
        for (const char* name : {"stop_time", "curve", "tone", "mix", "start_time"}) {
            const QRectF knob = rectOf(view, name);
            QVERIFY2(knob.top() >= deck.bottom(), name);
            QVERIFY2(std::abs(knob.top() - rectOf(view, "stop_time").top()) < 0.5, name);
            QVERIFY2(knob.left() > lastX, name);
            QVERIFY2(knob.bottom() <= view->height(), name);
            lastX = knob.left();
        }

        // The button: pressed, the tape stops (its square showing); undone, it runs (the triangle).
        QQuickItem* button = find(view, QStringLiteral("stop"));
        QVERIFY(!button->property("lit").toBool());
        QTRY_COMPARE(find(view, QStringLiteral("playGlyph"))->opacity(), 1.0);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(button));
        QCOMPARE(value("stop"), 1.0);
        QVERIFY(button->property("lit").toBool());
        QTRY_COMPARE(find(view, QStringLiteral("stopGlyph"))->opacity(), 1.0);
        QTRY_COMPARE(find(view, QStringLiteral("playGlyph"))->opacity(), 0.0);
        undo()->undo();
        QCOMPARE(value("stop"), 0.0);
        QTRY_COMPARE(find(view, QStringLiteral("playGlyph"))->opacity(), 1.0);
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
        // The engine says the tape goes at `value`, all the while `seen` is waited for (a quarter second without
        // it, the display goes inactive).
        const auto whileGoing = [&](double value, const std::function<bool()>& seen) {
            for (int i = 0; i < 300; ++i) {
                speed->advance(true, value, 0.016);
                if (seen())
                    return true;
                QTest::qWait(10);
            }
            return false;
        };
        // Running at full speed: turning.
        QVERIFY(whileGoing(1.0, [&] { return view->property("running").toBool(); }));
        QVERIFY(whileGoing(1.0, [&] { return view->property("angle").toDouble() != still; }));
        // Stopped (speed 0): still, and dimmed.
        QVERIFY(whileGoing(0.0, [&] { return !view->property("running").toBool(); }));
        const double stopped = view->property("angle").toDouble();
        QVERIFY(whileGoing(0.0, [&] { return find(view, QStringLiteral("reels"))->opacity() < 0.6; }));
        QCOMPARE(view->property("angle").toDouble(), stopped);
        for (int i = 0; i < 10; ++i) {
            speed->advance(true, 0.6, 0.016);
            QTest::qWait(30);
        }
        save(grab(), QStringLiteral("tape-stop.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsTapeStop)
#include "test_ui_device_editors_tape_stop.moc"
