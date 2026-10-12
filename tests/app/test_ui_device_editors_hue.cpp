// Hue's editor (ui/qml/devices/editors/HueEditor.qml, HueWheel) loaded as the
// device view loads it, over a real engine: it fits the view's height and,
// folded, is as wide as the Vintage Hall's, every caption and readout whole;
// Notes slides open the keyboard and the finer controls and shuts again; a key
// (a root and a scale) sets the twelve notes in one undo step and is read back
// from them ("Custom" when they are no scale, a new root moving them); a click
// on the wheel or a key switches a note; MIDI Notes shows the notes held; what
// the engine renders reaches the wheel's chromagram, notes kept louder than the
// ones taken out, and it comes to rest. With SUBSTATION_UI_SCREENSHOTS set,
// device-editors-hue*.png are saved there.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "EditorHarness.h"
#include "controls/KnobItem.h"
#include "devices/HueWheel.h"
#include "model/Device.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A growl: a saw-ish bass at A#1 (58.27 Hz) through 12 partials, wobbling in level, for `seconds`.
std::vector<float> growl(double seconds) {
    std::vector<float> samples(static_cast<size_t>(seconds * kSampleRate));
    for (size_t i = 0; i < samples.size(); ++i) {
        const double t = double(i) / kSampleRate;
        double tone = 0.0;
        for (int h = 1; h <= 40; ++h)
            tone += std::sin(2 * kPi * 58.27 * h * t) / h;
        const double wobble = 0.6 + 0.4 * std::sin(2 * kPi * 2.0 * t);
        samples[i] = float(0.25 * wobble * tone);
    }
    return samples;
}

// What shows with Notes folded.
const std::vector<const char*> kAlwaysShown = {"rootChoice", "scaleChoice", "quality", "midi", "end", "color", "start",
                                               "notesButton"};
const std::vector<const char*> kAll = {"rootChoice", "scaleChoice", "quality", "midi", "end", "color", "start",
                                       "notesButton", "keyboard", "wet_gain", "pre_shift", "post_shift", "mute_low",
                                       "mute_high"};
const std::vector<const char*> kKnobs = {"end", "color", "start", "wet_gain", "pre_shift", "post_shift"};

int bitsOf(std::initializer_list<int> notes) {
    int bits = 0;
    for (const int n : notes)
        bits |= 1 << n;
    return bits;
}

}  // namespace

class TestUiDeviceEditorsHue : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    struct Shown {
        QString track;
        QString device;
        QQuickItem* view = nullptr;
        HueWheel* wheel = nullptr;
    };

    // A Hue on a track playing `mono` for `seconds`, its editor shown.
    Shown hue(const std::vector<float>& mono, double seconds) {
        Shown shown;
        shown.track = audioTrackWith(mono, QStringLiteral("growl"), seconds);
        if (shown.track.isEmpty())
            return shown;
        shown.device = editor()->addDevice(shown.track, QStringLiteral("hue"));
        shown.view = show(QStringLiteral("hue"), shown.track, shown.device);
        if (shown.view)
            shown.wheel = find<HueWheel>(shown.view, QStringLiteral("hueWheel"));
        return shown;
    }

    KnobItem* knob(QQuickItem* view, const char* name) {
        QQuickItem* cell = find(view, QString::fromLatin1(name));
        auto* paramKnob = cell ? qvariant_cast<QQuickItem*>(cell->property("knob")) : nullptr;
        return paramKnob ? qvariant_cast<KnobItem*>(paramKnob->property("knob")) : nullptr;
    }

    // The rightmost edge of `names` in the view, each checked to be whole, beside the wheel and inside the body.
    double rightOf(QQuickItem* view, QQuickItem* wheelItem, const std::vector<const char*>& names) {
        const QRectF wheel = wheelItem->mapRectToItem(view, QRectF(0, 0, wheelItem->width(), wheelItem->height()));
        [&] { QVERIFY(wheel.left() >= 0 && wheel.top() >= 0 && wheel.bottom() <= view->height() + 0.5); }();
        double right = wheel.right();
        for (const char* name : names) {
            QQuickItem* item = find(view, QString::fromLatin1(name));
            if (!item) {
                qWarning("missing %s", name);
                return -1;
            }
            const QRectF rect = item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height()));
            [&] {
                QVERIFY2(rect.left() > wheel.right() && rect.top() >= 0 && rect.bottom() <= view->height() + 0.5, name);
                QVERIFY2(rect.right() <= view->width() + 0.5, name);
            }();
            right = std::max(right, rect.right());
            if (std::find_if(kKnobs.begin(), kKnobs.end(), [&](const char* k) { return std::string(k) == name; }) !=
                kKnobs.end()) {
                const QString problem = knobTextsProblem(item);
                [&] { QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(QString::fromLatin1(name), problem))); }();
            }
        }
        return right;
    }

    double value(const Shown& shown, const char* id) { return param(shown.track, shown.device, QString::fromLatin1(id)); }
    void set(const Shown& shown, const char* id, double v) {
        editor()->setDeviceParam(shown.track, shown.device, QString::fromLatin1(id), v);
    }
    int notes(const Shown& shown) {
        static const char* const kIds[12] = {"note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f",
                                             "note_fs", "note_g", "note_gs", "note_a", "note_as", "note_b"};
        int bits = 0;
        for (int c = 0; c < 12; ++c)
            if (value(shown, kIds[c]) >= 0.5)
                bits |= 1 << c;
        return bits;
    }

    void tick(int count, int ms = 16) {
        for (int i = 0; i < count; ++i) {
            QTest::qWait(ms);
            refreshDisplays();
        }
    }

    void settle(const Shown& shown, int most = 1500) {
        for (int i = 0, still = 0; i < most && still < 3; ++i) {
            tick(1, 4);
            still = shown.wheel->animating() ? 0 : still + 1;
        }
    }

    // Chooses entry `index` of a KeyChoice's menu (as a click on it would).
    void choose(QQuickItem* view, const char* choice, int index) {
        QQuickItem* item = find(view, QString::fromLatin1(choice));
        QVERIFY2(item, choice);
        QVERIFY(QMetaObject::invokeMethod(item, "chosen", Q_ARG(int, index)));
    }

    // The point of the wheel on pitch class `c`, in the window.
    QPoint onNote(HueWheel* wheel, int c) {
        const double angle = -kPi / 2 + 2 * kPi * c / 12.0, r = wheel->radius() * 0.85;
        const QPointF local = wheel->center() + QPointF(r * std::cos(angle), r * std::sin(angle));
        return wheel->mapToScene(local).toPoint();
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
        QMetaObject::invokeMethod(root_.get(), "editorFor", Q_RETURN_ARG(QVariant, url), Q_ARG(QVariant, QStringLiteral("hue")));
        QVERIFY(url.toString().endsWith(QStringLiteral("HueEditor.qml")));
    }

    void fitsAndFolds() {
        // The Vintage Hall's folded width, to be as wide.
        const QString hallTrack = audioTrackWith(growl(0.5), QStringLiteral("hall"), 0.5);
        const QString hallDevice = editor()->addDevice(hallTrack, QStringLiteral("vintage_hall"));
        QQuickItem* hall = show(QStringLiteral("vintage_hall"), hallTrack, hallDevice);
        QVERIFY(hall);
        const double hallWidth = hall->implicitWidth();
        clearHost();

        const Shown shown = hue(growl(1.0), 1.0);
        QVERIFY(shown.view && shown.wheel);
        QQuickItem* view = shown.view;
        QVERIFY2(view->implicitHeight() <= bodyHeight(),
                 qPrintable(QStringLiteral("%1 > %2").arg(view->implicitHeight()).arg(bodyHeight())));
        QVERIFY(fitted());
        QVERIFY(!find(view, QStringLiteral("notesSection"))->isVisible());
        const double folded = view->implicitWidth();
        QCOMPARE(folded, std::ceil(rightOf(view, shown.wheel, kAlwaysShown)) + 8);
        QCOMPARE(folded, hallWidth);

        // Notes slides open (not at once), everything inside the body and whole; and shuts again.
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(find(view, QStringLiteral("notesButton"))));
        QVERIFY(view->implicitWidth() < folded + 10);
        QTRY_VERIFY(view->property("reveal").toDouble() == 1.0);
        QVERIFY(fitted());
        QCOMPARE(view->implicitWidth(), std::ceil(rightOf(view, shown.wheel, kAll)) + 8);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(find(view, QStringLiteral("notesButton"))));
        QTRY_VERIFY(view->property("reveal").toDouble() == 0.0);
        QCOMPARE(view->implicitWidth(), folded);

        // The knobs are their parameters.
        QVERIFY(std::abs(knob(view, "color")->value() - 100.0) < 1e-3);
        QVERIFY(std::abs(knob(view, "start")->value() - 240.0) < 1e-3);
        QVERIFY(knob(view, "start")->logScale() && knob(view, "end")->logScale());
    }

    void keys() {
        const Shown shown = hue(growl(1.0), 1.0);
        QVERIFY(shown.view && shown.wheel);
        QQuickItem* view = shown.view;
        QCOMPARE(view->property("keyText").toString(), QStringLiteral("No Notes"));

        // F Minor (scale 1): the seven notes, one undo step.
        const int steps = undo()->index();
        choose(view, "rootChoice", 5);  // (no notes: the root alone gives its Major)
        QCOMPARE(notes(shown), bitsOf({5, 7, 9, 10, 0, 2, 4}));
        choose(view, "scaleChoice", 1);
        QCOMPARE(notes(shown), bitsOf({5, 7, 8, 10, 0, 1, 3}));
        QCOMPARE(undo()->index(), steps + 2);
        QCOMPARE(undo()->text(undo()->index() - 1), QStringLiteral("Change Key"));
        QCOMPARE(view->property("keyText").toString(), QStringLiteral("F Minor"));
        QCOMPARE(shown.wheel->caption(), QStringLiteral("F Minor"));

        // Another root keeps the scale.
        choose(view, "rootChoice", 2);
        QCOMPARE(notes(shown), bitsOf({2, 4, 5, 7, 9, 10, 0}));
        QCOMPARE(view->property("keyText").toString(), QStringLiteral("D Minor"));

        // A note off the scale: Custom; a new root moves the notes with it.
        shown.wheel->toggleNote(1);
        QCOMPARE(view->property("keyText").toString(), QStringLiteral("Custom"));
        choose(view, "rootChoice", 4);
        QCOMPARE(notes(shown), bitsOf({4, 6, 7, 9, 11, 0, 2, 3}));

        // The wheel and the keyboard switch notes, one undo step each.
        choose(view, "scaleChoice", -1);
        QCOMPARE(notes(shown), 0);
        tick(3);
        const int before = undo()->index();
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, onNote(shown.wheel, 9));
        QCOMPARE(notes(shown), bitsOf({9}));
        QCOMPARE(undo()->index(), before + 1);
        QCOMPARE(undo()->text(undo()->index() - 1), QStringLiteral("Add A"));
        QCOMPARE(view->property("keyText").toString(), QStringLiteral("A Root Only"));
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(find(view, QStringLiteral("notesButton"))));
        QTRY_VERIFY(view->property("reveal").toDouble() == 1.0);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(find(view, QStringLiteral("key_note_e"))));
        QCOMPARE(notes(shown), bitsOf({4, 9}));
        // (A# and G#, beside A, are moved onto it: down onto A for A#, up for G#)
        QCOMPARE(HueWheel::moveOf(shown.wheel->notes(), 10), -1);
        QCOMPARE(HueWheel::moveOf(shown.wheel->notes(), 8), 1);
    }

    void midiShowsHeld() {
        const Shown shown = hue(growl(1.0), 1.0);
        QVERIFY(shown.wheel);
        set(shown, "note_a", 1.0);
        set(shown, "midi", 1.0);
        QVERIFY(shown.wheel->midi());
        QCOMPARE(shown.view->property("keyText").toString(), QStringLiteral("MIDI"));
        // nothing held: no notes shown; a click on the wheel does nothing
        tick(2);
        QCOMPARE(shown.wheel->notes(), 0);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, onNote(shown.wheel, 2));
        QCOMPARE(value(shown, "note_d"), 0.0);
    }

    void chromagram() {
        const Shown shown = hue(growl(2.0), 2.0);
        QVERIFY(shown.wheel);
        HueWheel* wheel = shown.wheel;
        set(shown, "note_a", 1.0);
        set(shown, "note_e", 1.0);
        settle(shown);
        QCOMPARE(wheel->levelDb(9), sub::app::analysis::Chroma::kFloorDb);
        engine()->renderOffline(0.0, int(kSampleRate * 1.0));
        tick(4, 20);
        QVERIFY(wheel->animating());
        // The growl is an A#: in, A# is loud; out, it is moved onto A, the A louder than the A# left.
        QVERIFY2(wheel->inputLevelDb(10) > wheel->inputLevelDb(9) + 6, qPrintable(QString::number(wheel->inputLevelDb(10))));
        QVERIFY2(wheel->levelDb(9) > wheel->levelDb(10) + 6,
                 qPrintable(QStringLiteral("%1 %2").arg(wheel->levelDb(9)).arg(wheel->levelDb(10))));
        // Nothing more: it falls and rests.
        settle(shown, 4000);
        QVERIFY(!wheel->animating());
    }

    void screenshots() {
        if (qEnvironmentVariable("SUBSTATION_UI_SCREENSHOTS").isEmpty())
            QSKIP("SUBSTATION_UI_SCREENSHOTS not set");
        const Shown shown = hue(growl(4.0), 4.0);
        QVERIFY(shown.view && shown.wheel);
        tick(4);
        save(grab(), QStringLiteral("hue-empty.png"));
        choose(shown.view, "rootChoice", 5);
        choose(shown.view, "scaleChoice", 5);  // F Phrygian
        settle(shown);
        save(grab(), QStringLiteral("hue-idle.png"));
        auto play = [&](double seconds, int ticks) {
            engine()->renderOffline(0.0, int(seconds * kSampleRate));
            for (int i = 0; i < ticks; ++i)
                tick(1);
        };
        play(1.5, 10);
        QTest::qWait(50);
        save(grab(), QStringLiteral("hue.png"));
        set(shown, "quality", 1.0);
        set(shown, "color", 170.0);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                          centerOf(find(shown.view, QStringLiteral("notesButton"))));
        QTRY_VERIFY(shown.view->property("reveal").toDouble() == 1.0);
        QVERIFY(fitted());
        QTest::mouseMove(window_, QPoint(1, 1));
        play(1.5, 10);
        QTest::qWait(50);
        save(grab(), QStringLiteral("hue-notes.png"));
        // A power chord: most notes taken out, the ones beside moved.
        choose(shown.view, "rootChoice", 10);
        choose(shown.view, "scaleChoice", 15);
        play(1.5, 10);
        QTest::qWait(50);
        save(grab(), QStringLiteral("hue-power.png"));
    }
};

QTEST_MAIN(TestUiDeviceEditorsHue)
#include "test_ui_device_editors_hue.moc"
