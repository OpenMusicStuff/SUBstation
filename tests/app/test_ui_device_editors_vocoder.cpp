// The Vocoder's editor (ui/qml/devices/editors/VocoderEditor.qml): loaded as the device view loads it, over a real
// engine; every control there and fitting the view, the carrier's own settings shown for the carrier chosen, the
// filter bank (VocoderGraph) following Bands and the range, and what the engine renders reaching it: the band the
// input is in lit the most, the pitch followed with Pitch Tracking. With SUBSTATION_UI_SCREENSHOTS set to a folder,
// it is saved there as a PNG.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <cmath>

#include "EditorHarness.h"
#include "devices/VocoderGraph.h"
#include "editor/ProjectEditor.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsVocoder : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    void set(const char* id, double v) { editor()->setDeviceParam(track_, device_, QString::fromLatin1(id), v); }

    void tick(int count, int ms = 16) {
        for (int i = 0; i < count; ++i) {
            QTest::qWait(ms);
            refreshDisplays();
        }
    }

    QQuickItem* shown(const std::vector<float>& mono, double seconds) {
        track_ = audioTrackWith(mono, QStringLiteral("voice"), seconds);
        if (track_.isEmpty()) return nullptr;
        device_ = editor()->addDevice(track_, QStringLiteral("vocoder"));
        return show(QStringLiteral("vocoder"), track_, device_);
    }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void controlsAndCarriers() {
        QQuickItem* view = shown(tone(1000.0, kSampleRate), 1.0);
        QVERIFY(view);
        QVERIFY2(view->implicitHeight() <= bodyHeight(),
                 qPrintable(QStringLiteral("%1 > %2").arg(view->implicitHeight()).arg(bodyHeight())));
        for (const char* id : {"carrier", "enhance", "unvoiced", "sensitivity", "bands", "low", "high", "bandwidth",
                               "filter", "formant", "gate", "depth", "attack", "release", "level", "mix"})
            QVERIFY2(find<QQuickItem>(view, QStringLiteral("param_") + QString::fromLatin1(id)), id);
        QVERIFY(find<VocoderGraph>(view, QStringLiteral("vocoderGraph")));
        for (const char* name : {"speed0", "speed1", "channels0", "channels1", "channels2"})
            QVERIFY2(find<QQuickItem>(view, QString::fromLatin1(name)), name);
        // The boxes show their texts whole.
        for (const char* id : {"low", "high", "bandwidth", "track_low", "track_high"}) {
            QQuickItem* box = find<QQuickItem>(view, QStringLiteral("param_") + QString::fromLatin1(id));
            QVERIFY(box);
            const QString problem = boxTextsProblem(box);
            QVERIFY2(problem.isEmpty(), qPrintable(problem));
        }

        // Noise: Rate and Crackle; Pitch Tracking: its wave, Pitch and range.
        QQuickItem* rate = find<QQuickItem>(view, QStringLiteral("param_noise_rate"));
        QQuickItem* wave = find<QQuickItem>(view, QStringLiteral("param_wave"));
        QQuickItem* low = find<QQuickItem>(view, QStringLiteral("param_track_low"));
        QVERIFY(rate && wave && low);
        QVERIFY(rate->isVisible());
        QVERIFY(!wave->isVisible());
        set("carrier", 3.0);
        QTRY_VERIFY(wave->isVisible());
        QVERIFY(low->isVisible());
        QVERIFY(!rate->isVisible());
        set("carrier", 1.0);
        QTRY_VERIFY(!wave->isVisible());
        QVERIFY(!rate->isVisible());
    }

    void graphFollowsTheBands() {
        QQuickItem* view = shown(tone(1000.0, kSampleRate), 1.0);
        QVERIFY(view);
        auto* graph = find<VocoderGraph>(view, QStringLiteral("vocoderGraph"));
        QVERIFY(graph);
        QCOMPARE(graph->bandCount(), 20);
        QCOMPARE(graph->bandCentre(0), 80.0);
        set("bands", 9.0);
        QTRY_COMPARE(graph->bandCount(), 40);
        set("low", 200.0);
        QTRY_COMPARE(graph->bandCentre(0), 200.0);
    }

    void displaysReachTheGraph() {
        QQuickItem* view = shown(tone(1000.0, kSampleRate, 0.3), 1.0);
        QVERIFY(view);
        auto* graph = find<VocoderGraph>(view, QStringLiteral("vocoderGraph"));
        QVERIFY(graph);
        tick(2);
        engine()->renderOffline(0.0, kSampleRate / 2);
        tick(6);  // (the bars risen; they fall once nothing has come for a quarter of a second)
        int loudest = 0, nearest = 0;
        for (int k = 0; k < graph->bandCount(); ++k) {
            if (graph->bandLevel(k) > graph->bandLevel(loudest)) loudest = k;
            if (std::abs(std::log(graph->bandCentre(k) / 1000.0)) < std::abs(std::log(graph->bandCentre(nearest) / 1000.0)))
                nearest = k;
        }
        QCOMPARE(loudest, nearest);
        QVERIFY2(graph->bandLevel(loudest) > -40.0, qPrintable(QString::number(graph->bandLevel(loudest))));
        QCOMPARE(graph->pitch(), 0.0);  // (not tracking)
        // Nothing more comes: the bars fall.
        tick(30);
        QVERIFY2(graph->bandLevel(loudest) < -60.0, qPrintable(QString::number(graph->bandLevel(loudest))));

        // Pitch Tracking: the pitch it follows.
        set("carrier", 3.0);
        tick(2);
        engine()->renderOffline(0.0, kSampleRate / 2);
        tick(10);
        QVERIFY2(std::abs(graph->pitch() - 1000.0) < 10.0, qPrintable(QString::number(graph->pitch())));
        save(grab(), QStringLiteral("vocoder.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsVocoder)
#include "test_ui_device_editors_vocoder.moc"
