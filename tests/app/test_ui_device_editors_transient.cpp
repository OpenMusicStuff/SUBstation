// The Transient Shaper's editor (ui/qml/devices/editors/TransientEditor.qml,
// TransientShapeGraph, TransientGraph) loaded as the device view loads it, over
// a real engine: it fits the view's height and is as wide as its parts, every
// caption and readout whole; every control is bound to its parameter (Attack,
// Sustain and Output bipolar and signed), undoably, reaching the engine (the
// lookahead as latency); the shape's handles drag one parameter each (the
// direction picks which), Shift finely, in one undo step, reset on a
// double-click and give their amount's menu; the hit drawn is the engine's,
// morphing to a new shape; what the engine renders reaches both displays (the
// history, the gain's parts, sparks and a ring at each transient, a playhead
// along the hit), which then come to rest. With SUBSTATION_UI_SCREENSHOTS set,
// device-editors-transient*.png are saved there.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <vector>

#include "EditorHarness.h"
#include "audio/TransientResponse.h"
#include "controls/KnobItem.h"
#include "devices/TransientGraph.h"
#include "devices/TransientShapeGraph.h"
#include "model/Device.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Kick-like hits on a 110 Hz tone, every 250 ms from 50 ms: a 1 ms rise to 0.5, then 60 dB down over
// 300 ms, for `seconds`.
std::vector<float> hits(double seconds) {
    std::vector<float> samples(static_cast<size_t>(seconds * kSampleRate));
    for (size_t i = 0; i < samples.size(); ++i) {
        const double t = double(i) / kSampleRate;
        const double since = t < 0.05 ? -1.0 : std::fmod(t - 0.05, 0.25);
        const double level = since < 0.0 ? 0.0 : since < 0.001 ? since / 0.001 : std::pow(1e-3, (since - 0.001) / 0.3);
        samples[i] = float(0.5 * level * std::sin(2 * kPi * 110.0 * t));
    }
    return samples;
}

}  // namespace

class TestUiDeviceEditorsTransient : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    struct Shown {
        QString track;
        QString device;
        QQuickItem* view = nullptr;
        TransientShapeGraph* shape = nullptr;
        TransientGraph* history = nullptr;
    };

    // A Transient Shaper on a track playing `mono` for `seconds`, its editor shown.
    Shown shaper(const std::vector<float>& mono, double seconds) {
        Shown shown;
        shown.track = audioTrackWith(mono, QStringLiteral("hits"), seconds);
        if (shown.track.isEmpty())
            return shown;
        shown.device = editor()->addDevice(shown.track, QStringLiteral("transient"));
        shown.view = show(QStringLiteral("transient"), shown.track, shown.device);
        if (shown.view) {
            shown.shape = find<TransientShapeGraph>(shown.view, QStringLiteral("transientShape"));
            shown.history = find<TransientGraph>(shown.view, QStringLiteral("transientHistory"));
        }
        return shown;
    }

    // An EditorKnob's dial.
    KnobItem* knob(QQuickItem* view, const char* name) {
        QQuickItem* cell = find(view, QString::fromLatin1(name));
        auto* paramKnob = cell ? qvariant_cast<QQuickItem*>(cell->property("knob")) : nullptr;
        return paramKnob ? qvariant_cast<KnobItem*>(paramKnob->property("knob")) : nullptr;
    }

    // An EditorKnob's readout (its last child: caption, knob, readout).
    QString readout(QQuickItem* view, const char* name) {
        QQuickItem* cell = find(view, QString::fromLatin1(name));
        return cell && !cell->childItems().isEmpty() ? cell->childItems().last()->property("text").toString()
                                                     : QString();
    }

    // A ParamButton's (or ParamChoice's) button.
    QQuickItem* button(QQuickItem* view, const char* name) {
        QQuickItem* control = find(view, QString::fromLatin1(name));
        return control ? qvariant_cast<QQuickItem*>(control->property("button")) : nullptr;
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

    // Ticks until neither display has asked for a repaint three ticks running (at most `most`).
    void settle(const Shown& shown, int most = 400) {
        for (int i = 0, still = 0; i < most && still < 3; ++i) {
            tick(1);
            still = shown.shape->animating() || shown.history->animating() ? 0 : still + 1;
        }
    }

    // A drag of a shape handle from where it is drawn, by `by` in steps, with modifiers.
    void dragHandle(TransientShapeGraph* shape, bool attack, QPoint by,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint at = scenePoint(shape, attack ? shape->attackHandle() : shape->sustainHandle());
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, at);
        QVERIFY(shape->dragging());
        for (int step = 1; step <= 4; ++step)
            dragTo(at + by * step / 4, modifiers);
        QTest::mouseRelease(window_, Qt::LeftButton, modifiers, at + by);
        QVERIFY(!shape->dragging());
    }

    double latencyOf(const Shown& shown) {
        const auto id = bridge()->engineDeviceId(shown.track, shown.device);
        return id ? engine()->processorInfo(*id).latency : -1;
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
                                  Q_ARG(QVariant, QStringLiteral("transient")));
        QVERIFY(url.toString().endsWith(QStringLiteral("TransientEditor.qml")));
    }

    void fitsAndBinds() {
        const Shown shown = shaper(hits(1.0), 1.0);
        QVERIFY(shown.view && shown.shape && shown.history);
        QQuickItem* view = shown.view;
        QVERIFY2(view->implicitHeight() <= bodyHeight(),
                 qPrintable(QStringLiteral("%1 > %2").arg(view->implicitHeight()).arg(bodyHeight())));
        QVERIFY(fitted());

        // Every part inside the body, side by side, and the editor as wide as they are with its margin.
        const char* names[] = {"transientShape", "transientHistory", "attack",  "sustain", "threshold",
                               "output",         "attack_time",      "release", "mix",     "transientSide"};
        double right = 0.0;
        for (const char* name : names) {
            QQuickItem* item = find(view, QString::fromLatin1(name));
            QVERIFY2(item, name);
            const QRectF rect = item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height()));
            QVERIFY2(rect.left() >= 0 && rect.top() >= 0 && rect.bottom() <= view->height() + 0.5, name);
            right = std::max(right, rect.right());
        }
        QCOMPARE(view->implicitWidth(), std::ceil(right) + 8);
        const QRectF shape = shown.shape->mapRectToItem(view, QRectF(0, 0, shown.shape->width(), 1));
        const QRectF history = shown.history->mapRectToItem(view, QRectF(0, 0, shown.history->width(), 1));
        QVERIFY(shape.right() < history.left());

        // Every caption and every text a readout takes, whole.
        for (const char* name : {"attack", "sustain", "threshold", "output", "attack_time", "release", "mix"}) {
            const QString problem = knobTextsProblem(find(view, QString::fromLatin1(name)));
            QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(QString::fromLatin1(name), problem)));
        }

        // The defaults; bipolar only what is signed; the times log.
        const std::pair<const char*, double> defaults[] = {{"attack", 0.0},     {"sustain", 0.0},
                                                           {"attack_time", 20}, {"release", 300},
                                                           {"threshold", -60},  {"output", 0.0},
                                                           {"mix", 100}};
        for (const auto& [name, v] : defaults) {
            KnobItem* dial = knob(view, name);
            QVERIFY2(dial, name);
            QVERIFY2(std::abs(dial->value() - v) < 1e-4, name);
            QCOMPARE(dial->bipolar(), std::string(name) == "attack" || std::string(name) == "sustain" ||
                                          std::string(name) == "output");
        }
        QVERIFY(knob(view, "attack_time")->logScale());
        QVERIFY(knob(view, "release")->logScale());
        QCOMPARE(button(view, "lookahead")->property("text").toString(), QStringLiteral("0 ms"));
        QVERIFY(button(view, "lookahead")->implicitWidth() <= find(view, QStringLiteral("lookahead"))->width());

        // Attack and Sustain read with their sign.
        set(shown, "attack", 35.0);
        set(shown, "sustain", -20.0);
        QVERIFY2(readout(view, "attack").startsWith(QLatin1Char('+')), qPrintable(readout(view, "attack")));
        QVERIFY2(readout(view, "sustain").startsWith(QLatin1Char('-')), qPrintable(readout(view, "sustain")));
        set(shown, "attack", 0.0);
        QVERIFY2(!readout(view, "attack").startsWith(QLatin1Char('+')), qPrintable(readout(view, "attack")));
    }

    void controlsUndoable() {
        const Shown shown = shaper(hits(1.0), 1.0);
        QVERIFY(shown.view);
        // Clip: a click switches it, one undo step.
        const int steps = undo()->index();
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(button(shown.view, "clip")));
        QCOMPARE(value(shown, "clip"), 1.0);
        QCOMPARE(undo()->index(), steps + 1);
        QVERIFY(button(shown.view, "clip")->property("checked").toBool());
        undo()->undo();
        QCOMPARE(value(shown, "clip"), 0.0);
        // The lookahead is the device's latency in the engine.
        QCOMPARE(latencyOf(shown), 0.0);
        set(shown, "lookahead", 2.0);
        QTRY_COMPARE(latencyOf(shown), 240.0);
        QCOMPARE(button(shown.view, "lookahead")->property("text").toString(), QStringLiteral("5 ms"));
        undo()->undo();
        QTRY_COMPARE(latencyOf(shown), 0.0);
    }

    void handleDrags() {
        const Shown shown = shaper(hits(1.0), 1.0);
        QVERIFY(shown.shape);
        TransientShapeGraph* shape = shown.shape;
        tick(5);

        // Attack's handle up: Attack, a percent a pixel, one undo step; Attack Time untouched.
        int steps = undo()->index();
        dragHandle(shape, true, QPoint(0, -30));
        QVERIFY2(std::abs(value(shown, "attack") - 30.0) < 1.0, qPrintable(QString::number(value(shown, "attack"))));
        QCOMPARE(value(shown, "attack_time"), 20.0);
        QCOMPARE(undo()->index(), steps + 1);
        QCOMPARE(undo()->text(undo()->index() - 1), QStringLiteral("Change Attack"));
        // The curve follows a drag at once (no morph), the handle on it.
        QCOMPARE(shape->drawnDb(), shape->targetDb());
        tickAway(40);

        // Across: Attack Time, an octave per 40 px; Attack untouched.
        const double attack = value(shown, "attack");
        dragHandle(shape, true, QPoint(40, 0));
        QVERIFY2(std::abs(value(shown, "attack_time") - 40.0) < 2.0,
                 qPrintable(QString::number(value(shown, "attack_time"))));
        QCOMPARE(value(shown, "attack"), attack);
        tickAway(40);

        // Shift: a tenth as far.
        steps = undo()->index();
        dragHandle(shape, false, QPoint(0, 40), Qt::ShiftModifier);
        QVERIFY2(std::abs(value(shown, "sustain") - (-4.0)) < 0.6, qPrintable(QString::number(value(shown, "sustain"))));
        QCOMPARE(undo()->index(), steps + 1);
        QCOMPARE(undo()->text(undo()->index() - 1), QStringLiteral("Change Sustain"));
        tickAway(40);

        // Sustain's handle across: Release.
        dragHandle(shape, false, QPoint(-40, 0));
        QVERIFY2(std::abs(value(shown, "release") - 150.0) < 8.0, qPrintable(QString::number(value(shown, "release"))));
        tickAway(40);

        // A double-click puts a handle's two parameters back.
        const QPoint at = scenePoint(shape, shape->attackHandle());
        QTest::mouseDClick(window_, Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(value(shown, "attack"), 0.0);
        QCOMPARE(value(shown, "attack_time"), 20.0);
        QVERIFY(value(shown, "release") < 299.0);  // (the other handle's, as they were)
        tickAway(40);

        // A press off the handles is the frame's: nothing changes.
        steps = undo()->index();
        const QRectF plot = shape->plot();
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, scenePoint(shape, QPointF(plot.right() - 6, plot.top() + 6)));
        QVERIFY(!shape->dragging());
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, scenePoint(shape, QPointF(plot.right() - 6, plot.top() + 6)));
        QCOMPARE(undo()->index(), steps);

        // A right-click on a handle: its amount's menu.
        QTest::mouseClick(window_, Qt::RightButton, Qt::NoModifier, scenePoint(shape, shape->sustainHandle()));
        auto* menu = qvariant_cast<QObject*>(shown.view->property("handleMenu"));
        QVERIFY(menu);
        auto* menuParam = qvariant_cast<QObject*>(menu->property("param"));
        QVERIFY(menuParam);
        QCOMPARE(menuParam->property("paramId").toString(), QStringLiteral("sustain"));
        QTest::keyClick(window_, Qt::Key_Escape);
    }

    void curveIsTheEngines() {
        const Shown shown = shaper(hits(1.0), 1.0);
        QVERIFY(shown.shape);
        TransientShapeGraph* shape = shown.shape;
        const std::vector<float> before = shape->inDb();
        set(shown, "attack", 60.0);
        set(shown, "sustain", -40.0);
        set(shown, "attack_time", 10.0);
        set(shown, "release", 500.0);
        set(shown, "threshold", -70.0);
        settle(shown);
        const TransientHit hit = transientHit(60.0, -40.0, 10.0, 500.0, -70.0, shape->timesMs());
        QCOMPARE(shape->targetDb(), hit.outDb);
        QCOMPARE(shape->drawnDb(), hit.outDb);  // (settled: exactly there)
        QCOMPARE(shape->inDb(), before);        // (the hit itself never changes)

        // A new setting morphs to it: not there at once, there after a few ticks.
        set(shown, "attack", -60.0);
        QVERIFY(shape->drawnDb() != shape->targetDb());
        tick(1);
        QVERIFY(shape->animating());
        settle(shown);
        QCOMPARE(shape->drawnDb(), transientHit(-60.0, -40.0, 10.0, 500.0, -70.0, shape->timesMs()).outDb);
        QVERIFY(!shape->animating());

        // The attack handle at the shaped peak; Sustain's on the tail.
        const auto peak = std::max_element(shape->targetDb().begin(), shape->targetDb().end());
        QCOMPARE(shape->attackHandle().y(), shape->yOf(*peak));
        QVERIFY(std::abs(shape->msAt(shape->sustainHandle().x()) - TransientShapeGraph::kSustainMs) < 10.0);
    }

    void displaysReachTheGraphs() {
        const Shown shown = shaper(hits(2.0), 2.0);
        QVERIFY(shown.shape && shown.history);
        set(shown, "attack", 80.0);
        set(shown, "sustain", -50.0);
        tick(2);
        QVERIFY(shown.history->idle());

        // 350 ms: hits at 50 and 300 ms, the second recent enough to strike.
        engine()->renderOffline(0.0, int(kSampleRate * 0.35));
        tick(2, 20);
        TransientGraph* history = shown.history;
        QVERIFY(!history->idle());
        QVERIFY2(history->historySize() > 100, qPrintable(QString::number(history->historySize())));
        QVERIFY(!history->sparks().empty());
        QVERIFY(!history->rings().empty());
        QVERIFY(!shown.shape->heads().empty());
        // The gain's parts as the engine published them: Attack's up at the onsets, Sustain's down in the tails.
        float attackMost = 0.f, sustainLeast = 0.f;
        for (qint64 i = history->newest() - history->historySize(); i < history->newest(); ++i) {
            attackMost = std::max(attackMost, history->historyAt(TransientGraph::Attack, i));
            sustainLeast = std::min(sustainLeast, history->historyAt(TransientGraph::Sustain, i));
        }
        QVERIFY2(attackMost > 6.f, qPrintable(QString::number(attackMost)));
        QVERIFY2(sustainLeast < -3.f, qPrintable(QString::number(sustainLeast)));
        QVERIFY(history->animating());
        QVERIFY(shown.shape->animating());

        // No more audio: it stops where the values stop, the sparks and heads go, and everything rests.
        settle(shown);
        QVERIFY(!history->animating());
        QVERIFY(!shown.shape->animating());
        QVERIFY(history->sparks().empty());
        QVERIFY(shown.shape->heads().empty());
        QVERIFY(history->idle());
        QCOMPARE(history->scroll(), double(history->newest()));
    }

    void backlogStrikesNothing() {
        // A whole second at once, read late: no strike from what is long gone (the last hit was at 800 ms).
        const Shown shown = shaper(hits(1.0), 1.0);
        QVERIFY(shown.history);
        set(shown, "attack", 80.0);
        engine()->renderOffline(0.0, kSampleRate);
        tick(1);
        QVERIFY(shown.history->historySize() > 0);
        QVERIFY(shown.history->sparks().empty());
        QVERIFY(shown.shape->heads().empty());
    }

    void screenshots() {
        if (qEnvironmentVariable("SUBSTATION_UI_SCREENSHOTS").isEmpty())
            QSKIP("SUBSTATION_UI_SCREENSHOTS not set");
        const Shown shown = shaper(hits(4.0), 4.0);
        QVERIFY(shown.view && shown.shape && shown.history);
        set(shown, "attack", 70.0);
        set(shown, "sustain", -45.0);
        set(shown, "attack_time", 15.0);
        // The engine resets its devices before each offline render, so the sound is rendered in one go (in
        // pieces, each would start from silence: an onset at every piece), up to 40 ms after a hit (one every
        // 250 ms from 50 ms), then the displays ticked as the sparks and the playhead fly.
        auto play = [&](double seconds, int ticks) {
            engine()->renderOffline(0.0, int(seconds * kSampleRate));
            for (int i = 0; i < ticks; ++i)
                tick(1);
        };
        play(2.34, 8);
        QTest::qWait(50);
        save(grab(), QStringLiteral("transient.png"));

        // Hovering Attack's handle: its tag.
        QTest::mouseMove(window_, scenePoint(shown.shape, shown.shape->attackHandle()));
        tick(12);
        QTest::qWait(50);
        save(grab(), QStringLiteral("transient-hover.png"));
        QTest::mouseMove(window_, QPoint(1, 1));

        // The other way round: softer onsets, a longer tail.
        set(shown, "attack", -80.0);
        set(shown, "sustain", 70.0);
        set(shown, "release", 600.0);
        play(2.34, 8);
        QTest::qWait(50);
        save(grab(), QStringLiteral("transient-soft.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsTransient)
#include "test_ui_device_editors_transient.moc"
