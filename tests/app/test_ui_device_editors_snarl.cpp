// Snarl's editor (ui/qml/devices/editors/SnarlEditor.qml, SnarlScope, SnarlWobble):
// loaded as the device view loads it, over a real engine. Compact and fitting
// the view, every control there and bound (undoably), every knob's caption and
// readout whole, the lists as wide as their longest names (the sections not
// moving as they change); Ratio dimmed but settable outside FM and Ring, the
// Freq knob naming the vowel with the Vowel filter; the styles (one undo step
// each, the chip lit) and the dice (a roll and a nudge, one undo step each);
// what the engine plays reaching the scope (its cycle, its colour with the
// growl), the wobble's dot and the halos, all coming to rest after; the wobble's
// shape the engine's. With SUBSTATION_UI_SCREENSHOTS set to a folder, it is
// saved there as a PNG, playing.

#include <QColor>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <cmath>

#include "EditorHarness.h"
#include "audio/SnarlResponse.h"
#include "devices/DisplayValue.h"
#include "devices/SnarlScope.h"
#include "devices/SnarlWobble.h"
#include "editor/ProjectEditor.h"
#include "model/Clip.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsSnarl : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    double value(const char* id) { return param(track_, device_, QString::fromLatin1(id)); }

    QQuickItem* showSnarl() {
        track_ = editor()->addMidiTrack(-1, {}, QStringLiteral("snarl"));
        device_ = project()->track(track_).devices.front().id;
        return show(QStringLiteral("snarl"), track_, device_);
    }

    // A low note held from the start for `beats`, on the Snarl's track.
    void holdNote(double beats) {
        editor()->commitClips(QStringLiteral("Add"), {{track_, {Clip::midi(QStringLiteral("held"), QStringLiteral("held"),
                                                                           0.0, beats, 0.0, {Note{33, 0.0, beats}})}}});
    }
    // Renders `seconds` of the arrangement, then ticks the displays' clock a few times.
    void play(double seconds) {
        session_->bridge()->waitForDeviceStates();
        engine_->renderOffline(0.0, static_cast<int64_t>(seconds * kSampleRate));
        for (int i = 0; i < 6; ++i)
            refreshDisplays();
    }

    static const QStringList& knobNames() {
        static const QStringList names = {"wave", "unison", "detune", "width", "sub", "split", "growl", "ratio",
                                          "cutoff", "resonance", "drive", "squash", "wobbleFilter", "wobbleGrowl",
                                          "wobblePitch", "glide", "attack", "release", "volume"};
        return names;
    }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void compactAndBound() {
        QQuickItem* view = showSnarl();
        QVERIFY(view && fitted());
        QVERIFY2(view->implicitHeight() <= bodyHeight(), qPrintable(QString::number(view->implicitHeight())));
        QVERIFY2(view->implicitWidth() <= 860, qPrintable(QString::number(view->implicitWidth())));
        for (const QString& name : knobNames() + QStringList{"octave", "growlMode", "filterType", "dirtType",
                                                               "wobbleRate", "wobbleShape", "retrig", "scope",
                                                               "wobbleGraph", "dice", "styles"})
            QVERIFY2(find<QQuickItem>(view, name), qPrintable(name));
        QCOMPARE(itemsNamed(view, QStringLiteral("style")).size(), 1 + 6);  // (the chips' grid, then the chips)

        // Every knob's caption and every text its readout shows whole, and the cells as wide as those need.
        for (const QString& name : knobNames()) {
            QQuickItem* cell = find(view, name);
            const QString problem = knobTextsProblem(cell);
            QVERIFY2(problem.isEmpty(), qPrintable(problem));
        }
        // Everything inside the editor, the sections side by side and apart.
        const QRectF body(0, 0, view->width(), view->height());
        QQuickItem* sections = find(view, QStringLiteral("sections"));
        for (const char* name : {"mouth", "osc", "subSection", "growlSection", "filterSection", "dirtSection",
                                 "wobbleSection", "ampSection"}) {
            QQuickItem* item = find(view, QString::fromLatin1(name));
            const QRectF r = item->mapRectToItem(view, QRectF(0, 0, item->width(), item->height()));
            QVERIFY2(body.contains(r), name);
        }
        QQuickItem* before = nullptr;
        for (const char* name : {"osc", "subSection", "growlSection", "filterSection", "dirtSection", "wobbleSection",
                                 "ampSection"}) {
            QQuickItem* item = find(sections, QString::fromLatin1(name));
            if (before)
                QVERIFY2(item->x() >= before->x() + before->width() + 4, name);
            before = item;
        }

        // The lists as wide as their longest names: the sections stay put as they change.
        QQuickItem* dirt = find(view, QStringLiteral("dirtType"));
        const double dirtWidth = dirt->width();
        const double ampAt = find(view, QStringLiteral("ampSection"))->x();
        editor()->setDeviceParam(track_, device_, QStringLiteral("dirt_type"), 2.0);  // Rectify, the longest
        QTest::qWait(20);
        QCOMPARE(dirt->width(), dirtWidth);
        QCOMPARE(find(view, QStringLiteral("ampSection"))->x(), ampAt);
        QVERIFY(dirt->property("button").value<QQuickItem*>()->implicitWidth() <= dirtWidth + 0.5);

        // A knob set, undoably; a list too.
        editor()->setDeviceParam(track_, device_, QStringLiteral("growl"), 80.0);
        QCOMPARE(value("growl"), 80.0);
        undo()->undo();
        QCOMPARE(value("growl"), 30.0);  // (its default again)
        QMetaObject::invokeMethod(find(view, QStringLiteral("filterType")), "choose", Q_ARG(QVariant, 2));
        QCOMPARE(value("filter_type"), 2.0);

        // Ratio dimmed but settable outside FM and Ring.
        QQuickItem* ratio = find(view, QStringLiteral("ratio"));
        QCOMPARE(ratio->opacity(), 1.0);
        editor()->setDeviceParam(track_, device_, QStringLiteral("growl_mode"), 1.0);  // Sync
        QTRY_VERIFY(ratio->opacity() < 0.6);
        QVERIFY(ratio->isEnabled());
        editor()->setDeviceParam(track_, device_, QStringLiteral("growl_mode"), 3.0);  // Ring
        QTRY_COMPARE(ratio->opacity(), 1.0);
    }

    void vowels() {
        QQuickItem* view = showSnarl();
        QVERIFY(view);
        QQuickItem* vowel = find(view, QStringLiteral("vowel"));
        QQuickItem* cutoff = find(view, QStringLiteral("cutoff"));
        QVERIFY(!vowel->isVisible());
        QCOMPARE(cutoff->property("title").toString(), QStringLiteral("Freq"));
        editor()->setDeviceParams(track_, device_, {{QStringLiteral("filter_type"), 2.0},
                                                    {QStringLiteral("cutoff"), std::sqrt(30.0 * 18000.0)}});
        QTRY_VERIFY(vowel->isVisible());
        QCOMPARE(vowel->property("text").toString(), QStringLiteral("A"));  // the middle of the range
        QCOMPARE(cutoff->property("title").toString(), QStringLiteral("Vowel"));
        QCOMPARE(snarlVowel(snarlFreqPosition(std::sqrt(30.0 * 18000.0))), QStringLiteral("A"));
        // Between two vowels, both.
        editor()->setDeviceParam(track_, device_, QStringLiteral("cutoff"), 30.0 * std::pow(600.0, 0.375));
        QTRY_COMPARE(vowel->property("text").toString(), QStringLiteral("O→A"));
    }

    void stylesAndDice() {
        QQuickItem* view = showSnarl();
        QVERIFY(view);
        auto* scope = find<SnarlScope>(view, QStringLiteral("scope"));
        QVERIFY(scope);
        // A style: its patch in one undo step, its chip lit, the trace swelling.
        QQuickItem* riddim = find(view, QStringLiteral("styleRiddim"));
        QVERIFY(riddim);
        const int steps = undo()->count();
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(riddim));
        QCOMPARE(undo()->count(), steps + 1);
        QCOMPARE(value("growl_mode"), 2.0);  // Fold
        QCOMPARE(value("wobble_shape"), 4.0);  // Square
        QCOMPARE(value("unison"), 1.0);
        QCOMPARE(undo()->undoText(), QStringLiteral("Snarl Style: Riddim"));
        QVERIFY(riddim->property("lit").toBool());
        QVERIFY(!find(view, QStringLiteral("styleReese"))->property("lit").toBool());
        QVERIFY(scope->punchLeft() > 0.0);
        undo()->undo();
        QCOMPARE(value("growl_mode"), 0.0);  // (FM, as it was)
        QCOMPARE(value("unison"), 3.0);
        undo()->redo();
        // Every style a whole patch of the device's parameters, within their ranges.
        const QVariantList styles = view->property("styles").toList();
        QCOMPARE(styles.size(), 6);
        const Device* device = project()->findDevice(track_, device_);
        QVERIFY(device);
        for (const QVariant& style : styles) {
            const QVariantMap patch = style.toMap().value(QStringLiteral("patch")).toMap();
            QCOMPARE(patch.size(), 25);  // all but Volume
            for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
                const auto info = editor()->paramInfo(track_, device_, it.key());
                QVERIFY2(info.has_value(), qPrintable(it.key()));
                QVERIFY2(it.value().toDouble() >= info->minimum && it.value().toDouble() <= info->maximum,
                         qPrintable(it.key()));
            }
        }

        // The dice: a roll changes the sound in one step, and the face turns.
        QQuickItem* dice = find(view, QStringLiteral("dice"));
        const int before = undo()->count();
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centerOf(dice));
        QCOMPARE(undo()->count(), before + 1);
        QCOMPARE(undo()->undoText(), QStringLiteral("Roll Snarl"));
        QVERIFY(!riddim->property("lit").toBool());  // (no style now)
        QTRY_VERIFY(dice->rotation() > 0.0);
        for (const char* id : {"growl", "drive", "squash", "wobble_filter"}) {
            const double v = value(id);
            QVERIFY2(v != -999.0, id);
        }
        // Shift+click nudges: the lists stay, the knobs move a little.
        const double mode = value("growl_mode"), filter = value("filter_type"), growl = value("growl");
        QTest::mouseClick(window_, Qt::LeftButton, Qt::ShiftModifier, centerOf(dice));
        QCOMPARE(undo()->undoText(), QStringLiteral("Nudge Snarl"));
        QCOMPARE(value("growl_mode"), mode);
        QCOMPARE(value("filter_type"), filter);
        QVERIFY(std::abs(value("growl") - growl) <= 15.0 + 1e-9);
        QTRY_COMPARE(dice->rotation(), 360.0);
    }

    void playing() {
        QQuickItem* view = showSnarl();
        QVERIFY(view);
        auto* scope = find<SnarlScope>(view, QStringLiteral("scope"));
        auto* wobble = find<SnarlWobble>(view, QStringLiteral("wobbleGraph"));
        QVERIFY(scope && wobble);
        // Still before anything plays.
        QCOMPARE(scope->activity(), 0.0);
        QCOMPARE(wobble->lit(), 0.0);
        QQuickItem* growlHalo = find(find(view, QStringLiteral("growl")), QStringLiteral("halo"));
        QVERIFY(growlHalo);
        QVERIFY(!growlHalo->isVisible());

        // A note growling hard, the wobble moving the growl.
        editor()->setDeviceParams(track_, device_, {{QStringLiteral("growl"), 90.0},
                                                    {QStringLiteral("wobble_growl"), 0.0},
                                                    {QStringLiteral("filter_type"), 2.0}});
        holdNote(8.0);
        play(1.0);
        QVERIFY(scope->activity() > 0.0);
        // Its cycle reached the trace: not flat.
        double peak = 0.0;
        for (const double v : scope->shown()) peak = std::max(peak, std::abs(v));
        QVERIFY(peak > 0.01);
        // Coloured by the growl: towards magenta.
        for (int i = 0; i < 30; ++i) {
            engine_->renderOffline(0.0, static_cast<int64_t>(0.05 * kSampleRate));
            refreshDisplays();
        }
        QVERIFY(scope->heat() > 0.6);
        QCOMPARE(scope->traceColor(), SnarlScope::colorFor(scope->heat()));
        QVERIFY(SnarlScope::colorFor(1.0) != SnarlScope::colorFor(0.0));
        // The wobble's dot lit, where the engine's wobble is; the Growl knob's halo glowing.
        QVERIFY(wobble->lit() > 0.0);
        QVERIFY(wobble->phase() >= 0.0 && wobble->phase() <= 1.0);
        QTRY_VERIFY(growlHalo->isVisible());
        save(grab(), QStringLiteral("snarl.png"));

        // Nothing more comes: the trace fades and flattens, the dot goes out, everything comes to rest.
        for (int i = 0; i < 200 && (scope->activity() > 0.0 || wobble->lit() > 0.0); ++i) {
            refreshDisplays();
            QTest::qWait(2);
        }
        QCOMPARE(scope->activity(), 0.0);
        QCOMPARE(wobble->lit(), 0.0);
        QTRY_VERIFY(!growlHalo->isVisible());
    }

    void wobbleShape() {
        QQuickItem* view = showSnarl();
        QVERIFY(view);
        auto* wobble = find<SnarlWobble>(view, QStringLiteral("wobbleGraph"));
        QVERIFY(wobble);
        for (int shape = 0; shape < 5; ++shape) {
            editor()->setDeviceParam(track_, device_, QStringLiteral("wobble_shape"), shape);
            QTRY_COMPARE(wobble->shape(), shape);
            for (const double phase : {0.0, 0.2, 0.5, 0.7})
                QCOMPARE(wobble->curveAt(phase), snarlWobble(shape, phase, 0));
        }
        // Random: a few of its held steps, each the engine's for its cycle.
        editor()->setDeviceParam(track_, device_, QStringLiteral("wobble_shape"), 5.0);
        QTRY_COMPARE(wobble->shape(), 5);
        QCOMPARE(wobble->curveAt(0.1), snarlWobble(5, 0.0, 1));
        QCOMPARE(wobble->curveAt(0.9), snarlWobble(5, 0.0, SnarlWobble::kRandomSteps));
    }
};

QTEST_MAIN(TestUiDeviceEditorsSnarl)
#include "test_ui_device_editors_snarl.moc"
