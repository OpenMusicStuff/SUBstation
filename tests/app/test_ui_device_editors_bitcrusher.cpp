// The Bitcrusher's editor (ui/qml/devices/editors/BitcrusherEditor.qml):
// loaded as the device view loads it, over a real engine; its knobs and Output's
// box over the wave, the crushed sine it draws following Bits, Rate, Drive and
// Output, and the wave running along while sound comes in (DisplayValue). With
// SUBSTATION_UI_SCREENSHOTS set to a folder, it is saved there as a PNG.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <cmath>

#include "EditorHarness.h"
#include "devices/DisplayValue.h"
#include "editor/ProjectEditor.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsBitcrusher : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    void set(const char* id, double v) { editor()->setDeviceParam(track_, device_, QString::fromLatin1(id), v); }

    // The preview's held samples' values.
    static QList<double> levels(QQuickItem* view) {
        QList<double> out;
        for (const QVariant& step : view->property("steps").toList())
            out << step.toMap().value(QStringLiteral("y")).toDouble();
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

    void previewFollowsTheParameters() {
        track_ = audioTrackWith(tone(440.0, kSampleRate), QStringLiteral("tone"), 1.0);
        QVERIFY(!track_.isEmpty());
        device_ = editor()->addDevice(track_, QStringLiteral("bitcrusher"));
        QQuickItem* view = show(QStringLiteral("bitcrusher"), track_, device_);
        QVERIFY(view);
        QVERIFY(view->implicitHeight() <= bodyHeight());  // fits the view
        for (const char* name : {"bits", "rate", "drive", "dither", "tone", "output", "mix", "crushPreview"})
            QVERIFY2(find<QQuickItem>(view, QString::fromLatin1(name)), name);

        // Output is a box over the wave, not a knob.
        QQuickItem* preview = find(view, QStringLiteral("crushPreview"));
        QQuickItem* output = find(view, QStringLiteral("output"));
        QVERIFY(preview && output);
        const QRectF box = output->mapRectToItem(preview, QRectF(0, 0, output->width(), output->height()));
        QVERIFY(QRectF(0, 0, preview->width(), preview->height()).contains(box));

        // One cycle of 200 Hz at 48 kHz: at 11025 Hz each held sample lasts about 4.35 samples.
        const int held = levels(view).size();
        QVERIFY2(std::abs(held - 240.0 / (48000.0 / 11025.0)) <= 2, qPrintable(QString::number(held)));
        // Two bits: the values on halves.
        set("bits", 2.0);
        for (const double y : levels(view))
            QVERIFY2(std::abs(y * 2 - std::round(y * 2)) < 1e-9, qPrintable(QString::number(y)));
        // A lower rate: fewer, longer steps.
        set("rate", 1000.0);
        QCOMPARE(levels(view).size(), 5);
        // Driven hard: it clips at full scale.
        set("drive", 24.0);
        double most = 0;
        for (const double y : levels(view))
            most = std::max(most, std::abs(y));
        QCOMPARE(most, 1.0);
        // Output makes the wave shorter.
        set("output", -12.0);
        QTRY_VERIFY(view->property("outputGain").toDouble() < 0.3);

        // Still while nothing comes in; running along once sound does, then held where it got to.
        auto* input = find<DisplayValue>(view, QStringLiteral("levelDisplay"));
        QQuickItem* wave = find(view, QStringLiteral("wave"));
        QVERIFY(input && wave);
        const double still = wave->x();
        QTest::qWait(100);
        QCOMPARE(wave->x(), still);
        input->advance(true, 0.8, 0.016);
        QVERIFY(input->active());
        QTRY_VERIFY(wave->x() != still);
        input->advance(false, 0.0, 1.0);
        QVERIFY(!input->active());
        const double held2 = wave->x();
        QTest::qWait(100);
        QCOMPARE(wave->x(), held2);
        set("output", 0.0);
        set("rate", 6000.0);
        set("bits", 4.0);
        set("drive", 3.0);
        for (int i = 0; i < 10; ++i) {  // as sound comes in: glowing
            input->advance(true, 0.7, 0.016);
            QTest::qWait(30);
        }
        save(grab(), QStringLiteral("bitcrusher.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsBitcrusher)
#include "test_ui_device_editors_bitcrusher.moc"
