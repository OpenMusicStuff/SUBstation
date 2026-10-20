// Changeling's editor (ui/qml/devices/editors/ChangelingEditor.qml, ChangelingScope) loaded as the device view loads
// it, over a real engine: it fits the view's height; every control is bound to its parameter (its value as it is
// now, undoably), the knobs' texts whole whatever their values; Key and Scale are dimmed but in Quantize; the pages
// show their own controls, the display on both; what the engine renders reaches the display (the pitch heard and the
// pitch out, the axis taking both in). With SUBSTATION_UI_SCREENSHOTS set, device-editors-changeling*.png are saved.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <vector>

#include "EditorHarness.h"
#include "controls/KnobItem.h"
#include "devices/ChangelingScope.h"
#include "model/Device.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A voice-like tone: harmonics falling as 1/h, at `freq`, for `seconds`.
std::vector<float> voice(double freq, double seconds) {
    std::vector<float> samples(static_cast<size_t>(seconds * kSampleRate));
    for (size_t i = 0; i < samples.size(); ++i) {
        double s = 0.0;
        for (int h = 1; h * freq < 8000.0; ++h) s += std::sin(2 * kPi * h * freq * double(i) / kSampleRate) / h;
        samples[i] = float(0.25 * s);
    }
    return samples;
}

}  // namespace

class TestUiDeviceEditorsChangeling : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    struct Shown {
        QString track;
        QString device;
        QQuickItem* view = nullptr;
        ChangelingScope* scope = nullptr;
    };

    Shown changeling(const std::vector<float>& mono, double seconds) {
        Shown shown;
        shown.track = audioTrackWith(mono, QStringLiteral("voice"), seconds);
        if (shown.track.isEmpty())
            return shown;
        shown.device = editor()->addDevice(shown.track, QStringLiteral("changeling"));
        shown.view = show(QStringLiteral("changeling"), shown.track, shown.device);
        shown.scope = shown.view ? find<ChangelingScope>(shown.view, QStringLiteral("changelingScope")) : nullptr;
        return shown;
    }

    KnobItem* knob(QQuickItem* view, const char* name) {
        QQuickItem* cell = find(view, QString::fromLatin1(name));
        auto* paramKnob = cell ? qvariant_cast<QQuickItem*>(cell->property("knob")) : nullptr;
        return paramKnob ? qvariant_cast<KnobItem*>(paramKnob->property("knob")) : nullptr;
    }

    QString readout(QQuickItem* view, const char* name) {
        QQuickItem* cell = find(view, QString::fromLatin1(name));
        return cell && !cell->childItems().isEmpty() ? cell->childItems().last()->property("text").toString()
                                                     : QString();
    }

    QQuickItem* button(QQuickItem* view, const char* name) {
        QQuickItem* control = find(view, QString::fromLatin1(name));
        return control ? qvariant_cast<QQuickItem*>(control->property("button")) : nullptr;
    }

    void ticksNow(int count) {
        for (int i = 0; i < count; ++i) refreshDisplays();
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
                                  Q_ARG(QVariant, QStringLiteral("changeling")));
        QVERIFY(url.toString().endsWith(QStringLiteral("ChangelingEditor.qml")));
    }

    void fitsAndBinds() {
        const Shown shown = changeling(voice(220.0, 1.0), 1.0);
        QVERIFY(shown.view && shown.scope);
        QQuickItem* view = shown.view;
        QVERIFY2(view->implicitHeight() <= bodyHeight(),
                 qPrintable(QStringLiteral("%1 > %2").arg(view->implicitHeight()).arg(bodyHeight())));
        QCOMPARE(view->property("pages").toInt(), 2);
        QCOMPARE(view->property("pageNames").toStringList(), (QStringList{QStringLiteral("Shift"), QStringLiteral("Mutate")}));

        // Every control, each at its parameter's default.
        for (const char* name : {"mode0", "mode1", "mode2", "keyChoice", "scaleChoice", "quality0", "quality1",
                                 "quality2", "pitch", "link", "formant", "glide", "drive", "mix", "output",
                                 "harmonics", "twin", "interval", "detune", "width", "smear"})
            QVERIFY2(find(view, QString::fromLatin1(name)), name);
        const std::pair<const char*, double> defaults[] = {{"pitch", 0.0},  {"formant", 0.0}, {"glide", 20.0},
                                                           {"drive", 0.0},  {"mix", 100.0},  {"output", 0.0},
                                                           {"harmonics", 0.0}, {"twin", 0.0}, {"interval", -12.0},
                                                           {"detune", 0.0}, {"width", 0.0},  {"smear", 0.0}};
        for (const auto& [name, value] : defaults) {
            KnobItem* dial = knob(view, name);
            QVERIFY2(dial, name);
            QVERIFY2(std::abs(dial->value() - value) < 1e-4, name);
        }
        QVERIFY(knob(view, "glide")->logScale());
        QVERIFY(knob(view, "pitch")->bipolar());
        QCOMPARE(readout(view, "pitch"), QStringLiteral("0 st"));
        editor()->setDeviceParam(shown.track, shown.device, QStringLiteral("pitch"), 2.4);
        QCOMPARE(readout(view, "pitch"), QStringLiteral("+2.40 st"));
        undo()->undo();
        QCOMPARE(readout(view, "pitch"), QStringLiteral("0 st"));
        QCOMPARE(readout(view, "drive"), QStringLiteral("0.0"));

        // The modes and the quality lit as their parameters are (Transpose, Studio at first); Key and Scale only
        // in Quantize.
        QVERIFY(find(view, QStringLiteral("mode0"))->property("lit").toBool());
        QVERIFY(!find(view, QStringLiteral("mode1"))->property("lit").toBool());
        QVERIFY(find(view, QStringLiteral("quality1"))->property("lit").toBool());
        QVERIFY(!find(view, QStringLiteral("keyChoice"))->parentItem()->isEnabled());
        editor()->setDeviceParam(shown.track, shown.device, QStringLiteral("mode"), 1.0);
        QVERIFY(find(view, QStringLiteral("mode1"))->property("lit").toBool());
        QVERIFY(find(view, QStringLiteral("keyChoice"))->parentItem()->isEnabled());
        undo()->undo();

        // Every knob's texts whole, whatever its value, on both pages; each page shows its own.
        QVERIFY(find(view, QStringLiteral("shiftPage"))->isVisible());
        QVERIFY(!find(view, QStringLiteral("mutatePage"))->isVisible());
        for (const char* name : {"pitch", "formant", "glide", "drive", "mix", "output"}) {
            const QString problem = knobTextsProblem(find(view, QString::fromLatin1(name)));
            QVERIFY2(problem.isEmpty(), qPrintable(problem));
        }
        save(grab(), QStringLiteral("changeling.png"));
        view->setProperty("page", 1);
        QVERIFY(!find(view, QStringLiteral("shiftPage"))->isVisible());
        QVERIFY(find(view, QStringLiteral("mutatePage"))->isVisible());
        QVERIFY(shown.scope->isVisible());
        for (const char* name : {"harmonics", "twin", "interval", "detune", "width", "smear"}) {
            const QString problem = knobTextsProblem(find(view, QString::fromLatin1(name)));
            QVERIFY2(problem.isEmpty(), qPrintable(problem));
        }
        save(grab(), QStringLiteral("changeling-mutate.png"));

        // Inside the body, its margins kept, nothing over the display.
        const QRectF body(0, 0, view->width(), view->height());
        const QRectF scope = shown.scope->mapRectToItem(view, QRectF(0, 0, shown.scope->width(), shown.scope->height()));
        for (const char* name : {"harmonics", "twin", "interval", "detune", "width", "smear", "mode0", "quality2"}) {
            QQuickItem* item = find(view, QString::fromLatin1(name));
            const QRectF rect = item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height()));
            QVERIFY2(body.adjusted(8, 6, -8, -6).contains(rect), name);
            QVERIFY2(!rect.intersects(scope), name);
        }
    }

    void scopeFollowsTheVoice() {
        // A3 moved up three semitones: the display hears A3 and shows C4 coming out, its axis taking in both
        const Shown shown = changeling(voice(220.0, 2.0), 2.0);
        QVERIFY(shown.view && shown.scope);
        editor()->setDeviceParam(shown.track, shown.device, QStringLiteral("pitch"), 3.0);
        engine()->renderOffline(0.0, kSampleRate);
        ticksNow(6);
        ChangelingScope* scope = shown.scope;
        QVERIFY(scope->voiced());
        QVERIFY2(std::abs(scope->heard() - 57.0) < 0.05, qPrintable(QString::number(scope->heard())));
        QVERIFY2(std::abs(scope->out() - 60.0) < 0.05, qPrintable(QString::number(scope->out())));
        QVERIFY(scope->low() < 57.0 && scope->high() > 60.0);
        QVERIFY(scope->high() - scope->low() >= ChangelingScope::kLeastSpan - 1e-9);
        save(grab(), QStringLiteral("changeling-voice.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsChangeling)
#include "test_ui_device_editors_changeling.moc"
