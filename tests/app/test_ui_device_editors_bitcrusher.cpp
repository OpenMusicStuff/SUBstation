// The Bitcrusher's editor (ui/qml/devices/editors/BitcrusherEditor.qml):
// loaded as the device view loads it, over a real engine; its knobs, and the
// crushed sine it draws following Bits, Rate and Drive. With
// SUBSTATION_UI_SCREENSHOTS set to a folder, it is saved there as a PNG.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <cmath>

#include "EditorHarness.h"
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

        // At 11025 Hz each of two cycles of 200 Hz at 48 kHz holds its samples about 4.35 samples each.
        const int held = levels(view).size();
        QVERIFY2(std::abs(held - 480.0 / (48000.0 / 11025.0)) <= 2, qPrintable(QString::number(held)));
        // Two bits: the values on halves.
        set("bits", 2.0);
        for (const double y : levels(view))
            QVERIFY2(std::abs(y * 2 - std::round(y * 2)) < 1e-9, qPrintable(QString::number(y)));
        // A lower rate: fewer, longer steps.
        set("rate", 1000.0);
        QCOMPARE(levels(view).size(), 10);
        // Driven hard: it clips at full scale.
        set("drive", 24.0);
        double most = 0;
        for (const double y : levels(view))
            most = std::max(most, std::abs(y));
        QCOMPARE(most, 1.0);
        set("rate", 6000.0);
        set("bits", 4.0);
        set("drive", 3.0);
        QTest::qWait(50);
        save(grab(), QStringLiteral("bitcrusher.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsBitcrusher)
#include "test_ui_device_editors_bitcrusher.moc"
