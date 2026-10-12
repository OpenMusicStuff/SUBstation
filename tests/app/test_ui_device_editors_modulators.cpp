// The modulators' editors (ui/qml/devices/editors/LfoEditor.qml and
// EnvelopeEditor.qml, ModulatorGraph, ModulatorTargets) loaded as the device
// view loads them, over a real engine: they fit the view's height; the curve is
// the device's, a point dragged is one undo step (Ctrl snapping it to the
// grid), a shape from the menu replaces it; Map (beside Sync) waits for a
// parameter and maps the next one touched; what it moves folds away by the
// Targets button beside the knobs (folded at first, the count under it), and
// unfolded shows each target with its range (dragged, one undo step, the row
// outliving its edits), Invert and ×; what the engine renders reaches the
// graph (its output and playhead) and a knob it moves (its range and where it
// is now); the Envelope's Threshold counts only once a sidechain is chosen. With SUBSTATION_UI_SCREENSHOTS set, device-editors-lfo*.png and
// device-editors-envelope*.png are saved there.

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QUndoStack>

#include <cmath>
#include <vector>

#include "EditorHarness.h"
#include "controls/KnobItem.h"
#include "devices/DeviceParam.h"
#include "devices/ModulatorGraph.h"
#include "devices/ModulatorMappings.h"
#include "model/Automation.h"
#include "model/Device.h"
#include "session/ModulationLearn.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsModulators : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    // Renders `seconds` of the arrangement in one go (an offline render resets the devices first), then
    // ticks the displays' clock until the editors have read it all.
    void play(double seconds) {
        session_->bridge()->waitForDeviceStates();
        engine_->renderOffline(0.0, static_cast<int64_t>(seconds * kSampleRate));
        for (int i = 0; i < 8; ++i)
            refreshDisplays();
    }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display (Qt Quick draws the scene-graph items there)");
        startHost();
    }
    void cleanupTestCase() { stopHost(); }
    void init() { clearHost(); }

    void lfo() {
        const QString track = audioTrackWith(std::vector<float>(kSampleRate, 0.5f), QStringLiteral("held"), 1.0);
        QVERIFY(!track.isEmpty());
        const QString lfo = editor()->addDevice(track, QStringLiteral("lfo"));
        const QString utility = editor()->addDevice(track, QStringLiteral("utility"));
        QQuickItem* view = show(QStringLiteral("lfo"), track, lfo);
        QVERIFY(view);
        QVERIFY(view->implicitHeight() <= bodyHeight());
        auto* graph = find<ModulatorGraph>(view, QStringLiteral("modulatorGraph"));
        QVERIFY(graph);
        QVERIFY(graph->periodic());
        QCOMPARE(graph->points().size(), size_t(3));  // its triangle
        QCOMPARE(graph->shapes().front(), QStringLiteral("Triangle"));

        // A point dragged: one undo step; Ctrl snaps it to the grid.
        const int steps = undo()->count();
        const QPointF at = graph->toScreen(graph->points()[1].x, graph->points()[1].y);
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, scenePoint(graph, at));
        dragTo(scenePoint(graph, at + QPointF(7, 21)), Qt::ControlModifier);
        dragTo(scenePoint(graph, at + QPointF(9, 23)), Qt::ControlModifier);
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::ControlModifier, scenePoint(graph, at + QPointF(9, 23)));
        QCOMPARE(undo()->count(), steps + 1);
        const double x = graph->points()[1].x, y = graph->points()[1].y;
        QCOMPARE(std::round(x * 16), x * 16);
        QCOMPARE(std::round(y * 8), y * 8);
        graph->applyShape(3);  // Saw Up
        QCOMPARE(graph->points().size(), size_t(2));
        QCOMPARE(param(track, lfo, QStringLiteral("p3_used")), 0.0);

        // Map: it waits, the next parameter touched is mapped, the list shows it.
        auto* targets = find<QQuickItem>(view, QStringLiteral("modulatorTargets"));
        QVERIFY(targets);
        auto* mappings = qvariant_cast<ModulatorMappings*>(targets->property("mappings"));
        QVERIFY(mappings);
        QVERIFY(!targets->isVisible());  // (folded at first)
        const double narrow = view->implicitWidth();
        auto* map = find<QQuickItem>(view, QStringLiteral("map"));
        QVERIFY(map);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(map));
        QVERIFY(session_->modulationLearn()->isLearning(lfo));
        QVERIFY(mappings->learning());
        editor()->touchParameter(track, automation::deviceKey(utility, QStringLiteral("gain")));
        QVERIFY(!mappings->learning());
        QCOMPARE(mappings->count(), 1);
        auto* unfold = find<QQuickItem>(view, QStringLiteral("targetsButton"));
        QVERIFY(unfold);
        QCOMPARE(unfold->property("text").toString(), QStringLiteral("‹"));
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(unfold));
        QTRY_VERIFY(targets->isVisible());
        QVERIFY(view->implicitWidth() > narrow);
        fitted();
        QCOMPARE(mappings->rows().front().toMap().value("param").toString(), QStringLiteral("Gain"));
        QTest::qWait(50);
        save(grab(), QStringLiteral("lfo"));

        // Its range, as the list sets it: one undo step a gesture.
        const int before = undo()->count();
        mappings->setRange(utility, QStringLiteral("gain"), 0.3, 0.6, QStringLiteral("drag"));
        mappings->setRange(utility, QStringLiteral("gain"), 0.4, 0.7, QStringLiteral("drag"));
        QCOMPARE(undo()->count(), before + 1);
        // The bar dragged with the mouse: its max end, one undo step, the row the same row throughout.
        auto* range = find<QQuickItem>(targets, QStringLiteral("range"));
        QVERIFY(range);
        const QPointer<QQuickItem> rowBefore = range;
        const QPoint high = scenePoint(range, QPointF(0.7 * range->width(), range->height() / 2));
        const int dragged = undo()->count();
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, high);
        dragTo(high + QPoint(-10, 0));
        dragTo(high + QPoint(-20, 0));
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, high + QPoint(-20, 0));
        QCOMPARE(undo()->count(), dragged + 1);
        QVERIFY(project()->device(track, lfo).modulations.front().high < 0.65);
        QVERIFY(rowBefore);
        mappings->setInverted(utility, QStringLiteral("gain"), true);
        QVERIFY(project()->device(track, lfo).modulations.front().invert);

        // As it plays: the graph's output and playhead, and the knob it moves, its range and where it is.
        sub::ui::DeviceParam gain;
        gain.setSession(session_.get());
        gain.setTrackId(track);
        gain.setDeviceId(utility);
        gain.setParamId(QStringLiteral("gain"));
        QVERIFY(gain.modulated());
        QCOMPARE(gain.modulator(), lfo);
        const double highEnd = project()->device(track, lfo).modulations.front().high;  // (where the drag left it)
        QCOMPARE(gain.modulationFrom(), 0.4);
        QCOMPARE(gain.modulationTo(), highEnd);
        play(0.5);
        QVERIFY(graph->output() >= 0.0);
        QVERIFY(!graph->trail().empty());
        QVERIFY(gain.modulationValue() >= 0.4 - 1e-9 && gain.modulationValue() <= highEnd + 1e-9);
        // Switched off, it moves nothing (as the engine has it): the knob shows its own value again.
        editor()->setDeviceEnabled(track, lfo, false);
        QVERIFY(!gain.modulated());
        editor()->setDeviceEnabled(track, lfo, true);
        QVERIFY(gain.modulated());
        QTest::qWait(50);
        save(grab(), QStringLiteral("lfo-playing"));

        // ×: unmapped.
        mappings->remove(utility, QStringLiteral("gain"));
        QCOMPARE(mappings->count(), 0);
        QVERIFY(!gain.modulated());
    }

    void envelope() {
        const QString track = audioTrackWith(std::vector<float>(kSampleRate, 0.5f), QStringLiteral("held"), 1.0);
        QVERIFY(!track.isEmpty());
        const QString envelope = editor()->addDevice(track, QStringLiteral("envelope"));
        QQuickItem* view = show(QStringLiteral("envelope"), track, envelope);
        QVERIFY(view);
        QVERIFY(view->implicitHeight() <= bodyHeight());
        auto* graph = find<ModulatorGraph>(view, QStringLiteral("modulatorGraph"));
        QVERIFY(graph);
        QVERIFY(!graph->periodic());
        QCOMPARE(graph->shapes().front(), QStringLiteral("Pluck"));
        QVERIFY(find<QQuickItem>(view, QStringLiteral("map")));
        QVERIFY(find<QQuickItem>(view, QStringLiteral("sync")));
        // Threshold counts once a sidechain is chosen.
        auto* threshold = find<QQuickItem>(view, QStringLiteral("threshold"));
        QVERIFY(threshold && !threshold->isEnabled());
        const QString kick = audioTrackWith(std::vector<float>(kSampleRate, 0.5f), QStringLiteral("kick"), 1.0);
        editor()->setDeviceSidechain(track, envelope, Sidechain{kick, kPostFader});
        QTRY_VERIFY(threshold->isEnabled());
        QTest::qWait(50);
        save(grab(), QStringLiteral("envelope"));
    }

    void aModulatedKnob() {
        const QString track = audioTrackWith(std::vector<float>(kSampleRate, 0.5f), QStringLiteral("held"), 1.0);
        QVERIFY(!track.isEmpty());
        const QString lfo = editor()->addDevice(track, QStringLiteral("lfo"));
        const QString utility = editor()->addDevice(track, QStringLiteral("utility"));
        editor()->mapModulation(track, lfo, utility, QStringLiteral("pan"), 0.25, 0.75);
        QVariant cell;
        QMetaObject::invokeMethod(root_.get(), "showKnob", Q_RETURN_ARG(QVariant, cell), Q_ARG(QVariant, track),
                                  Q_ARG(QVariant, utility), Q_ARG(QVariant, QStringLiteral("pan")));
        auto* knob = qvariant_cast<KnobItem*>(qvariant_cast<QQuickItem*>(cell)->property("knob"));
        QVERIFY(knob);
        QVERIFY(knob->modulated());
        QCOMPARE(knob->modulationFrom(), 0.25);
        QCOMPARE(knob->modulationTo(), 0.75);
        play(0.3);
        QVERIFY(knob->modulationValue() >= 0.25 - 1e-9 && knob->modulationValue() <= 0.75 + 1e-9);
        // While Map waits, the knobs it can take ring (a mapped one too: it would move over).
        session_->modulationLearn()->start(track, lfo);
        QVERIFY(knob->mappable());
        session_->modulationLearn()->stop();
        QVERIFY(!knob->mappable());
        editor()->unmapModulation(track, utility, QStringLiteral("pan"));
        QVERIFY(!knob->modulated());
    }
};

QTEST_MAIN(TestUiDeviceEditorsModulators)
#include "test_ui_device_editors_modulators.moc"
