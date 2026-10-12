// The Vintage Hall's editor (ui/qml/devices/editors/VintageHallEditor.qml,
// VintageHallLoop) loaded as the device view loads it, over a real engine: it
// fits the view's height and is as wide as its parts, every caption and readout
// whole; every knob is bound to its parameter; the loop is the engine's (its
// length for Size, its taps' weights for Attack) and eases to a new Size; a
// drag on it sets Decay (up and down) and Size (across), Shift finely, in one
// undo step, and a double-click resets them; what the engine renders reaches it
// (each tap's level, comets for the onsets running round), which then comes to
// rest. With SUBSTATION_UI_SCREENSHOTS set, device-editors-vintage-hall*.png
// are saved there.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "EditorHarness.h"
#include "audio/VintageHallResponse.h"
#include "controls/KnobItem.h"
#include "devices/VintageHallLoop.h"
#include "model/Device.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Plucked chords: a 1 ms rise to 0.4 then 30 dB down over 250 ms, every 700 ms from 50 ms, for `seconds`.
std::vector<float> plucks(double seconds) {
    std::vector<float> samples(static_cast<size_t>(seconds * kSampleRate));
    for (size_t i = 0; i < samples.size(); ++i) {
        const double t = double(i) / kSampleRate;
        const double since = t < 0.05 ? -1.0 : std::fmod(t - 0.05, 0.7);
        const double level = since < 0.0 ? 0.0 : since < 0.001 ? since / 0.001 : std::pow(0.03, (since - 0.001) / 0.25);
        double tone = 0.0;
        for (const double f : {220.0, 277.18, 329.63, 440.0})
            tone += std::sin(2 * kPi * f * t) / 4;
        samples[i] = float(0.4 * level * tone);
    }
    return samples;
}

const std::vector<const char*> kKnobs = {"decay",      "predelay",        "size",           "attack",   "bass_mult",
                              "bass_xover", "high_shelf",      "high_freq",      "early_diffusion",
                              "late_diffusion", "mod_rate",    "mod_depth",      "high_cut", "low_cut",
                              "mix"};

// What shows with Advanced folded.
const std::vector<const char*> kAlwaysShown = {"decay", "predelay", "size", "attack", "high_cut", "mix", "low_cut",
                                               "advancedButton"};

}  // namespace

class TestUiDeviceEditorsVintageHall : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    struct Shown {
        QString track;
        QString device;
        QQuickItem* view = nullptr;
        VintageHallLoop* loop = nullptr;
    };

    // A Vintage Hall on a track playing `mono` for `seconds`, its editor shown.
    Shown hall(const std::vector<float>& mono, double seconds) {
        Shown shown;
        shown.track = audioTrackWith(mono, QStringLiteral("plucks"), seconds);
        if (shown.track.isEmpty())
            return shown;
        shown.device = editor()->addDevice(shown.track, QStringLiteral("vintage_hall"));
        shown.view = show(QStringLiteral("vintage_hall"), shown.track, shown.device);
        if (shown.view)
            shown.loop = find<VintageHallLoop>(shown.view, QStringLiteral("hallLoop"));
        return shown;
    }

    // An EditorKnob's dial.
    KnobItem* knob(QQuickItem* view, const char* name) {
        QQuickItem* cell = find(view, QString::fromLatin1(name));
        auto* paramKnob = cell ? qvariant_cast<QQuickItem*>(cell->property("knob")) : nullptr;
        return paramKnob ? qvariant_cast<KnobItem*>(paramKnob->property("knob")) : nullptr;
    }

    // The rightmost edge of `names` in the view, each checked to be whole, beside the loop and inside the body.
    double rightOf(QQuickItem* view, QQuickItem* loopItem, const std::vector<const char*>& names) {
        const QRectF loop = loopItem->mapRectToItem(view, QRectF(0, 0, loopItem->width(), loopItem->height()));
        [&] { QVERIFY(loop.left() >= 0 && loop.top() >= 0 && loop.bottom() <= view->height() + 0.5); }();
        double right = loop.right();
        for (const char* name : names) {
            QQuickItem* item = find(view, QString::fromLatin1(name));
            if (!item) {
                qWarning("missing %s", name);
                return -1;
            }
            const QRectF rect = item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height()));
            [&] {
                QVERIFY2(rect.left() > loop.right() && rect.top() >= 0 && rect.bottom() <= view->height() + 0.5, name);
                QVERIFY2(rect.right() <= view->width() + 0.5, name);
            }();
            right = std::max(right, rect.right());
            if (std::string(name) != "advancedButton") {
                const QString problem = knobTextsProblem(item);
                [&] { QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(QString::fromLatin1(name), problem))); }();
            }
        }
        return right;
    }

    double value(const Shown& shown, const char* id) {
        return param(shown.track, shown.device, QString::fromLatin1(id));
    }
    void set(const Shown& shown, const char* id, double v) {
        editor()->setDeviceParam(shown.track, shown.device, QString::fromLatin1(id), v);
    }

    // The displays' clock ticked `count` times, `ms` apart.
    void tick(int count, int ms = 16) {
        for (int i = 0; i < count; ++i) {
            QTest::qWait(ms);
            refreshDisplays();
        }
    }

    // The mouse off the editor, then `count` ticks (the wait after a gesture: no tooltip, no double-click).
    void tickAway(int count) {
        QTest::mouseMove(window_, QPoint(1, 1));
        tick(count);
    }

    // Ticks until the loop hasn't asked for a repaint three ticks running (at most `most`).
    void settle(const Shown& shown, int most = 1500) {
        for (int i = 0, still = 0; i < most && still < 3; ++i) {
            tick(1, 4);
            still = shown.loop->animating() ? 0 : still + 1;
        }
    }

    // A drag on the loop from its middle, by `by` in steps, with modifiers.
    void dragLoop(VintageHallLoop* loop, QPoint by, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint at = centerOf(loop);
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, at);
        QVERIFY(loop->dragging());
        for (int step = 1; step <= 4; ++step)
            dragTo(at + by * step / 4, modifiers);
        QTest::mouseRelease(window_, Qt::LeftButton, modifiers, at + by);
        QVERIFY(!loop->dragging());
    }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void registry() {
        QVariant url;
        QMetaObject::invokeMethod(root_.get(), "editorFor", Q_RETURN_ARG(QVariant, url),
                                  Q_ARG(QVariant, QStringLiteral("vintage_hall")));
        QVERIFY(url.toString().endsWith(QStringLiteral("VintageHallEditor.qml")));
    }

    void fitsAndBinds() {
        const Shown shown = hall(plucks(1.0), 1.0);
        QVERIFY(shown.view && shown.loop);
        QQuickItem* view = shown.view;
        QVERIFY2(view->implicitHeight() <= bodyHeight(),
                 qPrintable(QStringLiteral("%1 > %2").arg(view->implicitHeight()).arg(bodyHeight())));
        QVERIFY(fitted());

        // Folded: the loop, then the knobs always shown, inside the body; the editor as wide as they are and its
        // margin; Advanced's hidden.
        QVERIFY(!find(view, QStringLiteral("advancedSection"))->isVisible());
        const double folded = view->implicitWidth();
        QCOMPARE(folded, std::ceil(rightOf(view, shown.loop, kAlwaysShown)) + 8);

        // Advanced slides open (over a few frames), every knob inside the body and whole; and shuts again.
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(find(view, QStringLiteral("advancedButton"))));
        QVERIFY(view->implicitWidth() < folded + 10);  // (not at once)
        QTRY_VERIFY(view->property("reveal").toDouble() == 1.0);
        QVERIFY(fitted());
        QCOMPARE(view->implicitWidth(), std::ceil(rightOf(view, shown.loop, kKnobs)) + 8);
        QVERIFY(view->implicitWidth() > folded + 4 * 52);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(find(view, QStringLiteral("advancedButton"))));
        QTRY_VERIFY(view->property("reveal").toDouble() == 0.0);
        QCOMPARE(view->implicitWidth(), folded);
        QVERIFY(!find(view, QStringLiteral("advancedSection"))->isVisible());
        QVERIFY(fitted());

        // Each knob is its parameter: the defaults, the log ones log.
        const std::pair<const char*, double> defaults[] = {{"decay", 4000.0}, {"size", 100.0}, {"high_shelf", -24.0},
                                                           {"mod_depth", 38.0}, {"mix", 100.0}};
        for (const auto& [name, v] : defaults) {
            KnobItem* dial = knob(view, name);
            QVERIFY2(dial, name);
            QVERIFY2(std::abs(dial->value() - v) < 1e-3, name);
        }
        for (const char* name : {"decay", "bass_mult", "bass_xover", "high_freq", "mod_rate", "high_cut", "low_cut"})
            QVERIFY2(knob(view, name)->logScale(), name);

        // A knob's change is the parameter's, one undo step.
        const int steps = undo()->index();
        KnobItem* predelay = knob(view, "predelay");
        const QPoint at = centerOf(predelay);
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, at);
        for (int step = 1; step <= 4; ++step)
            dragTo(at - QPoint(0, 10 * step));
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, at - QPoint(0, 40));
        QVERIFY2(value(shown, "predelay") > 20.0, qPrintable(QString::number(value(shown, "predelay"))));
        QCOMPARE(undo()->index(), steps + 1);
        undo()->undo();
        QCOMPARE(value(shown, "predelay"), 20.0);
    }

    void loopIsTheEngines() {
        const Shown shown = hall(plucks(1.0), 1.0);
        QVERIFY(shown.loop);
        VintageHallLoop* loop = shown.loop;
        settle(shown);
        QVERIFY(std::abs(loop->loopSeconds() - 13442.0 / 24000.0) < 1e-9);
        const std::array<float, 4> weights = vintageHallTapWeights(50.0);
        QCOMPARE(loop->tapWeight(0), double(weights[0]));  // L1: the left's first
        QCOMPARE(loop->tapWeight(4), double(weights[3]));  // R1: the right's from the other half
        QVERIFY(loop->tapWeight(4) < 0.0);

        // A smaller hall: its loop shorter, the ring easing in to it.
        const double before = loop->radius();
        set(shown, "size", 30.0);
        QCOMPARE(loop->loopSeconds(), vintageHallRing(30.0).seconds);
        QCOMPARE(loop->radius(), before);  // (not there at once)
        tick(1);
        QVERIFY(loop->animating());
        settle(shown);
        QVERIFY(!loop->animating());
        QVERIFY(loop->radius() < before - 5.0);
        QCOMPARE(loop->fractionOf(VintageHallRing::RightIn), vintageHallRing(30.0).at[VintageHallRing::RightIn]);

        // Attack 100 %: only the third taps are heard.
        set(shown, "attack", 100.0);
        settle(shown);
        QCOMPARE(loop->tapWeight(0), 0.0);
        QCOMPARE(loop->tapWeight(2), 1.0);  // L3
        QCOMPARE(loop->tapWeight(7), 1.0);  // R4
    }

    void loopDrags() {
        const Shown shown = hall(plucks(1.0), 1.0);
        QVERIFY(shown.loop);
        VintageHallLoop* loop = shown.loop;
        tick(5);

        // Up: Decay, doubling every 50 px, Size as it was; one undo step.
        int steps = undo()->index();
        dragLoop(loop, QPoint(0, -50));
        QVERIFY2(std::abs(value(shown, "decay") - 8000.0) < 300.0, qPrintable(QString::number(value(shown, "decay"))));
        QCOMPARE(value(shown, "size"), 100.0);
        QCOMPARE(undo()->index(), steps + 1);
        QCOMPARE(undo()->text(undo()->index() - 1), QStringLiteral("Change Decay and Size"));
        tickAway(40);

        // Left: Size, 0.8 % a pixel; Shift a little.
        dragLoop(loop, QPoint(-40, 0));
        QVERIFY2(std::abs(value(shown, "size") - 68.0) < 2.0, qPrintable(QString::number(value(shown, "size"))));
        tickAway(40);
        const double decay = value(shown, "decay");
        dragLoop(loop, QPoint(0, 50), Qt::ShiftModifier);
        QVERIFY2(value(shown, "decay") < decay && value(shown, "decay") > 0.85 * decay,
                 qPrintable(QString::number(value(shown, "decay"))));
        tickAway(40);

        // A double-click puts both back.
        QTest::mouseDClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(loop));
        QCOMPARE(value(shown, "decay"), 4000.0);
        QCOMPARE(value(shown, "size"), 100.0);
        tickAway(40);
    }

    void displaysReachTheLoop() {
        const Shown shown = hall(plucks(2.0), 2.0);
        QVERIFY(shown.loop);
        VintageHallLoop* loop = shown.loop;
        settle(shown);
        QCOMPARE(loop->cometCount(), 0);
        const double floor = vintageHallDisplayFloorDb();
        QCOMPARE(loop->tapLevelDb(0), floor);

        // 800 ms: plucks at 50 and 750 ms, the second recent enough to set comets off.
        engine()->renderOffline(0.0, int(kSampleRate * 0.8));
        tick(2, 20);
        QVERIFY2(loop->cometCount() >= 2, qPrintable(QString::number(loop->cometCount())));
        double loudest = floor;
        for (int k = 0; k < VintageHallLoop::kTaps; ++k)
            loudest = std::max(loudest, loop->tapLevelDb(k));
        QVERIFY2(loudest > -40.0, qPrintable(QString::number(loudest)));
        QVERIFY(loop->outputLevelDb() > -40.0);
        QVERIFY(loop->animating());

        // No more audio: the glow falls, the comets fade as Decay says (a short one here), and it rests.
        set(shown, "decay", 400.0);
        settle(shown);
        QVERIFY(!loop->animating());
        QCOMPARE(loop->cometCount(), 0);
        QCOMPARE(loop->tapLevelDb(0), floor);
    }

    void backlogLaunchesLittle() {
        // Three seconds at once, read late: only what is recent sets comets off (the last pluck at 2.85 s).
        const Shown shown = hall(plucks(3.0), 3.0);
        QVERIFY(shown.loop);
        engine()->renderOffline(0.0, 3 * kSampleRate);
        tick(1);
        QVERIFY2(shown.loop->cometCount() <= 4, qPrintable(QString::number(shown.loop->cometCount())));
    }

    void screenshots() {
        if (qEnvironmentVariable("SUBSTATION_UI_SCREENSHOTS").isEmpty())
            QSKIP("SUBSTATION_UI_SCREENSHOTS not set");
        const Shown shown = hall(plucks(4.0), 4.0);
        QVERIFY(shown.view && shown.loop);
        set(shown, "mod_depth", 70.0);
        tick(4);
        save(grab(), QStringLiteral("vintage-hall-idle.png"));
        // The engine resets its devices before each offline render, so the sound is rendered in one go, to just
        // after a pluck (one every 700 ms from 50 ms), then the displays ticked as the comets run.
        auto play = [&](double seconds, int ticks) {
            engine()->renderOffline(0.0, int(seconds * kSampleRate));
            for (int i = 0; i < ticks; ++i)
                tick(1);
        };
        play(2.2, 14);
        QTest::qWait(50);
        save(grab(), QStringLiteral("vintage-hall.png"));
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                          centerOf(find(shown.view, QStringLiteral("advancedButton"))));
        QTRY_VERIFY(shown.view->property("reveal").toDouble() == 1.0);
        QVERIFY(fitted());
        QTest::mouseMove(window_, QPoint(1, 1));
        play(2.2, 14);
        QTest::qWait(50);
        save(grab(), QStringLiteral("vintage-hall-advanced.png"));

        // A small, swelling room.
        set(shown, "size", 35.0);
        set(shown, "attack", 90.0);
        set(shown, "late_diffusion", 40.0);
        play(2.2, 30);
        QTest::qWait(50);
        save(grab(), QStringLiteral("vintage-hall-small.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsVintageHall)
#include "test_ui_device_editors_vintage_hall.moc"
