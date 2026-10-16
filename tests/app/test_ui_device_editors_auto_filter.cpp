// The Auto Filter's and the Overdrive's editors (AutoFilterEditor.qml with
// AutoFilterGraph, OverdriveEditor.qml with OverdriveGraph): loaded as the
// device view loads them, over a real engine; fitting the view, their knobs and
// the controls that dim where they do nothing, the displays' drags (one undo
// step each, double-click resetting), the curve the engine's maths draws, and
// their animations following what the engine publishes, then coming to rest.

#include <QQuickItem>
#include <QTest>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "EditorHarness.h"
#include "audio/AutoFilterResponse.h"
#include "audio/OverdriveResponse.h"
#include "devices/AutoFilterGraph.h"
#include "devices/OverdriveGraph.h"
#include "editor/ProjectEditor.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsAutoFilter : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    QQuickItem* showDevice(const QString& kind, const std::vector<float>& mono) {
        track_ = audioTrackWith(mono, QStringLiteral("tone"), 1.0);
        if (track_.isEmpty())
            return nullptr;
        device_ = editor()->addDevice(track_, kind);
        return show(kind, track_, device_);
    }

    double value(const char* id) { return param(track_, device_, QString::fromLatin1(id)); }
    void set(const char* id, double v) { editor()->setDeviceParam(track_, device_, QString::fromLatin1(id), v); }

    // Rendered a piece at a time, the displays refreshed after each, as playing does.
    void play(int pieces, int frames = 2048) {
        const double beatsPerFrame = project()->tempo() / 60.0 / kSampleRate;
        for (int i = 0; i < pieces; ++i) {
            engine()->renderOffline(i * frames * beatsPerFrame, frames);
            refreshDisplays();
        }
    }

    // Everything in the view inside its body.
    static QString overflow(QQuickItem* view) {
        QList<QQuickItem*> items;
        const std::function<void(QQuickItem*)> collect = [&](QQuickItem* root) {
            for (QQuickItem* child : root->childItems()) {
                items << child;
                collect(child);
            }
        };
        collect(view);
        const QRectF body = QRectF(0, 0, view->width(), view->height()).adjusted(-0.5, -0.5, 0.5, 0.5);
        for (QQuickItem* item : items) {
            if (!item->isVisible() || item->width() <= 0 || item->height() <= 0)
                continue;
            const QRectF rect = item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height()));
            if (!body.contains(rect))
                return QStringLiteral("%1 (%2) at %3,%4 %5x%6")
                    .arg(item->objectName(), QString::fromLatin1(item->metaObject()->className()))
                    .arg(rect.x())
                    .arg(rect.y())
                    .arg(rect.width())
                    .arg(rect.height());
        }
        return {};
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
        for (const auto& [kind, file] : {std::pair{"auto_filter", "AutoFilterEditor.qml"},
                                         std::pair{"overdrive", "OverdriveEditor.qml"}}) {
            QVariant url;
            QMetaObject::invokeMethod(root_.get(), "editorFor", Q_RETURN_ARG(QVariant, url),
                                      Q_ARG(QVariant, QString::fromLatin1(kind)));
            QVERIFY(url.toString().endsWith(QString::fromLatin1(file)));
        }
    }

    void autoFilterFitsAndDims() {
        QQuickItem* view = showDevice(QStringLiteral("auto_filter"), tone(300.0, kSampleRate));
        QVERIFY(view);
        QVERIFY(view->implicitHeight() <= bodyHeight());
        QVERIFY2(view->implicitWidth() < 720, qPrintable(QString::number(view->implicitWidth())));
        const QString problem = overflow(view);
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
        for (const char* name : {"type", "circuit", "slope12", "slope24", "freq", "resonance", "drive", "morph",
                                 "envAmount", "envAttack", "envRelease", "lfoAmount", "lfoRate", "lfoSync", "lfoWave",
                                 "phase", "spinOn", "lfoOffset", "output", "mix"})
            QVERIFY2(find(view, QString::fromLatin1(name)), name);
        // Drive does nothing on Clean, Morph but on Morph: dimmed (still there), lit when they count.
        QVERIFY(!find(view, QStringLiteral("drive"))->isEnabled());
        QVERIFY(!find(view, QStringLiteral("morph"))->isEnabled());
        set("circuit", 2.0);
        set("type", 4.0);
        QTRY_VERIFY(find(view, QStringLiteral("drive"))->isEnabled());
        QVERIFY(find(view, QStringLiteral("morph"))->isEnabled());
        // The rate's knob swaps with Sync, Phase's with Spin.
        QCOMPARE(find(view, QStringLiteral("lfoRate"))->property("title").toString(), QStringLiteral("Freq"));
        set("lfo_sync", 1.0);
        QTRY_COMPARE(find(view, QStringLiteral("lfoRate"))->property("title").toString(), QStringLiteral("Rate"));
        set("spin_on", 1.0);
        QTRY_COMPARE(find(view, QStringLiteral("phase"))->property("title").toString(), QStringLiteral("Spin"));
    }

    void autoFilterDragsAndDraws() {
        QQuickItem* view = showDevice(QStringLiteral("auto_filter"), tone(300.0, kSampleRate));
        auto* graph = find<AutoFilterGraph>(view, QStringLiteral("autoFilterGraph"));
        QVERIFY(graph);
        QCOMPARE(graph->width(), double(AutoFilterGraph::kWidth));
        // The curve is the engine's maths at Frequency.
        const std::vector<QPointF>& curve = graph->curve();
        QVERIFY(curve.size() > 50);
        AutoFilterCurve filter;
        filter.cutoffHz = 2000.0;
        filter.resonance = 20.0;
        for (const QPointF& point : {curve[curve.size() / 4], curve[curve.size() / 2]}) {
            const double db = autoFilterResponseDb(filter, graph->sampleRate(), {graph->freqAt(point.x())})[0];
            QVERIFY(std::abs(point.y() - graph->yOfDb(db)) < 0.5);
        }
        // A drag: Frequency across, Resonance up; one undo step.
        const QPointF origin = graph->mapToItem(view, QPointF(0, 0));
        const QPoint from = (origin + QPointF(graph->xOf(500.0), graph->yOfResonance(20.0))).toPoint();
        const QPoint to = (origin + QPointF(graph->xOf(4000.0), graph->yOfResonance(80.0))).toPoint();
        const int before = undo()->count();
        QTest::mousePress(window_, Qt::LeftButton, {}, from);
        QTest::mouseMove(window_, (from + to) / 2);
        QTest::mouseMove(window_, to);
        QTest::mouseRelease(window_, Qt::LeftButton, {}, to);
        QVERIFY(std::abs(std::log2(value("freq") / 4000.0)) < 0.05);
        QVERIFY(std::abs(value("resonance") - 80.0) < 3.0);
        QCOMPARE(undo()->count(), before + 1);
        // A double-click resets both, in one more step.
        QTest::mouseDClick(window_, Qt::LeftButton, {}, to);
        QTRY_COMPARE(value("freq"), 2000.0);
        QCOMPARE(value("resonance"), 20.0);
    }

    void autoFilterSweepsWithTheSound() {
        QQuickItem* view = showDevice(QStringLiteral("auto_filter"), tone(300.0, 4 * kSampleRate));
        auto* graph = find<AutoFilterGraph>(view, QStringLiteral("autoFilterGraph"));
        QVERIFY(graph);
        set("lfo_amount", 50.0);
        set("lfo_freq", 4.0);
        QVERIFY(!graph->live());
        QCOMPARE(graph->cutoffLeft(), 2000.0);
        // The reach: two octaves either way.
        QVERIFY(std::abs(graph->reach().first - 500.0) < 1.0 && std::abs(graph->reach().second - 8000.0) < 1.0);
        // Playing: the curve follows where the engine has the cutoff, moving. (Each offline render starts the
        // device afresh: pieces of a whole cycle, a few ticks of the display after each.)
        std::vector<double> seen;
        const double beatsPerFrame = project()->tempo() / 60.0 / kSampleRate;
        for (int i = 0; i < 6; ++i) {
            engine()->renderOffline(i * 12000 * beatsPerFrame, 12000);
            for (int tick = 0; tick < 12; ++tick) {
                refreshDisplays();
                if (tick == 0)
                    QVERIFY(graph->live());
                seen.push_back(graph->cutoffLeft());
                QTest::qWait(16);
            }
        }
        const auto [low, high] = std::minmax_element(seen.begin(), seen.end());
        QVERIFY2(*high / *low > 1.05, qPrintable(QStringLiteral("%1..%2").arg(*low).arg(*high)));
        QVERIFY(*low >= 499.0 && *high <= 8001.0);
        // Stopped: it eases back to Frequency and comes to rest.
        for (int i = 0; i < 120 && (graph->live() || graph->animating()); ++i) refreshDisplays();
        QVERIFY(!graph->live());
        QVERIFY(!graph->animating());
        QVERIFY(std::abs(graph->cutoffLeft() - 2000.0) < 1.0);
    }

    void overdriveFitsDragsAndGlows() {
        QQuickItem* view = showDevice(QStringLiteral("overdrive"), tone(220.0, 4 * kSampleRate));
        QVERIFY(view);
        QVERIFY(view->implicitHeight() <= bodyHeight());
        QVERIFY2(view->implicitWidth() < 400, qPrintable(QString::number(view->implicitWidth())));
        const QString problem = overflow(view);
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
        for (const char* name : {"freq", "width", "drive", "tone", "dynamics", "mix"})
            QVERIFY2(find(view, QString::fromLatin1(name)), name);
        auto* graph = find<OverdriveGraph>(view, QStringLiteral("overdriveGraph"));
        QVERIFY(graph);

        // A drag on the band: Frequency across, Width up; one step; a double-click resets them.
        const QPointF origin = graph->mapToItem(view, QPointF(0, 0));
        const QPoint from = (origin + graph->dot()).toPoint();
        const QPoint to = (origin + QPointF(graph->xOf(400.0), graph->yOfWidth(2.0))).toPoint();
        const int before = undo()->count();
        QTest::mousePress(window_, Qt::LeftButton, {}, from);
        QTest::mouseMove(window_, to);
        QTest::mouseRelease(window_, Qt::LeftButton, {}, to);
        QVERIFY(std::abs(std::log2(value("freq") / 400.0)) < 0.05);
        QVERIFY(std::abs(value("width") - 2.0) < 0.2);
        QCOMPARE(undo()->count(), before + 1);
        QTest::mouseDClick(window_, Qt::LeftButton, {}, to);
        QTRY_COMPARE(value("freq"), 1500.0);
        QCOMPARE(value("width"), 6.5);

        // Playing, the curve lights as far as the sound drives it; quiet, it comes to rest.
        QCOMPARE(graph->driveActivity(), 0.0);
        play(20);
        QVERIFY(graph->driveActivity() > 0.1);
        QVERIFY(graph->outputLevel() > 0.05);
        for (int i = 0; i < 200 && graph->animating(); ++i) refreshDisplays();
        QVERIFY(!graph->animating());
        QCOMPARE(graph->driveActivity(), 0.0);
    }
    // Saved with SUBSTATION_UI_SCREENSHOTS set: each at rest, and playing.
    void screenshot() {
        QQuickItem* view = showDevice(QStringLiteral("auto_filter"), tone(150.0, 4 * kSampleRate));
        QVERIFY(view);
        set("resonance", 70.0);
        set("lfo_amount", 30.0);
        set("env_amount", 25.0);
        QTest::qWait(50);
        save(grab(), QStringLiteral("auto-filter-idle.png"));
        play(6, 4096);
        QTest::qWait(50);
        save(grab(), QStringLiteral("auto-filter.png"));

        clearHost();
        view = showDevice(QStringLiteral("overdrive"), tone(220.0, 4 * kSampleRate));
        QVERIFY(view);
        QTest::qWait(50);
        save(grab(), QStringLiteral("overdrive-idle.png"));
        play(6, 4096);
        QTest::qWait(50);
        save(grab(), QStringLiteral("overdrive.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsAutoFilter)
#include "test_ui_device_editors_auto_filter.moc"
