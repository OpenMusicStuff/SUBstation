// The Supersaw's editor (ui/qml/devices/editors/SupersawEditor.qml and
// SupersawGraph): loaded as the device view loads it, over a real engine;
// fitting the view, its four pages and three stacks showing every parameter
// of the device (each somewhere, whole, inside the body), its knobs editing
// the model undoably, those that do nothing dimmed; the graph drawing each
// stack's saws where the engine's maths puts them, the stacks' switches, a
// lane pressed showing its stack, dragged for its Detune and Width (one undo
// step) and double-clicked back to their defaults; and what the engine plays
// lighting the saws, which rest in silence. With SUBSTATION_UI_SCREENSHOTS set
// to a folder, its pages are saved there as PNGs.

#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTest>
#include <QUndoStack>

#include <cmath>

#include "EditorHarness.h"
#include "audio/SupersawLayout.h"
#include "devices/DeviceParam.h"
#include "devices/SupersawGraph.h"
#include "editor/ProjectEditor.h"
#include "model/Clip.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsSupersaw : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    QQuickItem* showSupersaw() {
        track_ = editor()->addMidiTrack(-1, {}, QStringLiteral("supersaw"));
        device_ = project()->track(track_).devices.front().id;
        return show(QStringLiteral("supersaw"), track_, device_);
    }
    void set(const char* id, double v) { editor()->setDeviceParam(track_, device_, QString::fromLatin1(id), v); }
    double get(const char* id) { return param(track_, device_, QString::fromLatin1(id)); }

    static void setPage(QQuickItem* view, int page, int stack = 0) {
        view->setProperty("page", page);
        QMetaObject::invokeMethod(view, "showStack", Q_ARG(QVariant, stack));
        QTest::qWait(20);
    }
    // The parameters the page shows (its cells' ids).
    QStringList shown(QQuickItem* view) {
        QStringList ids;
        for (QQuickItem* cell : itemsNamed(view, QStringLiteral("param_"))) {
            if (cell->isVisible())
                ids << cell->property("paramId").toString();
        }
        return ids;
    }
    SupersawGraph* graphOf(QQuickItem* view) { return find<SupersawGraph>(view, QStringLiteral("supersawGraph")); }

    // A note held from the start for `beats`, on the Supersaw's track; then `seconds` rendered and the displays'
    // clock ticked until the editor has read it.
    void playNote(double beats, double seconds) {
        editor()->commitClips(QStringLiteral("Add"), {{track_, {Clip::midi(QStringLiteral("held"), QStringLiteral("held"),
                                                                           0.0, beats, 0.0, {Note{57, 0.0, beats}})}}});
        session_->bridge()->waitForDeviceStates();
        engine_->renderOffline(0.0, static_cast<int64_t>(seconds * kSampleRate));
        for (int i = 0; i < 8; ++i) {
            refreshDisplays();
            QTest::qWait(5);
        }
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
                                  Q_ARG(QVariant, QStringLiteral("supersaw")));
        QVERIFY(url.toString().endsWith(QStringLiteral("SupersawEditor.qml")));
    }

    void everyParameterOnSomePage() {
        QQuickItem* view = showSupersaw();
        QVERIFY(view);
        QCOMPARE(view->property("pages").toInt(), 4);
        QCOMPARE(view->property("pageNames").toStringList(),
                 (QStringList{QStringLiteral("Stacks"), QStringLiteral("Filter"), QStringLiteral("Voice"),
                              QStringLiteral("Spread")}));
        QVERIFY(view->implicitHeight() <= bodyHeight());
        QSet<QString> all, seen;
        for (const auto& [id, value] : project()->device(track_, device_).params.asKeyValueRange()) all.insert(id);
        for (int page = 0; page < 4; ++page) {
            for (int stack = 0; stack < (page == 0 ? 3 : 1); ++stack) {
                setPage(view, page, stack);
                const QStringList ids = shown(view);
                QVERIFY(!ids.isEmpty());
                for (const QString& id : ids) {
                    QVERIFY2(all.contains(id), qPrintable(id));
                    seen.insert(id);
                    auto* cell = find<QQuickItem>(view, QStringLiteral("param_") + id);
                    QVERIFY(cell);
                    auto* p = qvariant_cast<sub::ui::DeviceParam*>(cell->property("param"));
                    QVERIFY2(p && p->valid(), qPrintable(id));
                    for (QQuickItem* text : cell->findChildren<QQuickItem*>()) {  // its caption and readout whole
                        if (text->isVisible() && text->property("truncated").toBool())
                            QFAIL(qPrintable(QStringLiteral("%1: \"%2\" cut short").arg(id, text->property("text").toString())));
                    }
                    const QPointF corner = cell->mapToItem(view, QPointF(cell->width(), cell->height()));
                    QVERIFY2(corner.x() <= view->width() + 0.5 && corner.y() <= view->height() + 0.5, qPrintable(id));
                }
                save(grab(), QStringLiteral("supersaw-%1-%2.png").arg(page).arg(stack + 1));
            }
        }
        // The stacks' switches are beside the graph; everything else is a cell somewhere.
        for (const char* id : {"s1_on", "s2_on", "s3_on"}) {
            QVERIFY(!seen.contains(QString::fromLatin1(id)));
            seen.insert(QString::fromLatin1(id));
        }
        QCOMPARE(seen, all);
    }

    void knobsEditUndoably() {
        QQuickItem* view = showSupersaw();
        QVERIFY(view);
        setPage(view, 0, 1);  // stack 2
        auto* cell = find<QQuickItem>(view, QStringLiteral("param_s2_detune"));
        QVERIFY(cell);
        auto* p = qvariant_cast<sub::ui::DeviceParam*>(cell->property("param"));
        QVERIFY(p);
        const int steps = session_->undoStack()->count();
        p->set(80.0);
        QCOMPARE(get("s2_detune"), 80.0);
        QCOMPARE(session_->undoStack()->count(), steps + 1);
        session_->undoStack()->undo();
        QCOMPARE(get("s2_detune"), 35.0);
        // Its knobs dimmed while it is off (settable all the same), lit once on.
        QTRY_VERIFY(cell->opacity() < 0.5);
        set("s2_on", 1.0);
        QTRY_COMPARE(cell->opacity(), 1.0);
        // One saw: Detune, Curve, Width and Blend do nothing.
        set("s2_saws", 1.0);
        QTRY_VERIFY(cell->opacity() < 0.5);
        QTRY_VERIFY(find<QQuickItem>(view, QStringLiteral("param_s2_blend"))->opacity() < 0.5);
        QTRY_COMPARE(find<QQuickItem>(view, QStringLiteral("param_s2_level"))->opacity(), 1.0);
        // The filter's knobs, while it is off.
        setPage(view, 1);
        set("filter_type", 0.0);
        QTRY_VERIFY(find<QQuickItem>(view, QStringLiteral("param_cutoff"))->opacity() < 0.5);
        QTRY_COMPARE(find<QQuickItem>(view, QStringLiteral("param_filter_type"))->opacity(), 1.0);
        set("filter_type", 2.0);
        QTRY_COMPARE(find<QQuickItem>(view, QStringLiteral("param_cutoff"))->opacity(), 1.0);
        // Spread: Tilt At while no stack is tilted, a tilt on a stack that is off, Focus's frequency without it.
        setPage(view, 3);
        QTRY_VERIFY(find<QQuickItem>(view, QStringLiteral("param_tilt_at"))->opacity() < 0.5);
        QTRY_VERIFY(find<QQuickItem>(view, QStringLiteral("param_s3_tilt"))->opacity() < 0.5);
        QTRY_COMPARE(find<QQuickItem>(view, QStringLiteral("param_s1_tilt"))->opacity(), 1.0);
        set("s1_tilt", 50.0);
        QTRY_COMPARE(find<QQuickItem>(view, QStringLiteral("param_tilt_at"))->opacity(), 1.0);
        QTRY_COMPARE(find<QQuickItem>(view, QStringLiteral("param_focus"))->opacity(), 1.0);
        QTRY_VERIFY(find<QQuickItem>(view, QStringLiteral("param_focus_freq"))->opacity() < 0.5);
        set("focus", 1.0);
        QTRY_COMPARE(find<QQuickItem>(view, QStringLiteral("param_focus_freq"))->opacity(), 1.0);
    }

    void graphDrawsTheEnginesLayout() {
        QQuickItem* view = showSupersaw();
        QVERIFY(view);
        SupersawGraph* graph = graphOf(view);
        QVERIFY(graph);
        QVERIFY(graph->width() >= SupersawGraph::kMinimumWidth);
        // At its defaults: stack 1's seven saws, the other two ready but off.
        QCOMPARE(graph->sawCount(0), 7);
        QVERIFY(graph->stackOn(0));
        QVERIFY(!graph->stackOn(1));
        QVERIFY(!graph->stackOn(2));
        // Each saw where the engine's maths puts it: across by its cents, the flattest leftmost, its place in the
        // stereo field up and down (left at the top), and as large as it is loud.
        set("s1_saws", 16.0);
        set("s1_detune", 80.0);
        QCOMPARE(graph->sawCount(0), 16);
        // The axis eases out past the widest stack's outermost saws (64 cents at 80 %) as the display ticks.
        for (int i = 0; i < 60; ++i)
            refreshDisplays();
        QVERIFY(graph->axisCents() > supersawDetuneCents(80.0));
        const std::vector<SupersawSaw> saws = supersawLayout(16, 80.0, 35.0, 75.0, 100.0);
        for (int i = 0; i < 16; ++i) {
            const QPointF at = graph->sawPoint(0, i);
            QCOMPARE(at.x(), graph->xOfCents(0, saws[size_t(i)].cents));
            const QRectF r = graph->plotRect(0);
            QVERIFY(r.adjusted(-0.01, -0.01, 0.01, 0.01).contains(at));
            if (saws[size_t(i)].pan < -0.5)
                QVERIFY(at.y() < r.center().y());
            if (saws[size_t(i)].pan > 0.5)
                QVERIFY(at.y() > r.center().y());
            if (i > 0)
                QVERIFY(at.x() > graph->sawPoint(0, i - 1).x());
        }
        QVERIFY(graph->sawPoint(0, 0).x() > graph->plotRect(0).left());
        // Blend 0: only the middle pair sounds; the others nothing, drawn smallest.
        set("s1_blend", 0.0);
        QVERIFY(graph->sawRadius(0, 7) > graph->sawRadius(0, 0));
        // A stack's switch: click it on (one undo step), and its lane says what it is.
        auto* power = find<QQuickItem>(view, QStringLiteral("power_1"));
        QVERIFY(power);
        const int steps = session_->undoStack()->count();
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(power));
        QTRY_COMPARE(get("s2_on"), 1.0);
        QCOMPARE(session_->undoStack()->count(), steps + 1);
        QVERIFY(graph->stackOn(1));
        // The switch sits beside its lane.
        const QRectF lane = graph->laneRect(1);
        const double powerY = power->mapToItem(graph, QPointF(0, power->height() / 2)).y();
        QVERIFY(powerY > lane.top() && powerY < lane.bottom());
    }

    void lanesShowAndDragTheirStack() {
        QQuickItem* view = showSupersaw();
        QVERIFY(view);
        QVERIFY(fitted());
        SupersawGraph* graph = graphOf(view);
        QVERIFY(graph);
        setPage(view, 2);
        // Pressing stack 3's lane shows it, on the Stacks page.
        const QRectF third = graph->laneRect(2);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, scenePoint(graph, third.center()));
        QTRY_COMPARE(view->property("stack").toInt(), 2);
        QCOMPARE(view->property("page").toInt(), 0);
        QCOMPARE(graph->stack(), 2);
        QVERIFY(shown(view).contains(QStringLiteral("s3_detune")));
        // Dragging stack 2's lane: across for its Detune (100 % over 240 px), up and down for its Width (100 %
        // over 100 px): one undo step.
        const QRectF second = graph->laneRect(1);
        const int steps = session_->undoStack()->count();
        const QPointF from = second.center();
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, scenePoint(graph, from));
        dragTo(scenePoint(graph, from + QPointF(24, 15)));
        dragTo(scenePoint(graph, from + QPointF(48, 30)));
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, scenePoint(graph, from + QPointF(48, 30)));
        QVERIFY(std::abs(get("s2_detune") - 55.0) < 0.6);
        QVERIFY(std::abs(get("s2_width") - 40.0) < 0.6);
        QCOMPARE(session_->undoStack()->count(), steps + 1);
        QCOMPARE(view->property("stack").toInt(), 1);
        session_->undoStack()->undo();
        QCOMPARE(get("s2_detune"), 35.0);
        QCOMPARE(get("s2_width"), 70.0);
        // Double-click: both back to their defaults.
        set("s1_detune", 90.0);
        set("s1_width", 10.0);
        const QPoint first = scenePoint(graph, graph->laneRect(0).center());
        QTest::mouseDClick(window_, Qt::LeftButton, Qt::NoModifier, first);
        QTRY_COMPARE(get("s1_detune"), 45.0);
        QCOMPARE(get("s1_width"), 100.0);
    }

    void sawsGlowWhileItPlays() {
        QQuickItem* view = showSupersaw();
        QVERIFY(view);
        SupersawGraph* graph = graphOf(view);
        QVERIFY(graph);
        refreshDisplays();
        QVERIFY(graph->resting());
        QCOMPARE(graph->glow(), 0.0);
        playNote(8.0, 1.0);  // held past the render's end
        QVERIFY(!graph->resting());
        QTRY_VERIFY(graph->glow() > 0.2);
        save(grab(), QStringLiteral("supersaw-playing.png"));
        // Silent, they come to rest.
        for (int i = 0; i < 200 && !graph->resting(); ++i) {
            refreshDisplays();
            QTest::qWait(2);
        }
        QVERIFY(graph->resting());
        for (int i = 0; i < 100 && graph->glow() > 0.0; ++i) {
            refreshDisplays();
            QTest::qWait(2);
        }
        QCOMPARE(graph->glow(), 0.0);
    }
};

QTEST_MAIN(TestUiDeviceEditorsSupersaw)
#include "test_ui_device_editors_supersaw.moc"
